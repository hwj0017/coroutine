#include "buffer.h"
#include "coroutine/channel.h"
#include "coroutine/coroutine.h"
#include "socket.h"
#include <iostream>
#include <memory>
namespace utils
{
class RpcSession : public std::enable_shared_from_this<RpcSession>
{
  private:
    Socket socket_;
    Channel<std::string> send_channel_;
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
            auto count = co_await session->socket_.write(data);
            if (count < 0)
            {
                break;
            }
            std::cout << "write " << count << " bytes" << std::endl;
        }
    }

  public:
    explicit RpcSession(Socket sock) : socket_(std::move(sock)) {}
    void start() { co_spawn(write_loop(shared_from_this())); }
    auto read(std::span<char> buffer) { return socket_.read(buffer); }
    // TODO 实现一个发送缓存
    auto send(std::string data) { return send_channel_.send(std::move(data)); }
    auto close() { return socket_.close(); }
};
} // namespace utils
