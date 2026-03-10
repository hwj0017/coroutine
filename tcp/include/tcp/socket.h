#pragma once
#include "coroutine/coroutine.h"
#include "coroutine/syscall.h"
#include "inetaddress.h"
#include <coroutine>
#include <span>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>
namespace utils
{
class Socket
{
  private:
    int fd_{-1};
    InetAddress local_addr_{}; // 本端地址
    InetAddress peer_addr_{};  // 对端地址

  public:
    Socket() = default;

    // 基础构造
    explicit Socket(int fd) : fd_(fd) {}

    // 供 Accept 使用的完整构造
    Socket(int fd, const InetAddress& local, const InetAddress& peer) : fd_(fd), local_addr_(local), peer_addr_(peer) {}

    ~Socket()
    {
        if (fd_ >= 0)
        {
            ::close(fd_);
        }
    }

    // Move-Only 语义 (记得同时移动地址状态)
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;

    Socket(Socket&& other) noexcept : fd_(other.fd_), local_addr_(other.local_addr_), peer_addr_(other.peer_addr_)
    {
        other.fd_ = -1;
    }

    Socket& operator=(Socket&& other) noexcept
    {
        if (this != &other)
        {
            if (fd_ >= 0)
                ::close(fd_);
            fd_ = other.fd_;
            local_addr_ = other.local_addr_;
            peer_addr_ = other.peer_addr_;
            other.fd_ = -1;
        }
        return *this;
    }

    // --- 状态获取接口 ---
    int fd() const { return fd_; }
    bool is_valid() const { return fd_ >= 0; }
    const InetAddress& local_address() const { return local_addr_; }
    const InetAddress& peer_address() const { return peer_addr_; }

    // --- 操作接口 ---
    void bind(const InetAddress& addr)
    {
        if (::bind(fd_, addr.get_sockaddr(), addr.get_socklen()) < 0)
        {
            throw std::runtime_error("Socket bind failed");
        }
        local_addr_ = addr; // 绑定成功后，记录本地地址
    }

    void listen(int backlog = SOMAXCONN)
    {
        if (::listen(fd_, backlog) < 0)
        {
            throw std::runtime_error("Socket listen failed");
        }
    }

    auto connect(const InetAddress& addr)
    {
        peer_addr_ = addr; // 记录对端地址
        return utils::connect(fd_, addr.get_sockaddr(), addr.get_socklen());
    }

    auto read(void* buf, size_t nbytes) noexcept { return ::utils::read(fd_, buf, nbytes); }

    auto read(std::span<char> buffer) noexcept { return ::utils::read(fd_, buffer.data(), buffer.size()); }
    auto write(const void* buf, size_t nbytes) noexcept { return ::utils::write(fd_, buf, nbytes); }
    auto write(std::span<const char> buffer) noexcept { return ::utils::write(fd_, buffer.data(), buffer.size()); }
    // ==========================================================
    // Accept Awaiter：直接组装并返回包含完整地址信息的 Socket
    // ==========================================================
    struct SocketAcceptAwaiter
    {
        int listen_fd_;
        struct sockaddr_in peer_addr_struct{};
        socklen_t peer_len{sizeof(sockaddr_in)};
        utils::AcceptAwaiter inner_awaiter;

        explicit SocketAcceptAwaiter(int listen_fd)
            : listen_fd_(listen_fd), inner_awaiter(utils::accept(listen_fd, (sockaddr*)&peer_addr_struct, &peer_len))
        {
        }

        bool await_ready() { return inner_awaiter.await_ready(); }

        template <typename Promise> auto await_suspend(std::coroutine_handle<Promise> h)
        {
            return inner_awaiter.await_suspend(h);
        }

        Socket await_resume()
        {
            int client_fd = inner_awaiter.await_resume();
            if (client_fd < 0)
            {
                return Socket{-1};
            }
            int flags = ::fcntl(client_fd, F_GETFL, 0);
            if (flags == -1)
            {
                ::close(client_fd); // 发生错误，必须清理 fd 防止泄漏
                return Socket{-1};
            }

            if (::fcntl(client_fd, F_SETFL, flags | O_NONBLOCK) == -1)
            {
                ::close(client_fd);
                return Socket{-1};
            }
            // 获取本地地址 (可选，因为 accept 出的 socket 也有自己的本地端口，特别是在多网卡时)
            struct sockaddr_in local_addr_struct{};
            socklen_t local_len = sizeof(local_addr_struct);
            ::getsockname(client_fd, (sockaddr*)&local_addr_struct, &local_len);

            // 构造一个完全体的 Socket 返回
            return Socket{client_fd, InetAddress(local_addr_struct), InetAddress(peer_addr_struct)};
        }
    };

    auto accept() noexcept { return SocketAcceptAwaiter{fd_}; }
    auto close() noexcept
    {
        if (fd_ >= 0)
        {
            ::close(fd_);
            fd_ = -1;
        }
    }
    static Socket create_tcp(int family = AF_INET)
    {
        // 直接在 socket 创建时指定 SOCK_NONBLOCK 和 SOCK_CLOEXEC (Linux 特有，高效)
        int fd = ::socket(family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_TCP);
        if (fd < 0)
        {
            throw std::runtime_error("Failed to create TCP socket");
        }
        return Socket{fd};
    }
};
} // namespace utils
