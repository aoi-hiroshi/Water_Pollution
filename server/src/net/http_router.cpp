#include "water/net/http_router.h"

#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace water::net {

namespace {

std::vector<std::string> splitPath(const std::string& path) {
    std::vector<std::string> segments;
    std::size_t cursor = 0;
    while (cursor < path.size()) {
        while (cursor < path.size() && path[cursor] == '/') {
            ++cursor;
        }
        if (cursor == path.size()) {
            break;
        }
        const auto end = path.find('/', cursor);
        segments.emplace_back(path.substr(
            cursor, end == std::string::npos ? std::string::npos : end - cursor));
        if (end == std::string::npos) {
            break;
        }
        cursor = end + 1;
    }
    return segments;
}

bool match(const std::vector<std::string>& pattern,
           const std::vector<std::string>& actual,
           std::unordered_map<std::string, std::string>& parameters) {
    if (pattern.size() != actual.size()) {
        return false;
    }
    for (std::size_t index = 0; index < pattern.size(); ++index) {
        if (!pattern[index].empty() && pattern[index].front() == ':') {
            parameters.insert_or_assign(pattern[index].substr(1), actual[index]);
        } else if (pattern[index] != actual[index]) {
            return false;
        }
    }
    return true;
}

HttpResponse errorResponse(int status, std::string code, std::string message) {
    HttpResponse response;
    response.status = status;
    response.body = "{\"code\":" + std::to_string(status) +
                    ",\"error\":\"" + std::move(code) +
                    "\",\"message\":\"" + std::move(message) +
                    "\",\"data\":null}";
    return response;
}

}  // namespace

void HttpRouter::add(HttpMethod method, std::string pattern,
                     HttpHandler handler) {
    if (method == HttpMethod::Unknown || pattern.empty() ||
        pattern.front() != '/' || !handler) {
        throw std::invalid_argument("invalid HTTP route");
    }
    routes_.push_back(Route{method, pattern, splitPath(pattern),
                            std::move(handler)});
}

void HttpRouter::get(std::string pattern, HttpHandler handler) {
    add(HttpMethod::Get, std::move(pattern), std::move(handler));
}

void HttpRouter::post(std::string pattern, HttpHandler handler) {
    add(HttpMethod::Post, std::move(pattern), std::move(handler));
}

void HttpRouter::dispatch(HttpRequest request, HttpReply reply) const {
    if (!reply) {
        throw std::invalid_argument("HTTP reply callback must not be empty");
    }

    const auto actual = splitPath(request.path);
    bool path_matched = false;
    for (const auto& route : routes_) {
        std::unordered_map<std::string, std::string> parameters;
        if (!match(route.segments, actual, parameters)) {
            continue;
        }
        path_matched = true;
        if (route.method != request.method) {
            continue;
        }
        request.path_parameters = std::move(parameters);
        try {
            route.handler(std::move(request), reply);
        } catch (...) {
            reply(errorResponse(500, "INTERNAL_ERROR", "internal server error"));
        }
        return;
    }

    reply(path_matched
              ? errorResponse(405, "METHOD_NOT_ALLOWED", "method not allowed")
              : errorResponse(404, "NOT_FOUND", "route not found"));
}

}  // namespace water::net
