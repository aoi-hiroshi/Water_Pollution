#include "water/controller/api_controller.h"
#include "support/classification_database.h"
#include <nlohmann/json.hpp>
#include <future>
#include <iostream>
#include <stdexcept>

namespace {
using namespace water;
using namespace std::chrono_literals;
using Json=nlohmann::json;
#define CHECK(x) do {if(!(x)) throw std::runtime_error(#x);}while(false)
class Classifier final:public inference::TraceClassifier {
public:
    inference::TraceModelPrediction classify(const inference::TraceFeatureVector& values) override {
        CHECK(values[2]==10); return {1,{{1,1.0}}};
    }
    bool available() const noexcept override {return true;}
    std::string modelName() const override {return "fixture";}
    std::string modelVersion() const override {return "test";}
};
}
int main() {
    try {
        auto state=std::make_shared<db::test::ClassificationDatabaseState>();
        db::DbExecutorOptions options;
        options.connection_pool.min_connections=options.connection_pool.max_connections=1;
        options.connection_pool.acquire_timeout=50ms;
        options.workers.core_threads=options.workers.max_threads=1;
        options.connection_pool.validation_interval=0ms;
        db::DbExecutor database(options,db::test::classificationFactory(state)); database.start();
        concurrency::ThreadPoolOptions cpu; cpu.core_threads=cpu.max_threads=1;cpu.queue_capacity=1;
        concurrency::ThreadPool analysis(cpu),inference(cpu); analysis.start(); inference.start();
        repository::WaterRepository repo;
        service::WaterService water(database,repo);Classifier classifier; inference::UnavailableForecaster forecaster("test");
        service::TraceService trace(database,inference,classifier,repo);
        service::ForecastService forecast(database,inference,forecaster,repo);
        service::ClassificationDataService service(database,analysis,repo);
        controller::ApiController controller(water,trace,forecast,&service);
        net::HttpRouter router;controller.registerRoutes(router);
        const auto invoke=[&](std::string path,Json body) {
            auto promise=std::make_shared<std::promise<net::HttpResponse>>(); auto future=promise->get_future();
            net::HttpRequest request;request.method=net::HttpMethod::Post;request.path=std::move(path);request.body=body.dump();
            router.dispatch(std::move(request),[promise](auto result){promise->set_value(std::move(result));});
            CHECK(future.wait_for(3s)==std::future_status::ready);return future.get();
        };
        const std::string prefix="/api/v1/data/classification/";
        const auto preview=invoke(prefix+"clean",{{"company_id",1},{"dataset","test_data"}});
        CHECK(preview.status==200);const auto data=Json::parse(preview.body).at("data");
        CHECK(data.at("sample_count")==130 && data.at("preview_after").size()==50);
        CHECK(data.at("chart_after").size()==130 && data.at("features")[2].at("outlier_count")==1);
        CHECK(data.at("preview_before")[0].at("cod").is_null());
        CHECK(!data.contains("processed_rows") && !data.at("persisted").get<bool>() && state->writes==0);
        std::cout<<"[PASS] cleaning HTTP preview with bounded JSON and untouched raw rows\n";
        const auto saved=invoke(prefix+"clean",{{"company_id",1},{"dataset","test_data"},{"persist",true}});
        CHECK(saved.status==200); const auto run=Json::parse(saved.body).at("data").at("cleaning_run_id").get<std::int64_t>();
        CHECK(run==100 && state->begins==1 && state->commits==1 && state->writes==3);
        CHECK(state->datasets.at(run)=="test" && state->last_changed_rows==2);
        CHECK(state->changed_masks.at(run).at(0)!=0 && state->changed_masks.at(run).at(50)!=0);
        const auto classified=invoke("/api/v1/inference/classify",{{"sample_id",1},{"dataset","test_data"},{"cleaning_run_id",run}});
        CHECK(classified.status==200 && Json::parse(classified.body).at("data").at("cleaning_run_id")==run);
        CHECK(invoke("/api/v1/inference/classify",{{"sample_id",1},{"dataset","test_data"}}).status==422);
        CHECK(invoke(prefix+"missing",{{"company_id",1},{"dataset","train_data"},{"cleaning_run_id",run}}).status==404);
        std::cout<<"[PASS] append-only transactional batches, saved-version classification and scope guard\n";
        CHECK(invoke(prefix+"missing",{{"company_id",1}}).status==200);
        CHECK(invoke(prefix+"distribution",{{"company_id",1},{"view","boxplot"}}).status==200);
        CHECK(invoke(prefix+"distribution",{{"company_id",1},{"view","comparison"}}).status==200);
        CHECK(invoke(prefix+"correlation",{{"company_id",1},{"method","spearman"},{"dataset","test_data"},{"cleaning_run_id",run}}).status==200);
        std::cout<<"[PASS] missing distribution comparison and saved-version correlation routes\n";
        for(const auto& body:std::vector<Json>{Json::array(),Json::object(),{{"company_id",true}},{{"company_id",1.2}},
            {{"company_id",18446744073709551615ULL}},{{"company_id",1},{"persist",1}},
            {{"company_id",1},{"window",1}},{{"company_id",1},{"threshold",0}},
            {{"company_id",1},{"feature","not_a_column"}},{{"company_id",1},{"unexpected",1}},
            {{"company_id",1},{"cleaning_run_id",-1}},{{"company_id",1},{"dataset","train_data;DROP TABLE x"}}})
            CHECK(invoke(prefix+"clean",body).status==400);
        CHECK(invoke(prefix+"correlation",{{"company_id",1},{"persist",true}}).status==400);
        CHECK(invoke(prefix+"missing",{{"company_id",2}}).status==404);
        std::cout<<"[PASS] strict argument types overflow allowlists and missing company\n";
        {std::lock_guard lock(state->mutex);state->fail_write=true;}
        CHECK(invoke(prefix+"clean",{{"company_id",1},{"persist",true}}).status==500);
        CHECK(state->rollbacks==1 && state->commits==1);
        {std::lock_guard lock(state->mutex);state->fail_write=false;for(auto& row:state->raw)row["tp"]=std::nullopt;}
        CHECK(invoke(prefix+"missing",{{"company_id",1},{"persist",true}}).status==422);
        {std::lock_guard lock(state->mutex);state->raw=db::test::classificationRows();}
        std::cout<<"[PASS] rollback on save failure and refusal to save unresolved null columns\n";
        {std::lock_guard lock(state->mutex);state->raw.clear();}
        CHECK(invoke(prefix+"missing",{{"company_id",1}}).status==404);
        {std::lock_guard lock(state->mutex);state->raw=db::test::classificationRows(50001);}
        CHECK(invoke(prefix+"missing",{{"company_id",1}}).status==413);
        {std::lock_guard lock(state->mutex);state->raw=db::test::classificationRows();state->raw[0]["temperature"]="nan";}
        CHECK(invoke(prefix+"missing",{{"company_id",1}}).status==422);
        {std::lock_guard lock(state->mutex);state->raw[0]["temperature"]="1e100";}
        CHECK(invoke(prefix+"missing",{{"company_id",1},{"persist",true}}).status==422);
        {std::lock_guard lock(state->mutex);state->raw=db::test::classificationRows();}
        std::cout<<"[PASS] empty oversized non-finite and model-range data errors\n";
        state->unavailable=true;
        const auto timeout=invoke(prefix+"missing",{{"company_id",1}});state->unavailable=false;
        CHECK(timeout.status==503 && Json::parse(timeout.body).at("error")=="DATABASE_UNAVAILABLE");
        std::cout<<"[PASS] acquisition failures before repository execution return HTTP errors\n";
        std::promise<void> cpuEntered;
        auto cpuRelease=std::make_shared<std::promise<void>>();auto cpuGate=cpuRelease->get_future().share();
        auto running=analysis.submit([&cpuEntered,cpuGate]{cpuEntered.set_value();cpuGate.wait();});cpuEntered.get_future().wait();
        auto queued=analysis.submit([]{});
        const auto busy=invoke(prefix+"missing",{{"company_id",1}});cpuRelease->set_value();running.get();queued.get();
        CHECK(busy.status==503 && Json::parse(busy.body).at("error")=="ANALYSIS_BUSY");
        std::cout<<"[PASS] bounded analysis queue applies backpressure\n";
        // Hold CPU work while the DB read finishes. Shutdown must still allow
        // the already-admitted CPU stage to submit its transactional save.
        std::promise<void> entered;
        auto release=std::make_shared<std::promise<void>>();auto gate=release->get_future().share();
        auto held=analysis.submit([&entered,gate]{entered.set_value();gate.wait();});entered.get_future().wait();
        domain::ClassificationDataRequest pending;pending.company_id=1;pending.operation="clean";pending.persist=true;
        auto promise=std::make_shared<std::promise<service::ClassificationDataResult>>();auto future=promise->get_future();
        service.execute(pending,[promise](auto result){promise->set_value(std::move(result));});
        database.submit([](db::DbConnection&){}).get();
        CHECK(database.statistics().connections.borrowed_connections==0);
        auto draining=std::async(std::launch::async,[&]{service.shutdown();});
        const bool was_waiting=draining.wait_for(30ms)==std::future_status::timeout;
        release->set_value();held.get();
        CHECK(was_waiting && future.wait_for(3s)==std::future_status::ready);
        CHECK(std::get<domain::ClassificationDataResult>(future.get()).persisted);
        CHECK(draining.wait_for(3s)==std::future_status::ready);draining.get();
        CHECK(state->commits==2);
        controller.stopAccepting(); CHECK(invoke(prefix+"missing",{{"company_id",1}}).status==503);
        domain::ClassificationDataRequest request;request.company_id=1;request.operation="missing";
        int status=0;service.execute(request,[&](auto result){status=std::get<service::ServiceError>(result).http_status;});CHECK(status==503);
        database.shutdown();analysis.shutdown();inference.shutdown();
        std::cout<<"[PASS] shutdown drains admitted DB-CPU-DB saves without retaining a DB lease\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"[FAIL] "<<e.what()<<'\n';return 1;}
}
