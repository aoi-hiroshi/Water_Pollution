#include "water/inference/trace_classifier.h"

#include <utility>

namespace water::inference {

UnavailableTraceClassifier::UnavailableTraceClassifier(std::string reason)
    : reason_(std::move(reason)) {
    if (reason_.empty()) {
        reason_ = "trace model is not configured";
    }
}

TraceModelPrediction UnavailableTraceClassifier::classify(
    const TraceFeatureVector&) {
    throw InferenceUnavailable(reason_);
}

std::string UnavailableTraceClassifier::modelName() const {
    return "unavailable";
}

std::string UnavailableTraceClassifier::modelVersion() const {
    return "not-loaded";
}

bool UnavailableTraceClassifier::available() const noexcept {
    return false;
}

}  // namespace water::inference
