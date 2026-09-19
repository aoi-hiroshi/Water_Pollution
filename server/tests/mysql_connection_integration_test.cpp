#include "water/db/connection_pool.h"
#include "water/db/mysql_connection.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

const char* requiredEnvironment(const char* name) {
    const char* value = std::getenv(name);
    if (!value || *value == '\0') {
        return nullptr;
    }
    return value;
}

}  // namespace

int main() {
    const char* user = requiredEnvironment("WATER_DB_USER");
    const char* password = requiredEnvironment("WATER_DB_PASSWORD");
    const char* database = requiredEnvironment("WATER_DB_NAME");
    if (!user || !password || !database) {
        std::cout << "SKIP: WATER_DB_USER, WATER_DB_PASSWORD and "
                     "WATER_DB_NAME are required\n";
        return 77;
    }

    try {
        water::db::MySqlConfig mysql;
        mysql.user = user;
        mysql.password = password;
        mysql.database = database;
        if (const char* host = std::getenv("WATER_DB_HOST")) {
            mysql.host = host;
        }

        water::db::ConnectionPoolOptions options;
        options.min_connections = 1;
        options.max_connections = 2;
        options.validation_interval = std::chrono::milliseconds{0};

        water::db::ConnectionPool pool(
            options, water::db::makeMySqlConnectionFactory(std::move(mysql)));
        pool.start();
        auto lease = pool.acquire();
        const auto rows = lease->query("SELECT 1 AS value");
        if (rows.size() != 1 || !rows.front().at("value").has_value() ||
            *rows.front().at("value") != "1") {
            throw std::runtime_error("unexpected SELECT 1 result");
        }
        lease.release();
        if (!pool.shutdown()) {
            throw std::runtime_error("connection pool did not drain");
        }
        std::cout << "[PASS] real MySQL connection and pooled query\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
