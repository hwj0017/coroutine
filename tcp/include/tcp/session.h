#include "buffer.h"
#include "coroutine/channel.h"
#include "coroutine/coroutine.h"
#include "socket.h"
#include <iostream>
#include <memory>
#include <ostream>
namespace utils
{
class RpcSession : public std::enable_shared_from_this<RpcSession>
{
  private:
    constexpr static size_t Capacity = 1024;
    Socket socket_;
    Channel<std::string, Capacity> send_channel_;
    // 私有的写协程
    static Coroutine<> write_loop(std::shared_ptr<RpcSession> session)
    {
        while (true)
        {
            auto [data, err] = co_await session->send_channel_.recv();
            if (err != State::OK)
            {
                break;
            }

            auto count = co_await session->socket_.send(data);
            if (count < 0)
            {
                break;
            }
        }
        session->close();
    }

  public:
    explicit RpcSession(Socket sock) : socket_(std::move(sock)) {}
    void start() { co_spawn(write_loop(shared_from_this())); }
    auto recv(std::span<char> buffer) { return socket_.recv(buffer); }
    // TODO 实现一个发送缓存
    auto send(std::string data) { return send_channel_.send(std::move(data)); }
    void close()
    {
        send_channel_.close();
        return socket_.close();
    }
};
} // namespace utils
