#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
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

}  // namespace water::domain
