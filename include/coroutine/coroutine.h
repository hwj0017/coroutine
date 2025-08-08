#pragma once

#include <coroutine>
#include <cstddef>
#include <cstdlib>

namespace utils {
// RAII Base Task

class Coroutine {
public:
  struct promise_type {
    size_t ref_count_ = 0;
    std::suspend_never initial_suspend() { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    void unhandled_exception() { std::exit(-1); }
    auto get_return_object() -> Coroutine;
  };
  Coroutine(promise_type &promise)
      : Coroutine(std::coroutine_handle<promise_type>::from_promise(promise)) {}
  Coroutine(std::coroutine_handle<promise_type> handle) : handle_(handle) {
    ++handle_.promise().ref_count_;
  }

  ~Coroutine() noexcept {
    if (--handle_.promise().ref_count_ == 0) {
      handle_.destroy();
    }
  }
  void resume() const noexcept { handle_.resume(); }
  bool done() noexcept { return handle_.done(); }

private:
  std::coroutine_handle<promise_type> handle_;
};
inline auto Coroutine::promise_type::get_return_object() -> Coroutine {
  return Coroutine{*this};
}
} // namespace utils
