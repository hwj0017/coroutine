#include "tcp/tcpconnection.h"
#include <span>
#include <string_view>
namespace utils
{

TcpConnection::TcpConnection(int sockfd, sockaddr_in addr) : sockfd_(sockfd), addr_(addr) {}
TcpConnection::~TcpConnection()
{
    if (sockfd_ != -1)
    {
        ::close(sockfd_);
    }
}

auto TcpConnection::read(std::span<char> buffer) -> utils::ReadAwaiter
{
    return utils::read(sockfd_, buffer.data(), buffer.size());
}

auto TcpConnection::write(std::span<const char> buffer) -> utils::WriteAwaiter
{
    return utils::write(sockfd_, buffer.data(), buffer.size());
}

} // namespace utils