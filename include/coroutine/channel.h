#pragma once

#include "coroutine.h"
#include <cassert>
#include <coroutine>
#include <cstddef>
#include <memory>
#include <optional>
#include <queue>

namespace utils {
// not thread safe
// class ChannelBase;
// TODO
class ChannelBase {
public:
  ChannelBase(size_t max_size, size_t init_size)
      : empty_size_(max_size - init_size), full_size_(init_size) {}
  ~ChannelBase() { close(); }
  void increase() {
    ++empty_size_;
    --full_size_;
  }
  void decrease() {
    --empty_size_;
    ++full_size_;
  }
  bool is_empty() const { return full_size_ <= 0; }
  bool is_full() const { return empty_size_ <= 0; }
  bool is_closed() const { return is_closed_; }
  // close the channel
  void close() {
    if (is_closed_) {
      return;
    }
    is_closed_ = true;
    while (!coros_.empty()) {
      auto coro = coros_.front();
      coros_.pop();
      coro.resume();
    }
  }

protected:
  size_t empty_size_ = 0;
  size_t full_size_ = 0;
  bool is_closed_ = false;
  // await pop or push
  std::queue<Coroutine> coros_;
};

// channel<T> is a bounded channel that can hold T type elements.
template <typename T = void> class Channel : public ChannelBase {
public:
  Channel(size_t max_size) : ChannelBase(max_size, 0) {}
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
  size_t max_size_;
  std::queue<T> resources_;
  friend struct AsyncPop;
  friend struct AsyncPush;
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
