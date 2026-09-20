#include "water/repository/water_repository.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace water::repository {

namespace {

struct FeatureDefinition {
    std::string_view field;
    std::string_view label;
};

constexpr std::array<FeatureDefinition, 10> kFeatures{{
    {"temperature", "水温"},
    {"ph", "pH"},
    {"cod", "COD"},
    {"nh3n", "氨氮"},
    {"tp", "总磷"},
    {"water_level", "液位"},
    {"orp", "ORP"},
    {"conductivity", "电导率"},
    {"dissolved_oxygen", "溶解氧"},
    {"turbidity", "浊度"},
}};

const db::DbCell& cell(const db::DbRow& row, std::string_view name) {
    const auto iterator = row.find(std::string{name});
    if (iterator == row.end()) {
        throw db::DbError("database result is missing column: " +
                          std::string{name});
    }
    return iterator->second;
}

std::string requiredString(const db::DbRow& row, std::string_view name) {
    const auto& value = cell(row, name);
    if (!value) {
        throw db::DbError("database column is unexpectedly NULL: " +
                          std::string{name});
    }
    return *value;
}

std::optional<std::string> optionalString(const db::DbRow& row,
                                          std::string_view name) {
    return cell(row, name);
}

std::int64_t requiredInt64(const db::DbRow& row, std::string_view name) {
    const auto text = requiredString(row, name);
    std::size_t consumed = 0;
    const auto value = std::stoll(text, &consumed);
    if (consumed != text.size()) {
        throw db::DbError("invalid integer in database column: " +
                          std::string{name});
    }
    return value;
}

std::uint64_t requiredUint64(const db::DbRow& row, std::string_view name) {
    const auto text = requiredString(row, name);
    std::size_t consumed = 0;
    const auto value = std::stoull(text, &consumed);
    if (consumed != text.size()) {
        throw db::DbError("invalid unsigned integer in database column: " +
                          std::string{name});
    }
    return value;
}

std::optional<double> optionalDouble(const db::DbRow& row,
                                     std::string_view name) {
    const auto& value = cell(row, name);
    if (!value) {
        return std::nullopt;
    }
    errno = 0;
    char* end = nullptr;
    const double parsed = std::strtod(value->c_str(), &end);
    if (errno == ERANGE || end != value->data() + value->size()) {
        throw db::DbError("invalid floating point value in database column: " +
                          std::string{name});
    }
    return parsed;
}

domain::Company toCompany(const db::DbRow& row) {
    return domain::Company{
        .id = requiredInt64(row, "company_id"),
        .name = requiredString(row, "company_name"),
        .code = requiredString(row, "company_code"),
        .task_type = optionalString(row, "task_type"),
        .location = optionalString(row, "location"),
        .description = optionalString(row, "description"),
        .created_at = optionalString(row, "created_at"),
    };
}

domain::WaterSample toSample(const db::DbRow& row) {
    return domain::WaterSample{
        .id = requiredInt64(row, "id"),
        .temperature = optionalDouble(row, "temperature"),
        .ph = optionalDouble(row, "ph"),
        .cod = optionalDouble(row, "cod"),
        .nh3n = optionalDouble(row, "nh3n"),
        .tp = optionalDouble(row, "tp"),
        .water_level = optionalDouble(row, "water_level"),
        .orp = optionalDouble(row, "orp"),
        .conductivity = optionalDouble(row, "conductivity"),
        .dissolved_oxygen = optionalDouble(row, "dissolved_oxygen"),
        .turbidity = optionalDouble(row, "turbidity"),
        .company_id = requiredInt64(row, "company_id"),
    };
}

std::string datasetTable(const std::string& dataset) {
    if (dataset == "train_data") {
        return "train_data";
    }
    if (dataset == "test_data") {
        return "test_data";
    }
    throw std::invalid_argument("unsupported dataset");
}

std::string datasetSplit(const std::string& dataset) {
    if (dataset == "train_data") {
        return "train";
    }
    if (dataset == "test_data") {
        return "test";
    }
    throw std::invalid_argument("unsupported dataset");
}

std::uint64_t changedMask(const domain::WaterSample& before,
                          const domain::WaterSample& after) {
    std::uint64_t mask = 0;
    for (std::size_t index = 0;
         index < domain::kClassificationFeatures.size(); ++index) {
        const auto member = domain::kClassificationFeatures[index].member;
        if (before.*member != after.*member) {
            mask |= std::uint64_t{1} << index;
        }
    }
    return mask;
}

std::string summarySql(const std::string& table) {
    std::string sql = "SELECT COUNT(*) AS total_rows";
    for (const auto& feature : kFeatures) {
        const std::string field{feature.field};
        sql += ", COUNT(" + field + ") AS " + field + "_count";
        sql += ", AVG(" + field + ") AS " + field + "_mean";
        sql += ", STDDEV_POP(" + field + ") AS " + field + "_std";
        sql += ", MIN(" + field + ") AS " + field + "_min";
        sql += ", MAX(" + field + ") AS " + field + "_max";
    }
    sql += " FROM " + table + " WHERE company_id = ?";
    return sql;
}

}  // namespace

