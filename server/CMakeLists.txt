cmake_minimum_required(VERSION 3.20)

project(water_server_components LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_library(water_concurrency
    src/concurrency/thread_pool.cpp
)

target_include_directories(water_concurrency
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(water_concurrency PUBLIC Threads::Threads)

add_library(water_database
    src/db/database.cpp
    src/db/connection_pool.cpp
    src/db/db_executor.cpp
)

target_include_directories(water_database
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(water_database PUBLIC water_concurrency Threads::Threads)

add_library(water_http
    src/net/http_types.cpp
    src/net/http_codec.cpp
    src/net/http_router.cpp
)

target_include_directories(water_http
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_library(water_inference
    src/inference/trace_classifier.cpp
    src/inference/forecaster.cpp
)

target_include_directories(water_inference
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

option(WATER_WITH_ONNX "Build ONNX Runtime classification and forecast adapters" OFF)
if(WATER_WITH_ONNX)
    find_path(WATER_ONNXRUNTIME_INCLUDE_DIR NAMES onnxruntime_cxx_api.h)
    find_library(WATER_ONNXRUNTIME_LIBRARY NAMES onnxruntime)
    if(NOT WATER_ONNXRUNTIME_INCLUDE_DIR OR NOT WATER_ONNXRUNTIME_LIBRARY)
        message(FATAL_ERROR
            "WATER_WITH_ONNX=ON requires ONNX Runtime headers and library")
    endif()

    target_sources(water_inference PRIVATE
        src/inference/onnx_trace_classifier.cpp
        src/inference/onnx_forecaster.cpp)
    target_include_directories(water_inference PRIVATE
        ${WATER_ONNXRUNTIME_INCLUDE_DIR})
    target_link_libraries(water_inference PRIVATE
        ${WATER_ONNXRUNTIME_LIBRARY})
    target_compile_definitions(water_inference PUBLIC WATER_WITH_ONNX=1)
    if(MINGW)
        # ORT 1.20 Windows headers spell this MSVC calling convention with
        # one underscore. MinGW uses __stdcall. Linux does not need this.
        target_compile_definitions(water_inference PRIVATE _stdcall=__stdcall)
    endif()
endif()

add_library(water_application_core
    src/analysis/classification_analysis.cpp
    src/analysis/forecast_data_analysis.cpp
    src/repository/water_repository.cpp
    src/service/classification_data_service.cpp
    src/service/forecast_data_service.cpp
    src/service/water_service.cpp
    src/service/trace_service.cpp
    src/service/forecast_service.cpp
)

target_include_directories(water_application_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(water_application_core
    PUBLIC water_database water_inference)

option(WATER_WITH_MYSQL "Build the MySQL C API adapter" OFF)
if(WATER_WITH_MYSQL)
    find_path(WATER_MYSQL_INCLUDE_DIR NAMES mysql.h PATH_SUFFIXES include)
    find_library(WATER_MYSQL_LIBRARY
        NAMES mysqlclient mysql libmysql
        PATH_SUFFIXES lib lib64
    )
    if(NOT WATER_MYSQL_INCLUDE_DIR OR NOT WATER_MYSQL_LIBRARY)
        message(FATAL_ERROR
            "WATER_WITH_MYSQL=ON requires MySQL client headers and library")
    endif()

    target_sources(water_database PRIVATE src/db/mysql_connection.cpp)
    target_include_directories(water_database PRIVATE ${WATER_MYSQL_INCLUDE_DIR})
    target_link_libraries(water_database PUBLIC ${WATER_MYSQL_LIBRARY})
    target_compile_definitions(water_database PUBLIC WATER_WITH_MYSQL=1)
endif()

if(MSVC)
    target_compile_options(water_concurrency PRIVATE /W4 /permissive-)
    target_compile_options(water_database PRIVATE /W4 /permissive-)
    target_compile_options(water_http PRIVATE /W4 /permissive-)
    target_compile_options(water_inference PRIVATE /W4 /permissive-)
    target_compile_options(water_application_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(water_concurrency PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(water_database PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(water_http PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(water_inference PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(water_application_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

option(WATER_WITH_MUDUO "Build the Linux Muduo HTTP server" OFF)
if(WATER_WITH_MUDUO)
    if(NOT UNIX)
        message(FATAL_ERROR "Muduo server builds are supported on Linux/Unix")
    endif()

    find_path(WATER_MUDUO_INCLUDE_DIR NAMES muduo/net/TcpServer.h)
    find_library(WATER_MUDUO_NET_LIBRARY NAMES muduo_net)
    find_library(WATER_MUDUO_BASE_LIBRARY NAMES muduo_base)
    if(NOT WATER_MUDUO_INCLUDE_DIR OR
       NOT WATER_MUDUO_NET_LIBRARY OR
       NOT WATER_MUDUO_BASE_LIBRARY)
        message(FATAL_ERROR
            "WATER_WITH_MUDUO=ON requires Muduo headers, muduo_net and muduo_base")
    endif()

    add_library(water_muduo_http src/net/muduo_http_server.cpp)
    target_include_directories(water_muduo_http
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
        PRIVATE ${WATER_MUDUO_INCLUDE_DIR}
    )
    target_link_libraries(water_muduo_http
        PUBLIC water_http
        PRIVATE
            ${WATER_MUDUO_NET_LIBRARY}
            ${WATER_MUDUO_BASE_LIBRARY}
            Threads::Threads
            ${CMAKE_DL_LIBS}
    )

    if(MSVC)
        target_compile_options(water_muduo_http PRIVATE /W4 /permissive-)
    else()
        target_compile_options(water_muduo_http PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    if(NOT WATER_WITH_MYSQL)
        message(FATAL_ERROR
            "WATER_WITH_MUDUO=ON currently requires WATER_WITH_MYSQL=ON")
    endif()

    find_package(nlohmann_json 3.2.0 CONFIG QUIET)
    if(NOT nlohmann_json_FOUND)
        find_path(WATER_NLOHMANN_JSON_INCLUDE_DIR NAMES nlohmann/json.hpp)
        if(NOT WATER_NLOHMANN_JSON_INCLUDE_DIR)
            message(FATAL_ERROR
                "The Muduo API server requires nlohmann/json.hpp")
        endif()
        add_library(water_nlohmann_json INTERFACE)
        target_include_directories(water_nlohmann_json INTERFACE
            ${WATER_NLOHMANN_JSON_INCLUDE_DIR})
        add_library(nlohmann_json::nlohmann_json ALIAS water_nlohmann_json)
    endif()

    add_library(water_api_controller src/controller/api_controller.cpp)
    target_include_directories(water_api_controller
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(water_api_controller
        PUBLIC
            water_http
            water_application_core
            nlohmann_json::nlohmann_json
    )

    add_executable(water_server
        src/config/server_config.cpp
        src/main.cpp
    )
    target_include_directories(water_server PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${WATER_MUDUO_INCLUDE_DIR}
    )
    target_link_libraries(water_server
        PRIVATE
            water_api_controller
            water_muduo_http
            water_application_core
            water_database
            ${WATER_MUDUO_NET_LIBRARY}
            ${WATER_MUDUO_BASE_LIBRARY}
    )

    if(MSVC)
        target_compile_options(water_api_controller PRIVATE /W4 /permissive-)
        target_compile_options(water_server PRIVATE /W4 /permissive-)
    else()
        target_compile_options(water_api_controller PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(water_server PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

option(WATER_BUILD_TESTS "Build water server component tests" ON)
if(WATER_BUILD_TESTS)
    enable_testing()
    add_executable(thread_pool_tests tests/thread_pool_test.cpp)
    target_link_libraries(thread_pool_tests PRIVATE water_concurrency)
    add_test(NAME thread_pool_tests COMMAND thread_pool_tests)

    add_executable(connection_pool_tests tests/connection_pool_test.cpp)
    target_link_libraries(connection_pool_tests PRIVATE water_database)
    add_test(NAME connection_pool_tests COMMAND connection_pool_tests)

    add_executable(http_core_tests tests/http_core_test.cpp)
    target_link_libraries(http_core_tests PRIVATE water_http)
    add_test(NAME http_core_tests COMMAND http_core_tests)

    add_executable(water_service_tests tests/water_service_test.cpp)
    target_link_libraries(water_service_tests PRIVATE water_application_core)
    add_test(NAME water_service_tests COMMAND water_service_tests)

    add_executable(trace_service_tests tests/trace_service_test.cpp)
    target_link_libraries(trace_service_tests PRIVATE water_application_core)
    add_test(NAME trace_service_tests COMMAND trace_service_tests)

    add_executable(forecast_service_tests tests/forecast_service_test.cpp)
    target_link_libraries(forecast_service_tests PRIVATE water_application_core)
    add_test(NAME forecast_service_tests COMMAND forecast_service_tests)
    add_executable(classification_analysis_tests tests/classification_analysis_test.cpp)
    target_link_libraries(classification_analysis_tests PRIVATE water_application_core)
    add_test(NAME classification_analysis_tests COMMAND classification_analysis_tests)
    add_executable(forecast_data_analysis_tests tests/forecast_data_analysis_test.cpp)
    target_link_libraries(forecast_data_analysis_tests PRIVATE water_application_core)
    add_test(NAME forecast_data_analysis_tests COMMAND forecast_data_analysis_tests)

    if(TARGET water_api_controller)
        add_executable(classification_pipeline_tests tests/classification_pipeline_test.cpp)
        target_link_libraries(classification_pipeline_tests PRIVATE water_api_controller)
        add_test(NAME classification_pipeline_tests COMMAND classification_pipeline_tests)
        add_executable(forecast_api_tests tests/forecast_api_test.cpp)
        target_link_libraries(forecast_api_tests PRIVATE water_api_controller)
        add_test(NAME forecast_api_tests COMMAND forecast_api_tests)
    endif()

    if(WATER_WITH_ONNX AND TARGET nlohmann_json::nlohmann_json)
        add_executable(onnx_trace_integration_test tests/onnx_trace_integration_test.cpp)
        target_link_libraries(onnx_trace_integration_test PRIVATE
            water_inference nlohmann_json::nlohmann_json)
        add_test(NAME onnx_trace_integration_test COMMAND onnx_trace_integration_test
            ${CMAKE_CURRENT_SOURCE_DIR}/models/trace_random_forest.onnx
            ${CMAKE_CURRENT_SOURCE_DIR}/models/trace_random_forest.golden.json)
        set_tests_properties(onnx_trace_integration_test PROPERTIES SKIP_RETURN_CODE 77)
        add_executable(onnx_forecast_integration_test tests/onnx_forecast_integration_test.cpp)
        target_link_libraries(onnx_forecast_integration_test PRIVATE
            water_inference nlohmann_json::nlohmann_json)
        add_test(NAME onnx_forecast_integration_test COMMAND onnx_forecast_integration_test
            ${CMAKE_CURRENT_SOURCE_DIR}/models/forecast_attention_lstm_h10.onnx
            ${CMAKE_CURRENT_SOURCE_DIR}/models/forecast_attention_lstm_h10.golden.json)
        set_tests_properties(onnx_forecast_integration_test PROPERTIES SKIP_RETURN_CODE 77)
    endif()

    if(WATER_WITH_MYSQL)
        add_executable(mysql_connection_integration_test
            tests/mysql_connection_integration_test.cpp
        )
        target_link_libraries(mysql_connection_integration_test
            PRIVATE water_database
        )
        add_test(NAME mysql_connection_integration_test
                 COMMAND mysql_connection_integration_test)
        set_tests_properties(mysql_connection_integration_test
            PROPERTIES SKIP_RETURN_CODE 77)
    endif()
endif()
