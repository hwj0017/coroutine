#pragma once
#include "concurrentdeque.h"
#include "coroutine/icallable.h"
#include "iocontext.h"
#include "randomer.h"
#include <atomic>
#include <cassert>
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <span>
#include <string>
#include <sys/eventfd.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace utils
{
using Handle = ICallable*;
// Processor (P)
class Processor
{
  public:
    enum class State
    {
        RUNNING,
        SPINNING,
        POLLING,
    };
    explicit Processor(size_t id, size_t capacity) : id(id), coros(capacity) {}

    ~Processor() {}
    size_t id{};
    std::thread thread{};
    std::atomic<Handle> run_next{};
    IOContext iocontext{};
    WorkStealingDeque<Handle> coros;
    // 是否自旋
    State state{State::SPINNING};
};
class Scheduler
{
  public:
    Scheduler();
    ~Scheduler() = default;
    void co_spawn(Handle coro, bool yield = false);
    void schedule();
    auto get_io_context() -> IOContext&;
    static auto& instance()
    {
        static Scheduler* scheduler = new Scheduler();
        return *scheduler;
    }
    static const int max_procs = 16;

  private:
    // M执行循环
    void processor_func(Processor* p);
    void resume_processor(Processor* p = nullptr);
    void need_spinning();
    auto get_coro() -> Handle;
    // 将就绪协程加入p
    auto get_global_coroutine(size_t max_count) -> std::vector<Handle>;
    void add_global_coroutine(std::span<Handle> coros);
    void add_coro_to_processor(std::span<Handle> coros, Processor* processor);
    void add_coro_to_processor(Handle coro, Processor* processor, bool yield);
    auto get_coro_from_processor(Processor* processor) -> Handle;
    auto get_coro_with_spinning(Processor* processor) -> Handle;
    auto steal_coroutine(Processor* p) -> std::vector<Handle>;

    void wake_from_idle(Processor* p);
    void wake_from_polling(Processor* p);
    bool can_spinning();
    size_t get_idle_count();
    // 全部P
    const std::vector<std::unique_ptr<Processor>> processors_;
    // 全局队列
    std::deque<Handle> global_coros_{};
    std::mutex global_coros_mtx_{};

    // 全局锁
    std::mutex global_mtx_{};

    // 一些原子变量加快访问速度
    // idle P 掩码
    std::atomic_uint32_t idle_mask_{0};
    // polling P 掩码
    std::atomic_uint32_t polling_mask_{0};
    // Running P 掩码
    std::atomic<uint32_t> running_mask_{0};
    // 自旋P
    std::atomic<int> spinning_processors_count_{0};
    std::atomic<bool> need_spinning_{false};

    static auto create_processors(size_t n) -> std::vector<std::unique_ptr<Processor>>
    {
        std::vector<std::unique_ptr<Processor>> procs;
        procs.reserve(n);
        for (size_t i = 0; i < n; ++i)
        {
            procs.push_back(std::make_unique<Processor>(i, max_local_queue_size));
        }

        return procs;
    }
    static thread_local Processor* current_processor_;
    static thread_local Randomer randomer_;

    // p本地队列最大任务
    static constexpr size_t max_local_queue_size = 256;
    // 最大自旋回合
    static constexpr size_t max_spinning_epoch = 10;
};

inline thread_local Processor* Scheduler::current_processor_{nullptr};
inline thread_local Randomer Scheduler::randomer_{};
inline Scheduler::Scheduler() : processors_(create_processors(max_procs))
{
    assert(max_procs >= 1);
    // 第一个协程不会唤醒
    spinning_processors_count_.store(1);
    // 除去自己
    idle_mask_ = (1 << max_procs) - 2;
    current_processor_ = processors_[0].get();
}

inline void Scheduler::schedule()
{
    // 启动主M,去除spinning
    spinning_processors_count_.store(0);
    processors_[0]->state = Processor::State::RUNNING;
    running_mask_.fetch_or(1 << processors_[0]->id);
    processor_func(processors_[0].get());
}
inline void Scheduler::co_spawn(Handle coro, bool yield)
{
    // 获取当前P
    if (!current_processor_)
    {
        add_global_coroutine({&coro, 1});
    }
    else
    {
        // 优先放入当前P的
        add_coro_to_processor(coro, current_processor_, yield);
    }
    int expected = 0;
    if (spinning_processors_count_.load() > 0 || !spinning_processors_count_.compare_exchange_strong(expected, 1))
    {
        return;
    }
    need_spinning();
}
inline auto Scheduler::get_io_context() -> IOContext&
{
    assert(current_processor_);
    return current_processor_->iocontext;
}
inline void Scheduler::processor_func(Processor* p)
{
    current_processor_ = p;
    while (true)
    {
        auto coro = get_coro();
        assert(coro);
        coro->invoke();
    }
}

inline auto Scheduler::get_coro() -> Handle
{
    // 执行完协程需要判断是否为空
    Processor* processor = current_processor_;
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
            running_mask_.fetch_and(~(1 << processor->id));

            if (can_spinning())
            {
                processor->state = Processor::State::SPINNING;
            }
            else
            {
                processor->state = Processor::State::POLLING;
            }
            break;
        }
        case Processor::State::SPINNING: {
            Handle coro = get_coro_with_spinning(processor);
            bool last_spinning = (spinning_processors_count_.fetch_sub(1) == 1);
            if (coro)
            {
                processor->state = Processor::State::RUNNING;
                running_mask_.fetch_or(1 << processor->id);
                return coro;
            }
            if (last_spinning)
            {
                // 如果是最后一个，需要在检查一次（全局队列）
                if (coro = get_coro_with_spinning(processor); coro)
                {
                    processor->state = Processor::State::RUNNING;
                    running_mask_.fetch_or(1 << processor->id);
                    return coro;
                }
            }
            processor->state = Processor::State::POLLING;
            break;
        }
        case Processor::State::POLLING: {
            // 阻塞监听io
            polling_mask_.fetch_or(1 << processor->id);
            auto coros = processor->iocontext.poll(true);
            polling_mask_.fetch_and(~(1 << processor->id));
            if (auto it = coros.begin(); it != coros.end())
            {
                auto coro = *it;
                ++it;
                add_coro_to_processor({it, coros.end()}, processor);
                processor->state = Processor::State::RUNNING;
                running_mask_.fetch_or(1 << processor->id);
                return coro;
            }
            if (can_spinning())
            {
                processor->state = Processor::State::SPINNING;
            }
            else
            {
                processor->state = Processor::State::POLLING;
            }
            break;
        }
        }
    }
}
inline void Scheduler::need_spinning()
{
    need_spinning_.store(true);
    if (auto mask = polling_mask_.load(); mask)
    {
        auto low_1bit = mask & -mask;
        auto index = __builtin_ctz(low_1bit);
        wake_from_polling(processors_[index].get());
        return;
    }
    // 已经确保不会饿死
    if (auto mask = idle_mask_.load(); mask)
    {
        auto low_1bit = mask & -mask;
        auto index = __builtin_ctz(low_1bit);
        auto new_mask = mask & ~low_1bit;
        if (idle_mask_.compare_exchange_strong(mask, new_mask))
        {
            if (!need_spinning_.exchange(false))
            {
                idle_mask_.fetch_or(low_1bit);
                return;
            }
            assert(index < max_procs && index > 0);
            wake_from_idle(processors_[index].get());
        }
    }
}

