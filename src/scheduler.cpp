#include "coroutine/scheduler.h"
#include "coroutine/coroutine.h"
#include <cassert>
#include <cstddef>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>
namespace utils
{
Scheduler::Scheduler(int max_procs, int max_machines) : max_procs_(max_procs), max_machines_(max_machines)
{
    assert(max_procs_ > 0 && max_machines_ > 0 && max_machines_ >= max_procs_);
    // 初始化P
    for (int i = 0; i < max_procs_; i++)
    {
        processors_.push_back(std::make_unique<Processor>());
        machines_.push_back(std::make_unique<Machine>());
    }
}
Scheduler::~Scheduler() { stop(); }
void Scheduler::schedule(Coroutine coro)
{
    auto id = std::this_thread::get_id();
    if (auto it = processor_map_.find(id); it != processor_map_.end())
    {
        it->second->addCoroutine(coro);
    }
    else
    {
        std::lock_guard<std::mutex> lock(global_mtx_);
        global_q_.push(coro);
    }
}
// 开始运行
void Scheduler::start()
{
    for (auto& machine : machines_)
    {
        machine->start([this, &machine] { machineFunc(machine.get()); });
    }
}
void Scheduler::stop()
{
    if (is_stopped.exchange(true))
    {
        return;
    }
    for (auto& machine : machines_)
    {
        machine->stop();
    }
    std::lock_guard<std::mutex> lock(global_mtx_);
    // 清理全局队列
    while (!global_q_.empty())
    {
        auto coro = global_q_.front();
        global_q_.pop();
        coro.destroy();
    }
}

void Scheduler::release_processor()
{
    auto id = std::this_thread::get_id();
    std::lock_guard<std::mutex> lock(global_mtx_);
    if (auto it = processor_map_.find(id); it != processor_map_.end())
    {
        idle_processors_.push_back(it->second);
        processor_map_.erase(it);
        if (!idle_machines_.empty())
        {
            idle_machines_.back()->wakeup();
        }
        else if (machines_.size() < max_machines_)
        {

            machines_.push_back(std::make_unique<Machine>());
            auto machine = machines_.back().get();
            machine->start([this, machine] { machineFunc(machine); });
        }
    }
}

auto Scheduler::getGlobalCoroutine() -> std::vector<Coroutine>
{
    std::lock_guard<std::mutex> lock(global_mtx_);
    if (global_q_.empty())
    {
        return {};
    }
    std::vector<Coroutine> coros;
    size_t max_size = global_q_.size() / max_procs_ + 1;
    while (coros.size() < max_size)
    {
        coros.push_back(std::move(global_q_.front()));
        global_q_.pop();
    }
    return coros;
}
void Scheduler::machineFunc(Machine* machine)
{
    auto spinning_epoch = 0;
    while (!machine->is_stopped())
    {
        if (!machine->processor())
        {
            // 获取一个P绑定
            {
                std::lock_guard<std::mutex> lock(global_mtx_);
                for (int i = 0; i < idle_processors_.size(); i++)
                {
                    if (!idle_processors_[i]->try_run())
                    {
                        idle_processors_.erase(idle_processors_.begin() + i);
                        processor_map_[std::this_thread::get_id()] = idle_processors_[i];
                        break;
                    }
                }
            }
            if (!machine->processor())
            {
                if (spinning_epoch++ >)
                    machine->sleep();
            }
        }
        else
        {
            // 获取任务
            if (auto coro = machine->processor()->getCoroutine(); coro)
            {
                // 从P的队列获取任务
                coro.resume();
            }
            else if (auto global_coros = getGlobalCoroutine(); global_coros.size() > 0)
            {
                // 从全局队列获取任务
                auto coro = global_coros.back();
                global_coros.pop_back();
                machine->processor()->addCoroutine(global_coros);
                // 当前p无任务执行完，解绑P
                {
                    std::lock_guard<std::mutex> lock(global_mtx_);
                    machine->set_processor(nullptr);
                    idle_processors_.push_back(machine->processor());
                    processor_map_.erase(std::this_thread::get_id());
                }
                machine->sleep();
            }
        }
    }
}
// 添加单个任务
void Processor::addCoroutine(Coroutine coro)
{
    // 优先放入next运行
    if (!run_next_)
    {
        run_next_ = coro;
        return;
    }
    runq_.push(std::exchange(run_next_, coro));
}

// 批量添加任务
void Processor::addCoroutine(std::vector<Coroutine>& coros)
{
    for (auto& coro : coros)
    {
        runq_.push(coro);
    }
}
auto Processor::getCoroutine() -> Coroutine
{
    if (run_next_)
    {
        return std::exchange(run_next_, {});
    }
    if (runq_.empty())
    {
        return {};
    }
    auto coro = runq_.front();
    runq_.pop();
    return coro;
}
auto listen_main() -> Coroutine
{
    co_await main_coro();
    utils::Scheduler::instance().stop();
}

} // namespace utils
int main()
{
    utils::listen_main();
    utils::Scheduler::instance().start();
    return 0;
}