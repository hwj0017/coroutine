#pragma once
#include "coroutine/cospawn.h"
#include <cassert>
#include <coroutine>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <utility>
#include <variant>
namespace utils
{
class YieldAwaiter
{
  public:
    bool await_ready() const noexcept { return false; }
    bool await_suspend(Handle handle) const noexcept
    {
        co_spawn(handle, true);
        return true;
    }
    void await_resume() const noexcept {}
};
class CoroutineBase;

class CoroutineBase
{
  public:
    class PromiseTypeBase : public Promise
    {
      public:
        PromiseTypeBase() = default;
        auto initial_suspend() noexcept { return std::suspend_always{}; }
        auto final_suspend() noexcept { return std::suspend_never{}; };
        void unhandled_exception() { std::exit(-1); }
        auto yield_value(std::monostate value = {}) { return YieldAwaiter{}; }
        auto get_self_handle() -> Handle { return std::coroutine_handle<Promise>::from_promise(*this); }
        void set_awaiter(CoroutineBase* awaiter) { awaiter_ = awaiter; }

      protected:
        CoroutineBase* awaiter_{nullptr};
    };
    CoroutineBase() noexcept = default;
    CoroutineBase(PromiseTypeBase* promise) noexcept : promise_(promise) {}
    CoroutineBase(const CoroutineBase&) = delete;
    CoroutineBase(CoroutineBase&& coro) noexcept : promise_(coro.promise_) { coro.promise_ = nullptr; }
    ~CoroutineBase()
    {
        if (promise_)
        {
            promise_->get_self_handle().destroy();
        }
    }
    CoroutineBase& operator=(CoroutineBase&& coro) noexcept
    {
        if (this == &coro)
        {
            return *this;
        }
        if (promise_)
        {
            promise_->get_self_handle().destroy();
        }
        promise_ = coro.promise_;
        coro.promise_ = nullptr;
        return *this;
    }
    CoroutineBase& operator=(const CoroutineBase&) = default;
    operator bool() { return promise_; }
    auto await_ready() { return false; }
    bool await_suspend(Handle handle)
    {
        handle_ = handle;
        promise_->set_awaiter(this);
        auto self_handle = promise_->get_self_handle();

        // 先置空
        promise_ = nullptr;
        co_spawn(self_handle);
        return true;
    }
    size_t get_id() { return promise_->get_id(); }

  protected:
    friend void co_spawn(CoroutineBase&& coro);
    PromiseTypeBase* promise_;
    // await_suspend的handle
    Handle handle_;
};

inline void co_spawn(CoroutineBase&& coro)
{
    auto handle = coro.promise_->get_self_handle();
    coro.promise_ = nullptr;
    co_spawn(handle);
}
template <typename T> class Coroutine;

template <typename T = void> class Coroutine : public CoroutineBase
{
  public:
    class PromiseType : public PromiseTypeBase
    {
      public:
        auto get_return_object() -> Coroutine<T>;
        void return_value(T value);
    };
    using promise_type = PromiseType;
    Coroutine(PromiseTypeBase* promise) : CoroutineBase(promise) {}
    auto await_resume() { return std::move(value_); }
    auto set_value(T value)
    {
        value_ = std::move(value);
        return handle_;
    }

  private:
    T value_;
    friend class Scheduler;
};
template <typename T> auto Coroutine<T>::PromiseType::get_return_object() -> Coroutine<T> { return Coroutine<T>(this); }
template <typename T> void Coroutine<T>::PromiseType::return_value(T value)
{
    if (awaiter_)
    {
        if (auto handle = static_cast<Coroutine<T>*>(awaiter_)->set_value(std::move(value)); handle)
        {
            co_spawn(handle);
        }
    }
}

template <> class Coroutine<void> : public CoroutineBase
{
  public:
    class PromiseType : public PromiseTypeBase
    {
      public:
        auto get_return_object() -> Coroutine<void>;
        void return_void();
    };
    using promise_type = PromiseType;
    Coroutine(promise_type* promise) : CoroutineBase(promise) {}
    auto await_ready() { return false; }
    void await_suspend(Handle handle)
    {
        handle_ = handle;
        promise_->set_awaiter(this);
        auto self_handle = promise_->get_self_handle();

        promise_ = nullptr;
        co_spawn(self_handle);
    }
    void await_resume() {}
    auto set_value() { return handle_; }

  private:
    Handle awaiter_handle_{nullptr};
};

inline auto Coroutine<>::PromiseType::get_return_object() -> Coroutine<void> { return Coroutine<void>(this); }
inline void Coroutine<>::PromiseType::return_void()
{
    if (awaiter_)
    {
        if (auto handle = static_cast<Coroutine<>*>(awaiter_)->set_value(); handle)
        {
            co_spawn(handle);
        }
    }
}

class MainCoroutine : public CoroutineBase
{
  public:
    class PromiseType : public PromiseTypeBase
    {
      public:
        auto get_return_object() -> MainCoroutine { return MainCoroutine(this); }
        void return_value(int value)
        {
            // 模拟main函数，先销毁局部变量
            get_self_handle().destroy();
            std::exit(value);
        }
    };
    using promise_type = PromiseType;
    MainCoroutine(promise_type* promise) : CoroutineBase(promise) {}
};
} // namespace utils
