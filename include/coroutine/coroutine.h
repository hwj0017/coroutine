#pragma once
#include <coroutine>
#include <cstddef>
#include <cstdlib>

namespace utils {

// class ThreadBoundCoroutine {
// public:
//   class promise_type;
//   class InitialAwaiter {
//   public:
//     auto await_ready() { return false; }
//     void await_suspend(std::coroutine_handle<promise_type> handle);
//     void await_resume() {}
//   };
//   class promise_type {
//   public:
//     auto initial_suspend() { return InitialAwaiter{}; };
//     auto final_suspend() noexcept { return std::suspend_never{}; }
//     void unhandled_exception() { std::exit(-1); }
//     void return_void() {}
//     auto get_return_object() {
//       return ThreadBoundCoroutine{
//           std::coroutine_handle<promise_type>::from_promise(*this)};
//     }
//   };
//   ThreadBoundCoroutine(std::coroutine_handle<promise_type> handle)
//       : handle_(handle) {}
//   void resume() { handle_.resume(); }

// private:
//   std::coroutine_handle<promise_type> handle_;
// };
class Scheduler;
class Coroutine {
public:
  class promise_type;
  class InitialAwaiter {
  public:
    auto await_ready() { return false; }
    void await_suspend(std::coroutine_handle<promise_type> handle);
    void await_resume() {}
  };
  class promise_type {
  public:
    auto initial_suspend() { return InitialAwaiter{}; };
    auto final_suspend() noexcept { return std::suspend_never{}; }
    void unhandled_exception() { std::exit(-1); }
    void return_void() {}
    auto get_return_object() {
      return Coroutine{
          std::coroutine_handle<promise_type>::from_promise(*this)};
    }
    void setWaiter(std::coroutine_handle<promise_type> waiter) {
      waiter_ = waiter;
    }
    void setProcessor(int processor_id) { processor_id_ = processor_id; }
    int getProcessor() { return processor_id_; }

  private:
    int processor_id_{-1};
    std::coroutine_handle<promise_type> waiter_{nullptr};
  };
  Coroutine() = default;
  Coroutine(std::coroutine_handle<promise_type> handle) : handle_(handle) {}
  operator bool() { return handle_ != nullptr; }
  auto getProcessor() { return handle_.promise().getProcessor(); }
  auto await_ready() { return false; }
  void await_suspend(std::coroutine_handle<promise_type> handle) {
    handle_.promise().setWaiter(handle);
  }
  auto await_resume() {}

private:
  std::coroutine_handle<promise_type> handle_{nullptr};
  void resume() { handle_.resume(); }
  friend class Scheduler;
};

} // namespace utils