inline void Scheduler::add_global_coroutine(std::span<Handle> coros)
{
    std::lock_guard<std::mutex> lock(global_coros_mtx_);
    global_coros_.insert(global_coros_.end(), coros.begin(), coros.end());
}

// TODO:yield
inline void Scheduler::add_coro_to_processor(Handle coro, Processor* processor, bool yield)
{
    assert(coro);
    if (!yield)
    {
        // 优先放入run_next
        auto old_run_next = processor->run_next.exchange(coro);
        if (!old_run_next)
        {
            return;
        }
        coro = old_run_next;
    }

    if (auto count = processor->coros.push_back({&coro, 1}); count == 1)
    {
        return;
    }
    auto half = processor->coros.pop_front_half();
    half.push_back(coro);
    add_global_coroutine(half);
    return;
}
inline void Scheduler::add_coro_to_processor(std::span<Handle> coros, Processor* processor)
{
    if (coros.empty())
    {
        return;
    }
    if (auto res = processor->coros.push_back(coros); res != coros.size())
    {
        add_global_coroutine(coros.subspan(res));
    }
}

inline auto Scheduler::get_coro_from_processor(Processor* processor) -> Handle
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

inline auto Scheduler::get_coro_with_spinning(Processor* processor) -> Handle
{
    std::vector<Handle> coros;
    //
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

inline auto Scheduler::get_global_coroutine(size_t max_count) -> std::vector<Handle>
{
    std::vector<Handle> coros;
    std::lock_guard<std::mutex> lock(global_coros_mtx_);
    auto get_coros_size = std::min(std::min(global_coros_.size() / max_procs + 1, global_coros_.size()), max_count);
    coros.reserve(get_coros_size);
    coros.insert(coros.end(), global_coros_.begin(), global_coros_.begin() + get_coros_size);
    global_coros_.erase(global_coros_.begin(), global_coros_.begin() + get_coros_size);
    return coros;
}

inline auto Scheduler::steal_coroutine(Processor* processor) -> std::vector<Handle>
{
    int rand_idx = randomer_.random(0, max_procs);
    // TODO:多几轮
    constexpr int max_rounds = 3;
    for (int round = 0; round < max_rounds; round++)
    {
        for (int i = 0; i < max_procs; i++)
        {
            auto steal_processor = processors_[(i + rand_idx) % max_procs].get();
            // 只窃取正在运行的P
            if (steal_processor == processor || !(running_mask_.load() & (1 << steal_processor->id)))
            {
                continue;
            }
            if (auto coros = steal_processor->coros.pop_front_half(); !coros.empty())
            {
                return coros;
            }
        }
    }
    for (int i = 0; i < max_procs; i++)
    {
        auto steal_processor = processors_[(i + rand_idx) % max_procs].get();
        if (steal_processor == processor || !(running_mask_.load() & (1 << steal_processor->id)))
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
inline bool Scheduler::can_spinning()
{
    // 先考虑need_spinning_
    if (need_spinning_.exchange(false))
    {
        return true;
    }
    // 考虑自旋数
    else if (auto count = spinning_processors_count_.load(); 3 * count <= get_idle_count())
    {
        // 自旋
        spinning_processors_count_.fetch_add(1);
        return true;
    }
    return false;
}
inline size_t Scheduler::get_idle_count()
{
    int running_count = __builtin_popcountll(running_mask_.load());
    return max_procs - running_count;
}
inline void Scheduler::wake_from_idle(Processor* p)
{
    p->state = Processor::State::SPINNING;
    p->thread = std::thread([p, this]() { processor_func(p); });
}
inline void Scheduler::wake_from_polling(Processor* p) { p->iocontext.wake(); }
} // namespace utils