std::vector<domain::Company> WaterRepository::listCompanies(
    db::DbConnection& connection) const {
    const auto rows = connection.query(
        "SELECT company_id, company_name, company_code, task_type, location, "
        "description, created_at FROM company_info ORDER BY company_id ASC");
    std::vector<domain::Company> companies;
    companies.reserve(rows.size());
    for (const auto& row : rows) {
        companies.push_back(toCompany(row));
    }
    return companies;
}

std::optional<domain::Company> WaterRepository::findCompany(
    db::DbConnection& connection, std::int64_t company_id) const {
    const db::DbParameters parameters{company_id};
    const auto rows = connection.query(
        "SELECT company_id, company_name, company_code, task_type, location, "
        "description, created_at FROM company_info WHERE company_id = ?",
        parameters);
    if (rows.empty()) {
        return std::nullopt;
    }
    return toCompany(rows.front());
}

domain::DataOverview WaterRepository::loadOverview(
    db::DbConnection& connection, std::int64_t company_id,
    const std::string& dataset, std::size_t preview_limit) const {
    const auto company = findCompany(connection, company_id);
    if (!company) {
        throw std::out_of_range("company not found");
    }

    const auto table = datasetTable(dataset);
    const db::DbParameters parameters{company_id};
    const auto aggregate_rows = connection.query(summarySql(table), parameters);
    if (aggregate_rows.size() != 1) {
        throw db::DbError("aggregate query returned an unexpected row count");
    }

    domain::DataOverview overview;
    overview.company = *company;
    overview.dataset = dataset;
    overview.total_rows = requiredUint64(aggregate_rows.front(), "total_rows");
    if (overview.total_rows == 0) {
        throw std::out_of_range("dataset is empty for this company");
    }

    overview.summary.reserve(kFeatures.size());
    for (const auto& feature : kFeatures) {
        const std::string prefix{feature.field};
        overview.summary.push_back(domain::FeatureSummary{
            .field = prefix,
            .label = std::string{feature.label},
            .count = requiredUint64(aggregate_rows.front(), prefix + "_count"),
            .mean = optionalDouble(aggregate_rows.front(), prefix + "_mean"),
            .standard_deviation =
                optionalDouble(aggregate_rows.front(), prefix + "_std"),
            .minimum = optionalDouble(aggregate_rows.front(), prefix + "_min"),
            .maximum = optionalDouble(aggregate_rows.front(), prefix + "_max"),
        });
    }

    const auto safe_limit = std::max<std::size_t>(
        1, std::min<std::size_t>(preview_limit, 50));
    const std::string preview_sql =
        "SELECT id, temperature, ph, cod, nh3n, tp, water_level, orp, "
        "conductivity, dissolved_oxygen, turbidity, company_id FROM " +
        table + " WHERE company_id = ? ORDER BY id ASC LIMIT " +
        std::to_string(safe_limit);
    const auto preview_rows = connection.query(preview_sql, parameters);
    overview.preview_rows.reserve(preview_rows.size());
    for (const auto& row : preview_rows) {
        overview.preview_rows.push_back(toSample(row));
    }
    return overview;
}

