#include "water/inference/onnx_forecaster.h"

#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <utility>
#include <vector>

namespace water::inference {

namespace {
constexpr std::array<std::int64_t, 3> kInputShape{1, kForecastLookback, kForecastFeatureCount};
constexpr std::array<std::int64_t, 3> kOutputShape{1, kForecastHorizon, kForecastTargetCount};
}

struct OnnxForecaster::Impl {
    explicit Impl(const OnnxForecasterOptions& options)
        : environment(ORT_LOGGING_LEVEL_WARNING, "water-forecast"), session(nullptr) {
        if (options.model_path.empty() || options.intra_op_threads <= 0) {
            throw std::invalid_argument("invalid ONNX forecast options");
        }
        Ort::SessionOptions session_options;
        session_options.SetIntraOpNumThreads(options.intra_op_threads);
        session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        const std::filesystem::path path{options.model_path};
        session = Ort::Session(environment, path.c_str(), session_options);
        if (session.GetInputCount() != 1 || session.GetOutputCount() != 1) {
            throw InferenceError("forecast model must have one input and one output");
        }
        Ort::AllocatorWithDefaultOptions allocator;
        const auto metadata = session.GetModelMetadata();
        const auto contract = metadata.LookupCustomMetadataMapAllocated("water.forecast.contract", allocator);
        if (!contract || std::string{contract.get()} != kForecastContract) {
            throw InferenceError("forecast model contract mismatch; use export_forecast_model.py");
        }
        const auto name = metadata.LookupCustomMetadataMapAllocated("water.model.name", allocator);
        const auto version = metadata.LookupCustomMetadataMapAllocated("water.model.version", allocator);
        if (!name || !version || std::string{name.get()}.empty() || std::string{version.get()}.empty()) {
            throw InferenceError("forecast model is missing its name/version metadata");
        }
        model_name = name.get();
        model_version = version.get();
        input_name = session.GetInputNameAllocated(0, allocator).get();
        output_name = session.GetOutputNameAllocated(0, allocator).get();
        // Keep the TypeInfo owner alive while using its tensor shape view.
        const auto input_type = session.GetInputTypeInfo(0);
        const auto output_type = session.GetOutputTypeInfo(0);
        const auto input_info = input_type.GetTensorTypeAndShapeInfo();
        const auto output_info = output_type.GetTensorTypeAndShapeInfo();
        if (input_info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
            input_info.GetShape() != std::vector<std::int64_t>(kInputShape.begin(), kInputShape.end()) ||
            output_info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
            output_info.GetShape() != std::vector<std::int64_t>(kOutputShape.begin(), kOutputShape.end())) {
            throw InferenceError("forecast tensor shapes must be float32 [1,120,10] -> [1,10,4]");
        }
    }
    Ort::Env environment;
    Ort::Session session;
    std::string input_name, output_name, model_name, model_version;
};

OnnxForecaster::OnnxForecaster(OnnxForecasterOptions options)
    : impl_(std::make_unique<Impl>(options)) {}
OnnxForecaster::~OnnxForecaster() = default;
OnnxForecaster::OnnxForecaster(OnnxForecaster&&) noexcept = default;
OnnxForecaster& OnnxForecaster::operator=(OnnxForecaster&&) noexcept = default;

ForecastModelPrediction OnnxForecaster::forecast(std::span<const float> window) {
    if (!impl_) { throw InferenceUnavailable("forecast model was moved from"); }
    if (window.size() != kForecastLookback * kForecastFeatureCount ||
        !std::all_of(window.begin(), window.end(), [](float value) { return std::isfinite(value); })) {
        throw InferenceError("forecast input must contain 1200 finite values");
    }
    // Request-local buffers; no mutable shared tensors across worker threads.
    std::vector<float> input(window.begin(), window.end());
    auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    auto tensor = Ort::Value::CreateTensor<float>(memory, input.data(), input.size(),
                                                 kInputShape.data(), kInputShape.size());
    const char* input_names[]{impl_->input_name.c_str()};
    const char* output_names[]{impl_->output_name.c_str()};
    try {
        auto outputs = impl_->session.Run(Ort::RunOptions{nullptr}, input_names, &tensor, 1, output_names, 1);
        if (outputs.size() != 1 || !outputs[0].IsTensor()) {
            throw InferenceError("forecast output is not a tensor");
        }
        const auto info = outputs[0].GetTensorTypeAndShapeInfo();
        if (info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
            info.GetShape() != std::vector<std::int64_t>(kOutputShape.begin(), kOutputShape.end())) {
            throw InferenceError("forecast output shape mismatch");
        }
        const float* values = outputs[0].GetTensorData<float>();
        ForecastModelPrediction predictions(kForecastHorizon);
        for (std::size_t step = 0; step < predictions.size(); ++step) {
            for (std::size_t feature = 0; feature < kForecastTargetCount; ++feature) {
                const float value = values[step * kForecastTargetCount + feature];
                if (!std::isfinite(value)) { throw InferenceError("forecast output contains NaN/Inf"); }
                predictions[step][feature] = value;
            }
        }
        return predictions;
    } catch (const Ort::Exception& error) {
        throw InferenceError(std::string{"ONNX Runtime forecast failed: "} + error.what());
    }
}

std::string OnnxForecaster::modelName() const { return impl_ ? impl_->model_name : "unavailable"; }
std::string OnnxForecaster::modelVersion() const { return impl_ ? impl_->model_version : "not-loaded"; }
bool OnnxForecaster::available() const noexcept { return impl_ != nullptr; }

}  // namespace water::inference
