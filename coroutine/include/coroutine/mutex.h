#pragma once
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
    class WaitAwaiter;
    auto lock() -> LockAwaiter;
    void unlock();
    auto guard() -> GuardAwaiter;
    auto unique() -> UniqueAwaiter;

  private:
    bool lock_impl(Awaiter& awaiter);
    std::mutex mtx_;
    bool locked_ = false;
    std::queue<Awaiter*> waiters_;
    friend class LockAwaiter;
};

class Mutex::Awaiter
{
  public:
    Awaiter(Mutex& m) : m_(m) {}
    ~Awaiter() = default;
    bool await_ready() const noexcept { return false; }
    bool await_suspend(Handle handle) noexcept
    {
        handle_ = handle;
        if (m_.lock_impl(*this))
        {
            return false;
        }
        return true;
    }
    auto set_value() -> std::coroutine_handle<> { return handle_; }

  protected:
    Mutex& m_;
    Handle handle_;
};

class Mutex::LockAwaiter : public Mutex::Awaiter
{
  public:
    LockAwaiter(Mutex& m) : Mutex::Awaiter(m) {}
    LockAwaiter(const LockAwaiter&) = delete;
    LockAwaiter& operator=(const LockAwaiter&) = delete;
    LockAwaiter(LockAwaiter&& other) = delete;
    LockAwaiter& operator=(LockAwaiter&& other) = delete;

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

    auto await_resume() const noexcept { return Guard{m_}; }
};

class Mutex::UniqueAwaiter : public Mutex::Awaiter
{
  public:
    class Unique
    {
      public:
        Unique(Mutex& m) : m_(m) {}

        void unlock() { m_.unlock(); }

        auto lock() { return m_.lock(); }

      private:
        Mutex& m_;
    };
    UniqueAwaiter(Mutex& m) : Mutex::Awaiter(m) {}
    UniqueAwaiter(const UniqueAwaiter&) = delete;
    UniqueAwaiter& operator=(const UniqueAwaiter&) = delete;
    UniqueAwaiter(UniqueAwaiter&& other) = delete;
    UniqueAwaiter& operator=(UniqueAwaiter&& other) = delete;
    void await_resume() const noexcept {}
};
class Mutex::WaitAwaiter
{
  public:
    WaitAwaiter(Mutex& m, std::function<bool()> p) : m_(m), predicate_(p) {}

  private:
    Mutex& m_;
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
inline void Mutex::unlock()
{
    Awaiter* awaiter = nullptr;
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
        if (auto handle = awaiter->set_value(); handle)
        {
            co_spawn(handle);
        }
    }
}
} // namespace utils