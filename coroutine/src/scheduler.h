#pragma once
#include "concurrentdeque.h"
#include "coroutine/coroutine.h"
#include "coroutine/intrusivelist.h"
#include "coroutine/spinlock.h"
#include "iocontext.h"
#include "randomer.h"
#include <array>
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
using Handle = Promise*;
class WorkStealingDeque
{
  public:
    constexpr static size_t Capacity = 256;
    constexpr static size_t Mask = Capacity - 1;

  private:
    alignas(64) std::atomic<size_t> bottom_{0};
    alignas(64) std::atomic<size_t> top_{0};
    alignas(64) std::array<std::atomic<Handle>, Capacity> buffer_;

  public:
    WorkStealingDeque() = default;

    ~WorkStealingDeque() = default;

    // 单线程
    auto push_back(IntrusiveList items) -> IntrusiveList;

    // 多线程
    auto pop_front() -> Handle;
    // 获取一半任务
    auto pop_front_half() -> IntrusiveList;
    // 窃取一半任务，返回一个任务
    auto steal(WorkStealingDeque& other) -> Handle;

    // === Utility ===
    bool empty() const
    {
        size_t b = bottom_.load(std::memory_order_relaxed);
        size_t t = top_.load(std::memory_order_relaxed);
        return b == t;
    }

    bool full() const
    {
        size_t b = bottom_.load(std::memory_order_relaxed);
        size_t t = top_.load(std::memory_order_relaxed);
        return (b - t) >= Capacity;
    }

    size_t size() const
    {
        size_t b = bottom_.load(std::memory_order_relaxed);
        size_t t = top_.load(std::memory_order_relaxed);
        return (b > t) ? (b - t) : 0;
    }
};
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
    explicit Processor(size_t id) : id(id) {}

    ~Processor() {}
    size_t id{};
    std::thread thread{};
    std::atomic<Handle> run_next{};
    IOContext iocontext{};
    WorkStealingDeque coros;
    int local_count_{0};
    // 是否自旋
    State state{State::SPINNING};
};
class Scheduler
{
  public:
    using Lock = std::mutex;

    static const size_t max_procs;
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

  private:
    // M执行循环
    void processor_func(Processor* p);
    void resume_processor(Processor* p = nullptr);
    void make_spinning();
    auto get_coro() -> Handle;
    // 将就绪协程加入p
    auto get_global_coroutine(size_t max_count) -> IntrusiveList;
    void add_global_coroutine(IntrusiveList coros);
    void add_coro_to_processor(IntrusiveList coros, Processor* processor);
    void add_coro_to_processor(Handle coro, Processor* processor, bool yield);
    auto get_coro_from_processor(Processor* processor) -> Handle;
    auto get_coro_with_spinning(Processor* processor) -> Handle;
    auto steal_coroutine(Processor* p) -> Handle;

    void wake_from_idle(Processor* p);
    void wake_from_polling(Processor* p);
    bool can_spinning();
    size_t get_idle_count();
    // 全部P
    const std::vector<std::unique_ptr<Processor>> processors_;
    // 全局队列
    IntrusiveList global_coros_{};
    Lock global_coros_mtx_{};

    // 全局锁
    // 一些原子变量加快访问速度
    // idle P 掩码
    std::atomic_uint64_t idle_mask_{0};
    // polling P 掩码
    std::atomic_uint64_t polling_mask_{0};
    // Running P 掩码
    std::atomic<uint64_t> running_mask_{0};
    // 自旋P
    std::atomic<int> spinning_processors_count_{0};
    std::atomic<bool> make_spinning_{false};

    static auto create_processors(size_t n) -> std::vector<std::unique_ptr<Processor>>
    {
        std::vector<std::unique_ptr<Processor>> procs;
        procs.reserve(n);
        for (size_t i = 0; i < n; ++i)
        {
            procs.push_back(std::make_unique<Processor>(i));
        }

        return procs;
    }
    static thread_local Processor* current_processor_;
    static thread_local Randomer randomer_;

    // 最大自旋回合
    static constexpr size_t max_spinning_epoch = 10;
};
inline const size_t Scheduler::max_procs = std::thread::hardware_concurrency();

inline thread_local Processor* Scheduler::current_processor_{nullptr};
inline thread_local Randomer Scheduler::randomer_{};
inline Scheduler::Scheduler() : processors_(create_processors(max_procs))
{
    assert(max_procs >= 1);
    // 第一个协程不会唤醒
    spinning_processors_count_.store(1);
    // 除去自己
    idle_mask_ = (1uLL << max_procs) - 2;
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
    assert(coro);
    // 获取当前P
    if (!current_processor_)
    {
        add_global_coroutine({coro});
    }
    else
    {
        // 优先放入当前P的
        add_coro_to_processor(coro, current_processor_, yield);
    }
    int expected = 0;
    if (spinning_processors_count_.load(std::memory_order_relaxed) > 0 ||
        !spinning_processors_count_.compare_exchange_strong(expected, 1))
    {
        return;
    }
    make_spinning();
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
        p->local_count_++;
        coro->resume();
    }
}

