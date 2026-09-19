#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace water::domain {

struct Company {
    std::int64_t id{0};
    std::string name;
    std::string code;
    std::optional<std::string> task_type;
    std::optional<std::string> location;
    std::optional<std::string> description;
    std::optional<std::string> created_at;
};

struct WaterSample {
    std::int64_t id{0};
    std::optional<double> temperature;
    std::optional<double> ph;
    std::optional<double> cod;
    std::optional<double> nh3n;
    std::optional<double> tp;
    std::optional<double> water_level;
    std::optional<double> orp;
    std::optional<double> conductivity;
    std::optional<double> dissolved_oxygen;
    std::optional<double> turbidity;
    std::int64_t company_id{0};
};

struct FeatureSummary {
    std::string field;
    std::string label;
    std::uint64_t count{0};
    std::optional<double> mean;
    std::optional<double> standard_deviation;
    std::optional<double> minimum;
    std::optional<double> maximum;
};

struct DataOverview {
    Company company;
    std::string dataset;
    std::uint64_t total_rows{0};
    std::vector<WaterSample> preview_rows;
    std::vector<FeatureSummary> summary;
};

}  // namespace water::domain
