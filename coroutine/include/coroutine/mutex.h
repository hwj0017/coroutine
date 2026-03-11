#pragma once
#include "coroutine/coroutine.h"
#include "coroutine/cospawn.h"
#include "coroutine/syscall.h"
#include <atomic>
#include <functional>
#include <mutex>
#include <queue>
namespace utils
{

class Mutex
{
  public:
    class Awaiter;
    class LockAwaiter;
    class GuardAwaiter;
    class UniqueAwaiter;

    auto lock() -> LockAwaiter;
    void unlock();
    auto guard() -> GuardAwaiter;
    auto unique() -> UniqueAwaiter;

  private:
    bool lock_impl(Awaiter& awaiter);

    std::mutex mtx_;
    bool locked_ = false;
    std::queue<Awaiter*> waiters_;
    friend class ConditionVariable;
};

class ConditionVariable
{
    class WaitAwaiter;
    ConditionVariable(Mutex& mutex) : mutex_(mutex) {}
    void notify_one() {}
    void notify_all() {}

  private:
    bool wait_impl(WaitAwaiter& awaiter);
    Mutex& mutex_;
    std::queue<WaitAwaiter*> waiters_;
};

class Mutex::Awaiter
{
  public:
    Awaiter(Mutex& m) : m_(m) {}
    virtual ~Awaiter() = default; // 包含虚函数，必须有虚析构
    bool await_ready() const noexcept { return false; }

    auto set_value() { return promise_; }
    // 【核心钩子】允许子类在接管锁之前做最后一次检查
    virtual bool try_claim_lock() { return true; }

  protected:
    Mutex& m_;
    Promise* promise_;
};

class Mutex::LockAwaiter : public Mutex::Awaiter
{
  public:
    LockAwaiter(Mutex& m) : Mutex::Awaiter(m) {}
    LockAwaiter(const LockAwaiter&) = delete;
    LockAwaiter& operator=(const LockAwaiter&) = delete;
    LockAwaiter(LockAwaiter&& other) = delete;
    LockAwaiter& operator=(LockAwaiter&& other) = delete;
    template <typename Promise> bool await_suspend(std::coroutine_handle<Promise> handle) noexcept
    {
        promise_ = &handle.promise();
        if (m_.lock_impl(*this))
        {
            return false;
        }
        return true;
    }
    void await_resume() const noexcept {}
};

class Mutex::GuardAwaiter : public Mutex::Awaiter
{
  public:
    class Guard
    {
      public:
        Guard(Mutex& m) : m_(m) {}
        ~Guard() { m_.unlock(); }

      private:
        Mutex& m_;
    };
    GuardAwaiter(Mutex& m) : Mutex::Awaiter(m) {}
    GuardAwaiter(const GuardAwaiter&) = delete;
    GuardAwaiter& operator=(const GuardAwaiter&) = delete;
    GuardAwaiter(GuardAwaiter&& other) = delete;
    GuardAwaiter& operator=(GuardAwaiter&& other) = delete;
    template <typename Promise> bool await_suspend(std::coroutine_handle<Promise> handle) noexcept
    {
        promise_ = &handle.promise();
        if (m_.lock_impl(*this))
        {
            return false;
        }
        return true;
    }
    auto await_resume() const noexcept { return Guard{m_}; }
};

// 【关键设计】必须继承 Mutex::Awaiter，因为我们要把它转移进 Mutex 队列
class ConditionVariable::WaitAwaiter : public Mutex::Awaiter
{
  public:
    WaitAwaiter(ConditionVariable& cv, std::function<bool()> p)
        : Mutex::Awaiter(cv.mutex_), cv_(cv), predicate_(std::move(p))
    {
    }

    // 第一次 co_await 时的检查。如果条件已满足，根本不挂起，直接放行
    bool await_ready() const { return predicate_ ? predicate_() : false; }

    template <typename Promise> bool await_suspend(std::coroutine_handle<Promise> handle) noexcept
    {
        this->promise_ = &handle.promise();

        { // 1. 先把自己加入 CV 的等待队列
            std::lock_guard<std::mutex> lock(cv_.mutex_.mtx_);
            cv_.waiters_.push(this);
        }

        // 2. 然后解锁绑定的 Mutex 锁（这会自动唤醒在 Mutex 上排队的其他协程）
        cv_.mutex_.unlock();

        return true; // 挂起当前协程
    }

    void await_resume() const noexcept {}

    bool try_claim_lock() override
    {
        if (!predicate_)
            return true; // 没有条件限制，直接抢锁成功
        if (predicate_())
            return true; // 条件满足，抢锁成功

        // 条件不满足！我们必须拒绝接管这把锁，并把自己重新放回 CV 队列排队
        std::lock_guard<std::mutex> lock(cv_.mutex_.mtx_);
        cv_.waiters_.push(this);
        return false;
    }

  private:
    ConditionVariable& cv_;
    std::function<bool()> predicate_;
};
inline auto Mutex::lock() -> LockAwaiter { return LockAwaiter(*this); }
inline auto Mutex::guard() -> GuardAwaiter { return GuardAwaiter(*this); }
inline bool Mutex::lock_impl(Awaiter& awaiter)
{
    std::lock_guard<std::mutex> lock(mtx_);
    if (locked_)
    {
        waiters_.push(&awaiter);
        return false;
    }
    locked_ = true;
    return true;
}
inline bool ConditionVariable::wait_impl(WaitAwaiter& awaiter)
{
    waiters_.push(&awaiter);
    return false;
}
inline void Mutex::unlock()
{
    Awaiter* awaiter_to_resume = nullptr;

    mtx_.lock();
    locked_ = false; // 假设我们能成功释放锁

    while (!waiters_.empty())
    {
        awaiter_to_resume = waiters_.front();
        waiters_.pop();

        locked_ = true; // 试探性地把锁移交给这个协程
        mtx_.unlock();  // 必须在此处释放自旋锁，防止 Predicate 里的业务代码死锁！

        bool accepted = false;
        try
        {
            // 让协程评估自己是否真的要这把锁（判断 Predicate）
            accepted = awaiter_to_resume->try_claim_lock();
        }
        catch (...)
        {
            mtx_.lock();
            locked_ = false;
            mtx_.unlock();
            throw; // 防止用户传进来的 predicate 抛出异常导致锁状态崩坏
        }

        if (accepted)
        {
            break; // 完美，移交成功，跳出循环去唤醒它
        }
        else
        {
            // 被拒绝了！该协程已经把自己塞回 CV 队列。
            awaiter_to_resume = nullptr;
            mtx_.lock(); // 重新拿到自旋锁，准备询问 Mutex 队列里的下一个协程
            locked_ = false;
        }
    }

    if (!awaiter_to_resume)
    {
        mtx_.unlock(); // 循环结束还没人要锁，彻底解锁
    }

    // 真正把协程丢入事件循环去执行（此时它已经稳稳持有了 Mutex 锁）
    if (awaiter_to_resume)
    {
        if (auto handle = awaiter_to_resume->set_value(); handle)
        {
            co_spawn(handle);
        }
    }
}
} // namespace utils