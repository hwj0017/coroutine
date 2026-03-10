#pragma once

#include "coroutine/channel.h"
#include "coroutine/coroutine.h"
#include "coroutine/mutex.h"
#include "rpc/message.h"
#include "tcp/socket.h"
#include "tcp/tcpserver.h"
#include <functional>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
namespace utils
{

class RpcServer
{
  public:
    RpcServer(std::string_view listen_ip, uint16_t port);
    ~RpcServer();
    auto start() -> Coroutine<>;

    template <typename F> void register_service(std::string method, F func);

  private:
    template <IsMessage R, IsMessage Arg> static auto wrap(R (*func)(Arg), std::string&& arg) -> std::string;

    template <IsMessage R, IsMessage Arg>
    static auto wrap(const std::function<R(Arg)>& func, std::string&& args) -> std::string;
    void register_service_impl(std::string method, std::function<std::string(std::string)> func);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

template <typename F> void RpcServer::register_service(std::string method, F func)
{
    register_service_impl(method, [func = std::move(func)](std::string args) { return wrap(func, std::move(args)); });
}
template <IsMessage R, IsMessage Arg> auto RpcServer::wrap(R (*func)(Arg), std::string&& args) -> std::string
{
    Arg arg;
    static_cast<Message*>(&arg)->ParseFromString(std::move(args));
    auto res = func(arg);
    return static_cast<Message*>(&res)->SerializeAsString();
}

template <IsMessage R, IsMessage Arg>
auto RpcServer::wrap(const std::function<R(Arg)>& func, std::string&& args) -> std::string
{
    // 判断子类
    Arg arg;
    static_cast<Message*>(&arg)->ParseFromString(std::move(args));
    auto res = func(arg);
    return static_cast<Message*>(&res)->SerializeAsString();
}
} // namespace utils
