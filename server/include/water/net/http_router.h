#pragma once

#include "water/net/http_types.h"

#include <string>
#include <vector>

namespace water::net {

class HttpRouter final {
public:
    void add(HttpMethod method, std::string pattern, HttpHandler handler);
    void get(std::string pattern, HttpHandler handler);
    void post(std::string pattern, HttpHandler handler);

    // The reply may be invoked immediately or later from a worker thread.
    void dispatch(HttpRequest request, HttpReply reply) const;

private:
    struct Route {
        HttpMethod method;
        std::string pattern;
        std::vector<std::string> segments;
        HttpHandler handler;
    };

    std::vector<Route> routes_;
};

}  // namespace water::net
