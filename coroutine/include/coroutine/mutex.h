#pragma once
#include "coroutine/cospawn.h"
#include <atomic>
#include <mutex>
#include <queue>
namespace utils
{
class LockAwaiter;
class LockGuard;
class Mutex
{
  public:
    auto lock() -> LockAwaiter;
    void unlock();
    auto guard() -> LockAwaiter;

  private:
    bool lock_impl(LockAwaiter* awaiter);
    std::mutex mtx_;
    bool locked_ = false;
    std::queue<LockAwaiter*> waiters_;
    friend class LockAwaiter;
};

class LockGuard
{
  public:
    ~LockGuard()
    {
        if (m_)
        {
            m_->unlock();
        }
    }

  private:
    LockGuard(Mutex* m) : m_(m) {}

    Mutex* m_;
    friend class LockAwaiter;
};

class LockAwaiter
{
  private:
    friend class Mutex;
    Mutex* m_;
    bool guard_;
    Handle handle_;

  public:
    LockAwaiter(Mutex* m, bool guard) : m_(m), guard_(guard) {}
    LockAwaiter(const LockAwaiter&) = delete;
    LockAwaiter& operator=(const LockAwaiter&) = delete;
    LockAwaiter(LockAwaiter&& other) = delete;
    LockAwaiter& operator=(LockAwaiter&& other) = delete;
    bool await_ready() const noexcept { return false; }
    bool await_suspend(Handle handle) noexcept
    {
        handle_ = handle;
        if (m_->lock_impl(this))
        {
            return false;
        }
        return true;
    }
    auto await_resume() const noexcept -> LockGuard { return LockGuard(guard_ ? m_ : nullptr); }
    auto set_value() { return handle_; }
};

inline auto Mutex::lock() -> LockAwaiter { return LockAwaiter(this, false); }
inline auto Mutex::guard() -> LockAwaiter { return LockAwaiter(this, true); }
inline bool Mutex::lock_impl(LockAwaiter* awaiter)
{
    std::lock_guard<std::mutex> lock(mtx_);
    if (locked_)
    {
        waiters_.push(awaiter);
        return false;
    }
    locked_ = true;
    return true;
}
inline void Mutex::unlock()
{
    LockAwaiter* awaiter = nullptr;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        locked_ = false;
        if (!waiters_.empty())
        {
            awaiter = waiters_.front();
            waiters_.pop();
        }
    }
    if (awaiter)
    {
        if (auto handle = awaiter->set_value())
        {
            co_spawn(handle);
        }
    }
}
} // namespace utils