std::optional<domain::WaterSample> WaterRepository::findSampleById(
    db::DbConnection& connection, const std::string& dataset,
    std::int64_t sample_id) const {
    const auto table = datasetTable(dataset);
    const db::DbParameters parameters{sample_id};
    const std::string sql =
        "SELECT id, temperature, ph, cod, nh3n, tp, water_level, orp, "
        "conductivity, dissolved_oxygen, turbidity, company_id FROM " +
        table + " WHERE id = ? LIMIT 1";
    const auto rows = connection.query(sql, parameters);
    if (rows.empty()) {
        return std::nullopt;
    }
    return toSample(rows.front());
}

std::vector<domain::WaterSample> WaterRepository::loadForecastWindow(
    db::DbConnection& connection, std::int64_t company_id,
    const std::string& dataset, std::int64_t end_sample_id,
    std::size_t lookback) const {
    if (company_id <= 0 || end_sample_id < 0 || lookback == 0 || lookback > 4096) {
        throw std::invalid_argument("invalid forecast window arguments");
    }
    const auto table = datasetTable(dataset);
    db::DbParameters parameters{company_id};
    std::string sql =
        "SELECT id, temperature, ph, cod, nh3n, tp, water_level, orp, "
        "conductivity, dissolved_oxygen, turbidity, company_id FROM " +
        table + " WHERE company_id = ?";
    if (end_sample_id > 0) {
        sql += " AND id <= ?";
        parameters.push_back(end_sample_id);
    }
    sql += " ORDER BY id DESC LIMIT " + std::to_string(lookback);
    const auto rows = connection.query(sql, parameters);
    std::vector<domain::WaterSample> samples;
    samples.reserve(rows.size());
    for (const auto& row : rows) { samples.push_back(toSample(row)); }
    std::reverse(samples.begin(), samples.end());
    return samples;
}

std::vector<domain::WaterSample> WaterRepository::loadClassificationSamples(
    db::DbConnection& connection, const domain::ClassificationDataRequest& request) const {
    const auto table = datasetTable(request.dataset);
    const auto split = datasetSplit(request.dataset);
    db::DbParameters parameters;
    std::string sql;
    if (request.cleaning_run_id > 0) {
        const db::DbParameters run_parameters{
            request.cleaning_run_id, request.company_id, split};
        const auto runs = connection.query(
            "SELECT run_id FROM processing_runs WHERE task_type = 'classification' "
            "AND run_id = ? AND company_id = ? AND data_split = ?",
            run_parameters);
        if (runs.empty()) { throw std::out_of_range("cleaning version not found for company/dataset"); }
        sql = "SELECT source_sample_id AS id, temperature, ph, cod, nh3n, tp, water_level, orp, "
              "conductivity, dissolved_oxygen, turbidity, company_id FROM processed_samples "
              "WHERE run_id = ? ORDER BY source_sample_id ASC";
        parameters = {request.cleaning_run_id};
    } else {
        sql = "SELECT id, temperature, ph, cod, nh3n, tp, water_level, orp, conductivity, "
              "dissolved_oxygen, turbidity, company_id FROM " + table;
        if (request.operation != "distribution" || request.view != "comparison") {
            sql += " WHERE company_id = ?";
            parameters.emplace_back(request.company_id);
        }
        sql += " ORDER BY company_id ASC, id ASC";
    }
    sql += " LIMIT " + std::to_string(domain::kClassificationRowLimit + 1);
    const auto rows = connection.query(sql, parameters);
    if (rows.size() > domain::kClassificationRowLimit) {
        throw std::length_error("classification analysis exceeds 50000 rows; narrow the dataset");
    }
    if (rows.empty()) { throw std::out_of_range("classification dataset is empty"); }
    std::vector<domain::WaterSample> samples;
    samples.reserve(rows.size());
    for (const auto& row : rows) { samples.push_back(toSample(row)); }
    return samples;
}

