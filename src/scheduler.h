#pragma once
#include "concurrentdeque.h"
#include "coroutine/handle.h"
#include "iocontext.h"
#include "randomer.h"
#include <atomic>
#include <cassert>
#include <coroutine>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <span>
#include <sys/eventfd.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace utils
{
class Machine;
class Processor;
class Scheduler
{
  public:
    Scheduler();
    ~Scheduler() = default;
    void co_spawn(Handle coro, bool yield = false);
    void schedule();
    void release();
    auto get_io_context() -> IOContext*;
    auto& instanse()
    {
        static Scheduler* scheduler = new Scheduler();
        return *scheduler;
    }
    static const int max_procs = 1;

  private:
    void test();
    // M执行循环
    void machine_func(Machine* m);
    void resume_processor(Processor* p = nullptr);
    void need_spinning();
    auto get_coro() -> Handle;
    // 将就绪协程加入p
    void add_coro_to_processor(std::span<Handle> coros, Processor* processor);
    auto get_coro_from_processor(Processor* processor) -> Handle;
    auto get_coro_with_spinning(Processor* processor) -> Handle;
    auto get_global_coroutine(size_t max_count) -> std::vector<Handle>;
    void add_global_coroutine(std::span<Handle> coros);
    auto get_idle_processor() -> Processor*
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
    void add_idle_processor(Processor* p)
    {
        idle_processors_.push(p);
        idle_processor_count_.fetch_add(1);
    }
    auto get_idle_machine() -> Machine*;
    void add_idle_machine(Machine* m);
    auto steal_coroutine(Processor* p) -> std::vector<Handle>;
    void start_processor(Processor* p);
    void sleep();
    void wake(Machine* m);
    void wake_from_polling(Processor* p);
    bool can_spinning();
    // 全部P
    const std::vector<std::unique_ptr<Processor>> processors_;
    // 全部M
    std::vector<std::unique_ptr<Machine>> machines_;
    // 主线程对应M
    std::unique_ptr<Machine> main_machine_{std::make_unique<Machine>()};

    // 全局队列
    std::queue<Handle> global_coros_{};
    std::mutex global_coros_mtx_{};

    // 空闲P
    std::queue<Processor*> idle_processors_{};
    std::atomic<int> idle_processor_count_{0};
    // 空闲M
    std::queue<Machine*> idle_machines_{};
    // 全局锁
    std::mutex global_mtx_{};

    // 一些原子变量加快访问速度
    // polling P 掩码
    std::atomic_uint32_t polling_processor_{0};
    // Running P 掩码
    std::atomic<uint32_t> running_processor_{0};
    // 自旋P
    std::atomic<int> spinning_processors_count_{0};
    std::atomic<bool> need_spinning_{false};
    std::atomic<bool> waiting_spining_{false};

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
    static thread_local Machine* current_machine_;
    static thread_local Randomer randomer_;

    // p本地队列最大任务
    static constexpr size_t max_local_queue_size = 256;
    // 最大自旋回合
    static constexpr size_t max_spinning_epoch = 10;
};

// Processor (P)
class Processor
{
  public:
    enum class State
    {
        IDLE,
        RUNNING,
        SPINNING,
        WAITINGSPINNING,
        POLLING,
        NOTFOUND,
    };
    explicit Processor(size_t id, size_t capacity) : id(id), coros(capacity) {}

    ~Processor() {}
    size_t id;
    std::atomic<Handle> run_next{};
    IOContext iocontext{};
    WorkStealingDeque<Handle> coros;
    // 是否自旋
    State state{State::IDLE};
    eventfd_t eventfd{0};
};

class Machine
{
  public:
    explicit Machine() = default;

    ~Machine()
    {
        if (thread.joinable())
        {
            thread.join();
        }
    }

  public:
    Processor* processor{nullptr};

  public:
    void start(std::function<void()> func)
    {
        ready.store(true);
        thread = std::thread(func);
    }

    std::atomic<bool> ready{false};
    std::thread thread{};
};

inline thread_local Machine* Scheduler::current_machine_{nullptr};
inline thread_local Randomer Scheduler::randomer_{};

inline Scheduler::Scheduler() : processors_(create_processors(max_procs))
{
    assert(max_procs >= 1);
    main_machine_->processor = processors_[0].get();
    for (int i = 1; i < processors_.size(); ++i)
    {
        idle_processors_.push(processors_[i].get());
    }
    idle_processor_count_.store(processors_.size() - 1);
    // 第一个协程不会needspinning
    spinning_processors_count_.store(1);
}

inline Scheduler* scheduler = nullptr;
} // namespace utils