#include "water/net/muduo_http_server.h"

#include <muduo/base/Timestamp.h>
#include <muduo/net/Buffer.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/InetAddress.h>
#include <muduo/net/TcpConnection.h>
#include <muduo/net/TcpServer.h>

#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace water::net {

namespace {

HttpResponse protocolError(DecodeStatus status) {
    HttpResponse response;
    response.close = true;
    if (status == DecodeStatus::PayloadTooLarge) {
        response.status = 413;
        response.body =
            R"({"code":413,"error":"PAYLOAD_TOO_LARGE","message":"request is too large","data":null})";
    } else {
        response.status = 400;
        response.body =
            R"({"code":400,"error":"BAD_REQUEST","message":"malformed HTTP request","data":null})";
    }
    return response;
}

HttpResponse timeoutResponse() {
    HttpResponse response;
    response.status = 504;
    response.close = true;
    response.body =
        R"({"code":504,"error":"REQUEST_TIMEOUT","message":"request processing timed out","data":null})";
    return response;
}

}  // namespace

struct MuduoHttpServer::Impl
    : public std::enable_shared_from_this<MuduoHttpServer::Impl> {
    struct Session {
        explicit Session(HttpCodecOptions codec_options)
            : codec(codec_options) {}

        HttpCodec codec;
        bool request_in_flight{false};
        std::uint64_t generation{0};
    };

    Impl(muduo::net::EventLoop* event_loop,
         const muduo::net::InetAddress& address,
         std::string name,
         std::shared_ptr<const HttpRouter> request_router,
         MuduoHttpServerOptions server_options)
        : loop(event_loop),
          server(event_loop, address, std::move(name)),
          router(std::move(request_router)),
          options(server_options) {
        if (loop == nullptr || !router) {
            throw std::invalid_argument("Muduo HTTP server requires a loop and router");
        }
        if (options.io_threads < 0 ||
            options.request_timeout <= std::chrono::milliseconds::zero()) {
            throw std::invalid_argument("invalid Muduo HTTP server options");
        }
        server.setThreadNum(options.io_threads);
    }

    void installCallbacks() {
        const std::weak_ptr<Impl> weak_self = shared_from_this();
        server.setConnectionCallback(
            [weak_self](const muduo::net::TcpConnectionPtr& connection) {
                if (const auto self = weak_self.lock()) {
                    self->onConnection(connection);
                }
            });
        server.setMessageCallback(
            [weak_self](const muduo::net::TcpConnectionPtr& connection,
                        muduo::net::Buffer* buffer,
                        muduo::Timestamp) {
                if (const auto self = weak_self.lock()) {
                    self->onMessage(connection, buffer);
                }
            });
    }

    void onConnection(const muduo::net::TcpConnectionPtr& connection) {
        std::lock_guard lock(sessions_mutex);
        if (connection->connected()) {
            sessions.insert_or_assign(
                connection->name(), std::make_unique<Session>(options.codec));
        } else {
            sessions.erase(connection->name());
        }
    }

    void onMessage(const muduo::net::TcpConnectionPtr& connection,
                   muduo::net::Buffer* buffer) {
        const auto bytes = buffer->retrieveAllAsString();
        {
            std::lock_guard lock(sessions_mutex);
            const auto iterator = sessions.find(connection->name());
            if (iterator == sessions.end()) {
                return;
            }
            iterator->second->codec.append(bytes);
        }
        processNext(connection);
    }

    void processNext(const muduo::net::TcpConnectionPtr& connection) {
        HttpRequest request;
        DecodeStatus status = DecodeStatus::Incomplete;
        std::uint64_t generation = 0;
        {
            std::lock_guard lock(sessions_mutex);
            const auto iterator = sessions.find(connection->name());
            if (iterator == sessions.end() ||
                iterator->second->request_in_flight) {
                return;
            }
            status = iterator->second->codec.next(request);
            if (status == DecodeStatus::Complete) {
                iterator->second->request_in_flight = true;
                generation = ++iterator->second->generation;
            }
        }

        if (status == DecodeStatus::Incomplete) {
            return;
        }
        if (status != DecodeStatus::Complete) {
            connection->send(protocolError(status).serialize());
            connection->shutdown();
            return;
        }

        const bool keep_alive = request.keep_alive;
        const std::weak_ptr<muduo::net::TcpConnection> weak_connection(
            connection);
        const std::weak_ptr<Impl> weak_self = shared_from_this();
        const double timeout_seconds =
            static_cast<double>(options.request_timeout.count()) / 1000.0;
        connection->getLoop()->runAfter(
            timeout_seconds, [weak_self, weak_connection, generation] {
                if (const auto self = weak_self.lock()) {
                    if (const auto locked = weak_connection.lock()) {
                        self->complete(locked, generation, timeoutResponse());
                    }
                }
            });

        router->dispatch(
            std::move(request),
            [weak_self, weak_connection, generation,
             keep_alive](HttpResponse response) mutable {
                response.close = response.close || !keep_alive;
                if (const auto locked = weak_connection.lock()) {
                    auto* connection_loop = locked->getLoop();
                    connection_loop->queueInLoop(
                        [weak_self, weak_connection, generation,
                         response = std::move(response)]() mutable {
                            if (const auto self = weak_self.lock()) {
                                if (const auto active = weak_connection.lock()) {
                                    self->complete(active, generation,
                                                   std::move(response));
                                }
                            }
                        });
                }
            });
    }

    void complete(const muduo::net::TcpConnectionPtr& connection,
                  std::uint64_t generation, HttpResponse response) {
        {
            std::lock_guard lock(sessions_mutex);
            const auto iterator = sessions.find(connection->name());
            if (iterator == sessions.end() ||
                !iterator->second->request_in_flight ||
                iterator->second->generation != generation) {
                return;
            }
            iterator->second->request_in_flight = false;
        }

        const bool close = response.close;
        connection->send(response.serialize());
        if (close) {
            connection->shutdown();
            return;
        }
        processNext(connection);
    }

    muduo::net::EventLoop* loop;
    muduo::net::TcpServer server;
    std::shared_ptr<const HttpRouter> router;
    MuduoHttpServerOptions options;
    std::mutex sessions_mutex;
    std::unordered_map<std::string, std::unique_ptr<Session>> sessions;
};

MuduoHttpServer::MuduoHttpServer(
    muduo::net::EventLoop* loop, const muduo::net::InetAddress& address,
    std::string name, std::shared_ptr<const HttpRouter> router,
    MuduoHttpServerOptions options)
    : impl_(std::make_shared<Impl>(loop, address, std::move(name),
                                   std::move(router), options)) {
    impl_->installCallbacks();
}

MuduoHttpServer::~MuduoHttpServer() = default;

void MuduoHttpServer::start() {
    impl_->server.start();
}

}  // namespace water::net
