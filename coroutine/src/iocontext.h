#pragma once

#include "coroutine/coroutine.h"
#include "coroutine/intrusivelist.h"
#include "coroutine/syscall.h"
#include "timewheel.h"
#include <array>
#include <atomic>
#include <cassert>
#include <cerrno>
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
using Handle = Promise*;
class Scheduler;
// TODO:不要使用计数来计算SQ的数量
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
    auto poll(bool block) -> IntrusiveList
    {
        assert(event_count_ > 0);
        // 提交所有未提交的IO操作，降低延迟
        if (unsubmitted_count_ > 0)
        {
            auto ret = io_uring_submit(&ring_);
            assert(ret == unsubmitted_count_);
            unsubmitted_count_ = 0;
        }
        if (block)
        {
            int next_timeout_ms = timer_wheel_.get_next_timeout();
            struct __kernel_timespec ts;
            struct __kernel_timespec* ts_ptr = nullptr;
            if (next_timeout_ms >= 0)
            {
                // 将总毫秒数拆分成“秒”和“纳秒”
                ts.tv_sec = next_timeout_ms / 1000;

                // 注意这里要乘以 1000000LL，把余下的毫秒转成纳秒
                ts.tv_nsec = (next_timeout_ms % 1000) * 1000000LL;

                // 指针指向结构体，表示我们要开启超时等待
                ts_ptr = &ts;
            }
            struct io_uring_cqe* cqe = nullptr;
            int ret = -EINTR;
            while (ret == -EINTR)
            {
                ret = io_uring_wait_cqe_timeout(&ring_, &cqe, ts_ptr);
                // ret = io_uring_submit_and_wait(&ring_, 1);
            }
        }

        IntrusiveList coroutines;
        int finished_count = 0;
        unsigned head;
        struct io_uring_cqe* cqe;
        // 批量遍历所有完成事件
        io_uring_for_each_cqe(&ring_, head, cqe)
        {
            // 只是唤醒
            auto awaiter = reinterpret_cast<SysAwaiterBase*>(cqe->user_data);
            if (awaiter == &eventfd_awaiter_)
            {
                reset_eventfd();
            }
            else
            {
                if (auto handle = awaiter->set_value(cqe->res); handle)
                {
                    coroutines.push_back(handle);
                }
            }
            ++finished_count;
        }

        if (finished_count)
        {
            io_uring_cq_advance(&ring_, finished_count);
            event_count_ -= finished_count;
            // 处理pending的函数
            while (event_count_ < entries && !pending_call_.empty())
            {
                auto awaiter = pending_call_.pop_front();
                process_impl(static_cast<SysAwaiterBase*>(awaiter));
            }
        }
        auto ready = timer_wheel_.update();
        for (auto timer : ready)
        {
            auto handle = static_cast<DelayAwaiter*>(timer)->set_value(0);
            assert(handle);
            coroutines.push_back(handle);
        }
        return coroutines;
    }
    void reset_eventfd();
    void wake()
    {
        uint64_t val = 1;
        auto ret = ::write(eventfd_, &val, sizeof(val));
        assert(ret == sizeof(val));
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
    // eventfd_ read 的缓冲区

    uint64_t eventfd_buf_ = 0;
    ReadAwaiter eventfd_awaiter_{eventfd_, &eventfd_buf_, 8};

    BitwiseTimerWheel timer_wheel_{MS(1), std::vector<size_t>{8, 6, 6, 6, 6}};
    IntrusiveList pending_call_{};
    size_t event_count_ = 0;
    size_t unsubmitted_count_ = 0;
    friend class Scheduler;
};

inline IOContext::IOContext()
{
    auto res = io_uring_queue_init(entries, &ring_, 0);
    assert(res >= 0);
    eventfd_awaiter_.fd_ = eventfd_;
    eventfd_awaiter_.buf_ = &eventfd_buf_;
    eventfd_awaiter_.nbytes_ = sizeof(eventfd_buf_);
    process(&eventfd_awaiter_);
}

