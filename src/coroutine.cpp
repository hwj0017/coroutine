#include "coroutine/coroutine.h"
#include "schedulerinterface.h"
namespace utils
{

void Coroutine::operator()() { SchedulerInterface::instance().add_coroutine(*this); }

} // namespace utils