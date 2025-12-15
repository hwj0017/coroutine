#pragma once
#include <any>
#include <atomic>
#include <coroutine>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <utility>

namespace utils
{

class Coroutine
{
  public:
    class promise_type;
    Coroutine() noexcept = default;
    Coroutine(std::coroutine_handle<promise_type> handle) noexcept : handle_(handle) {}
    Coroutine(const Coroutine&) = default;
    Coroutine(Coroutine&& coro) noexcept = default;
    ~Coroutine() = default;
    Coroutine& operator=(Coroutine&& coro) noexcept = default;
    Coroutine& operator=(const Coroutine&) = default;
    operator bool() { return bool(handle_); }
    void operator()();
    auto await_ready() { return false; }
    void await_suspend(std::coroutine_handle<promise_type> handle);
    auto await_resume() {}

    auto get_data() -> std::any&;
    void set_data(const std::any& data);
    size_t get_id();
    void resume()
    {
        if (handle_)
        {
            handle_.resume();
        }
    }

  private:
    auto& promise() { return handle_.promise(); }
    std::coroutine_handle<promise_type> handle_{nullptr};
};

class Coroutine::promise_type
{
  public:
    auto initial_suspend() noexcept { return std::suspend_always{}; }
    auto final_suspend() noexcept
    {
        if (waiter_)
        {
            waiter_();
        }
        return std::suspend_never{};
    };
    void unhandled_exception() { std::exit(-1); }
    void return_void() {}
    auto get_return_object()
    {
        id_ = next_id_.fetch_add(1);
        return Coroutine{std::coroutine_handle<promise_type>::from_promise(*this)};
    }
    void set_waiter(Coroutine waiter) { waiter_ = waiter; }
    auto& get_data() { return data_; }
    void set_data(const std::any& data) { data_ = data; }
    size_t get_id() { return id_; }

  private:
    Coroutine waiter_{};
    // 可存储信息
    std::any data_{};
    size_t id_{};
    static std::atomic<size_t> next_id_;
};

inline std::atomic<size_t> Coroutine::promise_type::next_id_ = 0;
inline void Coroutine::await_suspend(std::coroutine_handle<promise_type> handle)
{
    // 设置等待的协程
    promise().set_waiter(Coroutine{handle});
    // 开始调度
    (*this)();
}
inline auto Coroutine::get_data() -> std::any& { return promise().get_data(); }

inline void Coroutine::set_data(const std::any& data) { promise().set_data(data); }

inline size_t Coroutine::get_id() { return promise().get_id(); }
} // namespace utils