inline void IOContext::reset_eventfd() { process(&eventfd_awaiter_); }
template <typename Awaiter>
bool IOContext::process(Awaiter* awaiter)
    requires(std::is_base_of_v<SysAwaiterBase, Awaiter>)
{
    if constexpr (std::is_same_v<DelayAwaiter, Awaiter>)
    {
        awaiter = static_cast<DelayAwaiter*>(awaiter);
        timer_wheel_.add_timer(MS(size_t(awaiter->timeout_ * 1000)), awaiter);
        return true;
    }
    if (event_count_ >= entries)
    {
        pending_call_.push_back(awaiter);
        return true;
    }
    process_impl(awaiter);
    return true;
}
template <typename Awaiter>
bool IOContext::process_impl(Awaiter* awaiter)
    requires(std::is_base_of_v<SysAwaiterBase, Awaiter>)
{
    if constexpr (std::is_same_v<SysAwaiterBase, Awaiter>)
    {
        awaiter = static_cast<SysAwaiterBase*>(awaiter);
        switch (awaiter->type_)
        {
        case SysCallType::CONNECT: {
            process_impl(static_cast<ConnectAwaiter*>(awaiter));
            break;
        }
        case SysCallType::ACCEPT: {
            process_impl(static_cast<AcceptAwaiter*>(awaiter));
            break;
        }
        case SysCallType::READ: {
            process_impl(static_cast<ReadAwaiter*>(awaiter));
            break;
        }
        case SysCallType::WRITE: {
            process_impl(static_cast<WriteAwaiter*>(awaiter));
            break;
        }
        case SysCallType::RECV: {
            process_impl(static_cast<RecvAwaiter*>(awaiter));
            break;
        }
        case SysCallType::SEND: {
            process_impl(static_cast<SendAwaiter*>(awaiter));
            break;
        }
        default: {
            assert(false);
            break;
        }
        }
    }

    auto sqe = io_uring_get_sqe(&ring_);
    // TODO:没有空余的SQE了，应该有更好的处理方式
    assert(sqe);
    // 如果Syscall是AcceptAwaiter，则调用accept()
    if constexpr (std::is_same_v<AcceptAwaiter, Awaiter>)
    {
        auto accept_awaiter = static_cast<AcceptAwaiter*>(awaiter);
        io_uring_prep_accept(sqe, accept_awaiter->sockfd_, accept_awaiter->addr_, accept_awaiter->addrlen_, 0);
    }
    else if constexpr (std::is_same_v<ConnectAwaiter, Awaiter>)
    {
        auto connect_awaiter = static_cast<ConnectAwaiter*>(awaiter);
        io_uring_prep_connect(sqe, connect_awaiter->sockfd_, connect_awaiter->addr_, connect_awaiter->addrlen_);
    }
    else if constexpr (std::is_same_v<ReadAwaiter, Awaiter>)
    {
        auto read_awaiter = static_cast<ReadAwaiter*>(awaiter);
        io_uring_prep_read(sqe, read_awaiter->fd_, read_awaiter->buf_, read_awaiter->nbytes_, 0);
    }
    else if constexpr (std::is_same_v<WriteAwaiter, Awaiter>)
    {
        auto write_awaiter = static_cast<WriteAwaiter*>(awaiter);
        io_uring_prep_write(sqe, write_awaiter->fd_, write_awaiter->buf_, write_awaiter->nbytes_, 0);
    }
    else if constexpr (std::is_same_v<RecvAwaiter, Awaiter>)
    {
        auto recv_awaiter = static_cast<RecvAwaiter*>(awaiter);
        io_uring_prep_recv(sqe, recv_awaiter->fd_, recv_awaiter->buf_, recv_awaiter->nbytes_, recv_awaiter->flags_);
    }
    else if constexpr (std::is_same_v<SendAwaiter, Awaiter>)
    {
        // TODO: 需要6.0 才支持零拷贝发送
        // constexpr size_t ZC_THRESHOLD = 16384;
        // auto send_awaiter = static_cast<SendAwaiter*>(awaiter);
        // if (send_awaiter->nbytes_ < ZC_THRESHOLD)
        // {
        //     io_uring_prep_send(sqe, send_awaiter->fd_, send_awaiter->buf_, send_awaiter->nbytes_,
        //     send_awaiter->flags_);
        // }
        // else
        // {
        //     io_uring_prep_send_zc(sqe, send_awaiter->fd_, send_awaiter->buf_, send_awaiter->nbytes_,
        //                           send_awaiter->flags_, 0);
        // }
        auto send_awaiter = static_cast<SendAwaiter*>(awaiter);
        io_uring_prep_send(sqe, send_awaiter->fd_, send_awaiter->buf_, send_awaiter->nbytes_, send_awaiter->flags_);
    }

    // 先设置请求在设置user_data
    sqe->user_data = reinterpret_cast<uintptr_t>(awaiter);
    ++event_count_;
    if (++unsubmitted_count_ >= submit_interval)
    {
        auto ret = io_uring_submit(&ring_);
        assert(ret == unsubmitted_count_);
        unsubmitted_count_ = 0;
    }
    return true;
}

} // namespace utils