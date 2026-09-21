#pragma once

#include "water/domain/classification_data.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace water::domain {

// Physical-unit values in the order COD, NH3-N, TP, turbidity.
struct ForecastPoint {
    std::size_t step{};
    std::array<double, 4> values{};
};

struct ForecastResult {
    std::int64_t company_id{};
    std::string company_name;
    std::string dataset;
    std::int64_t start_sample_id{};
    std::int64_t end_sample_id{};
    std::size_t lookback{};
    std::string model_name;
    std::string model_version;
    std::vector<ForecastPoint> history;
    std::vector<ForecastPoint> predictions;
};

inline constexpr std::size_t kForecastDataRowLimit = 100'000;
inline constexpr std::size_t kForecastDataChartLimit = 300;

struct ForecastDataRequest {
    std::int64_t company_id{7};
    std::string dataset{"train_data"};
    std::string operation;  // sequence, missing, smooth, window, diagnosis
    std::string feature{"cod"};
    std::size_t window{24};
    std::size_t horizon{24};
    std::string diagnostic{"trend"}; // trend, seasonality, stationarity
};

struct ForecastMetric {
    std::string key;
    std::string label;
    std::optional<double> value;
    std::string text;
};

struct ForecastDataResult {
    std::int64_t company_id{0};
    std::string company_name;
    std::string dataset;
    std::string operation;
    std::string rule;
    std::size_t sample_count{0};
    std::size_t timestamp_count{0};
    std::size_t sequence_gap_count{0};
    std::vector<ClassificationFeatureStats> features;
    std::vector<ForecastMetric> metrics;
    std::vector<std::string> warnings;
    std::vector<WaterSample> preview_before;
    std::vector<WaterSample> preview_after;
    std::vector<WaterSample> chart_before;
    std::vector<WaterSample> chart_after;
};

} // namespace water::domain