std::int64_t WaterRepository::saveClassificationVersion(
    db::DbConnection& connection, const domain::ClassificationDataRequest& request,
    const domain::ClassificationDataResult& result) const {
    if (result.original_rows.size() != result.processed_rows.size() || result.processed_rows.empty() ||
        result.sample_count != result.processed_rows.size() || result.sample_count > domain::kClassificationRowLimit ||
        (request.operation != "missing" && request.operation != "clean")) {
        throw std::invalid_argument("invalid cleaning snapshot");
    }
    static_cast<void>(datasetTable(request.dataset));
    const auto split = datasetSplit(request.dataset);
    std::uint64_t changed_rows = 0;
    for (std::size_t i=0; i<result.sample_count; ++i) {
        const auto& before=result.original_rows[i]; const auto& after=result.processed_rows[i];
        if (after.id != before.id || after.company_id != request.company_id ||
            before.company_id != request.company_id || after.id <= 0) {
            throw std::invalid_argument("invalid cleaning snapshot scope");
        }
        if (changedMask(before, after) != 0) {
            ++changed_rows;
        }
    }
    db::Transaction transaction(connection);
    db::DbParameters parameters{request.company_id, split, request.operation, result.rule,
        request.cleaning_run_id > 0 ? db::DbValue{request.cleaning_run_id} : db::DbValue{std::monostate{}},
        static_cast<std::uint64_t>(request.window), request.threshold,
        static_cast<std::uint64_t>(result.sample_count), changed_rows};
    const auto inserted = connection.execute(
        "INSERT INTO processing_runs(task_type,company_id,data_split,operation,algorithm_name,"
        "parent_run_id,window_size,z_threshold,sample_count,changed_count,status,completed_at) "
        "VALUES('classification',?,?,?,?,?,?,?,?,?,'completed',CURRENT_TIMESTAMP)", parameters);
    if (inserted.last_insert_id == 0 ||
        inserted.last_insert_id > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        throw db::DbError("invalid cleaning run id returned by database");
    }
    const auto run_id = static_cast<std::int64_t>(inserted.last_insert_id);
    std::string prefix = "INSERT INTO processed_samples(run_id,source_sample_id,company_id";
    for (const auto& feature : domain::kClassificationFeatures) { prefix += "," + std::string{feature.field}; }
    prefix += ",changed_mask";
    prefix += ") VALUES ";
    constexpr std::size_t batch_size = 128;
    for (std::size_t start = 0; start < result.sample_count; start += batch_size) {
        std::string sql = prefix;
        parameters.clear();
        const auto end = std::min(start + batch_size, result.sample_count);
        for (auto index = start; index < end; ++index) {
            if (index != start) { sql += ','; }
            sql += "(";
            for (std::size_t column = 0; column < 14; ++column) { sql += column == 0 ? "?" : ",?"; }
            sql += ")";
            const auto& after = result.processed_rows[index];
            const auto& before = result.original_rows[index];
            parameters.insert(parameters.end(), {run_id, after.id, after.company_id});
            for (const auto& feature : domain::kClassificationFeatures) {
                const auto& value = after.*feature.member;
                parameters.push_back(value ? db::DbValue{*value} : db::DbValue{std::monostate{}});
            }
            parameters.emplace_back(changedMask(before, after));
        }
        connection.execute(sql, parameters);
    }
    transaction.commit();
    return run_id;
}

std::optional<domain::WaterSample> WaterRepository::findCleanedSampleById(
    db::DbConnection& connection, const std::string& dataset,
    std::int64_t sample_id, std::int64_t cleaning_run_id) const {
    static_cast<void>(datasetTable(dataset));
    const db::DbParameters parameters{
        cleaning_run_id, sample_id, datasetSplit(dataset)};
    const auto rows = connection.query(
        "SELECT s.source_sample_id AS id,s.temperature,s.ph,s.cod,s.nh3n,s.tp,s.water_level,s.orp,"
        "s.conductivity,s.dissolved_oxygen,s.turbidity,s.company_id FROM processed_samples s "
        "JOIN processing_runs r ON r.run_id=s.run_id "
        "WHERE s.run_id=? AND s.source_sample_id=? AND r.task_type='classification' "
        "AND r.data_split=? LIMIT 1", parameters);
    return rows.empty() ? std::nullopt : std::optional<domain::WaterSample>{toSample(rows.front())};
}

}  // namespace water::repository
