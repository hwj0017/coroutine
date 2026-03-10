#pragma once
#include "coroutine/coroutine.h"
#include "socket.h"
#include <functional>
namespace utils
{
class Socket;
class TcpServer
{
  public:
    // 定义业务处理句柄的类型：接收一个移动构造的 Socket，返回一个协程任务
    using ConnectionHandler = std::function<Coroutine<>(Socket)>;

  private:
    Socket listen_socket_;
    InetAddress server_addr_;
    ConnectionHandler on_connection_;

  public:
    // 构造函数：初始化监听 Socket
    TcpServer(const InetAddress& addr) : server_addr_(addr)
    {
        // 创建非阻塞的 IPv4 TCP Socket
        int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
        if (fd < 0)
        {
            throw std::runtime_error("Failed to create listen socket");
        }

        // 赋予 Socket 对象管理权
        listen_socket_ = Socket(fd);

        // 设置 SO_REUSEADDR，防止服务端重启时报 "Address already in use"
        int opt = 1;
        ::setsockopt(listen_socket_.fd(), SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    }

    // 注册业务处理函数
    void set_connection_handler(ConnectionHandler handler) { on_connection_ = std::move(handler); }

    // 启动服务器的主循环 (注意：这本身也是一个协程)
    auto start() -> Coroutine<>
    {
        if (!on_connection_)
        {
            throw std::runtime_error("Connection handler not set before starting server");
        }

        listen_socket_.bind(server_addr_);
        listen_socket_.listen();

        printf("TcpServer started, listening on %s:%d\n", server_addr_.ip().c_str(), server_addr_.port());

        // 核心：无尽的 accept 循环
        while (true)
        {
            // 1. 异步等待新连接，协程在此挂起，不阻塞主线程
            Socket client_socket = co_await listen_socket_.accept();

            // 2. 如果接受连接成功
            if (client_socket.is_valid())
            {
                // 3. 调用用户注册的 handler 生成协程，并用 co_spawn 扔给调度器去执行
                // 注意：使用 std::move 把 Socket 的所有权安全地转移给业务协程
                co_spawn(on_connection_(std::move(client_socket)));
            }
        }
    }
};
} // namespace utils
