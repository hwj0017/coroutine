#pragma once

#include "coroutine/coroutine.h"
#include "rpc/common.pb.h"
#include "rpc/message.h"
#include "tcp/tcpconnector.h"
#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
namespace utils
{
class RpcClient
{
  public:
    RpcClient(std::string_view host, uint16_t port) : connector_(std::make_unique<TcpConnector>(host, port)) {}
    template <IsMessage R, IsMessage Arg>
    auto call(std::string_view method, const Arg& arg, R* reply) -> Coroutine<bool>
    {
        if (!is_connected_)
        {
            if (auto res = co_await connector_->connect(); res < 0)
            {
                co_return false;
            }
            is_connected_ = true;
        }
        constexpr size_t buffer_size = 1024;
        std::array<char, buffer_size> buffer;
        ::rpc::Request request;
        request.set_method(std::string(method));
        request.set_input(arg.SerializeAsString());
        auto request_size = request.ByteSizeLong();
        if (request_size > buffer_size)
        {
            co_return false;
        }
        request.SerializeToArray(buffer.data(), buffer_size);
        auto write_byte = co_await connector_->write({buffer.data(), request_size});
        if (write_byte != request_size)
        {
            co_return false;
        }
        auto read_byte = co_await connector_->read(buffer);
        if (read_byte < 0)
        {
            co_return false;
        }
        rpc::Response response;
        response.ParseFromArray(buffer.data(), read_byte);
        co_return static_cast<Message*>(reply)->ParseFromArray(response.output().data(), response.output().size());
    }

  private:
    std::unique_ptr<TcpConnector> connector_;
    bool is_connected_ = false;
};

} // namespace utils