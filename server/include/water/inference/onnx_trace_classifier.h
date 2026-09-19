#pragma once

#include "water/inference/trace_classifier.h"

#include <cstdint>
#include <memory>
#include <string>

namespace water::inference {

struct OnnxTraceClassifierOptions {
    std::string model_path;
    std::string model_name{"random_forest"};
    std::string model_version{"1.0"};
    std::int64_t label_offset{1};
    int intra_op_threads{1};
};

// Loads the exported StandardScaler + RandomForest pipeline. The PIMPL keeps
// ONNX Runtime headers out of the rest of the application.
class OnnxTraceClassifier final : public TraceClassifier {
public:
    explicit OnnxTraceClassifier(OnnxTraceClassifierOptions options);
    ~OnnxTraceClassifier() override;

    OnnxTraceClassifier(const OnnxTraceClassifier&) = delete;
    OnnxTraceClassifier& operator=(const OnnxTraceClassifier&) = delete;
    OnnxTraceClassifier(OnnxTraceClassifier&&) noexcept;
    OnnxTraceClassifier& operator=(OnnxTraceClassifier&&) noexcept;

    [[nodiscard]] TraceModelPrediction classify(
        const TraceFeatureVector& features) override;
    [[nodiscard]] std::string modelName() const override;
    [[nodiscard]] std::string modelVersion() const override;
    [[nodiscard]] bool available() const noexcept override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace water::inference
