#pragma once
#include "coroutine/cospawn.h"
#include <atomic>
#include <cassert>
#include <coroutine>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <mimalloc.h>
#include <string>
#include <utility>
#include <variant>
namespace utils
{
class Promise;
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

class FinalAwaiter
{
  public:
    bool await_ready() const noexcept { return false; }
    template <typename P> auto await_suspend(std::coroutine_handle<P> handle) const noexcept -> std::coroutine_handle<>
    {
        auto promise = handle.promise();
        auto awaiter = promise.awaiter_;
        if (!awaiter)
        {
            handle.destroy();
            return std::noop_coroutine();
        }
        assert(awaiter->awaiter_promise_);
        auto awaiter_handle = std::coroutine_handle<Promise>::from_promise(*awaiter->awaiter_promise_);
        handle.destroy();
        return awaiter_handle;
    }
    void await_resume() const noexcept {}
};

class CoroutineBase;
class Promise
{
  public:
    Promise() = default;
    void* operator new(std::size_t size)
    {
        // 调用 minimalloc 的分配函数 (如果是你自己的库，替换为对应的 malloc)
        // 打印日志或做追踪也可以在这里加
        // return mi_malloc(size);
        return ::operator new(size);
    }
    // 核心：重写协程帧的内存释放
    void operator delete(void* ptr, std::size_t size)
    {
        // 调用 minimalloc 的释放函数
        // mi_free(ptr);
        ::operator delete(ptr);
    }
    auto initial_suspend() noexcept { return std::suspend_always{}; }

    void unhandled_exception() { std::exit(-1); }
    auto yield_value(std::monostate value = {}) { return YieldAwaiter{}; }
    void resume() { std::coroutine_handle<Promise>::from_promise(*this).resume(); }
    void destroy() { std::coroutine_handle<Promise>::from_promise(*this).destroy(); }
    void set_awaiter(CoroutineBase* awaiter) { awaiter_ = awaiter; }

    // auto operator new(size_t size) -> void*
    // {
    //     std::cout << "new coroutine" << size << std::endl;
    //     return ::operator new(size);
    // }

  protected:
    CoroutineBase* awaiter_{nullptr};
    // 用于实现侵入式链表
    Promise* next_{};
    friend class CoroQueue;
    friend class FinalAwaiter;
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
    friend class FinalAwaiter;
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
        auto final_suspend() noexcept { return FinalAwaiter{}; };
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
        static_cast<Coroutine<T>*>(awaiter_)->set_value(std::move(value));
    }
}

template <> class Coroutine<void> : public CoroutineBase
{
  public:
    class promise_type : public Promise
    {
      public:
        auto final_suspend() noexcept { return FinalAwaiter{}; };
        auto get_return_object() -> Coroutine<void>;
        void return_void() {}
    };
    Coroutine() = default;
    Coroutine(promise_type* promise) : CoroutineBase(promise) {}
    void await_resume() {}
};

inline auto Coroutine<>::promise_type::get_return_object() -> Coroutine<void> { return Coroutine<void>(this); }

class MainFinalAwaiter
{
  public:
    MainFinalAwaiter(int value) : value_(value) {}
    bool await_ready() const noexcept { return false; }
    template <typename P> auto await_suspend(std::coroutine_handle<P> handle) const noexcept
    {
        handle.destroy();
        std::exit(value_); // 直接退出程序，或者根据需要执行其他清理逻辑
    }
    void await_resume() const noexcept {}

  private:
    int value_;
};
class MainCoroutine : public CoroutineBase
{
  public:
    class promise_type : public Promise
    {
      public:
        auto final_suspend() noexcept { return MainFinalAwaiter{value_}; };
        auto get_return_object() -> MainCoroutine;
        int return_value(int value) { return value_; }
        int value_;
    };
    MainCoroutine() = default;
    MainCoroutine(promise_type* promise) : CoroutineBase(promise) {}
    void await_resume() {}
};
inline auto MainCoroutine::promise_type::get_return_object() -> MainCoroutine { return MainCoroutine(this); }
// 侵入式链表
class CoroQueue
{
  private:
    Promise* head_{};
    Promise* tail_{};
    size_t size_{};

  public:
    CoroQueue() = default;
    void push(Promise* promise)
    {
        if (tail_)
        {
            tail_->next_ = promise;
        }
        else
        {
            head_ = promise;
        }
        tail_ = promise;
        ++size_;
    }
    Promise* pop()
    {
        if (!head_)
        {
            return nullptr;
        }
        auto promise = head_;
        head_ = head_->next_;
        if (!head_)
        {
            tail_ = nullptr;
        }
        --size_;
        return promise;
    }
    bool empty() const { return size_ == 0; }
    size_t size() const { return size_; }
};
} // namespace utils
