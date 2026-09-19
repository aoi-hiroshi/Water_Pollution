#pragma once

#include "water/net/http_types.h"

#include <cstddef>
#include <string>
#include <string_view>

namespace water::net {

enum class DecodeStatus {
    Incomplete,
    Complete,
    BadRequest,
    PayloadTooLarge,
};

struct HttpCodecOptions {
    std::size_t max_header_bytes{32U * 1024U};
    std::size_t max_body_bytes{4U * 1024U * 1024U};
};

// Incremental HTTP/1.0 and HTTP/1.1 request decoder. One codec belongs to one
// TCP connection. Chunked request bodies are deliberately rejected in this
// first API version; Qt sends a Content-Length for JSON requests.
class HttpCodec final {
public:
    explicit HttpCodec(HttpCodecOptions options = {});

    void append(std::string_view bytes);
    [[nodiscard]] DecodeStatus next(HttpRequest& request);
    [[nodiscard]] std::size_t bufferedBytes() const noexcept;

private:
    HttpCodecOptions options_;
    std::string pending_;
};

}  // namespace water::net
