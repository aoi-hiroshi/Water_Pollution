#include "water/inference/forecaster.h"

#include <utility>

namespace water::inference {

UnavailableForecaster::UnavailableForecaster(std::string reason)
    : reason_(std::move(reason)) {}

ForecastModelPrediction UnavailableForecaster::forecast(std::span<const float>) {
    throw InferenceUnavailable(reason_);
}

std::string UnavailableForecaster::modelName() const { return "attention_lstm_h10"; }
std::string UnavailableForecaster::modelVersion() const { return "not-loaded"; }
bool UnavailableForecaster::available() const noexcept { return false; }

}  // namespace water::inference
