#pragma once
#include "coroutine/coroutine.h"
#include "coroutine/handle.h"
#include "iocontext.h"
#include <atomic>
#include <cstddef>
#include <queue>
#include <thread>
#include <vector>
namespace utils
{
class SimpleScheduler
{
  public:
    SimpleScheduler() = default;
    ~SimpleScheduler() = default;
    void co_spawn(Handle coro, bool yield = false) { coros_.push(coro); }
    void co_spawn(std::vector<Handle>&& coro)
    {
        for (auto& coro : coro)
        {
            coros_.push(coro);
        }
    }
    auto& get_io_context() { return iocontext_; }
    void schedule()
    {
        constexpr size_t poll_interval = 128;
        while (true)
        {
            size_t resume_count = 0;
            while (!is_stopped_ && !coros_.empty())
            {
                auto coro = coros_.front();
                coros_.pop();
                coro.resume();
                if (iocontext_.has_work() && ++resume_count >= poll_interval)
                {

                    if (auto coros = iocontext_.poll(false); !coros.empty())
                    {
                        co_spawn(std::move(coros));
                    }
                    resume_count = 0;
                }
            }
            if (iocontext_.has_work())
            {
                if (auto coros = iocontext_.poll(true); !coros.empty())
                {
                    co_spawn(std::move(coros));
                }
            }
        }
    }
    void release() {}
    static SimpleScheduler& instance()
    {
        static SimpleScheduler scheduler;
        return scheduler;
    }

  private:
    std::queue<Handle> coros_;
    std::atomic<bool> is_stopped_ = false;
    IOContext iocontext_;
};

} // namespace utils