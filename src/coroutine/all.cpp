#include "coroutine/cospawn.h"
#include "coroutine/syscall.h"
#include "schedule.h"
#include "simplescheduler.h"
namespace utils
{

auto instance() -> SimpleScheduler& { return SimpleScheduler::instance(); }

void co_spawn(Handle handle, bool yield) { instance().co_spawn(handle, yield); }

void release() { instance().release(); }

void schedule() { instance().schedule(); }

template <typename T> bool process(T* awaiter) { return instance().get_io_context().process(awaiter); }

template bool process(AcceptAwaiter* awaiter);
template bool process(DelayAwaiter* awaiter);
template bool process(ReadAwaiter* awaiter);
template bool process(WriteAwaiter* awaiter);

} // namespace utils