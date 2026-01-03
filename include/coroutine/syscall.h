#pragma once

#include "coroutine/handle.h"
#include <ctime>
#include <sys/socket.h>
#include <sys/types.h>
#include <type_traits>
namespace utils
{
class IOContext;
template <typename T> bool process(T* awaiter);
class SysAwaiterBase
{
  public:
    bool await_ready() const noexcept { return false; }
    int await_resume() const noexcept { return result_; }
    auto set_value(int result)
    {
        result_ = result;
        return handle_;
    }

  protected:
    int result_;
    Handle handle_;
};
template <typename T> class SysAwaiter : public SysAwaiterBase
{
  public:
    ~SysAwaiter() = default;
    bool await_suspend(Handle handle) noexcept
    {
        static_assert(std::is_base_of_v<SysAwaiter<T>, T>);
        return process(static_cast<T>(this));
    }
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
    ReadAwaiter() = default;
    ReadAwaiter(int fd, void* buf, size_t nbytes) : fd_(fd), buf_(buf), nbytes_(nbytes) {}

  private:
    int fd_;
    void* buf_;
    size_t nbytes_;
    friend class IOContext;
};

class WriteAwaiter : public SysAwaiter<WriteAwaiter>
{
  public:
    WriteAwaiter(int fd, const void* buf, size_t nbytes) : fd_(fd), buf_(buf), nbytes_(nbytes) {}

  private:
    int fd_;
    const void* buf_;
    size_t nbytes_;
    friend class IOContext;
};

class DelayAwaiter : public SysAwaiter<DelayAwaiter>
{
  public:
    DelayAwaiter(double timeout)
    {
        ts_.tv_sec = timeout;
        ts_.tv_nsec = (timeout - ts_.tv_sec) * 1e9;
    }

  private:
    struct timespec ts_;

    friend class IOContext;
    friend class Scheduler;
};

inline auto accept(int sockfd, sockaddr* addr, socklen_t* addrlen) noexcept
{
    return AcceptAwaiter(sockfd, addr, addrlen);
}
inline auto read(int fd, void* buf, size_t nbytes) noexcept { return ReadAwaiter(fd, buf, nbytes); }
inline auto write(int fd, const void* buf, size_t nbytes) noexcept { return WriteAwaiter(fd, buf, nbytes); }
inline auto delay(int timeout_ms) noexcept { return DelayAwaiter(timeout_ms); }
} // namespace utils