#pragma once

#include "coroutine/coroutine.h"
#include <liburing.h>
namespace utils
{

class IOContext;
template <typename T>
concept Syscall = requires(T t) { t.call(); };
template <typename Syscall> class SysAwaiter
{
  public:
    auto await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<Coroutine::promise_type> handle) noexcept
    {
        static_cast<Syscall*>(this)->call();
        coro_ = Coroutine(handle);
    }
    int await_resume() const noexcept { return result_; }

    void set_resule(int res)
    {
        result_ = res;
        coro_();
    }

  protected:
    int result_;
    Coroutine coro_;
};

class AcceptAwaiter : public SysAwaiter<AcceptAwaiter>
{
  public:
    AcceptAwaiter(int sockfd, sockaddr* addr, socklen_t* addrlen) : sockfd_(sockfd), addr_(addr), addrlen_(addrlen) {}

    void call() noexcept;

  private:
    int sockfd_;
    sockaddr* addr_;
    socklen_t* addrlen_;
    friend class IOContext;
};

class Readawaiter : public SysAwaiter<Readawaiter>
{
  public:
    Readawaiter(int fd, void* buf, size_t nbytes) : fd_(fd), buf_(buf), nbytes_(nbytes) {}
    void call() noexcept;

  private:
    int fd_;
    void* buf_;
    size_t nbytes_;
    friend class IOContext;
};

class Writeawaiter : public SysAwaiter<Writeawaiter>
{
  public:
    Writeawaiter(int fd, const void* buf, size_t nbytes) : fd_(fd), buf_(buf), nbytes_(nbytes) {}
    void call() noexcept;

  private:
    int fd_;
    const void* buf_;
    size_t nbytes_;
    friend class IOContext;
};

inline auto accept(int sockfd, sockaddr* addr, socklen_t* addrlen) noexcept
{
    return AcceptAwaiter(sockfd, addr, addrlen);
}
inline auto read(int fd, void* buf, size_t nbytes) noexcept { return Readawaiter(fd, buf, nbytes); }
inline auto write(int fd, const void* buf, size_t nbytes) noexcept { return Writeawaiter(fd, buf, nbytes); }
} // namespace utils