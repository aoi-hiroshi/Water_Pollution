#include "water/controller/api_controller.h"

#include <nlohmann/json.hpp>

#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <variant>

namespace water::controller {

namespace {

using Json = nlohmann::json;

net::HttpResponse jsonResponse(int status, Json body) {
    net::HttpResponse response;
    response.status = status;
    response.body = body.dump();
    return response;
}

net::HttpResponse success(Json data, std::string message = "success") {
    return jsonResponse(
        200, Json{{"code", 200}, {"message", std::move(message)},
                  {"data", std::move(data)}});
}

net::HttpResponse failure(const service::ServiceError& error) {
    return jsonResponse(
        error.http_status,
        Json{{"code", error.http_status}, {"error", error.code},
             {"message", error.message}, {"data", nullptr}});
}

net::HttpResponse badRequest(std::string message) {
    return failure(service::ServiceError{
        400, "INVALID_ARGUMENT", std::move(message)});
}

template <typename Value>
Json nullable(const std::optional<Value>& value) {
    return value ? Json(*value) : Json(nullptr);
}

Json companyJson(const domain::Company& company) {
    return Json{
        {"company_id", company.id},
        {"company_name", company.name},
        {"company_code", company.code},
        {"task_type", nullable(company.task_type)},
        {"location", nullable(company.location)},
        {"description", nullable(company.description)},
        {"created_at", nullable(company.created_at)},
    };
}

Json sampleJson(const domain::WaterSample& sample) {
    return Json{
        {"id", sample.id},
        {"temperature", nullable(sample.temperature)},
        {"ph", nullable(sample.ph)},
        {"cod", nullable(sample.cod)},
        {"nh3n", nullable(sample.nh3n)},
        {"tp", nullable(sample.tp)},
        {"water_level", nullable(sample.water_level)},
        {"orp", nullable(sample.orp)},
        {"conductivity", nullable(sample.conductivity)},
        {"dissolved_oxygen", nullable(sample.dissolved_oxygen)},
        {"turbidity", nullable(sample.turbidity)},
        {"company_id", sample.company_id},
        {"sample_index", sample.sample_index},
        {"sampled_at", nullable(sample.sampled_at)},
    };
}

Json overviewJson(const domain::DataOverview& overview) {
    Json preview = Json::array();
    for (const auto& sample : overview.preview_rows) {
        preview.push_back(sampleJson(sample));
    }
    Json summary = Json::array();
    for (const auto& item : overview.summary) {
        summary.push_back(Json{
            {"field", item.field},
            {"label", item.label},
            {"count", item.count},
            {"mean", nullable(item.mean)},
            {"std", nullable(item.standard_deviation)},
            {"min", nullable(item.minimum)},
            {"max", nullable(item.maximum)},
        });
    }
    return Json{
        {"company", companyJson(overview.company)},
        {"dataset", overview.dataset},
        {"total_rows", overview.total_rows},
        {"preview_rows", std::move(preview)},
        {"summary", std::move(summary)},
    };
}

Json traceCandidateJson(const domain::TraceCandidate& candidate) {
    return Json{
        {"rank", candidate.rank},
        {"class_label", candidate.class_label},
        {"company_id", candidate.company_id},
        {"company_name", candidate.company_name},
        {"company_code", candidate.company_code},
        {"probability", candidate.probability},
    };
}

Json traceResultJson(const domain::TraceResult& result) {
    Json candidates = Json::array();
    for (const auto& candidate : result.candidates) {
        candidates.push_back(traceCandidateJson(candidate));
    }
    return Json{
        {"sample_id", result.sample_id},
        {"dataset", result.dataset},
        {"cleaning_run_id", result.cleaning_run_id},
        {"model", Json{{"name", result.model_name},
                        {"version", result.model_version}}},
        {"predicted", traceCandidateJson(result.predicted)},
        {"candidates", std::move(candidates)},
    };
}

Json forecastPointJson(const domain::ForecastPoint& point) {
    return Json{{"step", point.step}, {"cod", point.values[0]},
                {"nh3n", point.values[1]}, {"tp", point.values[2]},
                {"turbidity", point.values[3]}};
}

Json forecastResultJson(const domain::ForecastResult& result) {
    Json history = Json::array(), predictions = Json::array();
    for (const auto& point : result.history) { history.push_back(forecastPointJson(point)); }
    for (const auto& point : result.predictions) { predictions.push_back(forecastPointJson(point)); }
    return Json{
        {"company_id", result.company_id}, {"company_name", result.company_name},
        {"dataset", result.dataset}, {"lookback", result.lookback},
        {"start_sample_id", result.start_sample_id}, {"end_sample_id", result.end_sample_id},
        {"horizon", result.predictions.size()}, {"axis", "sample_step"}, {"sampling_interval_seconds", nullptr},
        {"model", Json{{"name", result.model_name}, {"version", result.model_version}}},
        {"history", std::move(history)}, {"predictions", std::move(predictions)}};
}

std::optional<std::int64_t> jsonInteger(const Json& value) {
    if (value.is_number_unsigned()) {
        const auto integer = value.get<std::uint64_t>();
        if (integer > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) { return std::nullopt; }
        return static_cast<std::int64_t>(integer);
    }
    if (value.is_number_integer()) { return value.get<std::int64_t>(); }
    return std::nullopt;
}

Json classificationDataJson(const domain::ClassificationDataResult& result,
                            const domain::ClassificationDataRequest& request) {
    Json features=Json::array(), before=Json::array(), after=Json::array();
    Json chart_before=Json::array(), chart_after=Json::array(), groups=Json::array();
    for (const auto& row : result.preview_before) { before.push_back(sampleJson(row)); }
    for (const auto& row : result.preview_after) { after.push_back(sampleJson(row)); }
    for (const auto& row : result.chart_before) { chart_before.push_back(sampleJson(row)); }
    for (const auto& row : result.chart_after) { chart_after.push_back(sampleJson(row)); }
    for (const auto& item : result.features) {
        features.push_back(Json{{"field",item.field},{"label",item.label},{"unit",item.unit},
            {"valid_count",item.valid_count},{"missing_before",item.missing_before},
            {"missing_after",item.missing_after},{"filled_count",item.filled_count},
            {"outlier_count",item.outlier_count},{"changed_count",item.changed_count},
            {"mean",nullable(item.mean)},{"std",nullable(item.standard_deviation)},
            {"min",nullable(item.minimum)},{"max",nullable(item.maximum)},
            {"q10",nullable(item.q10)},{"q25",nullable(item.q25)},
            {"median",nullable(item.median)},{"q75",nullable(item.q75)},{"q90",nullable(item.q90)}});
    }
    for (const auto& item : result.groups) {
        Json bins=Json::array();
        for (const auto& bin : item.histogram) {
            bins.push_back(Json{{"lower",bin.lower},{"upper",bin.upper},{"count",bin.count}});
        }
        groups.push_back(Json{{"company_id",item.company_id},{"company_name",item.company_name},
            {"valid_count",item.valid_count},{"missing_count",item.missing_count},
            {"outlier_count",item.outlier_count},{"min",nullable(item.minimum)},
            {"max",nullable(item.maximum)},{"q10",nullable(item.q10)},
            {"q25",nullable(item.q25)},{"median",nullable(item.median)},
            {"q75",nullable(item.q75)},{"q90",nullable(item.q90)},
            {"lower_whisker",nullable(item.lower_whisker)},
            {"upper_whisker",nullable(item.upper_whisker)},{"histogram",std::move(bins)}});
    }
    Json matrix=Json::array(), pairs=Json::array();
    if (request.operation=="correlation") {
        for (const auto& row : result.correlation) {
            Json cells=Json::array();
            for (const auto& cell : row) { cells.push_back(nullable(cell)); }
            matrix.push_back(std::move(cells));
        }
    }
    for (const auto& pair : result.high_correlations) {
        pairs.push_back(Json{{"first",pair.first},{"second",pair.second},
                            {"coefficient",pair.coefficient},{"sample_count",pair.sample_count}});
    }
    return Json{{"company_id",result.company_id},{"company_name",result.company_name},
        {"dataset",result.dataset},{"operation",result.operation},{"rule",result.rule},
        {"input_cleaning_run_id",result.input_cleaning_run_id},{"cleaning_run_id",result.cleaning_run_id},
        {"persisted",result.persisted},{"sample_count",result.sample_count},
        {"axis","sample_order"},{"row_limit",domain::kClassificationRowLimit},
        {"preview_limit",50},{"chart_limit",domain::kClassificationChartLimit},
        {"parameters",Json{{"window",request.window},{"threshold",request.threshold},
            {"feature",request.feature},{"view",request.view},{"bins",request.bins},{"method",request.method}}},
        {"features",std::move(features)},{"preview_before",std::move(before)},
        {"preview_after",std::move(after)},{"chart_before",std::move(chart_before)},
        {"chart_after",std::move(chart_after)},{"groups",std::move(groups)},
        {"correlation",std::move(matrix)},{"pair_counts",result.pair_counts},
        {"high_correlations",std::move(pairs)},{"warnings",result.warnings}};
}

Json forecastDataJson(const domain::ForecastDataResult& result,
                      const domain::ForecastDataRequest& request) {
    Json features = Json::array();
    Json metrics = Json::array();
    Json before = Json::array();
    Json after = Json::array();
    Json chart_before = Json::array();
    Json chart_after = Json::array();
    for (const auto& item : result.features) {
        features.push_back(Json{{"field",item.field},{"label",item.label},{"unit",item.unit},
            {"valid_count",item.valid_count},{"missing_before",item.missing_before},
            {"missing_after",item.missing_after},{"filled_count",item.filled_count},
            {"changed_count",item.changed_count},{"mean",nullable(item.mean)},
            {"std",nullable(item.standard_deviation)},{"min",nullable(item.minimum)},
            {"max",nullable(item.maximum)},{"q10",nullable(item.q10)},
            {"q25",nullable(item.q25)},{"median",nullable(item.median)},
            {"q75",nullable(item.q75)},{"q90",nullable(item.q90)}});
    }
    for (const auto& item : result.metrics) {
        metrics.push_back(Json{{"key",item.key},{"label",item.label},
            {"value",nullable(item.value)},{"text",item.text}});
    }
    for (const auto& row : result.preview_before) { before.push_back(sampleJson(row)); }
    for (const auto& row : result.preview_after) { after.push_back(sampleJson(row)); }
    for (const auto& row : result.chart_before) { chart_before.push_back(sampleJson(row)); }
    for (const auto& row : result.chart_after) { chart_after.push_back(sampleJson(row)); }
    return Json{{"company_id",result.company_id},{"company_name",result.company_name},
        {"dataset",result.dataset},{"operation",result.operation},{"rule",result.rule},
        {"sample_count",result.sample_count},{"timestamp_count",result.timestamp_count},
        {"sequence_gap_count",result.sequence_gap_count},{"axis","sample_index"},
        {"parameters",Json{{"feature",request.feature},{"window",request.window},
            {"horizon",request.horizon},{"diagnostic",request.diagnostic}}},
        {"features",std::move(features)},{"metrics",std::move(metrics)},
        {"preview_before",std::move(before)},{"preview_after",std::move(after)},
        {"chart_before",std::move(chart_before)},{"chart_after",std::move(chart_after)},
        {"warnings",result.warnings}};
}

template <typename Integer>
std::optional<Integer> parseInteger(const std::string* text) {
    if (text == nullptr || text->empty()) {
        return std::nullopt;
    }
    Integer result{};
    const auto conversion =
        std::from_chars(text->data(), text->data() + text->size(), result);
    if (conversion.ec != std::errc{} ||
        conversion.ptr != text->data() + text->size()) {
        return std::nullopt;
    }
    return result;
}

}  // namespace

ApiController::ApiController(service::WaterService& service,
                             service::TraceService& trace_service,
                             service::ForecastService& forecast_service,
                             service::ClassificationDataService* classification_service,
                             service::ForecastDataService* forecast_data_service)
    : service_(service), trace_service_(trace_service), forecast_service_(forecast_service),
      classification_service_(classification_service),
      forecast_data_service_(forecast_data_service) {}

void ApiController::registerRoutes(net::HttpRouter& router) {
    const auto add=[this,&router](bool post, const char* path, auto member) {
        auto handler=[this,member](net::HttpRequest request, net::HttpReply reply) {
            if (!accepting_.load()) {
                reply(failure(service::ServiceError{503,"SERVER_STOPPING","server is stopping"})); return;
            }
            (this->*member)(std::move(request),std::move(reply));
        };
        if (post) { router.post(path,std::move(handler)); } else { router.get(path,std::move(handler)); }
    };
    add(false,"/api/v1/health",&ApiController::health);
    add(false,"/api/v1/companies",&ApiController::companies);
    add(false,"/api/v1/data/overview",&ApiController::overview);
    add(true,"/api/v1/inference/classify",&ApiController::classifyTrace);
    add(false,"/api/v1/inference/forecast/model",&ApiController::forecastModel);
    add(true,"/api/v1/inference/forecast",&ApiController::forecast);
    for (const std::string operation : {"missing","clean","distribution","correlation"}) {
        router.post("/api/v1/data/classification/"+operation,
            [this,operation](net::HttpRequest request,net::HttpReply reply) {
                if (!accepting_.load()) {
                    reply(failure(service::ServiceError{503,"SERVER_STOPPING","server is stopping"})); return;
                }
                classificationData(operation,std::move(request),std::move(reply));
            });
    }
    for (const std::string operation : {"sequence","missing","smooth","window","diagnosis"}) {
        router.post("/api/v1/data/forecast/"+operation,
            [this,operation](net::HttpRequest request,net::HttpReply reply) {
                if (!accepting_.load()) {
                    reply(failure(service::ServiceError{503,"SERVER_STOPPING","server is stopping"})); return;
                }
                forecastData(operation,std::move(request),std::move(reply));
            });
    }
}

void ApiController::forecastData(std::string operation, net::HttpRequest request,
                                 net::HttpReply reply) {
    domain::ForecastDataRequest options;
    options.operation = std::move(operation);
    try {
        const auto body = Json::parse(request.body);
        if (!body.is_object() || !body.contains("company_id")) {
            reply(badRequest("JSON object with company_id is required")); return;
        }
        const std::set<std::string> allowed{
            "company_id","dataset","feature","window","horizon","diagnostic"};
        for (auto item=body.begin(); item!=body.end(); ++item) {
            if (!allowed.contains(item.key())) {
                reply(badRequest("unknown argument: "+item.key())); return;
            }
        }
        const auto company_id = jsonInteger(body.at("company_id"));
        if (!company_id || *company_id <= 0) {
            reply(badRequest("company_id must be a positive int64")); return;
        }
        options.company_id = *company_id;
        for (const auto& [key,destination] :
             std::initializer_list<std::pair<const char*,std::string*>>{
                 {"dataset",&options.dataset},{"feature",&options.feature},
                 {"diagnostic",&options.diagnostic}}) {
            if (body.contains(key)) {
                if (!body.at(key).is_string()) {
                    reply(badRequest(std::string{key}+" must be a string")); return;
                }
                *destination = body.at(key).get<std::string>();
            }
        }
        for (const auto& [key,destination] :
             std::initializer_list<std::pair<const char*,std::size_t*>>{
                 {"window",&options.window},{"horizon",&options.horizon}}) {
            if (body.contains(key)) {
                const auto value = jsonInteger(body.at(key));
                if (!value || *value < 0 || *value > 1440) {
                    reply(badRequest(std::string{key}+" is out of range")); return;
                }
                *destination = static_cast<std::size_t>(*value);
            }
        }
        analysis::validateForecastDataRequest(options);
    } catch (const std::exception& error) {
        reply(badRequest(error.what())); return;
    }
    if (!forecast_data_service_) {
        reply(failure(service::ServiceError{
            503,"ANALYSIS_UNAVAILABLE","forecast data service is not configured"})); return;
    }
    forecast_data_service_->execute(options,
        [options,reply=std::move(reply)](service::ForecastDataResult result) mutable {
            try {
                if (const auto* error=std::get_if<service::ServiceError>(&result)) {
                    reply(failure(*error)); return;
                }
                reply(success(forecastDataJson(
                    std::get<domain::ForecastDataResult>(result),options),
                    "forecast preprocessing completed"));
            } catch (...) {
                reply(failure(service::ServiceError{
                    500,"INTERNAL_ERROR","failed to encode forecast preprocessing result"}));
            }
        });
}

void ApiController::classificationData(std::string operation, net::HttpRequest request,
                                      net::HttpReply reply) {
    domain::ClassificationDataRequest options;
    options.operation=std::move(operation);
    try {
        const auto body=Json::parse(request.body);
        if (!body.is_object() || !body.contains("company_id")) {
            reply(badRequest("JSON object with company_id is required")); return;
        }
        const std::set<std::string> allowed{"company_id","dataset","cleaning_run_id","persist",
            "window","threshold","feature","view","bins","method"};
        for (auto item=body.begin(); item!=body.end(); ++item) {
            if (!allowed.contains(item.key())) { reply(badRequest("unknown argument: "+item.key())); return; }
        }
        const auto id=jsonInteger(body.at("company_id"));
        if (!id || *id<=0) { reply(badRequest("company_id must be a positive int64")); return; }
        options.company_id=*id;
        for (const auto& [key,destination] : std::initializer_list<std::pair<const char*,std::string*>>{
            {"dataset",&options.dataset},{"feature",&options.feature},{"view",&options.view},{"method",&options.method}}) {
            if (body.contains(key)) {
                if (!body.at(key).is_string()) { reply(badRequest(std::string{key}+" must be a string")); return; }
                *destination=body.at(key).get<std::string>();
            }
        }
        if (body.contains("cleaning_run_id")) {
            const auto run=jsonInteger(body.at("cleaning_run_id"));
            if (!run || *run<0) { reply(badRequest("cleaning_run_id must be a nonnegative int64")); return; }
            options.cleaning_run_id=*run;
        }
        if (body.contains("persist")) {
            if (!body.at("persist").is_boolean()) { reply(badRequest("persist must be boolean")); return; }
            options.persist=body.at("persist").get<bool>();
        }
        for (const auto& [key,destination] : std::initializer_list<std::pair<const char*,std::size_t*>>{
            {"window",&options.window},{"bins",&options.bins}}) {
            if (body.contains(key)) {
                const auto value=jsonInteger(body.at(key));
                if (!value || *value<0 || *value>500) { reply(badRequest(std::string{key}+" is out of range")); return; }
                *destination=static_cast<std::size_t>(*value);
            }
        }
        if (body.contains("threshold")) {
            if (!body.at("threshold").is_number()) { reply(badRequest("threshold must be numeric")); return; }
            options.threshold=body.at("threshold").get<double>();
        }
        analysis::validateClassificationRequest(options);
    } catch (const std::exception& error) { reply(badRequest(error.what())); return; }
    if (!classification_service_) {
        reply(failure(service::ServiceError{503,"ANALYSIS_UNAVAILABLE","classification data service is not configured"})); return;
    }
    classification_service_->execute(options,
        [options,reply=std::move(reply)](service::ClassificationDataResult result) mutable {
            try {
                if (const auto* error=std::get_if<service::ServiceError>(&result)) { reply(failure(*error)); return; }
                reply(success(classificationDataJson(std::get<domain::ClassificationDataResult>(result),options)));
            } catch (...) { reply(failure(service::ServiceError{500,"INTERNAL_ERROR","failed to encode analysis result"})); }
        });
}

void ApiController::health(net::HttpRequest, net::HttpReply reply) const {
    reply(success(Json{{"backend", "muduo"},
                       {"status", "running"},
                       {"api_version", "v1"},
                       {"trace_inference",
                        Json{{"available", trace_service_.modelAvailable()},
                             {"model", trace_service_.modelName()},
                             {"version", trace_service_.modelVersion()}}},
                       {"forecast_inference",
                        Json{{"available", forecast_service_.modelAvailable()},
                             {"model", forecast_service_.modelName()},
                             {"version", forecast_service_.modelVersion()}}}},
                  "Muduo backend is running"));
}

void ApiController::companies(net::HttpRequest, net::HttpReply reply) {
    service_.listCompanies(
        [reply = std::move(reply)](service::CompaniesResult result) mutable {
            try {
                if (const auto* error =
                        std::get_if<service::ServiceError>(&result)) {
                    reply(failure(*error));
                    return;
                }
                Json companies = Json::array();
                for (const auto& company :
                     std::get<std::vector<domain::Company>>(result)) {
                    companies.push_back(companyJson(company));
                }
                reply(success(std::move(companies),
                              "company list loaded"));
            } catch (...) {
                reply(failure(service::ServiceError{}));
            }
        });
}

void ApiController::overview(net::HttpRequest request,
                             net::HttpReply reply) {
    const auto company_id =
        parseInteger<std::int64_t>(request.queryValue("company_id"));
    if (!company_id || *company_id <= 0) {
        reply(badRequest("company_id must be a positive integer"));
        return;
    }

    const auto* dataset_value = request.queryValue("dataset");
    std::string dataset = dataset_value == nullptr ? "train_data"
                                                    : *dataset_value;
    if (dataset != "train_data" && dataset != "val_data" &&
        dataset != "test_data") {
        reply(badRequest("dataset must be train_data, val_data or test_data"));
        return;
    }

    std::size_t limit = 10;
    if (request.queryValue("limit") != nullptr) {
        const auto parsed =
            parseInteger<std::size_t>(request.queryValue("limit"));
        if (!parsed || *parsed == 0 || *parsed > 50) {
            reply(badRequest("limit must be between 1 and 50"));
            return;
        }
        limit = *parsed;
    }

    service_.getOverview(
        *company_id, std::move(dataset), limit,
        [reply = std::move(reply)](service::OverviewResult result) mutable {
            try {
                if (const auto* error =
                        std::get_if<service::ServiceError>(&result)) {
                    reply(failure(*error));
                    return;
                }
                reply(success(overviewJson(
                                  std::get<domain::DataOverview>(result)),
                              "data overview loaded"));
            } catch (...) {
                reply(failure(service::ServiceError{}));
            }
        });
}

void ApiController::classifyTrace(net::HttpRequest request,
                                  net::HttpReply reply) {
    const Json body = Json::parse(request.body, nullptr, false);
    if (body.is_discarded() || !body.is_object()) {
        reply(badRequest("request body must be a JSON object"));
        return;
    }

    if (!body.contains("sample_id") ||
        (!body["sample_id"].is_number_integer() &&
         !body["sample_id"].is_number_unsigned())) {
        reply(badRequest("sample_id must be a positive integer"));
        return;
    }

    std::int64_t sample_id = 0;
    try {
        sample_id = jsonInteger(body["sample_id"]).value_or(0);
    } catch (...) {
        reply(badRequest("sample_id must be a positive integer"));
        return;
    }
    if (sample_id <= 0) {
        reply(badRequest("sample_id must be a positive integer"));
        return;
    }

    std::string dataset = "test_data";
    if (body.contains("dataset")) {
        if (!body["dataset"].is_string()) {
            reply(badRequest("dataset must be a string"));
            return;
        }
        dataset = body["dataset"].get<std::string>();
    }
    if (dataset != "train_data" && dataset != "test_data") {
        reply(badRequest("dataset must be train_data or test_data"));
        return;
    }

    std::int64_t cleaning_run_id = 0;
    if (body.contains("cleaning_run_id")) {
        const auto run = jsonInteger(body["cleaning_run_id"]);
        if (!run || *run < 0) { reply(badRequest("cleaning_run_id must be a nonnegative int64")); return; }
        cleaning_run_id = *run;
    }
    trace_service_.classifySample(
        sample_id, std::move(dataset),
        [reply = std::move(reply)](service::TraceResult result) mutable {
            try {
                if (const auto* error =
                        std::get_if<service::ServiceError>(&result)) {
                    reply(failure(*error));
                    return;
                }
                reply(success(
                    traceResultJson(std::get<domain::TraceResult>(result)),
                    "trace classification completed"));
            } catch (...) {
                reply(failure(service::ServiceError{
                    500, "INTERNAL_ERROR", "failed to encode trace result"}));
            }
        }, cleaning_run_id);
}

void ApiController::forecastModel(net::HttpRequest, net::HttpReply reply) const {
    reply(success(Json{
        {"available", forecast_service_.modelAvailable()},
        {"name", forecast_service_.modelName()}, {"version", forecast_service_.modelVersion()},
        {"lookback", inference::kForecastLookback}, {"max_horizon", inference::kForecastHorizon},
        {"input_features", inference::kForecastFeatures}, {"targets", inference::kForecastTargets},
        {"axis", "sample_step"}, {"sampling_interval_seconds", nullptr},
        {"input_preprocessing", "cleaned_physical_values"}}));
}

void ApiController::forecast(net::HttpRequest request, net::HttpReply reply) {
    const auto body = Json::parse(request.body, nullptr, false);
    if (body.is_discarded() || !body.is_object() || !body.contains("company_id")) {
        reply(badRequest("request body must include company_id"));
        return;
    }
    const auto company_id = jsonInteger(body["company_id"]);
    const auto end_sample_id = body.contains("end_sample_id") ? jsonInteger(body["end_sample_id"]) : std::optional<std::int64_t>{0};
    const auto horizon = body.contains("horizon") ? jsonInteger(body["horizon"]) : std::optional<std::int64_t>{inference::kForecastHorizon};
    if (!company_id || *company_id <= 0 || !end_sample_id || *end_sample_id < 0 ||
        !horizon || *horizon < 1 || *horizon > static_cast<std::int64_t>(inference::kForecastHorizon)) {
        reply(badRequest("company_id must be positive; end_sample_id >= 0; horizon between 1 and 10"));
        return;
    }
    std::string dataset = "test_data";
    if (body.contains("dataset")) {
        if (!body["dataset"].is_string()) { reply(badRequest("dataset must be a string")); return; }
        dataset = body["dataset"].get<std::string>();
    }
    forecast_service_.forecast(*company_id, std::move(dataset), *end_sample_id, static_cast<std::size_t>(*horizon),
        [reply = std::move(reply)](service::ForecastResult result) mutable {
            try {
                if (const auto* error = std::get_if<service::ServiceError>(&result)) { reply(failure(*error)); return; }
                reply(success(forecastResultJson(std::get<domain::ForecastResult>(result)), "forecast completed"));
            } catch (...) {
                reply(failure(service::ServiceError{500, "INTERNAL_ERROR", "failed to encode forecast result"}));
            }
        });
}

}  // namespace water::controller
