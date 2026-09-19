#pragma once

#include "water/domain/classification_data.h"

#include <vector>

namespace water::analysis {

// Pure CPU operations: no SQL, networking, Python, model or shared mutable state.
// Missing repair is an explicit new median policy; outlier repair reproduces
// notebook cell 1's degenerate Kalman expression (= previous repaired value).
void validateClassificationRequest(const domain::ClassificationDataRequest& request);

[[nodiscard]] domain::ClassificationDataResult analyzeClassificationData(
    const domain::ClassificationDataRequest& request,
    std::vector<domain::WaterSample> samples,
    const std::vector<domain::Company>& companies);

} // namespace water::analysis
