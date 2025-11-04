#include "coroutine/scheduler.h"
#include "coroutine/coroutine.h"
#include <cassert>
#include <cstddef>
#include <mutex>
#include <thread>
#include <utility>
namespace utils {
Scheduler::Scheduler(int max_procs, int max_machines)
    : max_procs_(max_procs), max_machines_(max_machines) {
  assert(max_procs_ > 0 && max_machines_ > 0);
  // 初始化P
  for (int i = 0; i < max_procs_; i++) {
    processors_.push_back(std::make_unique<Processor>(i, this));
  }

  // 初始化M
  for (int i = 0; i < max_machines_; i++) {
    machines_.push_back(std::make_unique<Machine>(i));
  }
}
void Scheduler::schedule(Coroutine coro) {
  auto id = std::this_thread::get_id();
  if (auto it = processor_map_.find(id); it != processor_map_.end()) {
    it->second->addCoroutine(coro);
  } else {
    std::lock_guard<std::mutex> lock(global_mtx_);
    global_q_.push(coro);
  }
}
// 开始运行
void Scheduler::start() {
  for (auto &machine : machines_) {
    machine->start([this, &machine] { machineFunc(machine.get()); });
  }
}
void Scheduler::stop() {}
auto Scheduler::getGlobalCoroutine() -> std::vector<Coroutine> {
  std::lock_guard<std::mutex> lock(global_mtx_);
  if (global_q_.empty()) {
    return {};
  }
  std::vector<Coroutine> coros;
  size_t max_size = global_q_.size() / max_procs_ + 1;
  while (coros.size() < max_size) {
    coros.push_back(std::move(global_q_.front()));
    global_q_.pop();
  }
  return coros;
}
void Scheduler::machineFunc(Machine *machine) {
  // 获取一个P绑定
  while (true) {
    Processor *p = nullptr;
    {
      std::lock_guard<std::mutex> lock(global_mtx_);
      for (int i = 0; i < processors_.size(); i++) {
        if (!processors_[i]->try_run()) {
          p = processors_[i].get();
          break;
        }
      }
      if (p) {
        processor_map_[std::this_thread::get_id()] = p;
      }
    }
    if (!p) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    while (true) {
      // 从P的队列获取任务
      Coroutine coro = p->getCoroutine();
      if (coro) {
        coro.resume();
      } else {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
    }
  }
}

void Processor::addCoroutine(Coroutine coro) {
  if (!run_next) {
    run_next = coro;
    return;
  }
  std::lock_guard lock(mtx);
  runq.push(std::exchange(run_next, coro));
}

auto Processor::getCoroutine() -> Coroutine {
  if (run_next) {
    return std::exchange(run_next, {});
  }
  std::lock_guard lock(mtx);
  if (runq.empty()) {
    auto global_coros = scheduler_->getGlobalCoroutine();
    if (global_coros.empty()) {
      return {};
    }
    for (auto coroutine : global_coros) {
      runq.push(coroutine);
    }
  }
  auto coro = runq.front();
  runq.pop();
  return coro;
}
auto listen_main() -> Coroutine {
  co_await main_coro();
  utils::Scheduler::instance().stop();
}

} // namespace utils
int main() {
  utils::listen_main();
  utils::Scheduler::instance().start();
  return 0;
}