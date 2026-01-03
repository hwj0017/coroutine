#include "coroutine/syscall.h"
#include "net/tcp/tcpconnection.h"
#include <arpa/inet.h>
#include <cassert>
#include <memory>
#include <netinet/in.h>
#include <sys/socket.h>
namespace utils
{
class ConnectionWaiter
{
  public:
    ConnectionWaiter(int sockfd)
        : sockfd_(sockfd), awaiter_(utils::accept(sockfd, reinterpret_cast<sockaddr*>(&addr_), &addrlen_))
    {
    }
    bool await_ready() const noexcept { return false; }
    bool await_suspend(Handle handle) noexcept { return awaiter_.await_suspend(handle); }

    auto await_resume() const noexcept -> std::shared_ptr<TcpConnection>
    {
        auto sockfd = awaiter_.await_resume();
        if (sockfd == -1)
        {
            return nullptr;
        }
        return std::make_shared<TcpConnection>(sockfd, addr_);
    }

  private:
    int sockfd_;
    sockaddr_in addr_;
    socklen_t addrlen_{sizeof(sockaddr_in)};
    AcceptAwaiter awaiter_;
};
class TcpAcceptor
{
  private:
    int port_;
    sockaddr_in addr_;
    int sockfd_{-1};

  public:
    TcpAcceptor(int port) : port_(port) {}
    ~TcpAcceptor()
    {
        if (sockfd_ != -1)
        {
            ::close(sockfd_);
        }
    }
    bool start()
    {
        addr_.sin_family = AF_INET;
        addr_.sin_port = htons(port_);
        if (inet_pton(AF_INET, "127.0.0.1", &addr_.sin_addr) <= 0)
        {
            return false;
        }
        auto addrLen_ = sizeof(addr_);
        sockfd_ = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        assert(sockfd_ != -1);
        if (::bind(sockfd_, reinterpret_cast<const sockaddr*>(&addr_), addrLen_) < 0)
        {
            return false;
        }
        if (::listen(sockfd_, SOMAXCONN) < 0)
        {
            return false;
        }
        return true;
    }
    auto accept() -> ConnectionWaiter { return ConnectionWaiter(sockfd_); }
};
} // namespace utils
