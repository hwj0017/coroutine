#pragma once
#include "coroutine/coroutine.h"
#include "coroutine/cospawn.h"
#include "coroutine/intrusivelist.h"
#include <atomic>
#include <coroutine>
#include <mutex>
#include <vector>

namespace utils
{
class WaitGroup
{

  public:
    class Waiter;
    void add(int delta) { counter_.fetch_add(delta, std::memory_order_relaxed); }

    void done()
    {
        if (counter_.fetch_sub(1, std::memory_order_acq_rel) == 1)
        {
            // 计数器归零，唤醒所有等待者
            resume_all();
        }
    }

    // 要求所有add在wait之前完成
    auto wait() -> Waiter;

  private:
    std::atomic<int> counter_{0};
    std::atomic<Waiter*> waiters_{nullptr}; // 等待者链表头
    void resume_all();
};
class WaitGroup::Waiter : public IntrusiveListNode
{
  private:
    WaitGroup& wg_;
    Promise* promise_{nullptr};
    friend class WaitGroup;

  public:
    Waiter(WaitGroup& wg) : wg_(wg) {}
    // 1. 检查是否需要挂起：如果计数器已经是 0，直接放行，不挂起
    bool await_ready() const noexcept { return wg_.counter_.load(std::memory_order_acquire) == 0; }

    // 2. 挂起逻辑：编译器打包好当前协程的句柄 (h) 传进来
    template <typename Promise> bool await_suspend(std::coroutine_handle<Promise> h) noexcept
    {
        promise_ = &h.promise();
        while (true)
        {
            if (wg_.counter_.load(std::memory_order_acquire) == 0)
            {
                return false; // 返回 false 表示取消挂起，立即恢复执行
            }
            auto head = wg_.waiters_.load();
            if (next_ = head; wg_.waiters_.compare_exchange_strong(head, this))
            {
                return true;
            }
        }
    }

    // 3. 恢复后的收尾工作：这里不需要做什么
    void await_resume() const noexcept {}
    auto set_value() { return promise_; }
};

class DoneGuard
{
  private:
    WaitGroup& wg_;

  public:
    DoneGuard(WaitGroup& wg) : wg_(wg) {}
    ~DoneGuard() { wg_.done(); }
};
inline void WaitGroup::resume_all()
{
    {
        // 原子取出整个链表
        Waiter* curr = waiters_.exchange(nullptr, std::memory_order_acquire);
        while (curr)
        {
            auto next = static_cast<Waiter*>(curr->next_);
            if (auto promise = curr->set_value(); promise)
            {
                co_spawn(promise);
            }
            curr = next;
        }
    }
}
inline auto WaitGroup::wait() -> Waiter { return Waiter(*this); }
} // namespace utils
