#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <ctime>
#include <deque>
#include <functional>
#include <future>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <queue>
#include <random>
#include <sstream>
#include <thread>
#include <vector>

// ============== 日志系统 ==============

class Logger {
public:
  enum Level { DEBUG, INFO, WARNING, ERROR };

  static Logger &instance() {
    static Logger logger;
    return logger;
  }

  void set_level(Level level) { log_level = level; }

  template <typename... Args>
  void log(Level level, const char *file, int line, Args &&...args) {
    if (level < log_level)
      return;

    auto now = std::chrono::system_clock::now();
    auto now_time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now.time_since_epoch()) %
              1000;

    std::ostringstream ss;
    ss << std::put_time(std::localtime(&now_time), "%T") << '.'
       << std::setfill('0') << std::setw(3) << ms.count() << " ["
       << level_str(level) << "] " << file << ":" << line << " - ";
    (ss << ... << args) << "\n";

    std::lock_guard<std::mutex> lock(log_mutex);
    std::cout << ss.str();
  }

private:
  Logger() = default;

  const char *level_str(Level level) {
    switch (level) {
    case DEBUG:
      return "DEBUG";
    case INFO:
      return "INFO";
    case WARNING:
      return "WARN";
    case ERROR:
      return "ERROR";
    default:
      return "UNKNOWN";
    }
  }

  Level log_level = INFO;
  std::mutex log_mutex;
};

