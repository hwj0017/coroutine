#include "schedulerinterface.h"
#include "scheduler.h"
#include "simplescheduler.h"
namespace utils
{

auto SchedulerInterface::instance() -> SchedulerInterface&
{
    static Scheduler scheduler(Scheduler::max_procs, Scheduler::max_machines);
    return scheduler;
}
} // namespace utils