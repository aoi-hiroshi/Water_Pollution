#pragma once
#include "water/domain/classification_data.h"
#include "water/db/database.h"
#include "water/db/connection_pool.h"
#include <atomic>
#include <map>
#include <mutex>
#include <stdexcept>

namespace water::db::test {
inline DbRow classificationCompanyRow() {
    return {{"company_id","1"},{"company_name","Factory One"},{"company_code","F1"},
            {"task_type","classification"},{"location",std::nullopt},
            {"description",std::nullopt},{"created_at",std::nullopt}};
}
inline DbRows classificationRows(std::size_t count=130) {
    DbRows rows;
    for(std::size_t i=0;i<count;++i) {
        DbRow row{{"id",std::to_string(i+1)},{"company_id","1"}};
        for(const auto& feature:domain::kClassificationFeatures) row[feature.field]=std::to_string(i+1);
        row["cod"]="10";
        if(i==0) row["cod"]=std::nullopt;
        if(i==50) row["cod"]="100";
        rows.push_back(std::move(row));
    } return rows;
}
struct ClassificationDatabaseState {
    std::mutex mutex;
    DbRows raw=classificationRows();
    std::map<std::int64_t,DbRows> versions;
    std::map<std::int64_t,std::string> datasets;
    std::map<std::int64_t,std::vector<std::uint64_t>> changed_masks;
    std::uint64_t last_changed_rows{0};
    std::atomic_int begins{0},commits{0},rollbacks{0},writes{0},queries{0};
    bool fail_write{false};
    std::atomic_bool unavailable{false};
    std::int64_t next_run{100};
};
class ClassificationConnection final:public DbConnection {
public:
    explicit ClassificationConnection(std::shared_ptr<ClassificationDatabaseState> state):state_(std::move(state)) {}
    bool ping() noexcept override { return !state_->unavailable.load(); }
    bool resetSession() noexcept override { return true; }
    ExecuteResult execute(std::string_view sql,std::span<const DbValue> params) override {
        std::lock_guard lock(state_->mutex); ++state_->writes;
        if(!sql.starts_with("INSERT INTO processing_") &&
           !sql.starts_with("INSERT INTO processed_"))
            throw std::runtime_error("raw mutation forbidden");
        if(sql.find("processing_runs")!=sql.npos) {
            if(params.size()!=9 || std::get<std::int64_t>(params[0])!=1)
                throw std::runtime_error("bad run params");
            const auto run=state_->next_run++;
            state_->datasets[run]=std::get<std::string>(params[1]);
            state_->last_changed_rows=std::get<std::uint64_t>(params[8]);
            return {1,static_cast<std::uint64_t>(run)};
        }
        if(state_->fail_write) throw DbError("injected save failure");
        if(params.size()%14!=0 || params.size()>128*14) throw std::runtime_error("bad batch size");
        for(std::size_t start=0;start<params.size();start+=14) {
            const auto run=std::get<std::int64_t>(params[start]);
            DbRow row{{"id",std::to_string(std::get<std::int64_t>(params[start+1]))},
                      {"company_id",std::to_string(std::get<std::int64_t>(params[start+2]))}};
            for(std::size_t i=0;i<10;++i) {
                const auto& value=params[start+3+i];
                row[domain::kClassificationFeatures[i].field]=std::holds_alternative<double>(value)
                    ? DbCell{std::to_string(std::get<double>(value))} : std::nullopt;
            }
            state_->changed_masks[run].push_back(
                std::get<std::uint64_t>(params[start+13]));
            state_->versions[run].push_back(std::move(row));
        } return {params.size()/14,0};
    }
    DbRows query(std::string_view sql,std::span<const DbValue> params) override {
        std::lock_guard lock(state_->mutex); ++state_->queries;
        if(sql.find("FROM company_info")!=sql.npos)
            return params.empty() || std::get<std::int64_t>(params[0])==1 ? DbRows{classificationCompanyRow()} : DbRows{};
        if(sql.find("FROM processing_runs")!=sql.npos) {
            const auto run=std::get<std::int64_t>(params[0]);
            return state_->datasets.contains(run) && std::get<std::int64_t>(params[1])==1 &&
                state_->datasets[run]==std::get<std::string>(params[2]) ? DbRows{{{"id",std::to_string(run)}}} : DbRows{};
        }
        if(sql.find("FROM processed_samples")!=sql.npos) {
            const auto run=std::get<std::int64_t>(params[0]);
            if(sql.find("JOIN")!=sql.npos) {
                if(!state_->datasets.contains(run) || state_->datasets[run]!=std::get<std::string>(params[2])) return {};
                for(const auto& row:state_->versions[run]) if(row.at("id")==std::to_string(std::get<std::int64_t>(params[1]))) return {row};
                return {};
            } return state_->versions[run];
        }
        if(sql.find("FROM train_data")!=sql.npos || sql.find("FROM test_data")!=sql.npos) {
            if(sql.find("WHERE id = ?")!=sql.npos) {
                for(const auto& row:state_->raw) if(row.at("id")==std::to_string(std::get<std::int64_t>(params[0]))) return {row};
                return {};
            } return state_->raw;
        } throw std::runtime_error("unexpected classification SQL");
    }
    void beginTransaction() override { ++state_->begins; }
    void commit() override { ++state_->commits; }
    void rollback() noexcept override { ++state_->rollbacks; }
private:
    std::shared_ptr<ClassificationDatabaseState> state_;
};
inline auto classificationFactory(std::shared_ptr<ClassificationDatabaseState> state) {
    return [state]()->std::unique_ptr<DbConnection>{
        if(state->unavailable) throw ConnectionPoolTimeout("injected acquisition timeout");
        return std::make_unique<ClassificationConnection>(state);
    };
}
}
