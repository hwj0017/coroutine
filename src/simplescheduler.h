#pragma once
#include "coroutine/coroutine.h"
#include "schedulerinterface.h"
#include <queue>
namespace utils
{
class SimpleScheduler : public SchedulerInterface
{
  public:
    SimpleScheduler() = default;
    ~SimpleScheduler() override = default;
    void add_coroutine(Coroutine& coro) override { coros_.push(std::move(coro)); }
    void schedule() override
    {
        while (!is_stopped_ && !coros_.empty())
        {
            auto coro = std::move(coros_.front());
            coros_.pop();
            coro.resume();
        }
    }
    static SimpleScheduler& instance()
    {
        static SimpleScheduler scheduler;
        return scheduler;
    }

  private:
    std::queue<Coroutine> coros_;
    bool is_stopped_ = false;
};

} // namespace utils