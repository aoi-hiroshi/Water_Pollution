#include "water/config/server_config.h"

#include <charconv>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace water::config {

namespace {

std::string environment(std::string_view name,
                        std::string default_value = {}) {
    const auto* value = std::getenv(std::string{name}.c_str());
    return value == nullptr ? std::move(default_value) : std::string{value};
}

template <typename Integer>
Integer environmentInteger(std::string_view name, Integer default_value,
                           Integer minimum, Integer maximum) {
    const auto text = environment(name);
    if (text.empty()) {
        return default_value;
    }
    Integer value{};
    const auto result =
        std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() ||
        value < minimum || value > maximum) {
        throw std::invalid_argument("invalid environment variable: " +
                                    std::string{name});
    }
    return value;
}

}  // namespace

ServerConfig ServerConfig::fromEnvironment() {
    ServerConfig config;
    config.listen_host = environment("WATER_SERVER_HOST", "0.0.0.0");
    config.listen_port = environmentInteger<std::uint16_t>(
        "WATER_SERVER_PORT", 8080, 1, 65535);

    config.mysql.host = environment("WATER_DB_HOST", "127.0.0.1");
    config.mysql.port = environmentInteger<std::uint16_t>(
        "WATER_DB_PORT", 3306, 1, 65535);
    config.mysql.user = environment("WATER_DB_USER");
    config.mysql.password = environment("WATER_DB_PASSWORD");
    config.mysql.database = environment("WATER_DB_NAME");
    if (config.mysql.user.empty() || config.mysql.database.empty()) {
        throw std::invalid_argument(
            "WATER_DB_USER and WATER_DB_NAME are required");
    }

    const auto minimum_connections = environmentInteger<std::size_t>(
        "WATER_DB_POOL_MIN", 2, 1, 1024);
    const auto maximum_connections = environmentInteger<std::size_t>(
        "WATER_DB_POOL_MAX", 8, minimum_connections, 1024);
    config.database.connection_pool.min_connections = minimum_connections;
    config.database.connection_pool.max_connections = maximum_connections;
    config.database.workers.core_threads = minimum_connections;
    config.database.workers.max_threads = maximum_connections;
    config.database.workers.queue_capacity = environmentInteger<std::size_t>(
        "WATER_DB_QUEUE_CAPACITY", 512, 1, 1'000'000);

    config.http.io_threads = environmentInteger<int>(
        "WATER_IO_THREADS", 2, 0, 256);

    config.trace.model_path = environment("WATER_TRACE_MODEL_PATH");
    config.trace.model_name = environment(
        "WATER_TRACE_MODEL_NAME", "random_forest");
    config.trace.model_version = environment(
        "WATER_TRACE_MODEL_VERSION", "1.0");
    config.trace.label_offset = environmentInteger<std::int64_t>(
        "WATER_TRACE_LABEL_OFFSET", 1, 0, 1'000'000);
    config.trace.onnx_intra_op_threads = environmentInteger<int>(
        "WATER_ONNX_INTRA_OP_THREADS", 1, 1, 256);
    const auto inference_core_threads = environmentInteger<std::size_t>(
        "WATER_INFERENCE_CORE_THREADS", 1, 1, 1024);
    const auto inference_max_threads = environmentInteger<std::size_t>(
        "WATER_INFERENCE_MAX_THREADS", 2, inference_core_threads, 1024);
    config.trace.workers.core_threads = inference_core_threads;
    config.trace.workers.max_threads = inference_max_threads;
    config.trace.workers.queue_capacity = environmentInteger<std::size_t>(
        "WATER_INFERENCE_QUEUE_CAPACITY", 128, 1, 1'000'000);
    config.forecast_model_path = environment("WATER_FORECAST_MODEL_PATH");
    config.analysis_workers.core_threads = environmentInteger<std::size_t>(
        "WATER_ANALYSIS_THREADS", 1, 1, 4);
    config.analysis_workers.max_threads = config.analysis_workers.core_threads;
    config.analysis_workers.queue_capacity = environmentInteger<std::size_t>(
        "WATER_ANALYSIS_QUEUE_CAPACITY", 4, 1, 32);
    return config;
}

}  // namespace water::config
