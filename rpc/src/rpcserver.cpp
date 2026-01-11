#include "rpc/rpcserver.h"

#include "coroutine/coroutine.h"
#include "rpc/common.pb.h"
#include "tcp/tcpconnection.h"
#include "tcp/tcpserver.h"
#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string_view>
namespace utils
{
RpcServer::RpcServer(std::string_view listen_ip, uint16_t port)
{
    auto handler = [this](std::shared_ptr<TcpConnection> connection) -> Coroutine<> {
        return handle_rpc_connection(std::move(connection));
    };
    tcp_server_ = std::make_unique<TcpServer>(listen_ip, port, std::move(handler));
}
RpcServer::~RpcServer() = default;

void RpcServer::start() { tcp_server_->start(); }
void RpcServer::register_service_impl(std::string method, std::function<std::string(std::string)> func)
{
    services_.emplace(std::move(method), std::move(func));
}

auto RpcServer::handle_rpc_connection(std::shared_ptr<TcpConnection> connection) -> Coroutine<>
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
        ::rpc::Response response;
        auto it = services_.find(request.method());
        response.set_method(std::move(request.method()));
        if (it == services_.end())
        {
            response.set_output({});
        }
        response.set_output(it->second(std::move(request.input())));
        auto required_size = response.ByteSizeLong();
        if (required_size > buffer_size)
        {
            std::cout << "error" << std::endl;
            continue;
        }
        std::cout << "send: " << response.SerializeAsString() << std::endl;
        assert(response.SerializeToArray(buffer.data(), buffer_size));
        co_await connection->write({buffer.data(), required_size});
    }
}
} // namespace utils
