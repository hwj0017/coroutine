#pragma once

#include "coroutine.h"
#include "coroutine/scheduler.h"
#include <atomic>
#include <cassert>
#include <coroutine>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <tuple>
#include <type_traits>
#include <variant>

namespace utils {
enum class State { OK, CLOSED };
template <typename T = std::monostate> class Channel;
template <typename T = std::monostate> class SendAwaiter {
public:
  SendAwaiter(Channel<T> *channel, T &&value)
      : channel_(channel), value_(std::move(value)) {}
  bool await_ready() const noexcept { return false; }
  bool await_suspend(
      std::coroutine_handle<Coroutine::promise_type> handle) noexcept {
    return channel_->send_impl(handle, this);
  }

  auto await_resume() const { return state_; }

  auto get_value() {
    state_ = State::OK;
    return std::move(value_);
  }

private:
  Channel<T> *channel_;
  T value_{};
  State state_{State::CLOSED};
};

// 接收操作
template <typename T = std::monostate> class RecvAwaiter {

public:
  RecvAwaiter(Channel<T> *channel) : channel_(channel) {}
  auto await_ready() const noexcept { return false; }

  auto await_suspend(
      std::coroutine_handle<Coroutine::promise_type> handle) noexcept {
    return channel_->recv_impl(handle, this);
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
  auto set_value(T &&value) {
    value_ = std::move(value);
    state_ = State::OK;
  }

private:
  Channel<T> *channel_;
  T value_{};
  State state_ = State::CLOSED;
};

template <typename T> class Channel {
public:
  Channel(size_t capacity = 0)
    requires(!std::is_same_v<T, std::monostate>)
      : capacity_(capacity) {}
  Channel(size_t capacity = 0, size_t initial_size = 0)
    requires(std::is_same_v<T, std::monostate>)
      : capacity_(capacity) {
    assert(initial_size <= capacity);
    for (size_t i = 0; i < initial_size; ++i) {
      resource_.push(std::monostate{});
    }
  }

  bool send_impl(Coroutine coro, SendAwaiter<T> *awaiter);
  bool recv_impl(Coroutine coro, RecvAwaiter<T> *awaiter);
  bool is_closed() const { return is_closed_; }
  bool is_empty() const { return resource_.empty() && send_request_.empty(); }
  bool is_full() const {
    return resource_.size() >= capacity_ && recv_request_.empty();
  }
  auto recv() { return RecvAwaiter<T>{this}; }

  auto send(T value) { return SendAwaiter<T>{this, std::move(value)}; }
  auto send()
    requires(std::is_same_v<T, std::monostate>)
  {
    return SendAwaiter<T>{this, {}};
  }

private:
  struct SendRequest {
    SendAwaiter<T> *awaiter;
    Coroutine coro;
  };

  struct RecvRequest {
    RecvAwaiter<T> *awaiter;
    Coroutine coro;
  };

  std::mutex mutex_{};
  size_t capacity_{};
  std::queue<T> resource_{};
  std::queue<SendRequest> send_request_;
  std::queue<RecvRequest> recv_request_;
  bool is_closed_ = false;
};

template <typename T>
bool Channel<T>::send_impl(Coroutine coro, SendAwaiter<T> *send_awaiter) {
  std::lock_guard lock(mutex_);
  // channel关闭，不阻塞
  if (is_closed()) {
    return false;
  }
  if (is_full()) {
    // channel未关闭且已满，阻塞
    send_request_.push({send_awaiter, coro});
    return true;
  }
  // channel未关闭且不空，唤醒

  if (!resource_.empty()) {
    resource_.push(send_awaiter->get_value());
  } else {
    auto [recv_awaiter, recv_coro] = std::move(recv_request_.front());
    recv_request_.pop();
    recv_awaiter->set_value(send_awaiter->get_value());
    Scheduler::instance().schedule(recv_coro);
  }
  return false;
}
template <typename T>
bool Channel<T>::recv_impl(Coroutine coro, RecvAwaiter<T> *recv_awaiter) {
  std::lock_guard lock(mutex_);
  // 关闭且为空，不阻塞
  if (is_closed() && is_empty()) {
    return false;
  }
  // channel未关闭且为空，阻塞
  if (is_empty()) {
    recv_request_.push({recv_awaiter, coro});
    return true;
  }
  // channel不空，唤醒
  if (!resource_.empty()) {
    recv_awaiter->set_value(std::move(resource_.front()));
    resource_.pop();
  } else {
    auto [send_awaiter, send_coro] = std::move(send_request_.front());
    send_request_.pop();
    recv_awaiter->set_value(send_awaiter->get_value());
    Scheduler::instance().schedule(send_coro);
  }
  return false;
}
// template <> class Channel<void> {
// public:
//   Channel(size_t capacity = 0, size_t initial_size = 0)
//       : capacity_(capacity), resources_(initial_size) {}
//   // 发送操作
//   struct SendAwaiter {
//     Channel *channel_;
//     State state_ = State::OK;

//     bool await_ready() const noexcept { return false; }
//     bool await_suspend(
//         std::coroutine_handle<Coroutine::promise_type> handle) noexcept {
//       std::lock_guard lock(channel_->mutex_);
//       // channel关闭，不阻塞
//       if (!channel_->is_closed() && channel_->is_full()) {
//         // channel未关闭且已满，阻塞
//         channel_->send_queue_.push({this, handle});
//       } else {
//         // channel已关闭, 唤醒
//         if (channel_->is_closed()) {
//           state_ = State::CLOSED;
//         } else { // channel不满，唤醒
//           if (!channel_->recv_queue_.empty()) {
//             auto [recv_awaiter, recv_coro] =
//                 std::move(channel_->recv_queue_.front());
//             channel_->recv_queue_.pop();
//             Scheduler::instance().schedule(std::move(recv_coro));
//           } else {
//             ++channel_->resources_;
//           }
//           state_ = State::OK;
//         }
//         Scheduler::instance().schedule(handle);
//       }
//       // 有调度，始终返回true
//       return true;
//     }

//     auto await_resume() const { return state_; }
//   };

//   // 接收操作
//   struct RecvAwaiter {
//     Channel *channel_;
//     State state_ = State::OK;
//     auto await_ready() const noexcept { return false; }

//     auto await_suspend(
//         std::coroutine_handle<Coroutine::promise_type> handle) noexcept {
//       std::lock_guard lock(channel_->mutex_);
//       // channel未关闭且为空，阻塞
//       if (!channel_->is_closed() && channel_->is_empty()) {
//         channel_->recv_queue_.push({this, handle});
//       } else {
//         if (!channel_->is_empty()) {
//           if (!channel_->send_queue_.empty()) {
//             auto [send_awaiter, send_coro] =
//                 std::move(channel_->send_queue_.front());
//             channel_->send_queue_.pop();
//             Scheduler::instance().schedule(std::move(send_coro));

//           } else {
//             --channel_->resources_;
//           }
//           state_ = State::OK;
//         } else {
//           state_ = State::CLOSED;
//         }
//         Scheduler::instance().schedule(handle);
//       }
//       return true;
//     }

//     auto await_resume() const { return state_; }
//   };

//   bool is_closed() const { return is_closed_; }
//   bool is_empty() const { return resources_ <= 0 && send_queue_.empty(); }
//   // 会将值先放入，无须判断是否有等待协程
//   bool is_full() const { return resources_ >= capacity_; }
//   auto recv() { return RecvAwaiter{this}; }
//   auto send() { return SendAwaiter{this}; }

// private:
//   struct SendRequest {
//     SendAwaiter *awaiter;
//     Coroutine coro;
//   };

//   struct RecvRequest {
//     RecvAwaiter *awaiter;
//     Coroutine coro;
//   };

//   size_t capacity_ = 0;
//   std::mutex mutex_{};
//   size_t resources_{};
//   std::queue<SendRequest> send_queue_;
//   std::queue<RecvRequest> recv_queue_;
//   bool is_closed_ = false;
// };

// template <typename T = void> class ThreadBoundChannel {
// public:
//   ThreadBoundChannel(size_t capacity = 0) : capacity_(capacity) {}
//   // 发送操作
//   class SendAwaiter {
//     ThreadBoundChannel *channel_;
//     T value_;
//     State state_;

//     bool await_ready() const noexcept { return false; }
//     void await_suspend(
//         std::coroutine_handle<Coroutine::promise_type> handle) noexcept {
//       // channel关闭，不阻塞
//       if (!channel_->is_closed() && channel_->is_full()) {
//         // channel未关闭且已满，阻塞
//         channel_->send_queue_.push({this, handle});
//       } else {
//         // channel已关闭, 唤醒
//         if (channel_->is_closed()) {
//           state_ = State::CLOSED;
//         } else { // channel不满，唤醒
//           if (!channel_->recv_queue_.empty()) {
//             auto [recv_awaiter, recv_coro] =
//                 std::move(channel_->recv_queue_.front());
//             channel_->recv_queue_.pop();
//             recv_awaiter.set_value(get_value());
//             schecule(recv_coro);
//           } else {
//             channel_->resources_.push(std::move(value_));
//           }
//           state_ = State::OK;
//         }
//         Scheduler::instance().schedule(handle);
//       }
//     }

//     auto await_resume() const { return state_; }

//     auto get_value() { return std::move(value_); }
//   };

//   // 接收操作
//   struct RecvAwaiter {
//     ThreadBoundChannel *channel_;
//     T value_;
//     State state_ = State::OK;
//     auto await_ready() const noexcept { return false; }

//     auto await_suspend(
//         std::coroutine_handle<Coroutine::promise_type> handle) noexcept {
//       // channel未关闭且为空，阻塞
//       if (!channel_->is_closed() && channel_->is_empty()) {
//         channel_->recv_queue_.push({this, handle});
//       } else {
//         if (!channel_->is_empty()) {
//           if (!channel_->send_queue_.empty()) {
//             auto [send_awaiter, send_coro] =
//                 std::move(channel_->send_queue_.front());
//             channel_->send_queue_.pop();
//             set_value(send_awaiter.get_value());
//           } else {
//             value_ = std::move(channel_->resources_.front());
//             channel_->resources_.pop();
//           }
//           state_ = State::OK;
//         } else {
//           state_ = State::CLOSED;
//         }
//         Scheduler::instance().schedule(handle);
//       }
//       return true;
//     }

//     auto await_resume() const {
//       return std::tuple<T, State>{std::move(value_), state_};
//     }
//     auto set_value(T &&value) { value_ = std::move(value); }
//   };

//   bool is_closed() const { return is_closed_; }
//   bool is_empty() const { return resources_.empty() && send_queue_.empty(); }
//   // 会将值先放入，无须判断是否有等待协程
//   bool is_full() const { return resources_.size() >= capacity_; }
//   auto recv() { return RecvAwaiter{*this}; }
//   auto send(T value) { return SendAwaiter{*this, std::move(value)}; }

// private:
//   struct SendRequest {
//     SendAwaiter *awaiter;
//     ThreadBoundCoroutine coro;
//   };

//   struct RecvRequest {
//     RecvAwaiter *awaiter;
//     ThreadBoundCoroutine coro;
//   };

//   size_t capacity_ = 0;
//   std::queue<T> resources_{};
//   std::queue<SendRequest> send_queue_;
//   std::queue<RecvRequest> recv_queue_;
//   bool is_closed_ = false;
// };
// template <> class ThreadBoundChannel<void> {
// public:
//   ThreadBoundChannel(size_t capacity = 0, size_t initial_size = 0)
//       : capacity_(capacity), resources_(initial_size) {}
//   // 发送操作
//   struct SendAwaiter {
//     ThreadBoundChannel *channel_;
//     State state_ = State::OK;

//     bool await_ready() const noexcept { return false; }
//     bool
//     await_suspend(std::coroutine_handle<ThreadBoundCoroutine::promise_type>
//                            handle) noexcept {
//       // channel关闭，不阻塞
//       if (!channel_->is_closed() && channel_->is_full()) {
//         // channel未关闭且已满，阻塞
//         channel_->send_queue_.push({this, handle});
//       } else {
//         // channel已关闭, 唤醒
//         if (channel_->is_closed()) {
//           state_ = State::CLOSED;
//         } else { // channel不满，唤醒
//           if (!channel_->recv_queue_.empty()) {
//             auto [recv_awaiter, recv_coro] =
//                 std::move(channel_->recv_queue_.front());
//             channel_->recv_queue_.pop();
//             Scheduler::instance().schedule(recv_coro);
//           } else {
//             ++channel_->resources_;
//           }
//           state_ = State::OK;
//         }
//         Scheduler::instance().schedule(handle);
//       }
//       // 有调度，始终返回true
//       return true;
//     }

//     auto await_resume() const { return state_; }
//   };

//   // 接收操作
//   struct RecvAwaiter {
//     ThreadBoundChannel *channel_;
//     State state_ = State::OK;
//     auto await_ready() const noexcept { return false; }

//     auto
//     await_suspend(std::coroutine_handle<ThreadBoundCoroutine::promise_type>
//                            handle) noexcept {
//       // channel未关闭且为空，阻塞
//       if (!channel_->is_closed() && channel_->is_empty()) {
//         channel_->recv_queue_.push({this, handle});
//       } else {
//         if (!channel_->is_empty()) {
//           if (!channel_->send_queue_.empty()) {
//             auto [send_awaiter, send_coro] =
//                 std::move(channel_->send_queue_.front());
//             channel_->send_queue_.pop();
//             Scheduler::instance().schedule(std::move(send_coro));

//           } else {
//             --channel_->resources_;
//           }
//           state_ = State::OK;
//         } else {
//           state_ = State::CLOSED;
//         }
//         Scheduler::instance().schedule(handle);
//       }
//       return true;
//     }

//     auto await_resume() const { return state_; }
//   };

//   bool is_closed() const { return is_closed_; }
//   bool is_empty() const { return resources_ <= 0 && send_queue_.empty(); }
//   // 会将值先放入，无须判断是否有等待协程
//   bool is_full() const { return resources_ >= capacity_; }
//   auto recv() { return RecvAwaiter{this}; }
//   auto send() { return SendAwaiter{this}; }

// private:
//   struct SendRequest {
//     SendAwaiter *awaiter;
//     ThreadBoundCoroutine coro;
//   };

//   struct RecvRequest {
//     RecvAwaiter *awaiter;
//     ThreadBoundCoroutine coro;
//   };

//   size_t capacity_ = 0;
//   size_t resources_{};
//   std::queue<SendRequest> send_queue_;
//   std::queue<RecvRequest> recv_queue_;
//   bool is_closed_ = false;
// };
} // namespace utils
