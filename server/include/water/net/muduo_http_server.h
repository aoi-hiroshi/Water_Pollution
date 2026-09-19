#pragma once

#include "water/net/http_codec.h"
#include "water/net/http_router.h"

#include <chrono>
#include <cstddef>
#include <memory>
#include <string>

namespace muduo::net {
class EventLoop;
class InetAddress;
}  // namespace muduo::net

namespace water::net {

struct MuduoHttpServerOptions {
    int io_threads{2};
    HttpCodecOptions codec{};
    std::chrono::milliseconds request_timeout{std::chrono::seconds{15}};
};

// Thin asynchronous HTTP/1.1 adapter around muduo::net::TcpServer. Route
// handlers may call HttpReply from any thread; the response is marshalled back
// to the connection's EventLoop before it is sent.
class MuduoHttpServer final {
public:
    MuduoHttpServer(muduo::net::EventLoop* loop,
                    const muduo::net::InetAddress& address,
                    std::string name,
                    std::shared_ptr<const HttpRouter> router,
                    MuduoHttpServerOptions options = {});
    ~MuduoHttpServer();

    MuduoHttpServer(const MuduoHttpServer&) = delete;
    MuduoHttpServer& operator=(const MuduoHttpServer&) = delete;

    void start();

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

}  // namespace water::net
