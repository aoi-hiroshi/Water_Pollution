#include "water/analysis/classification_analysis.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace water;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while(false)
bool near(double a,double b) { return std::abs(a-b)<1e-9; }
template<class F> void invalid(F function) {
    bool caught=false; try { function(); } catch(const std::invalid_argument&) { caught=true; } CHECK(caught);
}
domain::ClassificationDataRequest request(std::string operation) {
    domain::ClassificationDataRequest r; r.company_id=1; r.operation=std::move(operation); return r;
}
std::vector<domain::WaterSample> samples(std::size_t count) {
    std::vector<domain::WaterSample> result(count);
    for(std::size_t i=0;i<count;++i) {
        auto& row=result[i]; row.id=static_cast<std::int64_t>(i+1); row.company_id=1;
        for(const auto& feature:domain::kClassificationFeatures) row.*feature.member=double(i+1);
    } return result;
}
void missing() {
    auto rows=samples(5); rows[1].cod.reset(); rows[4].cod.reset();
    for(auto& row:rows) row.tp.reset();
    const auto result=analysis::analyzeClassificationData(request("missing"),rows,{});
    CHECK(result.features[2].filled_count==2 && result.features[2].missing_after==0);
    CHECK(near(*result.processed_rows[1].cod,3)); CHECK(!result.original_rows[1].cod);
    CHECK(result.features[4].missing_after==5 && !result.features[4].mean);
    CHECK(rows[1].cod==std::nullopt);
}
void cleaning() {
    auto rows=samples(53);
    for(auto& row:rows) row.cod=10;
    rows[50].cod=100; rows[51].cod=100;
    auto r=request("clean");
    auto result=analysis::analyzeClassificationData(r,rows,{});
    CHECK(result.features[2].outlier_count==2);
    CHECK(*result.processed_rows[50].cod==10 && *result.processed_rows[51].cod==10);
    CHECK(*result.original_rows[50].cod==100);
    CHECK(result.rule=="median_sliding_zscore_forward_hold_v1");
    r.window=500; result=analysis::analyzeClassificationData(r,rows,{});
    CHECK(result.features[2].outlier_count==0);
    rows=samples(3); rows[0].cod=2; rows[1].cod=4; rows[2].cod=10;
    r.window=2; r.threshold=3;
    result=analysis::analyzeClassificationData(r,rows,{});
    CHECK(result.features[2].outlier_count==1 && *result.processed_rows[2].cod==4);
}
void distribution() {
    auto r=request("distribution"); r.bins=5;
    auto result=analysis::analyzeClassificationData(r,samples(5),{});
    const auto& group=result.groups.front();
    CHECK(*group.q25==2 && *group.median==3 && *group.q75==4);
    std::size_t total=0; for(const auto& bin:group.histogram) total+=bin.count;
    CHECK(total==5 && group.histogram.back().count==1);
    auto rows=samples(5); for(auto& row:rows) row.cod=3;
    result=analysis::analyzeClassificationData(r,rows,{});
    CHECK(result.groups.front().histogram.size()==1 && result.groups.front().histogram[0].count==5);
    rows=samples(4); rows[2].company_id=rows[3].company_id=2; r.view="comparison";
    result=analysis::analyzeClassificationData(r,rows,{});
    CHECK(result.groups.size()==2);
    CHECK(result.groups[0].histogram[0].lower==result.groups[1].histogram[0].lower);
    CHECK(result.groups[0].histogram.back().upper==result.groups[1].histogram.back().upper);
    rows=samples(4); rows[1].cod=0; rows[2].cod=0; rows[0].cod=0; rows[3].cod=100;
    r.view="boxplot"; result=analysis::analyzeClassificationData(r,rows,{});
    CHECK(result.groups.front().outlier_count==1 && *result.groups.front().upper_whisker==0);
    rows=samples(33);for(std::size_t i=0;i<rows.size();++i)rows[i].company_id=static_cast<std::int64_t>(i+1);
    r.view="comparison";bool oversized=false;
    try{(void)analysis::analyzeClassificationData(r,rows,{});}catch(const std::length_error&){oversized=true;}CHECK(oversized);
}
void correlations() {
    auto rows=samples(5);
    for(std::size_t i=0;i<5;++i) { rows[i].ph=-double(i+1); rows[i].tp=8; }
    rows[0].ph.reset();
    auto r=request("correlation"); auto result=analysis::analyzeClassificationData(r,rows,{});
    CHECK(near(*result.correlation[0][1],-1)); CHECK(result.pair_counts[0][1]==4);
    CHECK(!result.correlation[4][4] && !result.correlation[0][4]);
    rows=samples(4); rows[0].cod=1;rows[1].cod=1;rows[2].cod=2;rows[3].cod=3;
    r.method="spearman"; result=analysis::analyzeClassificationData(r,rows,{});
    CHECK(near(*result.correlation[0][2],std::sqrt(.9)));
    CHECK(result.correlation[2][0]==result.correlation[0][2]);
    rows=samples(1); result=analysis::analyzeClassificationData(r,rows,{}); CHECK(!result.correlation[0][0]);
}
void bounds() {
    auto r=request("missing"); auto result=analysis::analyzeClassificationData(r,samples(1000),{});
    CHECK(result.sample_count==1000 && result.preview_after.size()==50 && result.chart_after.size()==300);
    CHECK(result.chart_after.front().id==1 && result.chart_after.back().id==1000);
    for(std::size_t i=0;i<300;++i) CHECK(result.chart_before[i].id==result.chart_after[i].id);
}
void validation() {
    auto r=request("missing");
    r.dataset="train_data;DROP TABLE x"; invalid([&]{analysis::validateClassificationRequest(r);});
    r=request("clean");r.window=1; invalid([&]{analysis::validateClassificationRequest(r);});
    r=request("clean");r.threshold=std::numeric_limits<double>::infinity();invalid([&]{analysis::validateClassificationRequest(r);});
    r=request("correlation");r.persist=true;invalid([&]{analysis::validateClassificationRequest(r);});
    r=request("distribution");r.view="comparison";r.cleaning_run_id=1;invalid([&]{analysis::validateClassificationRequest(r);});
    r=request("missing");auto rows=samples(2);rows[1].company_id=2;
    invalid([&]{(void)analysis::analyzeClassificationData(r,rows,{});});
    rows=samples(2);rows[1].id=rows[0].id;invalid([&]{(void)analysis::analyzeClassificationData(r,rows,{});});
    rows=samples(2);rows[0].cod=std::numeric_limits<double>::quiet_NaN();invalid([&]{(void)analysis::analyzeClassificationData(r,rows,{});});
    invalid([&]{(void)analysis::analyzeClassificationData(r,{},{});});
    invalid([&]{(void)analysis::analyzeClassificationData(r,samples(50001),{});});
}
}
int main() {
    try {
        for(const auto& test: {std::pair{"median and all-null policy",missing},
            {"notebook-compatible zscore and forward hold",cleaning}, {"quantiles histogram box company bins",distribution},
            {"pairwise Pearson and tied Spearman",correlations}, {"bounded aligned previews",bounds},
            {"strict validation and invalid data",validation}}) {
            test.second(); std::cout<<"[PASS] "<<test.first<<'\n';
        } return 0;
    } catch(const std::exception& e) { std::cerr<<"[FAIL] "<<e.what()<<'\n';return 1; }
}
