#include "water/analysis/forecast_data_analysis.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace water;
#define CHECK(expression) do { if (!(expression)) throw std::runtime_error(#expression); } while(false)

domain::Company company() {
    return domain::Company{.id=7,.name="forecast-company",.code="F7",
        .task_type="forecast",.location=std::nullopt,
        .description=std::nullopt,.created_at=std::nullopt};
}

domain::ForecastDataRequest request(std::string operation) {
    domain::ForecastDataRequest value;
    value.operation=std::move(operation);
    return value;
}

std::vector<domain::WaterSample> samples(std::size_t count) {
    std::vector<domain::WaterSample> rows(count);
    for (std::size_t index=0; index<count; ++index) {
        auto& row=rows[index];
        row.id=static_cast<std::int64_t>(index+1);
        row.sample_index=static_cast<std::int64_t>(index+1);
        row.company_id=7;
        row.sampled_at="2026-01-01 00:"+std::to_string(index);
        for (const auto& feature : domain::kClassificationFeatures) {
            row.*feature.member=static_cast<double>(index+1);
        }
    }
    return rows;
}

void validation() {
    auto value=request("sequence");
    value.dataset="val_data";
    analysis::validateForecastDataRequest(value);
    value.company_id=1;
    bool caught=false;
    try { analysis::validateForecastDataRequest(value); }
    catch (const std::invalid_argument&) { caught=true; }
    CHECK(caught);
}

void missingAndSmooth() {
    auto rows=samples(6);
    rows[0].cod.reset(); rows[2].cod.reset(); rows[5].cod.reset();
    auto result=analysis::analyzeForecastData(request("missing"),company(),rows);
    CHECK(result.features[2].missing_before==3);
    CHECK(result.features[2].missing_after==0);
    CHECK(*result.preview_after[0].cod==2.0);
    CHECK(*result.preview_after[2].cod==3.0);
    CHECK(*result.preview_after[5].cod==5.0);

    auto smooth_request=request("smooth"); smooth_request.window=2;
    result=analysis::analyzeForecastData(smooth_request,company(),samples(6));
    CHECK(std::abs(*result.preview_after[1].cod-1.5)<1e-9);
    CHECK(result.rule=="linear_interpolation_causal_moving_average_v1");
}

void windowsAndDiagnostics() {
    auto rows=samples(150);
    auto window_request=request("window"); window_request.horizon=10;
    auto result=analysis::analyzeForecastData(window_request,company(),rows);
    const auto windows=std::find_if(result.metrics.begin(),result.metrics.end(),
        [](const auto& metric) { return metric.key=="window_count"; });
    CHECK(windows!=result.metrics.end());
    CHECK(*windows->value==21.0);

    auto diagnostic=request("diagnosis"); diagnostic.diagnostic="trend";
    result=analysis::analyzeForecastData(diagnostic,company(),std::move(rows));
    CHECK(result.rule=="least_squares_linear_trend_v1");
    CHECK(result.metrics.size()>=5);
}
}

int main() {
    try {
        validation();
        missingAndSmooth();
        windowsAndDiagnostics();
        std::cout << "[PASS] forecast preprocessing analysis\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
