#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace water::net {

enum class HttpMethod {
    Get,
    Post,
    Put,
    Delete,
    Patch,
    Options,
    Unknown,
};

struct HttpRequest {
    HttpMethod method{HttpMethod::Unknown};
    std::string method_text;
    std::string target;
    std::string path;
    std::string version;
    std::unordered_map<std::string, std::string> query;
    std::unordered_map<std::string, std::string> headers;
    std::unordered_map<std::string, std::string> path_parameters;
    std::string body;
    bool keep_alive{true};

    [[nodiscard]] const std::string* queryValue(std::string_view name) const;
    [[nodiscard]] const std::string* headerValue(std::string_view name) const;
    [[nodiscard]] const std::string* pathParameter(std::string_view name) const;
};

struct HttpResponse {
    int status{200};
    std::string content_type{"application/json; charset=utf-8"};
    std::unordered_map<std::string, std::string> headers;
    std::string body;
    bool close{false};

    [[nodiscard]] std::string serialize() const;
};

using HttpReply = std::function<void(HttpResponse)>;
using HttpHandler = std::function<void(HttpRequest, HttpReply)>;

[[nodiscard]] HttpMethod parseHttpMethod(std::string_view method) noexcept;
[[nodiscard]] std::string_view httpReasonPhrase(int status) noexcept;

}  // namespace water::net
