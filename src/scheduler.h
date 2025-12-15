#pragma once

#include "concurrentdeque.h"
#include "coroutine/channel.h"
#include "coroutine/coroutine.h"
#include "randomer.h"
#include "schedulerinterface.h"
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <random>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

namespace utils
{

class Machine;
// Processor (P)
class Processor
{
  public:
    explicit Processor() : id_(next_id_++) {}

    ~Processor() {}
    auto id() const { return id_; }
    bool add_coroutine(Coroutine&& coro);
    bool add_coroutine(std::vector<Coroutine>&& coros);
    auto get_coroutine() -> Coroutine;
    auto exchange_run_next(Coroutine coro) -> Coroutine { return std::exchange(run_next_, coro); }
    void set_machine(Machine* m) { machine_ = m; }
    auto get_half_coroutines() -> std::vector<Coroutine> { return coros_.pop_back(capacity / 2); }
    auto steal_coroutine() -> std::vector<Coroutine>;

    auto spinning() const { return spinning_; }
    void set_spinning(bool sp) { spinning_ = sp; }
    constexpr static size_t capacity = 1 << 8;

  private:
    int id_;
    Coroutine run_next_{};
    WorkStealingDeque<Coroutine> coros_{capacity};
    Machine* machine_{nullptr};
    // 是否自旋
    bool spinning_{false};
    // 不保证线程安全
    static std::atomic<int> next_id_;
};

inline std::atomic<int> Processor::next_id_{0};

class Machine
{
  public:
    explicit Machine() : id_(next_id_++) {}

    ~Machine()
    {
        if (id_ > 0 && thread_.joinable())
        {
            thread_.join();
        }
    }

    void start(std::function<void()> func)
    {
        if (id_ > 0)
        {
            thread_ = std::thread(func);
        }
        else
        {
            func();
        }
    }

    void resume()
    {
        ready_.store(true);
        ready_.notify_one();
    }
    void sleep()
    {
        ready_.store(false);
        ready_.wait(true);
    }
    auto id() const { return id_; }

    auto started() const { return started_; }
    auto processor() const { return processor_; }
    void set_processor(Processor* p) { processor_ = p; }

  private:
    // id = 0为主线程
    int id_;
    std::thread thread_;
    Processor* processor_{nullptr};
    std::atomic<bool> ready_{false};
    bool started_{false};

    // 不保证线程安全
    static std::atomic<int> next_id_;
};

inline std::atomic<int> Machine::next_id_{0};

class Scheduler : public SchedulerInterface
{
  public:
    Scheduler(int max_procs, int max_machines);
    ~Scheduler();
    void add_coroutine(Coroutine& coro) override;
    void schedule() override;
    static const int max_procs = 16;
    static const int max_machines = 16;

  private:
    // M执行循环
    void machine_func(Machine* m);
    void notify();
    auto get_global_coroutine() -> std::vector<Coroutine>;
    void add_global_coroutine(std::vector<Coroutine>&& coros);
    void add_global_coroutine(Coroutine&& coro);
    auto get_coroutine(Processor* p) -> Coroutine;
    auto stopped() { return stopped_.load(); }
    auto get_idle_processor() -> Processor*;
    auto get_idle_machine() -> Machine*;
    void add_idle_processor(Processor* p);
    void add_idle_machine(Machine* m);
    auto steal_coroutine() -> std::vector<Coroutine>;

    const int max_machines_;
    const int max_procs_;
    // 全部P
    const std::vector<std::unique_ptr<Processor>> processors_;
    // 全部M
    std::vector<std::unique_ptr<Machine>> machines_;
    // 主线程对应M
    std::unique_ptr<Machine> main_machine_{std::make_unique<Machine>()};

    // 全局队列
    std::queue<Coroutine> global_coros_{};
    std::atomic<int> global_coros_count_{0};

    // 空闲P
    std::queue<Processor*> idle_processors_{};
    std::atomic<int> idle_processor_count_{0};
    // 空闲M
    std::queue<Machine*> idle_machines_{};
    std::atomic<int> idle_machine_count_{0};

    // 全局锁
    std::mutex global_mtx_{};
    // 停止信号
    std::atomic<bool> stopped_{true};
    // 自旋M
    std::atomic<int> spinning_machines_count_{0};

    static auto create_processors(size_t n) -> std::vector<std::unique_ptr<Processor>>
    {
        std::vector<std::unique_ptr<Processor>> procs;
        procs.reserve(n);
        for (size_t i = 0; i < n; ++i)
        {
            procs.push_back(std::make_unique<Processor>());
        }
        return procs;
    }
    static thread_local Machine* current_machine_;
    static thread_local Randomer randomer_;

    // p本地队列最大任务
    static const int max_local_queue_size = 256;
    // 最大自旋回合
    static const int max_spinning_epoch = 10;
};
inline thread_local Machine* Scheduler::current_machine_{nullptr};
inline thread_local Randomer Scheduler::randomer_{};
} // namespace utils
