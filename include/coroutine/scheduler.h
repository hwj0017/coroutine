#pragma once

#include "coroutine/coroutine.h"
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <unistd.h>
#include <vector>
namespace utils {
auto main_coro() -> Coroutine;
// Processor (P)
class Processor {
public:
  int id;
  Scheduler *scheduler_;
  std::queue<Coroutine> runq; // 本地运行队列
  std::mutex mtx;
  std::condition_variable cv;
  std::atomic<bool> running{false};
  Coroutine run_next{};
  int steal_count = 0;

  explicit Processor(int id, Scheduler *scheduler)
      : id(id), scheduler_(scheduler) {}

  ~Processor() {}
  void addCoroutine(Coroutine coro);
  Coroutine getCoroutine();
  bool try_run() { return running.exchange(true); }
  bool try_stop() { return !running.exchange(false); }
};

class Machine {
public:
  // id = 0为主线程
  int id;
  std::thread thread;
  explicit Machine(int id) : id(id) {}

  ~Machine() {
    if (id > 0 && thread.joinable()) {
      thread.join();
    }
  }

  void start(std::function<void()> func) {
    if (id > 0) {
      thread = std::thread(func);
    } else {
      func();
    }
  }
};

class Scheduler {
public:
  Scheduler(int max_procs, int max_machines);
  void schedule(Coroutine coro);
  void start();
  void stop();
  auto getGlobalCoroutine() -> std::vector<Coroutine>;

  static Scheduler &instance() {
    static Scheduler scheduler(Scheduler::max_procs, Scheduler::max_machines);
    return scheduler;
  }

private:
  // M执行循环
  void machineFunc(Machine *m);
  int max_machines_;
  int max_procs_;
  std::atomic<int> g_id_{0};

  // 全局队列
  std::queue<Coroutine> global_q_;
  std::mutex global_mtx_;
  std::atomic<int> global_q_size_{0};
  // 所有组件
  std::vector<std::unique_ptr<Processor>> processors_;
  std::vector<std::unique_ptr<Machine>> machines_;
  std::vector<Processor *> idle_processors_;
  std::vector<Machine *> idle_machines_;
  std::map<std::thread::id, Processor *> processor_map_;
  std::vector<Coroutine> system_goroutines;

  // 停止信号
  std::atomic<bool> stop_signal{false};
  std::thread balancer_thread;
  static const int max_procs = 1;
  static const int max_machines = 1;
};
} // namespace utils