inline auto Scheduler::get_coro() -> Handle
{
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
                // 阻塞监听io
                polling_mask_.fetch_or(1 << processor->id);
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
            // 阻塞监听io
            polling_mask_.fetch_or(1 << processor->id);
            processor->state = Processor::State::POLLING;
            break;
        }
        case Processor::State::POLLING: {
            // // 防止所有P同时POLLING导致饿死
            if (make_spinning_.exchange(false))
            {
                polling_mask_.fetch_and(~(1 << processor->id));
                processor->state = Processor::State::SPINNING;
                break;
            }
            // 阻塞监听io
            auto coros = processor->iocontext.poll(true);
            // 没有任务
            if (coros.empty())
            {
                // 考虑自旋
                if (can_spinning())
                {
                    polling_mask_.fetch_and(~(1 << processor->id));
                    processor->state = Processor::State::SPINNING;
                }
                break;
            }
            // TODO:增加自旋
            //  获取到任务，优先执行
            auto coro = coros.pop_front();
            add_coro_to_processor(std::move(coros), processor);
            // 转化成运行态
            processor->state = Processor::State::RUNNING;
            polling_mask_.fetch_and(~(1 << processor->id));
            running_mask_.fetch_or(1 << processor->id);
            return static_cast<Handle>(coro);
        }
        }
    }
}
inline void Scheduler::make_spinning()
{
    make_spinning_.store(true);
    if (auto mask = polling_mask_.load(std::memory_order::relaxed); mask)
    {
        auto low_1bit = mask & -mask;
        auto index = __builtin_ctz(low_1bit);
        wake_from_polling(processors_[index].get());
        return;
    }
    // 已经确保不会饿死
    if (auto mask = idle_mask_.load(std::memory_order::relaxed); mask)
    {
        auto low_1bit = mask & -mask;
        auto index = __builtin_ctz(low_1bit);
        auto new_mask = mask & ~low_1bit;
        if (idle_mask_.compare_exchange_strong(mask, new_mask))
        {
            if (!make_spinning_.exchange(false))
            {
                idle_mask_.fetch_or(low_1bit);
                return;
            }
            assert(index < max_procs && index > 0);
            wake_from_idle(processors_[index].get());
        }
    }
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

    if (auto left = processor->coros.push_back({coro}); left.empty())
    {
        return;
    }
    auto half = processor->coros.pop_front_half();
    half.push_back(coro);
    add_global_coroutine(std::move(half));
    return;
}
inline void Scheduler::add_coro_to_processor(IntrusiveList coros, Processor* processor)
{
    if (coros.empty())
    {
        return;
    }
    // TODO:增加自旋
    if (auto left = processor->coros.push_back(std::move(coros)); !left.empty())
    {
        add_global_coroutine(std::move(left));
    }
}

inline auto Scheduler::get_coro_from_processor(Processor* processor) -> Handle
{
    // 优先从run_next获取
    if (auto coro = processor->run_next.exchange({}); coro)
    {
        return coro;
    }
    // // 多次后，考虑从全局队列获取
    // constexpr int interval = 61;
    // if (processor->local_count_ % interval == 0)
    // {
    //     if (auto coros = get_global_coroutine(1); !coros.empty())
    //     {
    //         return coros.front();
    //     }
    // }
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
        if (coros.empty())
        {
            return {};
        }
        auto coro = coros.pop_front();
        if (!coros.empty())
        {
            add_coro_to_processor(std::move(coros), processor);
            spinning_processors_count_.fetch_add(1);
            make_spinning();
        }

        return static_cast<Handle>(coro);
    }
    return {};
}

inline auto Scheduler::get_coro_with_spinning(Processor* processor) -> Handle
{
    bool more = false;
    //
    if (auto coros = get_global_coroutine(WorkStealingDeque::Capacity / 2); !coros.empty())
    {
        auto coro = static_cast<Handle>(coros.pop_front());
        add_coro_to_processor(std::move(coros), processor);
        return coro;
    }
    if (auto coro = steal_coroutine(processor); coro)
    {
        return coro;
    }
    if (auto coros = processor->iocontext.poll(false); !coros.empty())
    {
        auto coro = static_cast<Handle>(coros.pop_front());
        bool need_spinning = !coros.empty();
        add_coro_to_processor(std::move(coros), processor);
        if (need_spinning)
        {
            spinning_processors_count_.fetch_add(1);
            make_spinning();
        }
        return static_cast<Handle>(coro);
    }
    return {};
}

inline void Scheduler::add_global_coroutine(IntrusiveList coros)
{

    std::lock_guard<Lock> lock(global_coros_mtx_);
    global_coros_.push_back(std::move(coros));
}

inline auto Scheduler::get_global_coroutine(size_t max_count) -> IntrusiveList
{
    std::lock_guard<Lock> lock(global_coros_mtx_);

    auto get_coros_size = std::min(std::min(global_coros_.size() / max_procs + 1, global_coros_.size()), max_count);
    auto result = global_coros_.pop_front(get_coros_size);

    return result;
}

