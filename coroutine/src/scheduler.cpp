#include "scheduler.h"
#include <atomic>
#include <cassert>
#include <iostream>
#include <mutex>
#include <string>
namespace utils
{

// 开始运行
void Scheduler::schedule()
{
    // 启动主M,去除spinning
    spinning_processors_count_.store(0);
    main_machine_->processor->state = Processor::State::RUNNING;
    running_processor_.fetch_or(1 << main_machine_->processor->id);
    machine_func(main_machine_.get());
}

void Scheduler::co_spawn(Handle coro, bool yield)
{
    // 获取当前P
    if (current_machine_ && current_machine_->processor && !yield)
    {
        // 优先放入当前P的
        add_coro_to_processor({&coro, 1}, current_machine_->processor);
    }
    else
    {
        add_global_coroutine({&coro, 1});
    }
    int expected = 0;
    if (spinning_processors_count_.load() > 0 || !spinning_processors_count_.compare_exchange_strong(expected, 1))
    {
        return;
    }
    need_spinning();
}

void Scheduler::machine_func(Machine* machine)
{
    current_machine_ = machine;
    while (true)
    {
        auto coro = get_coro();
        assert(coro);
        std::cout << "Handle " + std::to_string(promise(coro).get_id()) + " resume in processor " +
                         std::to_string(machine->processor->id) + "\n";
        coro.resume();
    }
}

void Scheduler::test()
{
    auto p = current_machine_->processor;
    std::cout << "get_coro_from_processor run\n";
    if (p->iocontext.has_work())
    {
        auto coros = p->iocontext.poll(true);
        std::cout << "get_coro_from_processor io\n";
        add_coro_to_processor({coros}, p);
    }
}
auto Scheduler::get_coro() -> Handle
{
    // 执行完协程需要判断是否为空
    Processor* processor = current_machine_->processor;
    if (!processor)
    {
        sleep();
    }
    assert(processor);

    while (true)
    {
        switch (processor->state)
        {
        case Processor::State::RUNNING: {
            if (auto coro = get_coro_from_processor(processor); coro)
            {
                return coro;
            }
            // 从掩码中删除
            running_processor_.fetch_and(~(1 << processor->id));

            if (can_spinning())
            {
                processor->state = Processor::State::SPINNING;
            }
            else
            {
                processor->state = Processor::State::NOTFOUND;
            }
            break;
        }
        case Processor::State::SPINNING: {
            Handle coro = get_coro_with_spinning(processor);
            bool last_spinning = (spinning_processors_count_.fetch_sub(1) == 1);
            if (!coro && last_spinning)
            {
                // 如果是最后一个，需要在检查一次（全局队列）
                coro = get_coro_with_spinning(processor);
            }

            if (coro)
            {
                processor->state = Processor::State::RUNNING;
                running_processor_.fetch_or(1 << processor->id);
                return coro;
            }
            processor->state = Processor::State::NOTFOUND;
            break;
        }
        case Processor::State::NOTFOUND: {
            if (processor->iocontext.has_work())
            {
                processor->state = Processor::State::POLLING;
            }
            else if (!waiting_spining_.exchange(true))
            {
                processor->state = Processor::State::WAITINGSPINNING;
            }
            else
            {
                processor->state = Processor::State::IDLE;
            }
            break;
        }
        case Processor::State::WAITINGSPINNING: {
            // 等待自旋
            need_spinning_.wait(false);
            waiting_spining_.store(false);
            if (can_spinning())
            {
                processor->state = Processor::State::SPINNING;
            }
            else
            {
                processor->state = Processor::State::NOTFOUND;
            }
            break;
        }
        case Processor::State::POLLING: {
            // 阻塞监听io
            polling_processor_.fetch_or(1 << processor->id);
            // 设置掩码后需再次考虑need_spinning_, 防止所有P同时进入Polling且得不到唤醒
            if (need_spinning_.exchange(false))
            {
                processor->state = Processor::State::SPINNING;
                polling_processor_.fetch_and(~(1 << processor->id));
                break;
            }
            auto coros = processor->iocontext.poll(true);
            polling_processor_.fetch_and(~(1 << processor->id));
            if (auto it = coros.begin(); it != coros.end())
            {
                auto coro = *it;
                ++it;
                add_coro_to_processor({it, coros.end()}, processor);
                processor->state = Processor::State::RUNNING;
                running_processor_.fetch_or(1 << processor->id);
                return coro;
            }
            if (can_spinning())
            {
                processor->state = Processor::State::SPINNING;
            }
            else
            {
                processor->state = Processor::State::NOTFOUND;
            }
            break;
        }
        case Processor::State::IDLE:
            // 空闲状态，休眠
            sleep();
            // 唤醒后P可能会改变,由唤醒者设置状态
            assert(current_machine_->processor);
            processor = current_machine_->processor;
        }
    }
}
bool Scheduler::can_spinning()
{
    // 先考虑need_spinning_
    if (need_spinning_.exchange(false))
    {
        return true;
    }
    // 考虑自旋数
    else if (auto count = spinning_processors_count_.load(); 2 * count <= idle_processor_count_.load())
    {
        // 自旋
        spinning_processors_count_.fetch_add(1);
        return true;
    }
    return false;
}
void Scheduler::release()
{
    if (!current_machine_ && !current_machine_->processor)
    {
        return;
    }
    auto processor = current_machine_->processor;
    current_machine_->processor = nullptr;
    // 判断是否有任务
    if (!processor->run_next.load() && processor->coros.empty())
    {
        // 是否需要自旋
        if (need_spinning_.exchange(false))
        {
            processor->state = Processor::State::SPINNING;
        }
        // 是否需要等待自旋
        else if (!waiting_spining_.exchange(true))
        {
            processor->state = Processor::State::WAITINGSPINNING;
        }
        else
        {
            std::lock_guard<std::mutex> lock(global_mtx_);
            add_idle_processor(processor);
            return;
        }
    }
    resume_processor(processor);
}

void Scheduler::need_spinning()
{
    // need_spinning配合waiting_spining保证不会遗漏任务，不会存在所有P同时进入空闲状态的极端情况
    need_spinning_.store(true);
    // 以下将尽量保证至少有一个P处于自旋状态
    // 如果等待自旋P，直接唤醒，哪怕已经唤醒
    if (waiting_spining_.load())
    {
        need_spinning_.notify_one();
        return;
    }
    // 将空闲P置为自旋状态并运行
    if (idle_processor_count_.load() > 0)
    {
        Processor* idle_processor = nullptr;
        Machine* idle_machine = nullptr;
        bool is_new_machine = false;
        {
            std::lock_guard<std::mutex> lock(global_mtx_);
            if (!idle_processors_.empty() && need_spinning_.exchange(false))
            {
                idle_processor = get_idle_processor();
                if (idle_machine = get_idle_machine(); !idle_machine)
                {
                    machines_.push_back(std::make_unique<Machine>());
                    idle_machine = machines_.back().get();
                    is_new_machine = true;
                }
            }
        }
        if (idle_processor)
        {
            idle_processor->state = Processor::State::SPINNING;
            idle_machine->processor = idle_processor;
            if (is_new_machine)
            {
                idle_machine->start([this, idle_machine] { machine_func(idle_machine); });
            }
            else
            {
                wake(idle_machine);
            }
            return;
        }
    }
    // 唤醒polling中的P
    // 获取掩码最低位的1
    auto mask = polling_processor_.load();
    auto low_1bit = mask & (~mask + 1);
    auto new_mask = mask & (~low_1bit);

    if (low_1bit && polling_processor_.compare_exchange_strong(mask, new_mask))
    {
        // 获取第一个P的索引
        auto index = __builtin_ctz(low_1bit);
        wake_from_polling(processors_[index].get());
    }
}
void Scheduler::resume_processor(Processor* processor)
{
    Machine* idle_machine = nullptr;
    bool is_new_machine = false;
    {
        std::lock_guard<std::mutex> lock(global_mtx_);
        if (idle_machine = get_idle_machine(); !idle_machine)
        {
            machines_.push_back(std::make_unique<Machine>());
            idle_machine = machines_.back().get();
            is_new_machine = true;
        }
    }
    idle_machine->processor = processor;
    if (is_new_machine)
    {
        idle_machine->start([this, idle_machine] { machine_func(idle_machine); });
    }
    else
    {
        wake(idle_machine);
    }
    return;
}

void Scheduler::add_global_coroutine(std::span<Handle> coros)
{
    std::lock_guard<std::mutex> lock(global_coros_mtx_);
    for (auto& coro : coros)
    {
        global_coros_.push(std::move(coro));
    }
}

void Scheduler::add_coro_to_processor(std::span<Handle> coros, Processor* processor)
{
    if (auto it = coros.begin(); it != coros.end())
    {
        // 优先放入run_next
        auto old_run_next = processor->run_next.exchange(*it);
        if (old_run_next)
        {
            *it = old_run_next;
        }
        else
        {
            ++it;
        }
        if (it != coros.end())
        {
            auto res = processor->coros.push_back({it, coros.end()});
            it += res;
            if (it != coros.end())
            {
                add_global_coroutine({it, coros.end()});
            }
        }
    }
}

auto Scheduler::get_coro_from_processor(Processor* processor) -> Handle
{
    // 优先从run_next获取
    if (auto coro = processor->run_next.exchange({}); coro)
    {
        return coro;
    }

    // 需要重试保证本地队列取完
    while (!processor->coros.empty())
    {
        if (auto coro = processor->coros.pop_front(); coro)
        {
            return coro;
        }
    }

    if (processor->iocontext.has_work())
    {
        auto coros = processor->iocontext.poll(false);
        if (auto it = coros.begin(); it != coros.end())
        {
            auto coro = *it;
            ++it;
            add_coro_to_processor({it, coros.end()}, processor);
            return coro;
        }
    }
    return {};
}

auto Scheduler::get_coro_with_spinning(Processor* processor) -> Handle
{
    std::vector<Handle> coros;
    if (coros = get_global_coroutine(max_local_queue_size / 2); coros.empty())
    {
        if (coros = steal_coroutine(processor); coros.empty() && processor->iocontext.has_work())
        {
            coros = processor->iocontext.poll(false);
        }
    }
    if (auto it = coros.begin(); it != coros.end())
    {
        auto coro = *it;
        ++it;
        add_coro_to_processor({it, coros.end()}, processor);
        return coro;
    }
    return {};
}

auto Scheduler::get_global_coroutine(size_t max_count) -> std::vector<Handle>
{
    std::vector<Handle> coros;
    std::lock_guard<std::mutex> lock(global_coros_mtx_);
    auto get_coros_size = std::min(std::min(global_coros_.size() / max_procs + 1, global_coros_.size()), max_count);
    coros.reserve(get_coros_size);
    for (int i = 0; i < get_coros_size; i++)
    {
        coros.push_back(global_coros_.front());
        global_coros_.pop();
    }
    return coros;
}

auto Scheduler::steal_coroutine(Processor* processor) -> std::vector<Handle>
{
    int rand_idx = randomer_.random(0, max_procs);
    // TODO:多几轮
    for (int i = 0; i < max_procs; i++)
    {
        auto steal_processor = processors_[(i + rand_idx) % max_procs].get();
        // 只窃取正在运行的P
        if (steal_processor == processor || !(running_processor_.load() & (1 << steal_processor->id)))
        {
            continue;
        }
        if (auto coros = steal_processor->coros.pop_front_half(); !coros.empty())
        {
            std::string debug = " ";
            for (auto& c : coros)
            {
                debug += " " + std::to_string(promise(c).get_id());
            }
            std::cout << " steal " + std::to_string(coros.size()) + debug + "\n";
            return coros;
        }
    }
    for (int i = 0; i < max_procs; i++)
    {
        auto steal_processor = processors_[(i + rand_idx) % max_procs].get();
        if (steal_processor == processor || !(running_processor_.load() & (1 << steal_processor->id)))
        {
            continue;
        }

        if (auto coro = steal_processor->run_next.exchange({}); coro)
        {
            return {coro};
        }
    }
    return {};
}
auto Scheduler::get_idle_machine() -> Machine*
{
    if (idle_machines_.empty())
    {
        return nullptr;
    }
    auto machine = idle_machines_.front();
    idle_machines_.pop();
    return machine;
}
void Scheduler::add_idle_machine(Machine* m) { idle_machines_.push(m); }

void Scheduler::sleep()
{
    assert(current_machine_);
    // 先设置ready再加锁放入空闲队列
    current_machine_->ready.store(false);
    auto processor = current_machine_->processor;
    current_machine_->processor = nullptr;
    {
        std::lock_guard<std::mutex> lock(global_mtx_);
        if (processor)
        {
            std::cout << "sleep " << processor->id << "\n";
            add_idle_processor(processor);
        }
        add_idle_machine(current_machine_);
    }
    current_machine_->ready.wait(false);
}

void Scheduler::wake(Machine* machine)
{
    machine->ready.store(true);
    machine->ready.notify_one();
}

void Scheduler::wake_from_polling(Processor* processor) { processor->iocontext.wake(); }

auto Scheduler::get_io_context() -> IOContext*
{
    assert(current_machine_ && current_machine_->processor);
    return &current_machine_->processor->iocontext;
}

} // namespace utils
