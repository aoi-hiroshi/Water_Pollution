#pragma once

#include "water/inference/inference_error.h"

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace water::inference {

inline constexpr std::size_t kForecastLookback = 120;
inline constexpr std::size_t kForecastHorizon = 10;
inline constexpr std::size_t kForecastFeatureCount = 10;
inline constexpr std::size_t kForecastTargetCount = 4;
inline constexpr const char* kForecastContract = "water.forecast.raw.v1";
inline constexpr std::array<std::string_view, 10> kForecastFeatures{
    "cod", "nh3n", "tp", "turbidity", "temperature", "ph",
    "water_level", "orp", "conductivity", "dissolved_oxygen"};
inline constexpr std::array<std::string_view, 4> kForecastTargets{
    "cod", "nh3n", "tp", "turbidity"};

using ForecastValues = std::array<float, kForecastTargetCount>;
using ForecastModelPrediction = std::vector<ForecastValues>;

// Row-major [1, 120, 10], oldest row first. Values are cleaned physical
// measurements, NOT standardized. The exported graph owns both scalers.
class Forecaster {
public:
    virtual ~Forecaster() = default;
    [[nodiscard]] virtual ForecastModelPrediction forecast(
        std::span<const float> window) = 0;
    [[nodiscard]] virtual std::string modelName() const = 0;
    [[nodiscard]] virtual std::string modelVersion() const = 0;
    [[nodiscard]] virtual bool available() const noexcept = 0;
};

class UnavailableForecaster final : public Forecaster {
public:
    explicit UnavailableForecaster(std::string reason);
    ForecastModelPrediction forecast(std::span<const float>) override;
    std::string modelName() const override;
    std::string modelVersion() const override;
    bool available() const noexcept override;
private:
    std::string reason_;
};

}  // namespace water::inference
