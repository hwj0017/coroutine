#pragma once

#include "coroutine/coroutine.h"
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <unistd.h>
#include <vector>
namespace utils
{
auto main_coro() -> Coroutine;

class Machine;
// Processor (P)
class Processor
{
  public:
    explicit Processor() : id_(next_id_++) {}

    ~Processor() {}
    void addCoroutine(Coroutine coro);
    void addCoroutine(std::vector<Coroutine>& coros);
    auto getCoroutine() -> Coroutine;
    bool try_run() { return running_.exchange(true); }
    bool try_stop() { return !running_.exchange(false); }

  private:
    int id_;
    std::queue<Coroutine> runq_; // 本地运行队列
    std::mutex mtx_;
    std::condition_variable cv_;
    std::atomic<bool> running_{false};
    Coroutine run_next_{};
    int steal_count = 0;

    // 不保证线程安全
    static int next_id_;
};

inline int Processor::next_id_{0};

class Machine
{
  public:
    explicit Machine() : id_(next_id_++) {}

    ~Machine()
    {
        stop();
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

    void stop()
    {
        if (auto is_stopped = is_stopped_.exchange(true); !is_stopped)
        {
            wakeup();
        }
    }

    auto processor() { return processor_; }

    auto set_processor(Processor* p) { processor_ = p; }

    auto is_stopped() { return is_stopped_.load(); }

    void sleep()
    {
        std::unique_lock<std::mutex> lock(mtx_);
        cv_.wait(lock);
    }

    void wakeup() { cv_.notify_one(); }

  private:
    // id = 0为主线程
    int id_;
    std::thread thread_;
    std::mutex mtx_;
    std::condition_variable cv_;
    // 只允许本线程访问
    Processor* processor_{nullptr};
    std::atomic<bool> is_stopped_{false};

    // 不保证线程安全
    static int next_id_;
};

inline int Machine::next_id_{0};

class Scheduler
{
  public:
    Scheduler(int max_procs, int max_machines);
    ~Scheduler();
    void schedule(Coroutine coro);
    void start();
    void stop();
    // 解除当前线程的p
    void release_processor();

    static Scheduler& instance()
    {
        static Scheduler scheduler(Scheduler::max_procs, Scheduler::max_machines);
        return scheduler;
    }

  private:
    // M执行循环
    void machineFunc(Machine* m);
    auto getGlobalCoroutine() -> std::vector<Coroutine>;

    const int max_machines_;
    const int max_procs_;

    // 全局队列
    std::queue<Coroutine> global_q_;
    std::mutex global_mtx_;
    // 所有组件
    std::vector<std::unique_ptr<Processor>> processors_;
    std::vector<std::unique_ptr<Machine>> machines_;
    std::vector<Processor*> idle_processors_;
    std::vector<Machine*> idle_machines_;
    std::map<std::thread::id, Processor*> processor_map_;
    std::vector<Coroutine> system_goroutines;

    // 停止信号
    std::atomic<bool> is_stopped{false};
    std::atomic<int> spinning_machines_{0};
    static const int max_procs = 1;
    static const int max_machines = 1;
    static const int max_spinning_epoch = 100;
};
} // namespace utils
