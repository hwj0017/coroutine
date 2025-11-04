#include "coroutine/coroutine.h"
#include "coroutine/scheduler.h"
namespace utils {
void Coroutine::InitialAwaiter::await_suspend(
    std::coroutine_handle<promise_type> handle) {
  // 调度
  Scheduler::instance().schedule(Coroutine(handle));
}

} // namespace utils
