#pragma once

namespace utils
{
class Coroutine;
class SchedulerInterface
{
  public:
    virtual ~SchedulerInterface() = default;
    virtual void schedule() = 0;
    virtual void add_coroutine(Coroutine& coro) = 0;
    static auto instance() -> SchedulerInterface&;
};

} // namespace utils