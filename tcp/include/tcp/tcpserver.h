// #include
#pragma once
#include "coroutine/coroutine.h"
#include <coroutine>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
namespace utils
{
class TcpConnection;
class TcpAcceptor;
// 用户处理连接的协程函数类型
// 注意：使用 shared_ptr 保证连接对象在协程中存活

class TcpServer
{
  public:
    using Handler = std::function<utils::Coroutine<>(std::shared_ptr<TcpConnection>)>;
    TcpServer(std::string_view host, uint16_t port, Handler handler);
    ~TcpServer();
    // 启动服务器（非阻塞，立即返回）
    // 内部会 co_spawn 接受连接的协程
    void start();
    // 停止服务器（关闭监听 socket，不再接受新连接）
    // 已建立的连接不受影响（由用户或连接自身管理）
    // TODO
    void stop() {}

    // 等待服务器完全退出（可选，用于优雅关闭）
    auto join() -> std::suspend_always;

  private:
    auto start_impl() -> Coroutine<>;
    int port_;
    std::unique_ptr<TcpAcceptor> acceptor_;
    Handler handler_;
};
} // namespace utils