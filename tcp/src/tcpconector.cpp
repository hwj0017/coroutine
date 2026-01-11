#include "coroutine/syscall.h"
#include "tcp/tcpconnector.h"
#include <arpa/inet.h>
#include <cassert>
#include <span>
#include <string_view>
namespace utils
{
TcpConnector::TcpConnector(std::string_view host, std::uint16_t port) : host_(host.data(), host.size()), port_(port)
{
    sockfd_ = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    assert(sockfd_ != -1);
    addr_.sin_family = AF_INET;
    addr_.sin_port = htons(port);
    assert(::inet_pton(AF_INET, host_.c_str(), &addr_.sin_addr));
}

TcpConnector::~TcpConnector()
{
    if (sockfd_ != -1)
    {
        ::close(sockfd_);
    }
}
auto TcpConnector::connect() -> ConnectAwaiter
{
    return utils::connect(sockfd_, reinterpret_cast<sockaddr*>(&addr_), sizeof(addr_));
}
auto TcpConnector::read(std::span<char> buffer) -> ReadAwaiter
{
    return utils::read(sockfd_, buffer.data(), buffer.size());
}
auto TcpConnector::write(std::span<const char> buffer) -> WriteAwaiter
{
    return utils::write(sockfd_, buffer.data(), buffer.size());
}
} // namespace utils