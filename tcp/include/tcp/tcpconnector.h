#pragma once

#include "coroutine/syscall.h"
#include <cstdint>
#include <netinet/in.h>
#include <span>
#include <string>
#include <string_view>
#include <sys/socket.h>
namespace utils
{
class TcpConnector
{
  public:
    TcpConnector(std::string_view host, std::uint16_t port);
    ~TcpConnector();
    auto connect() -> ConnectAwaiter;
    auto read(std::span<char> buffer) -> ReadAwaiter;
    auto write(std::span<const char> buffer) -> WriteAwaiter;

  private:
    std::string host_;
    std::uint16_t port_;
    sockaddr_in addr_;
    int sockfd_ = -1;
};
} // namespace utils
