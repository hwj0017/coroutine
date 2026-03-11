#pragma once
#include "coroutine/cospawn.h"
#include "coroutine/icallable.h"
#include <atomic>
#include <cassert>
#include <coroutine>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>
#include <variant>
namespace utils
{

class YieldAwaiter
{
  public:
    bool await_ready() const noexcept { return false; }
    template <typename Promise> void await_suspend(std::coroutine_handle<Promise> handle) const noexcept
    {
        co_spawn(&handle.promise(), true);
    }
    void await_resume() const noexcept {}
};
class CoroutineBase;
class Promise : public ICallable
{
  public:
    Promise() = default;
    auto initial_suspend() noexcept { return std::suspend_always{}; }
    auto final_suspend() noexcept { return std::suspend_never{}; };
    void unhandled_exception() { std::exit(-1); }
    auto yield_value(std::monostate value = {}) { return YieldAwaiter{}; }
    void invoke() override { std::coroutine_handle<Promise>::from_promise(*this).resume(); }
    void destroy() override { std::coroutine_handle<Promise>::from_promise(*this).destroy(); }
    void set_awaiter(CoroutineBase* awaiter) { awaiter_ = awaiter; }

  protected:
    CoroutineBase* awaiter_{nullptr};
};

class CoroutineBase
{
  public:
    CoroutineBase() noexcept = default;
    CoroutineBase(Promise* promise) noexcept : self_promise_(promise) {}
    CoroutineBase(const CoroutineBase&) = delete;
    ~CoroutineBase()
    {
        if (self_promise_)
        {
            self_promise_->destroy();
        }
    }
    operator bool() { return self_promise_; }
    auto await_ready() noexcept { return self_promise_ == nullptr; }
    template <typename P> auto await_suspend(std::coroutine_handle<P> handle) noexcept
    {
        awaiter_promise_ = &handle.promise();
        self_promise_->set_awaiter(this);
        auto self_handle = std::coroutine_handle<Promise>::from_promise(*self_promise_);
        // 先置空
        self_promise_ = nullptr;
        return self_handle;
    }

  protected:
    friend void co_spawn(CoroutineBase&& coro);
    Promise* self_promise_{};
    // await_suspend的handle
    Promise* awaiter_promise_{};
};

inline void co_spawn(CoroutineBase&& coro)
{
    auto promise = coro.self_promise_;
    coro.self_promise_ = nullptr;
    co_spawn(promise);
}
template <typename T> class Coroutine;

template <typename T = void> class Coroutine : public CoroutineBase
{
  public:
    class promise_type : public Promise
    {
      public:
        auto get_return_object() -> Coroutine<T>;
        void return_value(T value);
    };
    Coroutine() = default;
    Coroutine(promise_type* promise) : CoroutineBase(promise) {}
    auto await_resume() { return std::move(value_); }
    auto set_value(T value)
    {
        value_ = std::move(value);
        return awaiter_promise_;
    }

  private:
    T value_;
};
template <typename T> auto Coroutine<T>::promise_type::get_return_object() -> Coroutine<T>
{
    return Coroutine<T>(this);
}
template <typename T> void Coroutine<T>::promise_type::return_value(T value)
{
    if (awaiter_)
    {
        if (auto promise = static_cast<Coroutine<T>*>(awaiter_)->set_value(std::move(value)); promise)
        {
            co_spawn(promise);
        }
    }
}

template <> class Coroutine<void> : public CoroutineBase
{
  public:
    class promise_type : public Promise
    {
      public:
        auto get_return_object() -> Coroutine<void>;
        void return_void();
    };
    Coroutine() = default;
    Coroutine(promise_type* promise) : CoroutineBase(promise) {}
    void await_resume() {}
    auto set_value() { return awaiter_promise_; }
};

inline auto Coroutine<>::promise_type::get_return_object() -> Coroutine<void> { return Coroutine<void>(this); }
inline void Coroutine<>::promise_type::return_void()
{
    if (awaiter_)
    {
        if (auto promise = static_cast<Coroutine<>*>(awaiter_)->set_value(); promise)
        {
            co_spawn(promise);
        }
    }
}

class MainCoroutine : public CoroutineBase
{
  public:
    class promise_type : public Promise
    {
      public:
        auto get_return_object() -> MainCoroutine { return MainCoroutine(this); }
        void return_value(int value)
        {
            destroy();
            std::exit(value);
        }
    };
    MainCoroutine(promise_type* promise) : CoroutineBase(promise) {}
};
} // namespace utils
