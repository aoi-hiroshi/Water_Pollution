#pragma once

#include "water/inference/forecaster.h"

#include <memory>
#include <string>

namespace water::inference {

struct OnnxForecasterOptions {
    std::string model_path;
    int intra_op_threads{1};
};

class OnnxForecaster final : public Forecaster {
public:
    explicit OnnxForecaster(OnnxForecasterOptions options);
    ~OnnxForecaster() override;
    OnnxForecaster(const OnnxForecaster&) = delete;
    OnnxForecaster& operator=(const OnnxForecaster&) = delete;
    OnnxForecaster(OnnxForecaster&&) noexcept;
    OnnxForecaster& operator=(OnnxForecaster&&) noexcept;

    ForecastModelPrediction forecast(std::span<const float> window) override;
    std::string modelName() const override;
    std::string modelVersion() const override;
    bool available() const noexcept override;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace water::inference
