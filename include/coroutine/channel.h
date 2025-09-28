#pragma once

#include "coroutine.h"
#include <atomic>
#include <cassert>
#include <coroutine>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>

namespace utils {
// not thread safe
// class ChannelBase;
// TODO
// class ChannelBase {
// public:
//   ChannelBase(size_t max_size, size_t init_size)
//       : empty_size_(max_size - init_size), full_size_(init_size) {}
//   ~ChannelBase() { close(); }
//   void increase() {
//     ++empty_size_;
//     --full_size_;
//   }
//   void decrease() {
//     --empty_size_;
//     ++full_size_;
//   }
//   bool is_empty() const { return full_size_ <= 0; }
//   bool is_full() const { return empty_size_ <= 0; }
//   bool is_closed() const { return is_closed_; }
//   // close the channel
//   void close() {
//     if (is_closed_.exchange(true)) {
//       return;
//     }
//     // 唤醒所有等待的接收者
//     while (!recv_queue_.empty()) {
//       auto &[output, handle] = recv_queue_.front();
//       *output = T{}; // 默认值
//       handle.resume();
//       recv_queue_.pop_front();
//     }

//     // 唤醒所有等待的发送者
//     while (!send_queue_.empty()) {
//       auto &[value, handle] = send_queue_.front();
//       handle.resume();
//       send_queue_.pop_front();
//     }
//   }

// protected:
//   std::atomic<bool> is_closed_ = false;
//   std::queue<Coroutine> recv_queue_;
//   std::queue<Coroutine> send_queue_;
// };

// channel<T> is a bounded channel that can hold T type elements.
template <typename T = void> class Channel {
public:
  Channel(size_t capacity = 0) : capacity_(capacity) {}
  // 发送操作
  struct SendAwaiter {
    Channel &channel;
    T &value;

    bool await_ready() const noexcept {
      std::lock_guard lock(channel.mutex_);
      return channel->try_send(value);
    }

    bool await_suspend(
        std::coroutine_handle<Coroutine::promise_type> handle) noexcept {
      std::lock_guard lock(channel->mutex_);
      if (channel->closed_)
        return false;
      // 加入发送等待队列
      channel->send_queue_.push_back({std::move(value), handle});
      return true;
    }

    bool await_resume() const {
      std::lock_guard lock(channel->mutex_);
      if (channel->closed_) {
        return false;
      }
      return true;
    }
  };

  // 接收操作
  struct RecvAwaiter {
    Channel &channel;
    T &value;

    bool await_ready() const noexcept {
      std::lock_guard lock(channel->mutex_);
      return channel->try_recv(value);
    }

    bool await_suspend(
        std::coroutine_handle<Coroutine::promise_type> handle) noexcept {
      std::lock_guard lock(channel->mutex_);
      if (channel->closed_) {
        return false;
      }

      // 加入接收等待队列
      channel->recv_queue_.push_back({value, handle});
      return true;
    }

    bool await_resume() const {
      std::lock_guard lock(channel->mutex_);
      if (channel->closed_ && channel->buffer_.empty()) {
        throw std::runtime_error("receive on closed channel");
      }
      return std::move(*output);
    }
  };
  // TODO
  struct AsyncPop {
    Channel<T> &channel_;
    AsyncPop(Channel<T> &channel) : channel_(channel) {}
    bool await_ready() { return await_ready(channel_); }
    void await_suspend(std::coroutine_handle<Coroutine::promise_type> handle) {
      channel_.coros_.push(Coroutine{handle});
    }
    auto await_resume() -> std::optional<T> { return await_resume(channel_); }
    static bool await_ready(Channel<T> &channel) {
      // resume Push
      auto ret = channel.is_closed() || !channel.is_empty();
      channel.decrease();
      if (!channel.is_closed() && channel.is_full()) {
        auto coro = std::move(channel.coros_.front());
        channel.coros_.pop();
        coro.resume();
      }
      return ret;
    }
    static auto await_resume(Channel<T> &channel) -> std::optional<T> {
      if (channel.is_empty()) {
        channel.increase();
        return {};
      }
      auto res = std::move(channel.resources_.front());
      channel.resources_.pop();
      return res;
    }
  };
  struct AsyncPush {
    Channel<T> &channel_;
    T value_;
    AsyncPush(Channel<T> &channel, T &&value)
        : channel_(channel), value_(std::move(value)) {}

    bool await_ready() { return await_ready(channel_, std::move(value_)); }

    template <typename promise_type>
    void await_suspend(std::coroutine_handle<promise_type> handle) {
      channel_.coros_.push({handle});
    }
    bool await_resume() { return await_resume(channel_); }
    static bool await_ready(Channel<T> &channel, T &&value) {
      auto ret = channel.is_closed() || channel.is_full();
      if (!channel.is_closed()) {
        channel.resources_.push(std::move(value));
        // resume Pop
        if (channel.is_empty()) {
          auto task = std::move(channel.coros_.front());
          channel.coros_.pop();
          task.resume();
        }
      }
      return ret;
    }
    static bool await_resume(Channel<T> &channel) {
      if () {
        ++channel.empty_size_;
        --channel.full_size_;
        return false;
      }
      return true;
    }
  };
  auto pop() -> std::optional<T> {
    AsyncPop::await_ready(*this);
    return AsyncPop::await_resume(*this);
  }

  bool push(T value) {
    AsyncPush::await_ready(*this, std::move(value));
    return AsyncPush::await_resume(*this);
  }
  bool is_empty() const { return resources_.empty() && coros_.empty(); }
  // 会将值先放入，无须判断是否有等待协程
  bool is_full() const { return resources_.size() >= max_size_ }
  auto async_pop() -> AsyncPop { return AsyncPop{*this}; }
  auto async_push(T value) -> AsyncPush {
    return AsyncPush{*this, std::move(value)};
  }

private:
  struct SendRequest {
    T &value;
    Coroutine coro;
  };

  struct RecvRequest {
    T &value;
    Coroutine coro;
  };

  // 尝试发送（内部使用）
  bool try_send(T &value) {
    if (is_closed_)
      return false;

    // 尝试直接传递给等待的接收者
    if (!recv_queue_.empty()) {
      auto &[output, handle] = recv_queue_.front();
      *output = std::move(value);
      recv_queue_.pop_front();
      handle.resume();
      return true;
    }

    // 如果有缓冲区空间
    if (buffer_.size() < capacity_) {
      buffer_.push_back(std::move(value));
      return true;
    }

    return false;
  }

  // 尝试接收（内部使用）
  bool try_recv(T &value) {
    // 尝试从缓冲区获取
    if (!buffer_.empty()) {
      *output = std::move(buffer_.front());
      buffer_.pop_front();

      // 如果有等待的发送者，唤醒一个
      if (!send_queue_.empty()) {
        auto &[value, handle] = send_queue_.front();
        buffer_.push_back(std::move(value));
        send_queue_.pop_front();
        handle.resume();
      }
      return true;
    }

    // 尝试从等待的发送者获取
    if (!send_queue_.empty()) {
      auto &[value, handle] = send_queue_.front();
      *output = std::move(value);
      send_queue_.pop_front();
      handle.resume();
      return true;
    }

    // 如果已关闭且缓冲区为空
    if (closed_) {
      return false;
    }

    return false;
  }
  size_t capacity_ = 0;
  std::mutex mutex_{};
  std::queue<T> resources_{};
  std::deque<SendRequest> send_queue_;
  std::deque<RecvRequest> recv_queue_;
};
template <> class Channel<void> : public ChannelBase {
public:
  Channel(size_t max_size, size_t init_size = 0)
      : ChannelBase(max_size, init_size) {}
  struct AsyncPop {
    Channel<> &channel_;
    AsyncPop(Channel<> &channel) : channel_(channel) {}
    bool await_ready() { return await_ready(channel_); }
    template <typename promiss_type>
    void await_suspend(std::coroutine_handle<promiss_type> coro) {
      channel_.coroutines_.push({coro});
    }
    auto await_resume() -> bool { return await_resume(channel_); }
    static bool await_ready(Channel<> &channel) {
      // increment empty size and decrement full size
      ++channel.empty_size_;
      --channel.full_size_;
      if (!channel.is_closed()) { // resume Push
        if (channel.is_full() && !channel.coroutines_.empty()) {
          auto task = std::move(channel.coroutines_.front());
          channel.coroutines_.pop();
          task.resume();
        }
        while (channel.is_empty() && !channel.empty_tasks_.empty()) {
          auto task = std::move(channel.empty_tasks_.front());
          channel.empty_tasks_.pop();
          task.resume();
        }
        while (!channel.is_full() && !channel.full_tasks_.empty()) {
          auto task = std::move(channel.full_tasks_.front());
          channel.full_tasks_.pop();
          task.resume();
        }
      }

      return channel.is_closed() || channel.full_size_ >= 0;
    }
    static auto await_resume(Channel<> &channel) -> bool {
      if (channel.full_size_ < 0) {
        --channel.empty_size_;
        ++channel.full_size_;
        return false;
      }
      return true;
    }
  };
  struct AsyncPush {
    Channel<> &channel_;
    AsyncPush(Channel<> &channel) : channel_(channel) {}

    bool await_ready() { return await_ready(channel_); }

    template <typename promise_type>
    void await_suspend(std::coroutine_handle<promise_type> coro) {
      channel_.coroutines_.push({coro});
    }
    bool await_resume() { return await_resume(channel_); }
    static bool await_ready(Channel<> &channel) {
      --channel.empty_size_;
      ++channel.full_size_;
      if (!channel.is_closed()) { // resume Pop
        if (channel.is_empty() && !channel.coroutines_.empty()) {
          auto task = std::move(channel.coroutines_.front());
          channel.coroutines_.pop();
          task.resume();
        }
        while (channel.is_full() && !channel.full_tasks_.empty()) {
          auto task = std::move(channel.full_tasks_.front());
          channel.full_tasks_.pop();
          task.resume();
        }
        while (!channel.is_empty() && !channel.empty_tasks_.empty()) {
          auto task = std::move(channel.empty_tasks_.front());
          channel.empty_tasks_.pop();
          task.resume();
        }
      }
      return channel.is_closed() || channel.empty_size_ >= 0;
    }
    static bool await_resume(Channel<> &channel) {
      if (channel.empty_size_ < 0) {
        ++channel.empty_size_;
        --channel.full_size_;
        return false;
      }
      return true;
    }
  };
  auto pop() -> bool {
    AsyncPop::await_ready(*this);
    return AsyncPop::await_resume(*this);
  }

  bool push() {
    AsyncPush::await_ready(*this);
    return AsyncPush::await_resume(*this);
  }
  auto async_pop() -> AsyncPop { return AsyncPop{*this}; }
  auto async_push() -> AsyncPush { return AsyncPush{*this}; }
  friend struct AsyncPop;
  friend struct AsyncPush;
};
} // namespace utils