inline auto Scheduler::steal_coroutine(Processor* processor) -> Handle
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
            if (steal_processor == processor ||
                !(running_mask_.load(std::memory_order::relaxed) & (1 << steal_processor->id)))
            {
                continue;
            }
            while (!steal_processor->coros.empty())
            {
                if (auto coro = processor->coros.steal(steal_processor->coros); coro)
                {
                    return coro;
                }
            }
        }
    }
    for (int i = 0; i < max_procs; i++)
    {
        auto steal_processor = processors_[(i + rand_idx) % max_procs].get();
        if (steal_processor == processor ||
            !(running_mask_.load(std::memory_order::relaxed) & (1 << steal_processor->id)))
        {
            continue;
        }

        if (auto coro = steal_processor->run_next.exchange({}); coro)
        {
            return coro;
        }
    }
    return {};
}
inline bool Scheduler::can_spinning()
{
    // 先考虑make_spinning_
    if (make_spinning_.exchange(false))
    {
        return true;
    }
    // 考虑自旋数
    else if (auto count = spinning_processors_count_.load(std::memory_order::relaxed); 3 * count <= get_idle_count())
    {
        // 自旋
        spinning_processors_count_.fetch_add(1);
        return true;
    }
    return false;
}
inline size_t Scheduler::get_idle_count()
{
    int running_count = __builtin_popcountll(running_mask_.load(std::memory_order::relaxed));
    return max_procs - running_count;
}
inline void Scheduler::wake_from_idle(Processor* p)
{
    p->state = Processor::State::SPINNING;
    p->thread = std::thread([p, this]() { processor_func(p); });
}
inline void Scheduler::wake_from_polling(Processor* p) { p->iocontext.wake(); }

inline auto WorkStealingDeque::push_back(IntrusiveList items) -> IntrusiveList
{
    auto count = items.size();
    if (count == 0)
        return {};

    size_t b = bottom_.load(std::memory_order_acquire);
    size_t t = top_.load(std::memory_order_acquire);
    size_t res_count = std::min(Capacity - (b - t), count);

    for (size_t i = 0; i < res_count; ++i)
    {
        buffer_[(b + i) & Mask].store(static_cast<Handle>(items.pop_front()), std::memory_order_relaxed);
    }

    bottom_.store(b + res_count, std::memory_order_release);
    return items;
}

inline auto WorkStealingDeque::pop_front() -> Handle
{
    size_t t = top_.load(std::memory_order_acquire);
    size_t b = bottom_.load(std::memory_order_acquire);
    if (t >= b)
        return {};

    auto item = buffer_[t & Mask].load(std::memory_order_relaxed);
    size_t expected = t;
    auto new_top = t + 1;
    if (!top_.compare_exchange_strong(expected, new_top, std::memory_order_release, std::memory_order_relaxed))
    {
        return Handle{};
    }
    return item;
}

inline auto WorkStealingDeque::pop_front_half() -> IntrusiveList
{
    size_t t = top_.load(std::memory_order_acquire);
    size_t b = bottom_.load(std::memory_order_acquire);
    if (t >= b)
        return {};

    size_t steal_count = (b - t + 1) / 2;

    IntrusiveList out;
    // 读取数据
    for (size_t i = 0; i < steal_count; ++i)
    {
        out.push_back(buffer_[(t + i) & Mask].load(std::memory_order_relaxed));
    }

    // 原子推进 top
    size_t expected = t;
    auto new_top = t + steal_count;
    if (!top_.compare_exchange_strong(expected, new_top, std::memory_order_release, std::memory_order_relaxed))
    {
        return {};
    }
    return out;
}

inline auto WorkStealingDeque::steal(WorkStealingDeque& other) -> Handle
{
    assert(empty());
    size_t other_t = other.top_.load(std::memory_order_acquire);
    size_t other_b = other.bottom_.load(std::memory_order_acquire);
    if (other_t >= other_b)
        return {};

    size_t steal_count = (other_b - other_t + 1) / 2;

    size_t b = bottom_.load(std::memory_order_acquire);
    // 取第一个
    auto handle = other.buffer_[other_t & Mask].load(std::memory_order_relaxed);
    // 窃取剩余数据
    for (size_t i = 1; i < steal_count; ++i)
    {
        auto item = other.buffer_[(other_t + i) & Mask].load(std::memory_order_relaxed);
        buffer_[(b + i - 1) & Mask].store(item, std::memory_order_relaxed);
    }

    // 原子推进 top
    size_t expected = other_t;
    auto new_top = other_t + steal_count;
    if (!other.top_.compare_exchange_strong(expected, new_top, std::memory_order_release, std::memory_order_relaxed))
    {
        return {};
    }
    // 推进 bottom
    bottom_.fetch_add(steal_count - 1, std::memory_order_release);
    return handle;
}
} // namespace utils