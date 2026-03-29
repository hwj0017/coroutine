#pragma once

#include "coroutine/coroutine.h"
#include "coroutine/cospawn.h"
#include "coroutine/spinlock.h"
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
using Lock = std::mutex;
enum class State
{
    OK,
    CLOSED
};

template <typename T = void> class Channel;

template <typename T> class Channel
{
  public:
    class SendAwaiter
    {
      public:
        SendAwaiter(Channel<T>* channel, T&& value) : channel_(channel), value_(std::move(value)) {}
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
        Channel<T>* channel_;
        T value_{};
        State state_{State::CLOSED};
        Promise* promise_{};
    };

    // 接收操作
    class RecvAwaiter
    {

      public:
        RecvAwaiter(Channel<T>* channel) : channel_(channel) {}
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

      private:
        Channel<T>* channel_;
        T value_{};
        State state_ = State::CLOSED;
        Promise* promise_;
    };
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
            auto p = send_awaiters_.front()->set_value(State::CLOSED);
            send_awaiters_.pop();
            co_spawn(p);
        }
        while (!recv_awaiters_.empty())
        {
            auto p = recv_awaiters_.front()->set_value({}, State::CLOSED);
            recv_awaiters_.pop();
            co_spawn(p);
        }
    }

  private:
    bool send_impl(SendAwaiter* awaiter);
    bool recv_impl(RecvAwaiter* awaiter);
    bool is_closed() const { return is_closed_; }
    bool is_empty() const { return resource_.empty() && send_awaiters_.empty(); }
    bool is_full() const { return resource_.size() >= capacity_ && recv_awaiters_.empty(); }

    Lock mutex_{};
    size_t capacity_{};
    std::queue<T> resource_{};
    std::queue<SendAwaiter*> send_awaiters_;
    std::queue<RecvAwaiter*> recv_awaiters_;
    bool is_closed_ = false;
};

template <typename T> bool Channel<T>::send_impl(SendAwaiter* send_awaiter)
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
            send_awaiters_.push(send_awaiter);
            return true;
        }
        // channel未关闭且不满，唤醒
        resource_.push(send_awaiter->get_value());
        if (!recv_awaiters_.empty())
        {
            auto recv_awaiter = recv_awaiters_.front();
            recv_awaiters_.pop();
            notify = recv_awaiter->set_value(std::move(resource_.front()), State::OK);
            resource_.pop();
        }
    }
    send_awaiter->set_value(State::OK);
    if (notify)
    {
        co_spawn(notify);
    }
    return false;
}
template <typename T> bool Channel<T>::recv_impl(RecvAwaiter* recv_awaiter)
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
            notify = send_awaiter->set_value(State::OK);
        }
        recv_awaiter->set_value(std::move(resource_.front()), State::OK);
        resource_.pop();
    }
    if (notify)
    {
        co_spawn(notify);
    }
    return false;
}

template <> class Channel<void>
{
  public:
    // 以下将特化channel<void>
    class SendAwaiter
    {
      public:
        SendAwaiter(Channel<void>* channel) : channel_(channel) {}
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
        Channel<void>* channel_;
        State state_{State::CLOSED};
        Promise* promise_{};
    };

    // 接收操作
    class RecvAwaiter
    {

      public:
        RecvAwaiter(Channel<void>* channel) : channel_(channel) {}
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
        Channel<void>* channel_;
        State state_ = State::CLOSED;
        Promise* promise_;
    };
    Channel(size_t capacity = 0, size_t initial_size = 0) : capacity_(capacity), size_(initial_size)
    {
        assert(initial_size <= capacity);
    }
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
            auto p = send_awaiters_.front()->set_value(State::CLOSED);
            send_awaiters_.pop();
            co_spawn(p);
        }
        while (!recv_awaiters_.empty())
        {
            auto p = recv_awaiters_.front()->set_value(State::CLOSED);
            recv_awaiters_.pop();
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
    bool is_full() const { return size_ >= capacity_ && recv_awaiters_.empty(); }

    Lock mutex_{};
    size_t capacity_{};
    size_t size_{};
    std::queue<SendAwaiter*> send_awaiters_;
    std::queue<RecvAwaiter*> recv_awaiters_;
    bool is_closed_ = false;
};
inline bool Channel<void>::send_impl(SendAwaiter* send_awaiter)
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
            send_awaiters_.push(send_awaiter);
            return true;
        }
        // channel未关闭且不满，唤醒
        size_++;
        if (!recv_awaiters_.empty())
        {
            auto recv_awaiter = recv_awaiters_.front();
            recv_awaiters_.pop();
            notify = recv_awaiter->set_value(State::OK);
            size_--;
        }
        send_awaiter->set_value(State::OK);
    }
    if (notify)
    {
        co_spawn(notify);
    }
    return false;
}
inline bool Channel<void>::recv_impl(RecvAwaiter* recv_awaiter)
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
            recv_awaiters_.push(recv_awaiter);
            return true;
        }
        // channel不空，唤醒
        // 保持FIFO
        if (!send_awaiters_.empty())
        {
            auto send_awaiter = send_awaiters_.front();
            send_awaiters_.pop();
            size_++;
            notify = send_awaiter->set_value(State::OK);
        }
        recv_awaiter->set_value(State::OK);
        size_--;
    }
    if (notify)
    {
        co_spawn(notify);
    }
    return false;
}

} // namespace utils
