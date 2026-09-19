#pragma once

#include "water/inference/inference_error.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace water::inference {

inline constexpr std::size_t kTraceFeatureCount = 10;
inline constexpr std::size_t kTraceClassCount = 6;
using TraceFeatureVector = std::array<float, kTraceFeatureCount>;

struct ClassProbability {
    std::int64_t label{0};
    double probability{0.0};
};

struct TraceModelPrediction {
    std::int64_t predicted_label{0};
    std::vector<ClassProbability> probabilities;
};

// Synchronous model boundary. TraceService always calls classify() from its
// dedicated inference thread pool, never from a Muduo IO or database thread.
class TraceClassifier {
public:
    virtual ~TraceClassifier() = default;

    [[nodiscard]] virtual TraceModelPrediction classify(
        const TraceFeatureVector& features) = 0;
    [[nodiscard]] virtual std::string modelName() const = 0;
    [[nodiscard]] virtual std::string modelVersion() const = 0;
    [[nodiscard]] virtual bool available() const noexcept = 0;
};

class UnavailableTraceClassifier final : public TraceClassifier {
public:
    explicit UnavailableTraceClassifier(std::string reason);

    [[nodiscard]] TraceModelPrediction classify(
        const TraceFeatureVector& features) override;
    [[nodiscard]] std::string modelName() const override;
    [[nodiscard]] std::string modelVersion() const override;
    [[nodiscard]] bool available() const noexcept override;

private:
    std::string reason_;
};

}  // namespace water::inference
