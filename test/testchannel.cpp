#include <concepts>
#include <coroutine>
#include <deque>
#include <exception>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

// ============== 协程基础工具 ==============

template <typename T = void> struct Task {
  struct promise_type {
    std::suspend_always initial_suspend() noexcept { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    Task get_return_object() { return Task{Handle::from_promise(*this)}; }
    void unhandled_exception() { exception_ = std::current_exception(); }
    void return_value(T value) { value_ = std::move(value); }

    T result() {
      if (exception_)
        std::rethrow_exception(exception_);
      return std::move(value_.value());
    }

  private:
    std::optional<T> value_;
    std::exception_ptr exception_;
  };

  using Handle = std::coroutine_handle<promise_type>;

  explicit Task(Handle handle) : handle_(handle) {}

  ~Task() {
    if (handle_)
      handle_.destroy();
  }

  Task(Task &&other) noexcept
      : handle_(std::exchange(other.handle_, nullptr)) {}

  Task &operator=(Task &&other) noexcept {
    if (this != &other) {
      if (handle_)
        handle_.destroy();
      handle_ = std::exchange(other.handle_, nullptr);
    }
    return *this;
  }

  T get() {
    if (!handle_.done())
      handle_.resume();
    return handle_.promise().result();
  }

private:
  Handle handle_;
};

template <> struct Task<void> {
  struct promise_type {
    std::suspend_always initial_suspend() noexcept { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    Task get_return_object() { return Task{Handle::from_promise(*this)}; }
    void unhandled_exception() { exception_ = std::current_exception(); }
    void return_void() {}

    void result() {
      if (exception_)
        std::rethrow_exception(exception_);
    }

  private:
    std::exception_ptr exception_;
  };

  using Handle = std::coroutine_handle<promise_type>;

  explicit Task(Handle handle) : handle_(handle) {}

  ~Task() {
    if (handle_)
      handle_.destroy();
  }

  Task(Task &&other) noexcept
      : handle_(std::exchange(other.handle_, nullptr)) {}

  Task &operator=(Task &&other) noexcept {
    if (this != &other) {
      if (handle_)
        handle_.destroy();
      handle_ = std::exchange(other.handle_, nullptr);
    }
    return *this;
  }

  void get() {
    if (!handle_.done())
      handle_.resume();
    handle_.promise().result();
  }

private:
  Handle handle_;
};

// ============== Channel 实现 ==============

template <typename T> class Channel {
public:
  explicit Channel(size_t buffer_size = 0) : capacity_(buffer_size) {}

  ~Channel() { close(); }

  // 发送操作
  struct SendAwaiter {
    Channel *channel;
    T value;

    bool await_ready() const noexcept {
      std::lock_guard lock(channel->mutex_);
      return channel->try_send(value);
    }

    bool await_suspend(std::coroutine_handle<> handle) noexcept {
      std::lock_guard lock(channel->mutex_);
      if (channel->closed_)
        return false;

      if (channel->try_send(value)) {
        return false; // 不需要挂起
      }

      // 加入发送等待队列
      channel->send_queue_.push_back({std::move(value), handle});
      return true;
    }

    void await_resume() const {
      std::lock_guard lock(channel->mutex_);
      if (channel->closed_) {
        throw std::runtime_error("send on closed channel");
      }
    }
  };

  // 接收操作
  struct RecvAwaiter {
    Channel *channel;
    T *output;

    bool await_ready() const noexcept {
      std::lock_guard lock(channel->mutex_);
      return channel->try_recv(output);
    }

    bool await_suspend(std::coroutine_handle<> handle) noexcept {
      std::lock_guard lock(channel->mutex_);
      if (channel->try_recv(output)) {
        return false; // 不需要挂起
      }

      if (channel->closed_) {
        return false;
      }

      // 加入接收等待队列
      channel->recv_queue_.push_back({output, handle});
      return true;
    }

    T await_resume() const {
      std::lock_guard lock(channel->mutex_);
      if (channel->closed_ && channel->buffer_.empty()) {
        throw std::runtime_error("receive on closed channel");
      }
      return std::move(*output);
    }
  };

  // 发送协程接口
  auto send(T value) { return SendAwaiter{this, std::move(value)}; }

  // 接收协程接口
  auto recv() {
    T value;
    RecvAwaiter awaiter{this, &value};
    return std::pair{awaiter, value};
  }

  // 关闭Channel
  void close() {
    std::lock_guard lock(mutex_);
    if (closed_)
      return;

    closed_ = true;

    // 唤醒所有等待的接收者
    while (!recv_queue_.empty()) {
      auto &[output, handle] = recv_queue_.front();
      *output = T{}; // 默认值
      handle.resume();
      recv_queue_.pop_front();
    }

    // 唤醒所有等待的发送者
    while (!send_queue_.empty()) {
      auto &[value, handle] = send_queue_.front();
      handle.resume();
      send_queue_.pop_front();
    }
  }

  // 检查是否关闭
  bool is_closed() const {
    std::lock_guard lock(mutex_);
    return closed_;
  }

  // 范围for支持
  struct Iterator {
    Channel *channel;
    T value;

    bool operator!=(std::nullptr_t) const {
      return !channel->is_closed() || !channel->buffer_.empty();
    }

    Iterator &operator++() {
      auto [awaiter, new_value] = channel->recv();
      awaiter.await_resume();
      value = std::move(new_value);
      return *this;
    }

    T operator*() const { return value; }
  };

  Iterator begin() {
    auto [awaiter, value] = recv();
    awaiter.await_resume();
    return Iterator{this, std::move(value)};
  }

  std::nullptr_t end() { return nullptr; }

private:
  mutable std::mutex mutex_;
  std::deque<T> buffer_;
  size_t capacity_ = 0;
  bool closed_ = false;

  struct SendRequest {
    T value;
    std::coroutine_handle<> handle;
  };

  struct RecvRequest {
    T *output;
    std::coroutine_handle<> handle;
  };

  std::deque<SendRequest> send_queue_;
  std::deque<RecvRequest> recv_queue_;

  // 尝试发送（内部使用）
  bool try_send(T value) {
    if (closed_)
      return false;

    // 尝试直接传递给等待的接收者
    if (!recv_queue_.empty()) {
      auto &[output, handle] = recv_queue_.front();
      *output = std::move(value);
      recv_queue_.pop_front();
      handle.resume();
      return true;
    }

    // 如果有缓冲区空间
    if (buffer_.size() < capacity_) {
      buffer_.push_back(std::move(value));
      return true;
    }

    return false;
  }

  // 尝试接收（内部使用）
  bool try_recv(T *output) {
    // 尝试从缓冲区获取
    if (!buffer_.empty()) {
      *output = std::move(buffer_.front());
      buffer_.pop_front();

      // 如果有等待的发送者，唤醒一个
      if (!send_queue_.empty()) {
        auto &[value, handle] = send_queue_.front();
        buffer_.push_back(std::move(value));
        send_queue_.pop_front();
        handle.resume();
      }
      return true;
    }

    // 尝试从等待的发送者获取
    if (!send_queue_.empty()) {
      auto &[value, handle] = send_queue_.front();
      *output = std::move(value);
      send_queue_.pop_front();
      handle.resume();
      return true;
    }

    // 如果已关闭且缓冲区为空
    if (closed_) {
      return false;
    }

    return false;
  }
};

// ============== 辅助函数 ==============

template <typename T> auto make_channel(size_t buffer_size = 0) {
  return Channel<T>(buffer_size);
}

// ============== 使用示例 ==============

#include <iostream>
#include <thread>
#include <vector>

Task<> producer(Channel<int> &ch, int id) {
  for (int i = 0; i < 5; i++) {
    co_await ch.send(id * 100 + i);
    std::cout << "Producer " << id << " sent: " << id * 100 + i << std::endl;
  }
}

Task<> consumer(Channel<int> &ch, int id) {
  try {
    while (true) {
      auto [awaiter, value] = ch.recv();
      co_await awaiter;
      std::cout << "Consumer " << id << " received: " << value << std::endl;
    }
  } catch (const std::exception &e) {
    std::cout << "Consumer " << id << " exited: " << e.what() << std::endl;
  }
}

Task<> select_example(Channel<int> &ch1, Channel<int> &ch2) {
  for (int i = 0; i < 10; i++) {
    // 模拟select：等待多个channel
    if (auto [awaiter, value] = ch1.recv(); co_await awaiter) {
      std::cout << "Received from ch1: " << value << std::endl;
    } else if (auto [awaiter, value] = ch2.recv(); co_await awaiter) {
      std::cout << "Received from ch2: " << value << std::endl;
    }
  }
}

Task<> range_example(Channel<int> &ch) {
  for (auto value : ch) {
    std::cout << "Range received: " << value << std::endl;
  }
  std::cout << "Channel closed" << std::endl;
}

int main() {
  // 无缓冲Channel
  auto unbuffered = make_channel<int>();

  // 缓冲Channel
  auto buffered = make_channel<int>(5);

  // 生产者协程
  auto prod1 = producer(unbuffered, 1);
  auto prod2 = producer(unbuffered, 2);

  // 消费者协程
  auto cons1 = consumer(unbuffered, 1);
  auto cons2 = consumer(unbuffered, 2);

  // 启动协程
  std::thread t1([&] { prod1.get(); });
  std::thread t2([&] { prod2.get(); });
  std::thread t3([&] { cons1.get(); });
  std::thread t4([&] { cons2.get(); });

  // 等待生产者完成
  t1.join();
  t2.join();

  // 关闭Channel
  unbuffered.close();

  // 等待消费者完成
  t3.join();
  t4.join();

  // Select示例
  auto ch1 = make_channel<int>(2);
  auto ch2 = make_channel<int>(2);

  std::thread select_thread([&] { select_example(ch1, ch2).get(); });

  std::thread sender1([&]() -> Task<> {
    for (int i = 0; i < 5; i++) {
      co_await ch1.send(i);
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    ch1.close();
  });

  std::thread sender2([&]() -> Task<> {
    for (int i = 10; i < 15; i++) {
      co_await ch2.send(i);
      std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }
    ch2.close();
  });

  sender1.join();
  sender2.join();
  select_thread.join();

  // Range示例
  auto range_ch = make_channel<int>(3);

  std::thread range_producer([&]() -> Task<> {
    for (int i = 0; i < 10; i++) {
      co_await range_ch.send(i);
    }
    range_ch.close();
  });

  std::thread range_consumer([&] { range_example(range_ch).get(); });

  range_producer.join();
  range_consumer.join();

  return 0;
}