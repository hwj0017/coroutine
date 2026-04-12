#pragma once

#include "coroutine/coroutine.h"
#include "coroutine/cospawn.h"
#include "coroutine/intrusivelist.h"
#include "coroutine/ringbuffer.h"
#include "coroutine/spinlock.h"
#include <cassert>
#include <coroutine>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <tuple>
#include <type_traits>
#include <variant>

namespace utils
{
enum class State
{
    OK,
    CLOSED
};

template <typename T, size_t Capacity> class Channel;

template <typename T, size_t Capacity> class Channel
{
  public:
    using Lock = std::mutex;

    class SendAwaiter : public IntrusiveListDNode
    {
      public:
        SendAwaiter(Channel<T, Capacity>* channel, T&& value) : channel_(channel), value_(std::move(value)) {}
        bool await_ready() const noexcept { return false; }
        template <typename Promise> bool await_suspend(std::coroutine_handle<Promise> handle) noexcept
        {
            promise_ = &handle.promise();
            return channel_->send_impl(this);
        }

        auto await_resume() const { return state_; }

        auto get_value() { return std::move(value_); }
        auto set_value(State state)
        {
            state_ = state;
            return promise_;
        }

      private:
        Channel<T, Capacity>* channel_;
        T value_{};
        State state_{State::CLOSED};
        Promise* promise_{};
    };

    // 接收操作
    class RecvAwaiter : public IntrusiveListDNode
    {

      public:
        RecvAwaiter(Channel<T, Capacity>* channel) : channel_(channel) {}
        auto await_ready() const noexcept { return false; }

        template <typename Promise> bool await_suspend(std::coroutine_handle<Promise> handle) noexcept
        {
            promise_ = &handle.promise();
            return channel_->recv_impl(this);
        }

        auto await_resume() const
            requires(!std::is_same_v<T, std::monostate>)
        {
            return std::tuple<T, State>{std::move(value_), state_};
        }
        auto await_resume() const
            requires(std::is_same_v<T, std::monostate>)
        {
            return state_;
        }
        auto set_value(T&& value, State state)
        {
            value_ = std::move(value);
            state_ = state;
            return promise_;
        }
        void cancel() {}

      private:
        Channel<T, Capacity>* channel_;
        T value_{};
        State state_ = State::CLOSED;
        Promise* promise_;
    };
    Channel() = default;
    ~Channel() { close(); }

    auto recv() { return RecvAwaiter{this}; }

    auto send(T value) { return SendAwaiter{this, std::move(value)}; }
    auto send()
        requires(std::is_same_v<T, std::monostate>)
    {
        return SendAwaiter{this, {}};
    }
    void close()
    {
        {
            auto guard = std::lock_guard(mutex_);
            if (is_closed_)
            {
                return;
            }
            is_closed_ = true;
        }
        while (!send_awaiters_.empty())
        {
            auto awaiter = send_awaiters_.pop_front();
            auto p = static_cast<SendAwaiter*>(awaiter)->set_value(State::CLOSED);
            co_spawn(p);
        }
        while (!recv_awaiters_.empty())
        {
            auto awaiter = recv_awaiters_.pop_front();
            auto p = static_cast<RecvAwaiter*>(awaiter)->set_value({}, State::CLOSED);
            co_spawn(p);
        }
    }

  private:
    bool send_impl(SendAwaiter* awaiter);
    bool recv_impl(RecvAwaiter* awaiter);
    bool is_closed() const { return is_closed_; }
    bool is_empty() const { return resource_.empty() && send_awaiters_.empty(); }
    bool is_full() const { return resource_.full() && recv_awaiters_.empty(); }

    Lock mutex_{};
    RingBuffer<T, Capacity> resource_{};

    IntrusiveDList send_awaiters_;
    IntrusiveDList recv_awaiters_;
    bool is_closed_ = false;
};

template <typename T, size_t Capacity> bool Channel<T, Capacity>::send_impl(SendAwaiter* send_awaiter)
{
    Promise* notify = nullptr;
    {
        std::lock_guard lock(mutex_);
        // channel关闭，不阻塞
        if (is_closed())
        {
            return false;
        }
        if (is_full())
        {
            // channel未关闭且已满，阻塞
            send_awaiters_.push_back(send_awaiter);
            return true;
        }
        // channel未关闭且不满，唤醒
        // 优先给等待的接收者，防止缓冲区溢出
        if (!recv_awaiters_.empty())
        {
            auto recv_awaiter = recv_awaiters_.pop_front();
            notify =
                static_cast<RecvAwaiter*>(recv_awaiter)->set_value(std::move(send_awaiter->get_value()), State::OK);
        }
        else
        {
            resource_.push(send_awaiter->get_value());
        }
    }
    send_awaiter->set_value(State::OK);
    if (notify)
    {
        co_spawn(notify);
    }
    return false;
}
template <typename T, size_t Capacity> bool Channel<T, Capacity>::recv_impl(RecvAwaiter* recv_awaiter)
{
    Promise* notify = nullptr;
    {
        std::lock_guard lock(mutex_);
        // 关闭且为空，不阻塞
        if (is_closed() && is_empty())
        {
            return false;
        }
        // channel未关闭且为空，阻塞
        if (is_empty())
        {
            recv_awaiters_.push_back(recv_awaiter);
            return true;
        }
        // channel不空，唤醒
        // 保持FIFO
        if (!resource_.empty())
        {
            recv_awaiter->set_value(std::move(resource_.front()), State::OK);
            resource_.pop();
            if (!send_awaiters_.empty())
            {
                auto send_awaiter = static_cast<SendAwaiter*>(send_awaiters_.pop_front());
                resource_.push(send_awaiter->get_value());
                notify = send_awaiter->set_value(State::OK);
            }
        }
        else
        {
            auto send_awaiter = static_cast<SendAwaiter*>(send_awaiters_.pop_front());
            recv_awaiter->set_value(send_awaiter->get_value(), State::OK);
            notify = send_awaiter->set_value(State::OK);
        }
    }
    if (notify)
    {
        co_spawn(notify);
    }
    return false;
}

template <size_t Capacity> class Channel<void, Capacity>
{
  public:
    using Lock = std::mutex;

    // 以下将特化channel<void>
    class SendAwaiter : public IntrusiveListDNode
    {
      public:
        SendAwaiter(Channel<void, Capacity>* channel) : channel_(channel) {}
        bool await_ready() noexcept { return false; }
        template <typename Promise> bool await_suspend(std::coroutine_handle<Promise> handle) noexcept
        {
            promise_ = &handle.promise();
            return channel_->send_impl(this);
        }

        auto await_resume() const { return state_; }
        auto set_value(State state)
        {
            state_ = state;
            return promise_;
        }

      private:
        Channel<void, Capacity>* channel_;
        State state_{State::CLOSED};
        Promise* promise_{};
    };

    // 接收操作
    class RecvAwaiter : public IntrusiveListDNode
    {

      public:
        RecvAwaiter(Channel<void, Capacity>* channel) : channel_(channel) {}
        auto await_ready() noexcept { return false; }

        template <typename Promise> bool await_suspend(std::coroutine_handle<Promise> handle) noexcept
        {
            promise_ = &handle.promise();
            return channel_->recv_impl(this);
        }

        auto await_resume() const { return state_; }
        auto set_value(State state)
        {
            state_ = state;
            return promise_;
        }

      private:
        Channel<void, Capacity>* channel_;
        State state_ = State::CLOSED;
        Promise* promise_;
    };
    Channel(size_t initial_size = 0) : size_(initial_size) { assert(initial_size <= Capacity); }
    ~Channel() { close(); }
    void close()
    {
        {
            auto guard = std::lock_guard(mutex_);
            if (is_closed_)
            {
                return;
            }
            is_closed_ = true;
        }
        while (!send_awaiters_.empty())
        {
            auto awaiter = static_cast<SendAwaiter*>(send_awaiters_.pop_front());
            auto p = awaiter->set_value(State::CLOSED);
            co_spawn(p);
        }
        while (!recv_awaiters_.empty())
        {
            auto awaiter = static_cast<RecvAwaiter*>(recv_awaiters_.pop_front());
            auto p = awaiter->set_value(State::CLOSED);
            co_spawn(p);
        }
    }
    auto recv() { return RecvAwaiter{this}; }

    auto send() { return SendAwaiter{this}; }

  private:
    bool try_send(SendAwaiter* awaiter);
    bool try_recv(RecvAwaiter* awaiter);
    bool send_impl(SendAwaiter* awaiter);
    bool recv_impl(RecvAwaiter* awaiter);
    bool is_closed() const { return is_closed_; }
    bool is_empty() const { return size_ == 0 && send_awaiters_.empty(); }
    bool is_full() const { return size_ >= Capacity && recv_awaiters_.empty(); }

    Lock mutex_{};
    size_t size_{};
    IntrusiveDList send_awaiters_;
    IntrusiveDList recv_awaiters_;
    bool is_closed_ = false;
};
template <size_t Capacity> bool Channel<void, Capacity>::send_impl(SendAwaiter* send_awaiter)
{
    Promise* notify = nullptr;
    {
        std::lock_guard lock(mutex_);
        // channel关闭，不阻塞
        if (is_closed())
        {
            return false;
        }
        if (is_full())
        {
            // channel未关闭且已满，阻塞
            send_awaiters_.push_back(send_awaiter);
            return true;
        }
        // channel未关闭且不满，唤醒
        if (!recv_awaiters_.empty())
        {
            auto recv_awaiter = static_cast<RecvAwaiter*>(recv_awaiters_.pop_front());
            notify = recv_awaiter->set_value(State::OK);
        }
        else
        {
            size_++;
        }
        send_awaiter->set_value(State::OK);
    }
    if (notify)
    {
        co_spawn(notify);
    }
    return false;
}
template <size_t Capacity> bool Channel<void, Capacity>::recv_impl(RecvAwaiter* recv_awaiter)
{
    Promise* notify = nullptr;
    {
        std::lock_guard lock(mutex_);
        // 关闭且为空，不阻塞
        if (is_closed() && is_empty())
        {
            return false;
        }
        // channel未关闭且为空，阻塞
        if (is_empty())
        {
            recv_awaiters_.push_back(recv_awaiter);
            return true;
        }
        // channel不空，唤醒
        // 保持FIFO
        if (!send_awaiters_.empty())
        {
            auto send_awaiter = static_cast<SendAwaiter*>(send_awaiters_.pop_front());
            notify = send_awaiter->set_value(State::OK);
        }
        else
        {
            size_--;
        }
        recv_awaiter->set_value(State::OK);
    }
    if (notify)
    {
        co_spawn(notify);
    }
    return false;
}

} // namespace utils
