#pragma once

#include "water/db/database.h"
#include "water/domain/water_data.h"
#include "water/domain/classification_data.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace water::repository {

// Stateless SQL repository. A DbConnection is supplied by DbExecutor for each
// operation, keeping connection ownership out of the business layer.
class WaterRepository final {
public:
    [[nodiscard]] std::vector<domain::Company> listCompanies(
        db::DbConnection& connection) const;

    [[nodiscard]] std::optional<domain::Company> findCompany(
        db::DbConnection& connection, std::int64_t company_id) const;

    [[nodiscard]] domain::DataOverview loadOverview(
        db::DbConnection& connection, std::int64_t company_id,
        const std::string& dataset, std::size_t preview_limit) const;

    [[nodiscard]] std::optional<domain::WaterSample> findSampleById(
        db::DbConnection& connection, const std::string& dataset,
        std::int64_t sample_id) const;

    [[nodiscard]] std::vector<domain::WaterSample> loadClassificationSamples(
        db::DbConnection& connection,
        const domain::ClassificationDataRequest& request) const;

    // Stores an immutable processing version. Original values stay in
    // water_samples and are resolved through source_sample_id.
    [[nodiscard]] std::int64_t saveClassificationVersion(
        db::DbConnection& connection,
        const domain::ClassificationDataRequest& request,
        const domain::ClassificationDataResult& result) const;

    [[nodiscard]] std::optional<domain::WaterSample> findCleanedSampleById(
        db::DbConnection& connection, const std::string& dataset,
        std::int64_t sample_id, std::int64_t cleaning_run_id) const;

    // Single-company history only. IDs must follow sampling order at import.
    // end_sample_id == 0 selects the latest window; otherwise it is inclusive.
    [[nodiscard]] std::vector<domain::WaterSample> loadForecastWindow(
        db::DbConnection& connection, std::int64_t company_id,
        const std::string& dataset, std::int64_t end_sample_id,
        std::size_t lookback) const;
};

}  // namespace water::repository
