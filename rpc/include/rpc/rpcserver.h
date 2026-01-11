#pragma once

#include "coroutine/coroutine.h"
#include "rpc/message.h"
#include <functional>

#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace utils
{
class TcpServer;
class TcpConnection;
class RpcServer
{
  public:
    RpcServer(std::string_view listen_ip, uint16_t port);
    ~RpcServer();
    void start();
    template <typename F> void register_service(std::string method, F func);
    void stop();
    auto join() -> std::suspend_always { return {}; }

  private:
    template <IsMessage R, IsMessage Arg> static auto wrap(R (*func)(Arg), std::string&& arg) -> std::string;

    template <IsMessage R, IsMessage Arg>
    static auto wrap(const std::function<R(Arg)>& func, std::string&& args) -> std::string;
    void register_service_impl(std::string method, std::function<std::string(std::string)> func);
    auto handle_rpc_connection(std::shared_ptr<TcpConnection> tcp_conn) -> Coroutine<>;
    std::unique_ptr<TcpServer> tcp_server_;
    class ServiceHandler;
    std::map<std::string, std::function<std::string(std::string)>> services_;
};

template <typename F> void RpcServer::register_service(std::string method, F func)
{
    register_service_impl(method, [func = std::move(func)](std::string args) { return wrap(func, std::move(args)); });
}
template <IsMessage R, IsMessage Arg> auto RpcServer::wrap(R (*func)(Arg), std::string&& arg) -> std::string
{
    return wrap(std::function<R(Arg)>(func), std::move(arg));
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
