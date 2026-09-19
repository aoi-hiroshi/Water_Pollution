#include "water/net/http_types.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace water::net {

namespace {

std::string lower(std::string_view value) {
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                   });
    return result;
}

}  // namespace

const std::string* HttpRequest::queryValue(std::string_view name) const {
    const auto iterator = query.find(std::string{name});
    return iterator == query.end() ? nullptr : &iterator->second;
}

const std::string* HttpRequest::headerValue(std::string_view name) const {
    const auto iterator = headers.find(lower(name));
    return iterator == headers.end() ? nullptr : &iterator->second;
}

const std::string* HttpRequest::pathParameter(std::string_view name) const {
    const auto iterator = path_parameters.find(std::string{name});
    return iterator == path_parameters.end() ? nullptr : &iterator->second;
}

std::string HttpResponse::serialize() const {
    std::ostringstream stream;
    stream << "HTTP/1.1 " << status << ' ' << httpReasonPhrase(status)
           << "\r\n";
    stream << "Content-Type: " << content_type << "\r\n";
    stream << "Content-Length: " << body.size() << "\r\n";
    stream << "Server: water-muduo/1.0\r\n";
    stream << "Connection: " << (close ? "close" : "keep-alive") << "\r\n";
    for (const auto& [name, value] : headers) {
        stream << name << ": " << value << "\r\n";
    }
    stream << "\r\n";
    stream << body;
    return stream.str();
}

HttpMethod parseHttpMethod(std::string_view method) noexcept {
    if (method == "GET") {
        return HttpMethod::Get;
    }
    if (method == "POST") {
        return HttpMethod::Post;
    }
    if (method == "PUT") {
        return HttpMethod::Put;
    }
    if (method == "DELETE") {
        return HttpMethod::Delete;
    }
    if (method == "PATCH") {
        return HttpMethod::Patch;
    }
    if (method == "OPTIONS") {
        return HttpMethod::Options;
    }
    return HttpMethod::Unknown;
}

std::string_view httpReasonPhrase(int status) noexcept {
    switch (status) {
        case 200:
            return "OK";
        case 201:
            return "Created";
        case 202:
            return "Accepted";
        case 204:
            return "No Content";
        case 400:
            return "Bad Request";
        case 404:
            return "Not Found";
        case 405:
            return "Method Not Allowed";
        case 408:
            return "Request Timeout";
        case 413:
            return "Payload Too Large";
        case 415:
            return "Unsupported Media Type";
        case 422:
            return "Unprocessable Entity";
        case 429:
            return "Too Many Requests";
        case 500:
            return "Internal Server Error";
        case 503:
            return "Service Unavailable";
        case 504:
            return "Gateway Timeout";
        default:
            return "Unknown";
    }
}

}  // namespace water::net
