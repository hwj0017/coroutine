#include "net/tcp/tcpconnection.h"
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

auto TcpConnection::read(char* buffer, std::size_t n) -> utils::ReadAwaiter { return utils::read(sockfd_, buffer, n); }

auto TcpConnection::write(const char* buffer, size_t n) -> utils::WriteAwaiter
{
    return utils::write(sockfd_, buffer, n);
}
} // namespace utils