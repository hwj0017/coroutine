#pragma once

#include "coroutine/cospawn.h"
#include <cassert>
#include <coroutine>
#include <cstddef>
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
template <typename T = std::monostate> class Channel;
template <typename T = std::monostate> class SendAwaiter
{
  public:
    using Handle = std::coroutine_handle<>;
    SendAwaiter(Channel<T>* channel, T&& value) : channel_(channel), value_(std::move(value)) {}
    bool await_ready() const noexcept { return false; }
    bool await_suspend(Handle handle) noexcept
    {
        handle_ = handle;
        return channel_->send_impl(this);
    }

    auto await_resume() const { return state_; }

    auto get_value() { return std::move(value_); }
    auto set_value()
    {
        state_ = State::OK;
        return handle_;
    }

  private:
    Channel<T>* channel_;
    T value_{};
    State state_{State::CLOSED};
    Handle handle_;
};

// 接收操作
template <typename T = std::monostate> class RecvAwaiter
{

  public:
    RecvAwaiter(Channel<T>* channel) : channel_(channel) {}
    auto await_ready() const noexcept { return false; }

    auto await_suspend(Handle handle) noexcept
    {
        handle_ = handle;
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
    auto set_value(T&& value)
    {
        value_ = std::move(value);
        state_ = State::OK;
        return handle_;
    }

  private:
    Channel<T>* channel_;
    T value_{};
    State state_ = State::CLOSED;
    Handle handle_;
};

template <typename T> class Channel
{
  public:
    Channel(size_t capacity = 0)
        requires(!std::is_same_v<T, std::monostate>)
        : capacity_(capacity)
    {
    }
    Channel(size_t capacity = 0, size_t initial_size = 0)
        requires(std::is_same_v<T, std::monostate>)
        : capacity_(capacity)
    {
        assert(initial_size <= capacity);
        for (size_t i = 0; i < initial_size; ++i)
        {
            resource_.push(std::monostate{});
        }
    }
    ~Channel() {}

    auto recv() { return RecvAwaiter<T>{this}; }

    auto send(T value) { return SendAwaiter<T>{this, std::move(value)}; }
    auto send()
        requires(std::is_same_v<T, std::monostate>)
    {
        return SendAwaiter<T>{this, {}};
    }

  private:
    bool send_impl(SendAwaiter<T>* awaiter);
    bool recv_impl(RecvAwaiter<T>* awaiter);
    bool is_closed() const { return is_closed_; }
    bool is_empty() const { return resource_.empty() && send_awaiters_.empty(); }
    bool is_full() const { return resource_.size() >= capacity_ && recv_awaiters_.empty(); }

    std::mutex mutex_{};
    size_t capacity_{};
    std::queue<T> resource_{};
    std::queue<SendAwaiter<T>*> send_awaiters_;
    std::queue<RecvAwaiter<T>*> recv_awaiters_;
    bool is_closed_ = false;
    friend class SendAwaiter<T>;
    friend class RecvAwaiter<T>;
};

template <typename T> bool Channel<T>::send_impl(SendAwaiter<T>* send_awaiter)
{
    Handle notify;
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
            send_awaiters_.push(send_awaiter);
            return true;
        }
        // channel未关闭且不满，唤醒
        resource_.push(send_awaiter->get_value());
        if (!recv_awaiters_.empty())
        {
            auto recv_awaiter = recv_awaiters_.front();
            recv_awaiters_.pop();

            notify = recv_awaiter->set_value(std::move(resource_.front()));
            resource_.pop();
        }
    }
    if (notify)
    {
        co_spawn(notify);
    }
    return false;
}
template <typename T> bool Channel<T>::recv_impl(RecvAwaiter<T>* recv_awaiter)
{
    Handle notify;
    ;
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
            recv_awaiters_.push(recv_awaiter);
            return true;
        }
        // channel不空，唤醒
        // 保持FIFO
        if (!send_awaiters_.empty())
        {
            auto send_awaiter = send_awaiters_.front();
            send_awaiters_.pop();
            resource_.push(send_awaiter->get_value());
        }

        notify = recv_awaiter->set_value(std::move(resource_.front()));
        resource_.pop();
    }
    if (notify)
    {
        co_spawn(notify);
    }
    return false;
}
} // namespace utils
