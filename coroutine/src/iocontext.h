#pragma once

#include "coroutine/syscall.h"
#include <array>
#include <atomic>
#include <cassert>
#include <coroutine>
#include <cstddef>
#include <ctime>
#include <functional>
#include <iostream>
#include <liburing.h>
#include <mutex>
#include <queue>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <vector>
namespace utils
{

class Scheduler;
class IOContext
{
  public:
    IOContext();
    ~IOContext() { io_uring_queue_exit(&ring_); }
    auto has_work() -> bool
    {
        // 包含eventfd的IO操作
        return event_count_ > 1;
    }
    auto poll(bool block) -> std::vector<Handle>
    {
        assert(event_count_ > 1);
        if (block)
        {
            assert(io_uring_submit_and_wait(&ring_, 1) == unsubmitted_count_);
            unsubmitted_count_ = 0;
        }
        else if (unsubmitted_count_ > 0)
        {
            assert(io_uring_submit(&ring_) == unsubmitted_count_);
            unsubmitted_count_ = 0;
        }
        std::vector<Handle> coroutines;
        bool wake_up = false;
        int finished_count = 0;
        unsigned head;
        struct io_uring_cqe* cqe;
        // 批量遍历所有完成事件
        io_uring_for_each_cqe(&ring_, head, cqe)
        {
            // 只是唤醒
            if (!cqe->user_data)
            {
                wake_up = true;
            }
            else
            {
                auto awaiter = reinterpret_cast<SysAwaiterBase*>(cqe->user_data);
                auto handle = awaiter->set_value(cqe->res);
                coroutines.push_back(handle);
            }
            ++finished_count;
        }

        if (finished_count)
        {
            io_uring_cq_advance(&ring_, finished_count);
            event_count_ -= finished_count;
            // 包含eventfd的IO操作
            if (wake_up)
            {
                reset_eventfd();
            }
            // 处理pending的函数
            while (event_count_ < entries && !pending_call_.empty())
            {
                auto func = pending_call_.front();
                pending_call_.pop();
                func();
            }
        }
        return coroutines;
    }
    void reset_eventfd();
    void wake()
    {
        uint64_t val = 1;
        assert(::write(eventfd_, &val, sizeof(val)) == sizeof(val));
    }
    template <typename Awaiter>
    bool process(Awaiter* awaiter)
        requires(std::is_base_of_v<SysAwaiterBase, Awaiter>);

    void delay(DelayAwaiter& awaiter);

  private:
    template <typename Awaiter>
    bool process_impl(Awaiter* awaiter)
        requires(std::is_base_of_v<SysAwaiterBase, Awaiter>);
    constexpr static size_t submit_interval = 64;
    constexpr static size_t entries = 1024;
    io_uring ring_;
    int eventfd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    uint64_t eventfd_buf_ = 0;
    ReadAwaiter eventfd_awaiter_;

    // eventfd_ read 的缓冲区
    std::queue<std::function<void()>> pending_call_;
    size_t event_count_ = 0;
    size_t unsubmitted_count_ = 0;
    friend class Scheduler;
};

inline IOContext::IOContext() : ring_()
{
    assert(!io_uring_queue_init(8, &ring_, 0));
    eventfd_awaiter_ = {eventfd_, &eventfd_buf_, 8};
    process(&eventfd_awaiter_);
}

inline void IOContext::reset_eventfd() { process(&eventfd_awaiter_); }
template <typename Awaiter>
bool IOContext::process(Awaiter* awaiter)
    requires(std::is_base_of_v<SysAwaiterBase, Awaiter>)
{
    if (event_count_ >= entries)
    {
        pending_call_.push([this, awaiter]() { process_impl(awaiter); });
        return true;
    }
    process_impl(awaiter);
    return true;
}
template <typename Awaiter>
bool IOContext::process_impl(Awaiter* awaiter)
    requires(std::is_base_of_v<SysAwaiterBase, Awaiter>)
{
    auto sqe = io_uring_get_sqe(&ring_);
    assert(sqe);
    // 如果Syscall是AcceptAwaiter，则调用accept()
    if constexpr (std::is_same_v<AcceptAwaiter, Awaiter>)
    {
        awaiter = static_cast<AcceptAwaiter*>(awaiter);
        io_uring_prep_accept(sqe, awaiter->sockfd_, awaiter->addr_, awaiter->addrlen_, 0);
    }
    else if constexpr (std::is_same_v<ConnectAwaiter, Awaiter>)
    {
        awaiter = static_cast<ConnectAwaiter*>(awaiter);
        io_uring_prep_connect(sqe, awaiter->sockfd_, awaiter->addr_, awaiter->addrlen_);
    }
    else if constexpr (std::is_same_v<ReadAwaiter, Awaiter>)
    {
        awaiter = static_cast<ReadAwaiter*>(awaiter);
        io_uring_prep_read(sqe, awaiter->fd_, awaiter->buf_, awaiter->nbytes_, 0);
    }
    else if constexpr (std::is_same_v<WriteAwaiter, Awaiter>)
    {
        awaiter = static_cast<WriteAwaiter*>(awaiter);
        io_uring_prep_write(sqe, awaiter->fd_, awaiter->buf_, awaiter->nbytes_, 0);
    }
    else if constexpr (std::is_same_v<DelayAwaiter, Awaiter>)
    {
        awaiter = static_cast<DelayAwaiter*>(awaiter);
        io_uring_prep_timeout(sqe, reinterpret_cast<__kernel_timespec*>(&awaiter->ts_), 0, 0);
    }

    // 先设置请求在设置user_data
    sqe->user_data = reinterpret_cast<uintptr_t>(awaiter);
    ++event_count_;
    if (++unsubmitted_count_ >= submit_interval)
    {
        assert(io_uring_submit(&ring_) == unsubmitted_count_);
        unsubmitted_count_ = 0;
    }
    return true;
}

// inline void IOContext::delay(DelayAwaiter& awaiter)
// {
//     struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
//     struct __kernel_timespec ts;
//     ts.tv_sec = awaiter.timeout_;
//     ts.tv_nsec = (awaiter.timeout_ - ts.tv_sec) * 1e9;
//     io_uring_prep_timeout(sqe, &ts, 0, 0);
//     io_uring_submit(&ring_);
// }
} // namespace utils