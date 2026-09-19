#include "water/net/http_codec.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace water::net {

namespace {

std::string_view trim(std::string_view value) {
    while (!value.empty() &&
           std::isspace(static_cast<unsigned char>(value.front())) != 0) {
        value.remove_prefix(1);
    }
    while (!value.empty() &&
           std::isspace(static_cast<unsigned char>(value.back())) != 0) {
        value.remove_suffix(1);
    }
    return value;
}

std::string lower(std::string_view value) {
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                   });
    return result;
}

int hexValue(char character) {
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    return -1;
}

bool decodeUrlComponent(std::string_view encoded, std::string& decoded) {
    decoded.clear();
    decoded.reserve(encoded.size());
    for (std::size_t index = 0; index < encoded.size(); ++index) {
        const char character = encoded[index];
        if (character == '+') {
            decoded.push_back(' ');
            continue;
        }
        if (character != '%') {
            decoded.push_back(character);
            continue;
        }
        if (index + 2 >= encoded.size()) {
            return false;
        }
        const int high = hexValue(encoded[index + 1]);
        const int low = hexValue(encoded[index + 2]);
        if (high < 0 || low < 0) {
            return false;
        }
        decoded.push_back(static_cast<char>((high << 4) | low));
        index += 2;
    }
    return true;
}

bool parseTarget(std::string_view target, HttpRequest& request) {
    const auto query_position = target.find('?');
    const auto encoded_path = target.substr(0, query_position);
    if (encoded_path.empty() || encoded_path.front() != '/') {
        return false;
    }
    if (!decodeUrlComponent(encoded_path, request.path)) {
        return false;
    }

    if (query_position == std::string_view::npos) {
        return true;
    }

    auto query = target.substr(query_position + 1);
    while (!query.empty()) {
        const auto separator = query.find('&');
        const auto pair = query.substr(0, separator);
        const auto equals = pair.find('=');
        std::string key;
        std::string value;
        if (!decodeUrlComponent(pair.substr(0, equals), key)) {
            return false;
        }
        if (equals != std::string_view::npos &&
            !decodeUrlComponent(pair.substr(equals + 1), value)) {
            return false;
        }
        if (!key.empty()) {
            request.query.insert_or_assign(std::move(key), std::move(value));
        }
        if (separator == std::string_view::npos) {
            break;
        }
        query.remove_prefix(separator + 1);
    }
    return true;
}

bool parseSize(std::string_view text, std::size_t& result) {
    text = trim(text);
    if (text.empty()) {
        return false;
    }
    std::uint64_t value = 0;
    const auto conversion =
        std::from_chars(text.data(), text.data() + text.size(), value);
    if (conversion.ec != std::errc{} ||
        conversion.ptr != text.data() + text.size() ||
        value > std::numeric_limits<std::size_t>::max()) {
        return false;
    }
    result = static_cast<std::size_t>(value);
    return true;
}

}  // namespace

HttpCodec::HttpCodec(HttpCodecOptions options) : options_(options) {
    if (options_.max_header_bytes == 0 || options_.max_body_bytes == 0) {
        throw std::invalid_argument("HTTP codec limits must be positive");
    }
}

void HttpCodec::append(std::string_view bytes) {
    pending_.append(bytes.data(), bytes.size());
}

DecodeStatus HttpCodec::next(HttpRequest& request) {
    const auto header_end = pending_.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        return pending_.size() > options_.max_header_bytes
                   ? DecodeStatus::PayloadTooLarge
                   : DecodeStatus::Incomplete;
    }
    if (header_end + 4 > options_.max_header_bytes) {
        return DecodeStatus::PayloadTooLarge;
    }

    HttpRequest parsed;
    const std::string_view headers{pending_.data(), header_end};
    const auto first_line_end = headers.find("\r\n");
    const auto request_line = headers.substr(0, first_line_end);
    const auto first_space = request_line.find(' ');
    const auto second_space = first_space == std::string_view::npos
                                  ? std::string_view::npos
                                  : request_line.find(' ', first_space + 1);
    if (first_space == std::string_view::npos ||
        second_space == std::string_view::npos ||
        request_line.find(' ', second_space + 1) != std::string_view::npos) {
        return DecodeStatus::BadRequest;
    }

    parsed.method_text = std::string{request_line.substr(0, first_space)};
    parsed.method = parseHttpMethod(parsed.method_text);
    parsed.target = std::string{
        request_line.substr(first_space + 1, second_space - first_space - 1)};
    parsed.version = std::string{request_line.substr(second_space + 1)};
    if (parsed.method == HttpMethod::Unknown ||
        (parsed.version != "HTTP/1.1" && parsed.version != "HTTP/1.0") ||
        !parseTarget(parsed.target, parsed)) {
        return DecodeStatus::BadRequest;
    }

    std::size_t cursor = first_line_end == std::string_view::npos
                             ? headers.size()
                             : first_line_end + 2;
    while (cursor < headers.size()) {
        const auto line_end = headers.find("\r\n", cursor);
        const auto line = headers.substr(
            cursor, line_end == std::string_view::npos
                        ? headers.size() - cursor
                        : line_end - cursor);
        const auto colon = line.find(':');
        if (colon == std::string_view::npos) {
            return DecodeStatus::BadRequest;
        }
        const auto name = lower(trim(line.substr(0, colon)));
        const auto value = trim(line.substr(colon + 1));
        if (name.empty()) {
            return DecodeStatus::BadRequest;
        }
        const auto existing = parsed.headers.find(name);
        if (existing != parsed.headers.end()) {
            if (name == "content-length" || name == "transfer-encoding" ||
                name == "host") {
                return DecodeStatus::BadRequest;
            }
            existing->second += ",";
            existing->second += value;
        } else {
            parsed.headers.emplace(name, std::string{value});
        }
        if (line_end == std::string_view::npos) {
            break;
        }
        cursor = line_end + 2;
    }

    const auto transfer_encoding = parsed.headers.find("transfer-encoding");
    const auto length = parsed.headers.find("content-length");
    if (transfer_encoding != parsed.headers.end() &&
        length != parsed.headers.end()) {
        return DecodeStatus::BadRequest;
    }
    if (transfer_encoding != parsed.headers.end() &&
        lower(transfer_encoding->second) != "identity") {
        return DecodeStatus::BadRequest;
    }

    std::size_t content_length = 0;
    if (length != parsed.headers.end() &&
        !parseSize(length->second, content_length)) {
        return DecodeStatus::BadRequest;
    }
    if (content_length > options_.max_body_bytes) {
        return DecodeStatus::PayloadTooLarge;
    }

    const std::size_t message_size = header_end + 4 + content_length;
    if (pending_.size() < message_size) {
        return DecodeStatus::Incomplete;
    }

    parsed.body.assign(pending_.data() + header_end + 4, content_length);
    const auto connection = parsed.headers.find("connection");
    const auto connection_value =
        connection == parsed.headers.end() ? std::string{} : lower(connection->second);
    parsed.keep_alive = parsed.version == "HTTP/1.1"
                            ? connection_value != "close"
                            : connection_value == "keep-alive";

    pending_.erase(0, message_size);
    request = std::move(parsed);
    return DecodeStatus::Complete;
}

std::size_t HttpCodec::bufferedBytes() const noexcept {
    return pending_.size();
}

}  // namespace water::net
