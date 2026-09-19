#include "water/inference/onnx_trace_classifier.h"

#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace water::inference {

struct OnnxTraceClassifier::Impl {
    explicit Impl(OnnxTraceClassifierOptions classifier_options)
        : options(std::move(classifier_options)),
          environment(ORT_LOGGING_LEVEL_WARNING, "water-trace"),
          session_options(),
          session(nullptr) {
        if (options.model_path.empty()) {
            throw std::invalid_argument("ONNX trace model path is empty");
        }
        if (options.intra_op_threads <= 0) {
            throw std::invalid_argument(
                "ONNX intra-op thread count must be positive");
        }

        // Concurrency is controlled by the application inference pool. Keep
        // each individual ONNX call single-threaded to avoid oversubscription.
        session_options.SetIntraOpNumThreads(options.intra_op_threads);
        session_options.SetGraphOptimizationLevel(
            GraphOptimizationLevel::ORT_ENABLE_ALL);
        const std::filesystem::path model_path{options.model_path};
        session = Ort::Session(environment, model_path.c_str(),
                               session_options);

        Ort::AllocatorWithDefaultOptions allocator;
        if (session.GetInputCount() != 1 || session.GetOutputCount() < 2) {
            throw InferenceError(
                "trace model must expose one input and label/probability outputs");
        }

        input_name = session.GetInputNameAllocated(0, allocator).get();
        for (std::size_t index = 0; index < session.GetOutputCount(); ++index) {
            output_names.emplace_back(
                session.GetOutputNameAllocated(index, allocator).get());
        }

        const auto input_type = session.GetInputTypeInfo(0);
        const auto input_info = input_type.GetTensorTypeAndShapeInfo();
        if (input_info.GetElementType() !=
            ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
            throw InferenceError("trace model input must be float32");
        }
        const auto shape = input_info.GetShape();
        if (shape.size() != 2 ||
            (shape[1] >= 0 &&
             shape[1] != static_cast<std::int64_t>(kTraceFeatureCount))) {
            throw InferenceError("trace model input shape must be [N, 10]");
        }
    }

    OnnxTraceClassifierOptions options;
    Ort::Env environment;
    Ort::SessionOptions session_options;
    Ort::Session session;
    std::string input_name;
    std::vector<std::string> output_names;
};

OnnxTraceClassifier::OnnxTraceClassifier(OnnxTraceClassifierOptions options)
    : impl_(std::make_unique<Impl>(std::move(options))) {}

OnnxTraceClassifier::~OnnxTraceClassifier() = default;
OnnxTraceClassifier::OnnxTraceClassifier(OnnxTraceClassifier&&) noexcept =
    default;
OnnxTraceClassifier& OnnxTraceClassifier::operator=(
    OnnxTraceClassifier&&) noexcept = default;

TraceModelPrediction OnnxTraceClassifier::classify(
    const TraceFeatureVector& features) {
    if (!impl_) {
        throw InferenceUnavailable("ONNX trace classifier was moved from");
    }
    for (const float value : features) {
        if (!std::isfinite(value)) {
            throw InferenceError("trace input contains a non-finite value");
        }
    }

    auto input_values = features;
    constexpr std::array<std::int64_t, 2> input_shape{
        1, static_cast<std::int64_t>(kTraceFeatureCount)};
    auto memory_info = Ort::MemoryInfo::CreateCpu(
        OrtArenaAllocator, OrtMemTypeDefault);
    auto input_tensor = Ort::Value::CreateTensor<float>(
        memory_info, input_values.data(), input_values.size(),
        input_shape.data(), input_shape.size());

    const std::array<const char*, 1> input_names{impl_->input_name.c_str()};
    std::vector<const char*> output_name_views;
    output_name_views.reserve(impl_->output_names.size());
    for (const auto& name : impl_->output_names) {
        output_name_views.push_back(name.c_str());
    }

    std::vector<Ort::Value> outputs;
    try {
        outputs = impl_->session.Run(
            Ort::RunOptions{nullptr}, input_names.data(), &input_tensor,
            input_names.size(), output_name_views.data(),
            output_name_views.size());
    } catch (const Ort::Exception& error) {
        throw InferenceError(std::string{"ONNX Runtime failed: "} +
                             error.what());
    }

    const float* probability_data = nullptr;
    std::size_t probability_count = 0;
    for (auto& output : outputs) {
        if (!output.IsTensor()) {
            continue;
        }
        const auto info = output.GetTensorTypeAndShapeInfo();
        if (info.GetElementType() == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT &&
            info.GetElementCount() > 1) {
            probability_data = output.GetTensorData<float>();
            probability_count = info.GetElementCount();
            break;
        }
    }
    if (probability_data == nullptr ||
        probability_count != kTraceClassCount) {
        throw InferenceError(
            "trace model probability tensor must contain six classes");
    }

    std::vector<double> normalized(probability_count);
    double total = 0.0;
    for (std::size_t index = 0; index < probability_count; ++index) {
        const double probability = probability_data[index];
        if (!std::isfinite(probability) || probability < 0.0) {
            throw InferenceError(
                "trace model returned an invalid probability");
        }
        normalized[index] = probability;
        total += probability;
    }
    if (total <= 0.0) {
        throw InferenceError("trace probability sum is zero");
    }
    for (auto& probability : normalized) {
        probability /= total;
    }

    const auto best = std::max_element(normalized.begin(), normalized.end());
    TraceModelPrediction prediction;
    prediction.predicted_label =
        static_cast<std::int64_t>(std::distance(normalized.begin(), best)) +
        impl_->options.label_offset;
    prediction.probabilities.reserve(normalized.size());
    for (std::size_t index = 0; index < normalized.size(); ++index) {
        prediction.probabilities.push_back(ClassProbability{
            .label = static_cast<std::int64_t>(index) +
                     impl_->options.label_offset,
            .probability = normalized[index],
        });
    }
    return prediction;
}

std::string OnnxTraceClassifier::modelName() const {
    return impl_ ? impl_->options.model_name : "unavailable";
}

std::string OnnxTraceClassifier::modelVersion() const {
    return impl_ ? impl_->options.model_version : "not-loaded";
}

bool OnnxTraceClassifier::available() const noexcept {
    return impl_ != nullptr;
}

}  // namespace water::inference
