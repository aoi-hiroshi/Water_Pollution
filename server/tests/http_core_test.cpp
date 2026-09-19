#include "water/net/http_codec.h"
#include "water/net/http_router.h"
#include "water/net/http_types.h"

#include <cstdlib>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using water::net::DecodeStatus;
using water::net::HttpCodec;
using water::net::HttpMethod;
using water::net::HttpRequest;
using water::net::HttpResponse;
using water::net::HttpRouter;

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression)) {                                                  \
            throw std::runtime_error(std::string{"check failed: "} +         \
                                     #expression);                            \
        }                                                                     \
    } while (false)

void testFragmentedRequestAndQueryDecoding() {
    HttpCodec codec;
    HttpRequest request;
    codec.append("GET /api/v1/data/overview?company_id=42&dataset=train%5Fdata ");
    CHECK(codec.next(request) == DecodeStatus::Incomplete);
    codec.append("HTTP/1.1\r\nHost: localhost\r\nConnection: keep-alive\r\n\r\n");
    CHECK(codec.next(request) == DecodeStatus::Complete);
    CHECK(request.method == HttpMethod::Get);
    CHECK(request.path == "/api/v1/data/overview");
    CHECK(*request.queryValue("company_id") == "42");
    CHECK(*request.queryValue("dataset") == "train_data");
    CHECK(request.keep_alive);
    CHECK(codec.bufferedBytes() == 0);
}

void testJsonBodyAndPipelining() {
    HttpCodec codec;
    const std::string body = R"({"sample_id":7})";
    codec.append("POST /api/v1/inference/classify HTTP/1.1\r\n"
                 "Host: localhost\r\nContent-Type: application/json\r\n"
                 "Content-Length: " +
                 std::to_string(body.size()) + "\r\n\r\n" + body +
                 "GET /api/v1/health HTTP/1.1\r\nHost: localhost\r\n\r\n");

    HttpRequest first;
    CHECK(codec.next(first) == DecodeStatus::Complete);
    CHECK(first.method == HttpMethod::Post);
    CHECK(first.body == body);

    HttpRequest second;
    CHECK(codec.next(second) == DecodeStatus::Complete);
    CHECK(second.path == "/api/v1/health");
}

void testProtocolLimitsAndErrors() {
    HttpCodec codec({.max_header_bytes = 32, .max_body_bytes = 8});
    codec.append("GET /this/header/is/too/large HTTP/1.1\r\n");
    HttpRequest request;
    CHECK(codec.next(request) == DecodeStatus::PayloadTooLarge);

    HttpCodec body_codec({.max_header_bytes = 1024, .max_body_bytes = 4});
    body_codec.append("POST / HTTP/1.1\r\nContent-Length: 5\r\n\r\n12345");
    CHECK(body_codec.next(request) == DecodeStatus::PayloadTooLarge);

    HttpCodec chunked;
    chunked.append("POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n");
    CHECK(chunked.next(request) == DecodeStatus::BadRequest);

    HttpCodec ambiguous;
    ambiguous.append("POST / HTTP/1.1\r\nContent-Length: 0\r\n"
                     "Content-Length: 1\r\n\r\n");
    CHECK(ambiguous.next(request) == DecodeStatus::BadRequest);
}

void testRouterParametersAndErrors() {
    HttpRouter router;
    router.get("/api/v1/samples/:sample_id",
               [](HttpRequest request, water::net::HttpReply reply) {
                   const auto* value = request.pathParameter("sample_id");
                   HttpResponse response;
                   response.body = value == nullptr ? "missing" : *value;
                   reply(std::move(response));
               });

    HttpRequest request;
    request.method = HttpMethod::Get;
    request.path = "/api/v1/samples/91";
    HttpResponse response;
    router.dispatch(std::move(request),
                    [&response](HttpResponse value) {
                        response = std::move(value);
                    });
    CHECK(response.status == 200);
    CHECK(response.body == "91");

    request = HttpRequest{};
    request.method = HttpMethod::Post;
    request.path = "/api/v1/samples/91";
    router.dispatch(std::move(request),
                    [&response](HttpResponse value) {
                        response = std::move(value);
                    });
    CHECK(response.status == 405);

    request = HttpRequest{};
    request.method = HttpMethod::Get;
    request.path = "/missing";
    router.dispatch(std::move(request),
                    [&response](HttpResponse value) {
                        response = std::move(value);
                    });
    CHECK(response.status == 404);
}

void testResponseSerialization() {
    HttpResponse response;
    response.status = 200;
    response.body = "{}";
    response.headers.emplace("X-Request-Id", "abc");
    const auto wire = response.serialize();
    CHECK(wire.find("HTTP/1.1 200 OK\r\n") == 0);
    CHECK(wire.find("Content-Length: 2\r\n") != std::string::npos);
    CHECK(wire.ends_with("\r\n\r\n{}"));
}

}  // namespace

int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> tests{
        {"fragmented request/query", testFragmentedRequestAndQueryDecoding},
        {"JSON body/pipelining", testJsonBodyAndPipelining},
        {"protocol limits/errors", testProtocolLimitsAndErrors},
        {"router parameters/errors", testRouterParametersAndErrors},
        {"response serialization", testResponseSerialization},
    };

    for (const auto& [name, test] : tests) {
        try {
            test();
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception& error) {
            std::cerr << "[FAIL] " << name << ": " << error.what() << '\n';
            return EXIT_FAILURE;
        }
    }
    return EXIT_SUCCESS;
}
