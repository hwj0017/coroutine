#pragma once
#include <arpa/inet.h>
#include <string>
#include <string_view>

namespace utils
{
class InetAddress
{
  private:
    struct sockaddr_in addr_{};

  public:
    // 1. 供主动发起连接或绑定监听时使用 (例如: 8080, "127.0.0.1")
    explicit InetAddress(uint16_t port = 0, std::string_view ip = "0.0.0.0")
    {
        addr_.sin_family = AF_INET;
        addr_.sin_port = htons(port);
        inet_pton(AF_INET, ip.data(), &addr_.sin_addr);
    }

    // 2. 供 accept 接收到底层结构时转换使用
    explicit InetAddress(const struct sockaddr_in& addr) : addr_(addr) {}

    // 提供给底层 Socket 调用的内部接口
    const sockaddr* get_sockaddr() const { return reinterpret_cast<const sockaddr*>(&addr_); }
    socklen_t get_socklen() const { return sizeof(addr_); }

    // 提供给上层业务调用的可读接口
    std::string ip() const
    {
        char buf[64] = "";
        inet_ntop(AF_INET, &addr_.sin_addr, buf, sizeof(buf));
        return std::string(buf);
    }
    uint16_t port() const { return ntohs(addr_.sin_port); }
};
} // namespace utils