#define LOG_DEBUG(...)                                                         \
  Logger::instance().log(Logger::DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_INFO(...)                                                          \
  Logger::instance().log(Logger::INFO, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_WARN(...)                                                          \
  Logger::instance().log(Logger::WARNING, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...)                                                         \
  Logger::instance().log(Logger::ERROR, __FILE__, __LINE__, __VA_ARGS__)

// ============== GMP 核心组件 ==============

// Goroutine 状态
enum class GState { Ready, Running, Waiting, Dead };

// Goroutine 结构 (G)
struct Goroutine {
  int id;
  std::function<void()> task;
  GState state = GState::Ready;
  bool is_system = false; // 系统goroutine标志
};

// Processor (P)
class Processor {
public:
  int id;
  std::deque<Goroutine *> runq; // 本地运行队列
  std::mutex mtx;
  std::condition_variable cv;
  std::atomic<bool> running{false};
  Goroutine *running_g = nullptr;
  std::atomic<int> runq_size{0};
  int steal_count = 0;

  explicit Processor(int id) : id(id) {
    LOG_DEBUG("Processor ", id, " created");
  }

  ~Processor() { LOG_DEBUG("Processor ", id, " destroyed"); }

  void add_task(Goroutine *g) {
    std::unique_lock<std::mutex> lock(mtx);
    runq.push_back(g);
    runq_size++;
    LOG_DEBUG("P", id, " added G", g->id, " (runq size: ", runq_size, ")");
    cv.notify_one();
  }

  Goroutine *get_task(bool block) {
    std::unique_lock<std::mutex> lock(mtx);
    if (!block && runq.empty()) {
      LOG_DEBUG("P", id, " runq empty, return null");
      return nullptr;
    }

    while (runq.empty()) {
      LOG_DEBUG("P", id, " runq empty, waiting...");
      cv.wait(lock);
    }

    auto g = runq.front();
    runq.pop_front();
    runq_size--;
    LOG_DEBUG("P", id, " got G", g->id, " (remaining: ", runq_size, ")");
    return g;
  }
};

// Machine (M) - 系统线程
class Machine {
public:
  int id;
  Processor *p = nullptr; // 绑定的P
  std::thread thread;
  std::atomic<bool> running{false};

  explicit Machine(int id) : id(id) { LOG_DEBUG("Machine ", id, " created"); }

  ~Machine() {
    if (thread.joinable()) {
      thread.join();
    }
    LOG_DEBUG("Machine ", id, " destroyed");
  }

  void start(std::function<void()> f) {
    if (running)
      return;

    running = true;
    thread = std::thread([this, f] {
      LOG_DEBUG("M", id, " thread started");
      try {
        f();
      } catch (const std::exception &e) {
        LOG_ERROR("M", id, " crashed: ", e.what());
      }
      running = false;
      LOG_DEBUG("M", id, " thread exited");
    });

// 设置线程名称 (Linux)
#ifdef __linux__
    pthread_setname_np(thread.native_handle(),
                       ("M" + std::to_string(id)).c_str());
#endif
  }

  void join() {
    if (thread.joinable()) {
      thread.join();
    }
  }
};

// 全局调度器 (Scheduler)
class Scheduler {
public:
  int max_machines;
  int max_procs;
  std::atomic<int> g_id{0};

  // 全局队列
  std::deque<Goroutine *> global_q;
  std::mutex global_mtx;
  std::atomic<int> global_q_size{0};

  // 所有组件
  std::vector<Processor *> processors;
  std::vector<Machine *> machines;
  std::vector<Goroutine *> system_goroutines;

  // 停止信号
  std::atomic<bool> stop_signal{false};
  std::thread balancer_thread;

  Scheduler(int max_machines, int max_procs)
      : max_machines(max_machines), max_procs(max_procs) {
    LOG_INFO("Initializing scheduler with ", max_machines, " machines and ",
             max_procs, " processors");

    // 初始化P
    for (int i = 0; i < max_procs; i++) {
      processors.push_back(new Processor(i));
    }

    // 初始化M
    for (int i = 0; i < max_machines; i++) {
      machines.push_back(new Machine(i));
    }
    start();
  }

  ~Scheduler() {
    stop();

    // 清理goroutines
    for (auto g : global_q) {
      delete g;
    }
    for (auto g : system_goroutines) {
      delete g;
    }

    for (auto m : machines) {
      delete m;
    }
    for (auto p : processors) {
      delete p;
    }

    LOG_INFO("Scheduler destroyed");
  }

  // 创建新Goroutine
  template <typename F, typename... Args>
  Goroutine *create_goroutine(F &&f, Args &&...args) {
    auto task = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
    int id = g_id++;
    Goroutine *g = new Goroutine{id, task};
    LOG_DEBUG("Created G", g->id);

    if (!try_schedule_locally(g)) {
      std::unique_lock<std::mutex> lock(global_mtx);
      global_q.push_back(g);
      global_q_size++;
      LOG_DEBUG("G", g->id, " added to global queue (size: ", global_q_size,
                ")");
    }

    return g;
  }

  // 启动调度器
  void start() {
    LOG_INFO("Starting scheduler");
    stop_signal = false;

    // 创建系统Goroutine
    create_system_goroutine([this]() { system_load_balancer(); });

    // 启动所有M
    for (int i = 0; i < machines.size(); i++) {
      machines[i]->start([this, i]() { machine_loop(machines[i]); });
    }
  }

  // 停止调度器
  void stop() {
    LOG_INFO("Stopping scheduler");
    stop_signal = true;

    // 通知所有处理器
    for (auto p : processors) {
      p->cv.notify_all();
    }

    // 等待负载均衡器线程
    if (balancer_thread.joinable()) {
      balancer_thread.join();
    }

    // 等待所有机器线程
    for (auto m : machines) {
      if (m->running) {
        m->join();
      }
    }
  }

  // 等待所有任务完成
  void wait_idle() {
    while (!stop_signal) {
      if (global_q_size == 0 && !any_processor_busy()) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }

  // 打印统计信息
  void print_stats() {
    LOG_INFO("==== Scheduler Statistics ====");
    LOG_INFO("Global queue size: ", global_q_size.load());
    for (auto p : processors) {
      LOG_INFO("P", p->id, ": runq_size=", p->runq_size.load(),
               ", steal_count=", p->steal_count);
    }
  }
  static Scheduler &instance() {
    static Scheduler sched(4, 2); // 默认配置
    return sched;
  }

private:
  // M执行循环
  void machine_loop(Machine *m);

  // 尝试本地调度
  bool try_schedule_locally(Goroutine *g);

  // 创建系统Goroutine
  template <typename F, typename... Args>
  void create_system_goroutine(F &&f, Args &&...args) {
    auto task = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
    int id = g_id++;
    Goroutine *g = new Goroutine{id, task};
    g->is_system = true;
    system_goroutines.push_back(g);

    std::unique_lock<std::mutex> lock(global_mtx);
    global_q.push_front(g); // 系统G优先
    global_q_size++;
    LOG_DEBUG("Created system G", g->id);
  }

  // 系统级负载均衡器
  void system_load_balancer();

  // 处理全局队列
  void process_global_queue();

  // 平衡工作负载
  void balance_workload();

  // 调度策略
  void schedule(Goroutine *g);

  // 检查是否有处理器忙
  bool any_processor_busy() const {
    for (auto p : processors) {
      if (p->runq_size > 0 || p->running_g) {
        return true;
      }
    }
    return false;
  }
};

// ============== 实现细节 ==============

void Scheduler::machine_loop(Machine *m) {
  LOG_DEBUG("M", m->id, " entering machine loop");

  // 获取一个P绑定
  int assigned_p = -1;
  for (int i = 0; i < processors.size(); i++) {
    if (!processors[i]->running.exchange(true)) {
      assigned_p = i;
      m->p = processors[i];
      LOG_DEBUG("M", m->id, " bound to P", i);
      break;
    }
  }

  if (assigned_p == -1) {
    // 所有P都忙，等待随机P
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dist(0, processors.size() - 1);

    while (!stop_signal) {
      int p_index = dist(gen);
      if (!processors[p_index]->running.exchange(true)) {
        assigned_p = p_index;
        m->p = processors[p_index];
        LOG_DEBUG("M", m->id, " bound to P", p_index,
                  " after random selection");
        break;
      }
      std::this_thread::sleep_for(std::chrono::microseconds(10));
    }
  }

  if (stop_signal) {
    LOG_DEBUG("M", m->id, " exiting due to stop signal");
    return;
  }

  Processor *p = m->p;

  while (!stop_signal && m->running) {
    // 从P的队列获取任务
    LOG_DEBUG("M", m->id, " trying to get task from P", p->id);
    Goroutine *g = p->get_task(true); // 阻塞获取

    if (!g) {
      LOG_DEBUG("M", m->id, " got null task, continue");
      continue;
    }

    // 更新状态
    g->state = GState::Running;
    p->running_g = g;

    LOG_DEBUG("M", m->id, " executing G", g->id);

    try {
      // 执行goroutine任务
      g->task();
      g->state = GState::Dead;
      LOG_DEBUG("M", m->id, " completed G", g->id);
    } catch (const std::exception &e) {
      g->state = GState::Dead;
      LOG_ERROR("M", m->id, " G", g->id, " crashed: ", e.what());
    } catch (...) {
      g->state = GState::Dead;
      LOG_ERROR("M", m->id, " G", g->id, " crashed with unknown exception");
    }

    // 清理
    p->running_g = nullptr;

    // 回收goroutine资源
    if (!g->is_system) {
      delete g;
    }
  }

  // 解绑P
  if (p) {
    p->running = false;
    m->p = nullptr;
    LOG_DEBUG("M", m->id, " unbound from P", p->id);
  }

  LOG_DEBUG("M", m->id, " exiting machine loop");
}

bool Scheduler::try_schedule_locally(Goroutine *g) {
  static thread_local Processor *last_p = nullptr;

  // 尝试使用上次的P
  if (last_p && last_p->runq_size < 256) {
    last_p->add_task(g);
    return true;
  }

  // 查找最轻载的P
  Processor *candidate = nullptr;
  int min_load = std::numeric_limits<int>::max();

  for (auto p : processors) {
    int load = p->runq_size.load();
    if (load < min_load) {
      min_load = load;
      candidate = p;
    }
  }

  if (candidate && min_load < 512) { // 限制队列大小
    candidate->add_task(g);
    last_p = candidate;
    return true;
  }

  return false;
}

void Scheduler::schedule(Goroutine *g) {
  if (!try_schedule_locally(g)) {
    std::unique_lock<std::mutex> lock(global_mtx);
    global_q.push_back(g);
    global_q_size++;
    LOG_DEBUG("G", g->id, " added to global queue (size: ", global_q_size, ")");
  }
}

void Scheduler::system_load_balancer() {
  LOG_INFO("System load balancer started");

  while (!stop_signal) {
    // 处理全局队列
    process_global_queue();

    // 平衡工作负载
    balance_workload();

    // 休眠一段时间
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  LOG_INFO("System load balancer exited");
}

void Scheduler::process_global_queue() {
  constexpr int batch_size = 16;

  if (global_q_size == 0)
    return;

  std::vector<Goroutine *> batch;
  batch.reserve(batch_size);

  {
    std::unique_lock<std::mutex> lock(global_mtx);
    if (global_q.empty())
      return;

    auto end_it = global_q.size() > batch_size
                      ? std::next(global_q.begin(), batch_size)
                      : global_q.end();
    batch.assign(global_q.begin(), end_it);
    global_q.erase(global_q.begin(), end_it);
    global_q_size -= batch.size();
    LOG_DEBUG("Processed ", batch.size(), " tasks from global queue");
  }

  // 将任务分配到空闲的P
  for (auto g : batch) {
    schedule(g);
  }
}

void Scheduler::balance_workload() {
  // 平衡P之间的工作负载
  for (auto p1 : processors) {
    if (p1->runq_size == 0) {
      for (auto p2 : processors) {
        if (p2 != p1 && p2->runq_size > 1) {
          // 尝试从p2偷取一半工作
          int steal_count = (p2->runq_size + 1) / 2;
          std::vector<Goroutine *> stolen;

          {
            std::unique_lock<std::mutex> lock(p2->mtx);
            if (p2->runq.empty())
              continue;

            auto end_it = std::next(
                p2->runq.begin(),
                std::min(steal_count, static_cast<int>(p2->runq.size())));
            stolen.assign(p2->runq.begin(), end_it);
            p2->runq.erase(p2->runq.begin(), end_it);
            p2->runq_size -= stolen.size();
            LOG_DEBUG("Stole ", stolen.size(), " tasks from P", p2->id, " to P",
                      p1->id);
          }

          {
            std::unique_lock<std::mutex> lock(p1->mtx);
            for (auto g : stolen) {
              p1->runq.push_back(g);
              p1->runq_size++;
            }
          }

          p2->steal_count++;
          break;
        }
      }
    }
  }
}

// ============== 用户接口 ==============

// 协程调度包装器
template <typename F, typename... Args> auto async_g(F &&f, Args &&...args) {
  Scheduler *sched = &Scheduler::instance();

  using return_type = std::invoke_result_t<F, Args...>;

  auto task = [f = std::forward<F>(f), ... args = std::forward<Args>(args)] {
    if constexpr (std::is_same_v<return_type, void>) {
      f(args...);
    } else {
      return f(args...);
    }
  };

  if constexpr (std::is_same_v<return_type, void>) {
    sched->create_goroutine(task);
    return;
  } else {
    auto promise = std::make_shared<std::promise<return_type>>();
    auto future = promise->get_future();

    auto wrapped_task = [task, promise] {
      try {
        if constexpr (!std::is_same_v<return_type, void>) {
          promise->set_value(task());
        } else {
          task();
          promise->set_value();
        }
      } catch (...) {
        promise->set_exception(std::current_exception());
      }
    };

    sched->create_goroutine(wrapped_task);
    return future;
  }
}

// ============== 使用示例 ==============

int main() {
  // 设置日志级别
  Logger::instance().set_level(Logger::INFO);

  LOG_INFO("==== Starting GMP Scheduler Demo ====");

  // 创建并发任务
  auto task1 = [] {
    for (int i = 0; i < 5; i++) {
      LOG_INFO("Task1 running on M", std::this_thread::get_id(), " count: ", i);
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  };

  auto task2 = [](int id) {
    LOG_INFO("Task2 started with id ", id);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    return id * 2;
  };

  // 提交任务
  async_g(task1);

  // 获取异步结果
  auto future = async_g(task2, 42);

  // 创建100个短期任务
  for (int i = 0; i < 100; i++) {
    async_g([i] {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      if (i % 20 == 0) {
        LOG_INFO("Short task ", i, " completed");
      }
    });
  }

  try {
    int result = future.get(); // 阻塞获取结果
    LOG_INFO("Task2 result: ", result);
  } catch (...) {
    LOG_ERROR("Error in task2");
  }

  // 等待所有任务完成
  LOG_INFO("Waiting for all tasks to complete...");
  std::this_thread::sleep_for(std::chrono::seconds(2));

  // 获取调度器实例并打印统计信息
  static Scheduler *sched = nullptr;
  if (!sched) {
    static Scheduler default_sched(4, 2);
    sched = &default_sched;
  }
  sched->print_stats();

  LOG_INFO("==== Demo Completed ====");

  return 0;
}