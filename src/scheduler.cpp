#include "scheduler.h"
#include "coroutine/coroutine.h"
#include <algorithm>
#include <any>
#include <atomic>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>
namespace utils
{
Scheduler::Scheduler(int max_procs, int max_machines)
    : max_procs_(max_procs), max_machines_(max_machines), processors_(create_processors(max_procs))
{
    assert(max_procs_ >= 1 && max_machines_ >= max_procs_);
    current_machine_ = main_machine_.get();
    main_machine_->set_processor(processors_[0].get());
    {
        std::lock_guard<std::mutex> lock(global_mtx_);
        for (int i = 1; i < processors_.size(); ++i)
        {
            idle_processors_.push(processors_[i].get());
        }
    }
    idle_processor_count_ = processors_.size() - 1;
}
Scheduler::~Scheduler()
{
    if (stopped_.exchange(true))
    {
        return;
    }
    for (auto& machine : machines_)
    {
        machine->resume();
    }
}

// 开始运行
void Scheduler::schedule()
{
    if (!stopped_.exchange(false))
    {
        return;
    }
    // 启动主M
    main_machine_->start([this, machine = main_machine_.get()] { machine_func(machine); });
}

void Scheduler::add_coroutine(Coroutine& coro)
{
    bool need_notify = false;
    // 获取当前P
    if (current_machine_ && current_machine_->processor())
    {
        // 优先放入当前P的
        auto processor = current_machine_->processor();
        coro = processor->exchange_run_next(coro);
        if (coro)
        {
            need_notify = true;
            if (!processor->add_coroutine(std::move(coro)))
            {
                auto coros = processor->get_half_coroutines();
                coros.push_back(std::move(coro));
                std::lock_guard<std::mutex> lock(global_mtx_);
                add_global_coroutine(std::move(coros));
            }
        }
    }
    else
    {
        need_notify = true;
        std::lock_guard<std::mutex> lock(global_mtx_);
        add_global_coroutine(std::move(coro));
    }
    if (need_notify)
    {
        notify();
    }
}

void Scheduler::notify()
{
    if (idle_processor_count_.load() == 0)
    {
        return;
    }
    int expected = 0;
    if (spinning_machines_count_.load() != 0 || !spinning_machines_count_.compare_exchange_strong(expected, 1))
    {
        return;
    }

    Processor* resumed_processor = nullptr;
    Machine* resumed_machine = nullptr;

    {
        std::lock_guard<std::mutex> lock(global_mtx_);
        if (resumed_processor = get_idle_processor(); !resumed_processor)
        {
            return;
        }
        if (resumed_machine = get_idle_machine(); !resumed_machine)
        {
            add_idle_processor(resumed_processor);
            return;
        }
    }

    resumed_processor->set_spinning(true);
    resumed_machine->set_processor(resumed_processor);
    resumed_machine->resume();
}

void Scheduler::machine_func(Machine* machine)
{
    current_machine_ = machine;
    while (!stopped())
    {
        if (!current_machine_->processor())
        {
            current_machine_->sleep();
        }
        else
        {
            auto coro = get_coroutine(current_machine_->processor());
            if (coro)
            {
                std::cout << " resume " + std::to_string(coro.get_id()) + " machine " +
                                 std::to_string(current_machine_->id()) + " processor " +
                                 std::to_string(current_machine_->processor()->id()) + "\n";
                coro.resume();
                // 执行任务中可能解绑P
            }
            else
            {
                std::lock_guard<std::mutex> lock(global_mtx_);
                add_idle_processor(current_machine_->processor());
                add_idle_machine(current_machine_);
                current_machine_->set_processor(nullptr);
            }
        }
    }
}

auto Scheduler::get_coroutine(Processor* processor) -> Coroutine
{

    Coroutine coro;
    if (!processor->spinning())
    { // 从p获取任务
        if (coro = processor->get_coroutine(); coro)
        {
            return coro;
        }
        // 开始自旋
        if (auto count = spinning_machines_count_.load();
            count + 1 <= (max_procs_ - idle_processor_count_.load()) / 2 &&
            spinning_machines_count_.compare_exchange_strong(count, count + 1))
        {
            processor->set_spinning(true);
        }
        else
        {
            return {};
        }
    }
    std::vector<Coroutine> coros;
    int spinning_epoch = 0;
    while (!stopped() && spinning_epoch < max_spinning_epoch)
    {
        // 从全局队列获取任务
        {
            std::lock_guard<std::mutex> lock(global_mtx_);
            coros = get_global_coroutine();
        }
        if (!coros.empty())
        {
            break;
        }

        int rand_idx = randomer_.random(0, max_procs_);
        for (int i = 0; i < max_procs_; i++)
        {
            if (processors_[i + rand_idx].get() == processor)
            {
                continue;
            }
            if (coros = processors_[i]->steal_coroutine(); !coros.empty())
            {
                std::string debug = " ";
                for (auto& c : coros)
                {
                    debug += " " + std::to_string(c.get_id());
                }
                std::cout << " steal " + std::to_string(coros.size()) + debug + "\n";
                break;
            }
        }
        if (!coros.empty())
        {
            break;
        }
        ++spinning_epoch;
        std::this_thread::yield();
    }

    processor->set_spinning(false);
    spinning_machines_count_.fetch_sub(1);

    if (!coros.empty())
    {
        coro = std::move(coros.back());
        coros.pop_back();
        processor->add_coroutine(std::move(coros));
    }
    return coro;
}

// void Scheduler::release_processor()
// {
//     bool need_notify = false;
//     auto id = std::this_thread::get_id();
//     {
//         std::lock_guard<std::mutex> lock(global_mtx_);
//         if (auto it = machine_map_.find(id); it != machine_map_.end())
//         {
//             if (auto processor = std::move(it->second->processor()); processor)
//             {
//                 if (processor->has_coroutine() || !global_coros_.empty())
//                 {
//                     pending_processors_.push(std::move(processor));
//                     need_notify = true;
//                 }
//                 else
//                 {
//                     idle_processors_.push(std::move(processor));
//                 }
//             }
//         }
//     }
//     if (need_notify)
//     {
//         if (idle_machine_count_.load() > 0)
//         {
//             global_cv_.notify_one();
//         }
//         else if (machine_count_.fetch_add(1) < max_machines_)
//         {
//             // 所有M均在运行，新建M
//             machines_.push_back(std::make_unique<Machine>());
//             machines_.back()->start([this, machine = machines_.back().get()] { machineFunc(machine); });
//         }
//         else
//         {
//             machine_count_.fetch_sub(1);
//         }
//     }
// }

// void Scheduler::bind_processor() {}
void Scheduler::add_global_coroutine(Coroutine&& coro) { global_coros_.push(std::move(coro)); }
void Scheduler::add_global_coroutine(std::vector<Coroutine>&& coros)
{
    for (auto& coro : coros)
    {
        global_coros_.push(std::move(coro));
    }
}

auto Scheduler::get_global_coroutine() -> std::vector<Coroutine>
{
    if (global_coros_.empty())
    {
        return {};
    }
    std::vector<Coroutine> coros;
    size_t max_size = std::min(global_coros_.size() / max_procs_ + 1, Processor::capacity / 2);
    while (coros.size() < max_size && !global_coros_.empty())
    {
        coros.push_back(std::move(global_coros_.front()));
        global_coros_.pop();
    }
    return coros;
}

auto Scheduler::get_idle_processor() -> Processor*
{
    if (idle_processors_.empty())
    {
        return nullptr;
    }
    auto processor = idle_processors_.front();
    idle_processors_.pop();
    idle_processor_count_.fetch_sub(1);
    return processor;
}

auto Scheduler::get_idle_machine() -> Machine*
{
    if (idle_machines_.empty())
    {
        if (machines_.size() >= max_machines_)
        {
            return nullptr;
        }
        machines_.push_back(std::make_unique<Machine>());
        machines_.back()->start([this, machine = machines_.back().get()] { machine_func(machine); });
        return machines_.back().get();
    }
    auto machine = idle_machines_.front();
    idle_machines_.pop();
    idle_machine_count_.fetch_sub(1);
    return machine;
}

void Scheduler::add_idle_processor(Processor* p)
{
    idle_processors_.push(p);
    idle_processor_count_.fetch_add(1);
}

void Scheduler::add_idle_machine(Machine* m)
{
    idle_machines_.push(m);
    idle_machine_count_.fetch_add(1);
}

bool Processor::add_coroutine(Coroutine&& coro) { return coros_.push_back(std::move(coro)); }
// 批量添加任务
bool Processor::add_coroutine(std::vector<Coroutine>&& coros) { return coros_.push_back(coros); }
auto Processor::get_coroutine() -> Coroutine
{
    if (run_next_)
    {
        return std::exchange(run_next_, {});
    }
    return coros_.pop_back();
}

auto Processor::steal_coroutine() -> std::vector<Coroutine> { return coros_.pop_front(coros_.size() / 2); }

} // namespace utils