#include <algorithm> // for std::min
#include <atomic>
#include <cassert>
#include <cstddef>
#include <type_traits> // for static_assert
#include <vector>

namespace utils
{

// 多线程读单线程写的双端队列
template <typename T> class WorkStealingDeque
{
    static_assert(std::is_trivially_copyable_v<T>,
                  "WorkStealingDeque requires T to be trivially copyable (e.g., pointers, integers).");

  private:
    alignas(64) std::atomic<size_t> bottom_{0};
    alignas(64) std::atomic<size_t> top_{0};
    alignas(64) std::vector<std::atomic<T>> buffer_;
    alignas(64) const size_t capacity_;
    alignas(64) const size_t mask = capacity_ - 1;

    // Helper: 判断 T 是否为指针类型（用于条件断言）
    static constexpr bool is_pointer = std::is_pointer_v<T>;

  public:
    WorkStealingDeque(size_t capacity) : buffer_(capacity), capacity_(capacity)
    {
        assert((capacity_ & mask) == 0 && "Capacity must be a power of two.");
    }

    ~WorkStealingDeque() = default;

    // === Owner: push ===
    // 原子化地将 item 放入 buffer 的尾部，并返回是否成功
    auto push_back(T item) -> bool;
    auto push_back(const std::vector<T>& items) -> bool;

    // === Owner: pop ===
    // 原子化地从 buffer 的尾部取出 item，返回是否成功
    auto pop_back() -> T;
    auto pop_back(size_t max_count) -> std::vector<T>;

    // === Thief: steal ===
    // 原子化地从 buffer 的头部取出 item，返回是否成功
    auto pop_front() -> T;
    auto pop_front(size_t max_count) -> std::vector<T>;

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
        return (b - t) >= capacity_;
    }

    size_t size() const
    {
        size_t b = bottom_.load(std::memory_order_relaxed);
        size_t t = top_.load(std::memory_order_relaxed);
        return (b > t) ? (b - t) : 0;
    }
};

template <typename T> auto WorkStealingDeque<T>::push_back(T item) -> bool
{
    size_t b = bottom_.load(std::memory_order_relaxed);
    size_t t = top_.load(std::memory_order_acquire);
    if ((b - t + 1) > capacity_)
    {
        return false;
    }

    buffer_[b & mask].store(item, std::memory_order_relaxed);
    bottom_.store(b + 1, std::memory_order_release);
    return true;
}

template <typename T> auto WorkStealingDeque<T>::push_back(const std::vector<T>& items) -> bool
{
    auto count = items.size();
    if (count == 0)
        return true;

    size_t b = bottom_.load(std::memory_order_relaxed);
    size_t t = top_.load(std::memory_order_acquire);
    if ((b - t + count) > capacity_)
    {
        return false;
    }

    for (size_t i = 0; i < count; ++i)
    {
        buffer_[(b + i) & mask].store(items[i], std::memory_order_relaxed);
    }

    std::atomic_thread_fence(std::memory_order_release);
    bottom_.store(b + count, std::memory_order_relaxed);
    return true;
}
template <typename T> auto WorkStealingDeque<T>::pop_back() -> T
{
    size_t b = bottom_.load(std::memory_order_relaxed);
    size_t t = top_.load(std::memory_order_acquire);
    if (b == t)
    {
        return T{}; // 返回默认值（nullptr for pointers, 0 for integers）
    }

    size_t new_bottom = b - 1;
    bottom_.store(new_bottom, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_acquire);

    // 重新读取 top（关键修正！）
    size_t current_top = top_.load(std::memory_order_acquire);
    if (current_top > new_bottom)
    {
        // 竞争发生：恢复 bottom
        bottom_.store(b, std::memory_order_relaxed);
        return T{};
    }

    T item = buffer_[new_bottom & mask].load(std::memory_order_relaxed);
    buffer_[new_bottom & mask].store(T{}, std::memory_order_relaxed);
    return item;
}

template <typename T> auto WorkStealingDeque<T>::pop_back(size_t max_count) -> std::vector<T>
{
    if (max_count == 0)
        return {};

    size_t b = bottom_.load(std::memory_order_relaxed);
    size_t t = top_.load(std::memory_order_acquire);
    if (b <= t)
        return {};

    size_t pop_count = std::min(max_count, b - t);
    size_t new_bottom = b - pop_count;
    std::vector<T> out;
    out.resize(pop_count);
    // 先读取数据
    for (size_t i = 0; i < pop_count; ++i)
    {
        out[i] = buffer_[(b - i - 1) & mask].load(std::memory_order_relaxed);
    }

    // 更新 bottom
    bottom_.store(new_bottom, std::memory_order_relaxed);
    // std::atomic_thread_fence(std::memory_order_acquire);

    // 重新读取 top（关键修正！）
    size_t current_top = top_.load(std::memory_order_release);
    if (current_top > new_bottom)
    {
        // 竞争发生：恢复 bottom（不清除 buffer，因 thief 可能已读取）
        bottom_.store(b, std::memory_order_relaxed);
        return {};
    }

    // 清除 buffer（安全，因无竞争）
    for (size_t i = 0; i < pop_count; ++i)
    {
        buffer_[(b - i - 1) & mask].store(T{}, std::memory_order_relaxed);
    }

    return out;
}

template <typename T> auto WorkStealingDeque<T>::pop_front() -> T
{
    size_t t = top_.load(std::memory_order_relaxed);
    size_t b = bottom_.load(std::memory_order_acquire);
    if (t >= b)
        return T{};

    T item = buffer_[t & mask].load(std::memory_order_relaxed);
    size_t expected = t;
    auto new_top = t + 1;
    if (!top_.compare_exchange_strong(expected, new_top, std::memory_order_relaxed, std::memory_order_relaxed))
    {
        return T{};
    }
    if (auto current_bottom = bottom_.load(std::memory_order_release); current_bottom < new_top)
    {
        top_.store(t, std::memory_order_relaxed);
        return T{};
    }
    return item;
}

template <typename T> auto WorkStealingDeque<T>::pop_front(size_t max_count) -> std::vector<T>
{
    size_t t = top_.load(std::memory_order_relaxed);
    size_t b = bottom_.load(std::memory_order_acquire);
    if (t >= b)
        return {};

    size_t steal_count = std::min(max_count, (b - t + 1) / 2);
    if (steal_count == 0)
        return {};

    std::vector<T> out;
    out.resize(steal_count);
    // 读取数据
    for (size_t i = 0; i < steal_count; ++i)
    {
        out[i] = buffer_[(t + i) & mask].load(std::memory_order_relaxed);
    }

    // 原子推进 top
    size_t expected = t;
    auto new_top = t + steal_count;
    if (!top_.compare_exchange_strong(expected, new_top, std::memory_order_acquire, std::memory_order_acquire))
    {
        return {};
    }
    if (auto current_bottom = bottom_.load(std::memory_order_acquire); current_bottom < new_top)
    {
        top_.store(t, std::memory_order_relaxed);
        return {};
    }
    return out;
}
} // namespace utils