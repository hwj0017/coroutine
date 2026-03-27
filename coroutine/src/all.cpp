#include "coroutine/coroutine.h"
#include "coroutine/cospawn.h"
#include "coroutine/syscall.h"
#include "schedule.h"
#include "scheduler.h"
#include "simplescheduler.h"
#include <iostream>
namespace utils
{

auto& instance() { return Scheduler::instance(); }

void co_spawn(Promise* call, bool yield)
{
    auto& scheduler = instance();
    scheduler.co_spawn(call, yield);
}

void schedule() { instance().schedule(); }

template <typename T> bool process(T* awaiter) { return instance().get_io_context().process(awaiter); }

template bool process(ConnectAwaiter* awaiter);
template bool process(AcceptAwaiter* awaiter);
template bool process(DelayAwaiter* awaiter);
template bool process(ReadAwaiter* awaiter);
template bool process(WriteAwaiter* awaiter);

} // namespace utils