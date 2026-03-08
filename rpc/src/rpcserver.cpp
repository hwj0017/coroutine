#include "rpc/rpcserver.h"

#include "coroutine/coroutine.h"
#include "rpc/common.pb.h"
#include "tcp/tcpconnection.h"
#include "tcp/tcpserver.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string_view>
namespace utils
{
struct RpcServer::Impl
{
    Impl(std::string_view listen_ip, uint16_t port)
        : tcp_server_(listen_ip, port, [this](std::shared_ptr<TcpConnection> connection) -> Coroutine<> {
              return handle_rpc_connection(std::move(connection));
          })
    {
    }
    void start() { tcp_server_.start(); }
    auto handle_rpc_connection(std::shared_ptr<TcpConnection> connection) -> Coroutine<>
    {
        constexpr size_t buffer_size = 1024;
        std::array<char, buffer_size> buffer;
        while (true)
        {
            auto count = co_await connection->read(buffer);
            if (count <= 0)
            {
                break;
            }
            ::rpc::Request request;
            if (!request.ParseFromArray(buffer.data(), count))
            {
                std::cout << "error" << std::endl;
                continue;
            }
            co_spawn(handle_rpc_request(connection, std::move(request)));
        }
    }
    auto handle_rpc_request(std::shared_ptr<TcpConnection> connection, rpc::Request request) -> Coroutine<>
    {
        ::rpc::Response response;
        auto it = services_.find(request.method());
        response.set_method(std::move(request.method()));
        if (it == services_.end())
        {
            response.set_output({});
        }
        response.set_output(it->second(std::move(request.input())));
        {
            auto guard = co_await mutex_.guard();
            co_await connection->write(response.SerializeAsString());
        }
    }
    void register_service_impl(std::string&& method, std::function<std::string(std::string)>&& func)
    {
        services_.emplace(std::move(method), std::move(func));
    }
    TcpServer tcp_server_;
    Mutex mutex_;
    std::unordered_map<std::string, std::function<std::string(std::string)>> services_;
};
RpcServer::RpcServer(std::string_view listen_ip, uint16_t port) : impl_(std::make_unique<Impl>(listen_ip, port)) {}
RpcServer::~RpcServer() = default;

void RpcServer::start() { impl_->start(); }
void RpcServer::register_service_impl(std::string method, std::function<std::string(std::string)> func)
{
    impl_->register_service_impl(std::move(method), std::move(func));
}

} // namespace utils
