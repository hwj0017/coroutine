#pragma once

#include "coroutine/sysawaiter.h"
#include <liburing.h>
namespace utils
{

class IOContext
{
  public:
    IOContext() = default;
    ~IOContext() = default;
    template <typename Syscall> void call(Syscall* syscall);
    static IOContext& instance();

  private:
    io_uring ring;
};

template <typename Syscall> void IOContext::call(Syscall* syscall)
{
    auto sqe = io_uring_get_sqe(&ring);
    // 如果Syscall是AcceptAwaiter，则调用accept()
    if constexpr (std::is_same_v<Syscall, AcceptAwaiter>)
    {
        syscall = static_cast<AcceptAwaiter*>(syscall);
        io_uring_prep_accept(sqe, syscall->sockfd_, syscall->addr_, syscall->addrlen_, 0);
    }
    else if constexpr (std::is_same_v<Syscall, Readawaiter>)
    {
        syscall = static_cast<Readawaiter*>(syscall);
        io_uring_prep_read(sqe, syscall->fd_, syscall->buf_, syscall->nbytes_, 0);
    }
    else if constexpr (std::is_same_v<Syscall, Writeawaiter>)
    {
        syscall = static_cast<Writeawaiter*>(syscall);
        io_uring_prep_write(sqe, syscall->fd_, syscall->buf_, syscall->nbytes_, 0);
    }
    io_uring_submit(&ring);
}

inline void AcceptAwaiter::call() noexcept { IOContext::instance().call(this); }

inline void Readawaiter::call() noexcept { IOContext::instance().call(this); }

inline void Writeawaiter::call() noexcept { IOContext::instance().call(this); }
} // namespace utils