#pragma once

#include "water/domain/forecast_data.h"

#include <vector>

namespace water::analysis {

void validateForecastDataRequest(const domain::ForecastDataRequest& request);

[[nodiscard]] domain::ForecastDataResult analyzeForecastData(
    const domain::ForecastDataRequest& request,
    domain::Company company,
    std::vector<domain::WaterSample> samples);

} // namespace water::analysis
