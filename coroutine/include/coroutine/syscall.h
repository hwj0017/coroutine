#pragma once

#include "coroutine/coroutine.h"
#include <ctime>
#include <fcntl.h>
#include <string_view>
#include <sys/socket.h>
#include <sys/types.h>
#include <type_traits>
#include <unistd.h>
namespace utils
{

class IOContext;
template <typename T> bool process(T* awaiter);
class SysAwaiterBase
{
  public:
    bool await_ready() const noexcept { return false; }
    int await_resume() const noexcept { return result_; }
    virtual auto set_value(int result) -> Promise*
    {
        result_ = result;
        return promise_;
    }

  protected:
    int result_{0};
    Promise* promise_{nullptr};
    friend class IOContext;
};
template <typename T> class SysAwaiter : public SysAwaiterBase
{
  public:
    ~SysAwaiter() = default;
    template <typename Promise> bool await_suspend(std::coroutine_handle<Promise> handle) noexcept
    {
        promise_ = &handle.promise();
        static_assert(std::is_base_of_v<SysAwaiter<T>, T>);
        return process(static_cast<T*>(this));
    }
};
class ConnectAwaiter : public SysAwaiter<ConnectAwaiter>
{
  public:
    ConnectAwaiter(int sockfd, const sockaddr* addr, socklen_t addrlen)
        : sockfd_(sockfd), addr_(addr), addrlen_(addrlen)
    {
    }

  private:
    int sockfd_;
    const sockaddr* addr_;
    socklen_t addrlen_;
    friend class IOContext;
};
class AcceptAwaiter : public SysAwaiter<AcceptAwaiter>
{
  public:
    AcceptAwaiter(int sockfd, sockaddr* addr, socklen_t* addrlen) : sockfd_(sockfd), addr_(addr), addrlen_(addrlen) {}

  private:
    int sockfd_;
    sockaddr* addr_;
    socklen_t* addrlen_;
    friend class IOContext;
};

class ReadAwaiter : public SysAwaiter<ReadAwaiter>
{
  public:
    ReadAwaiter(int fd, void* buf, size_t nbytes) : fd_(fd), buf_(buf), nbytes_(nbytes) {}

  protected:
    int fd_;
    void* buf_;
    size_t nbytes_;
    friend class IOContext;
};

class FileReadAwaiter : public ReadAwaiter
{
  public:
    FileReadAwaiter(std::string_view file_path, void* buf, size_t nbytes)
        : ReadAwaiter(open(file_path.data(), O_RDONLY), buf, nbytes)
    {
    }
    ~FileReadAwaiter()
    {
        if (fd_ != -1)
            close(fd_);
    }
};

class WriteAwaiter : public SysAwaiter<WriteAwaiter>
{
  public:
    WriteAwaiter(int fd, const void* buf, size_t nbytes) : fd_(fd), buf_(buf), nbytes_(nbytes) {}
    auto set_value(int result) -> Promise* override
    {
        if (result <= 0)
        {
            return promise_;
        }
        // 如果是部分写入，继续写剩余数据
        buf_ = static_cast<const char*>(buf_) + result;
        nbytes_ -= result;
        result_ += result;
        if (nbytes_ == 0)
        {
            return promise_;
        }
        process(this);
        return nullptr; // 不立即恢复，等待下一次写入完成
        // return SysAwaiterBase::set_value(result);
    }

  private:
    int fd_;
    const void* buf_;
    size_t nbytes_;
    friend class IOContext;
};

class RecvAwaiter : public ReadAwaiter
{
  public:
    RecvAwaiter(int fd, void* buf, size_t nbytes, int flags) : ReadAwaiter(fd, buf, nbytes), flags_(flags)
    {
        flags_ = flags & ~MSG_DONTWAIT;
    }

  private:
    int flags_;
    friend class IOContext;
};
class SendAwaiter : public WriteAwaiter
{
  public:
    SendAwaiter(int fd, const void* buf, size_t nbytes, int flags) : WriteAwaiter(fd, buf, nbytes), flags_(flags)
    {
        // 1. 强制添加 MSG_NOSIGNAL，防止对端关闭时崩溃
        // 2. 屏蔽 MSG_DONTWAIT
        flags_ = (flags | MSG_NOSIGNAL) & ~MSG_DONTWAIT;
    }

  private:
    int flags_;
    friend class IOContext;
};

class DelayAwaiter : public SysAwaiter<DelayAwaiter>
{
  public:
    DelayAwaiter(double timeout) : timeout_(timeout) {}

  private:
    double timeout_;
    friend class IOContext;
    friend class Scheduler;
};
inline auto connect(int sockfd, const sockaddr* addr, socklen_t addrlen)
{
    return ConnectAwaiter(sockfd, addr, addrlen);
}
inline auto accept(int sockfd, sockaddr* addr, socklen_t* addrlen) noexcept
{
    return AcceptAwaiter(sockfd, addr, addrlen);
}
inline auto read(int fd, void* buf, size_t nbytes) noexcept { return ReadAwaiter(fd, buf, nbytes); }
inline auto read(std::string_view file_name, void* buf, size_t nbytes)
{
    return FileReadAwaiter(file_name, buf, nbytes);
}
inline auto write(int fd, const void* buf, size_t nbytes) noexcept { return WriteAwaiter(fd, buf, nbytes); }

inline auto recv(int fd, void* buf, size_t nbytes, int flags) noexcept { return RecvAwaiter(fd, buf, nbytes, flags); }

inline auto send(int fd, const void* buf, size_t nbytes, int flags) noexcept
{
    return SendAwaiter(fd, buf, nbytes, flags);
}
inline auto delay(int timeout_ms) noexcept { return DelayAwaiter(timeout_ms); }
} // namespace utils