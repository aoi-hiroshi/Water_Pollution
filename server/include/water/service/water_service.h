#pragma once

#include "water/db/db_executor.h"
#include "water/domain/water_data.h"
#include "water/repository/water_repository.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <variant>
#include <vector>

namespace water::service {

struct ServiceError {
    int http_status{500};
    std::string code{"INTERNAL_ERROR"};
    std::string message{"internal server error"};
};

using CompaniesResult =
    std::variant<std::vector<domain::Company>, ServiceError>;
using OverviewResult = std::variant<domain::DataOverview, ServiceError>;

class WaterService final {
public:
    WaterService(db::DbExecutor& database,
                 const repository::WaterRepository& repository);

    // Completion callbacks run on a database worker. Network adapters must
    // marshal their reply back to the connection's IO loop.
    void listCompanies(std::function<void(CompaniesResult)> completion);
    void getOverview(std::int64_t company_id, std::string dataset,
                     std::size_t preview_limit,
                     std::function<void(OverviewResult)> completion);

private:
    db::DbExecutor& database_;
    const repository::WaterRepository& repository_;
};

}  // namespace water::service
