#pragma once

#include "water/domain/water_data.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace water::domain {

struct ClassificationFeature {
    const char* field;
    const char* label;
    const char* unit;
    std::optional<double> WaterSample::*member;
};

inline constexpr std::array<ClassificationFeature, 10> kClassificationFeatures{{
    {"temperature", "水温", "℃", &WaterSample::temperature},
    {"ph", "pH", "无量纲", &WaterSample::ph},
    {"cod", "COD", "mg/L", &WaterSample::cod},
    {"nh3n", "氨氮", "mg/L", &WaterSample::nh3n},
    {"tp", "总磷", "mg/L", &WaterSample::tp},
    {"water_level", "液位", "无", &WaterSample::water_level},
    {"orp", "ORP", "无", &WaterSample::orp},
    {"conductivity", "电导率", "μS/cm", &WaterSample::conductivity},
    {"dissolved_oxygen", "溶解氧", "mg/L", &WaterSample::dissolved_oxygen},
    {"turbidity", "浊度", "无", &WaterSample::turbidity},
}};

inline constexpr std::size_t kClassificationRowLimit = 50'000;
inline constexpr std::size_t kClassificationChartLimit = 300;
inline constexpr std::size_t kClassificationGroupLimit = 32;

struct ClassificationDataRequest {
    std::int64_t company_id{0};
    std::string dataset{"train_data"};
    std::string operation;  // missing, clean, distribution, correlation
    std::int64_t cleaning_run_id{0}; // 0 = raw table, otherwise immutable version
    bool persist{false};
    std::size_t window{50};
    double threshold{3.0};
    std::string feature{"cod"};
    std::string view{"histogram"}; // histogram, boxplot, comparison
    std::size_t bins{50};
    std::string method{"pearson"};
};

struct ClassificationFeatureStats {
    std::string field, label, unit;
    std::size_t valid_count{0}, missing_before{0}, missing_after{0};
    std::size_t filled_count{0}, outlier_count{0}, changed_count{0};
    std::optional<double> mean, standard_deviation, minimum, maximum;
    std::optional<double> q10, q25, median, q75, q90;
};

struct HistogramBin {
    double lower{0}, upper{0};
    std::size_t count{0};
};

struct DistributionGroup {
    std::int64_t company_id{0};
    std::string company_name;
    std::size_t valid_count{0}, missing_count{0}, outlier_count{0};
    std::optional<double> minimum, maximum, q10, q25, median, q75, q90;
    std::optional<double> lower_whisker, upper_whisker;
    std::vector<HistogramBin> histogram;
};

using CorrelationMatrix = std::array<std::array<std::optional<double>, 10>, 10>;
using PairCountMatrix = std::array<std::array<std::size_t, 10>, 10>;

struct CorrelationPair {
    std::string first, second;
    double coefficient{0};
    std::size_t sample_count{0};
};

struct ClassificationDataResult {
    std::int64_t company_id{0};
    std::string company_name, dataset, operation, rule;
    std::int64_t input_cleaning_run_id{0}, cleaning_run_id{0};
    std::size_t sample_count{0};
    bool persisted{false};
    std::vector<std::string> warnings;
    std::vector<ClassificationFeatureStats> features;
    // Full snapshots are only used internally for an append-only save. HTTP
    // responses contain at most 50 preview rows and 300 aligned chart points.
    std::vector<WaterSample> original_rows, processed_rows;
    std::vector<WaterSample> preview_before, preview_after, chart_before, chart_after;
    std::vector<DistributionGroup> groups;
    CorrelationMatrix correlation{};
    PairCountMatrix pair_counts{};
    std::vector<CorrelationPair> high_correlations;
};

} // namespace water::domain
