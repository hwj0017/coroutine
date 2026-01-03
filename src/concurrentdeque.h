#include <algorithm> // for std::min
#include <atomic>
#include <cassert>
#include <cstddef>
#include <span>
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
    alignas(64) const size_t mask;

    // Helper: 判断 T 是否为指针类型（用于条件断言）
    static constexpr bool is_pointer = std::is_pointer_v<T>;

  public:
    WorkStealingDeque(size_t capacity) : buffer_(capacity), capacity_(capacity), mask(capacity - 1)
    {
        assert((capacity_ & mask) == 0 && "Capacity must be a power of two.");
    }

    ~WorkStealingDeque() = default;

    // 单线程
    auto push_back(std::span<T> items) -> size_t;

    // 多线程
    auto pop_front() -> T;
    auto pop_front_half() -> std::vector<T>;

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

template <typename T> auto WorkStealingDeque<T>::push_back(std::span<T> items) -> size_t
{
    auto count = items.size();
    if (count == 0)
        return 0;

    size_t b = bottom_.load(std::memory_order_acquire);
    size_t t = top_.load(std::memory_order_acquire);
    size_t res_count = std::min(capacity_ - (b - t), count);

    for (size_t i = 0; i < res_count; ++i)
    {
        buffer_[(b + i) & mask].store(items[i], std::memory_order_relaxed);
    }

    bottom_.store(b + res_count, std::memory_order_release);
    return res_count;
}

template <typename T> auto WorkStealingDeque<T>::pop_front() -> T
{
    size_t t = top_.load(std::memory_order_acquire);
    size_t b = bottom_.load(std::memory_order_acquire);
    if (t >= b)
        return T{};

    T item = buffer_[t & mask].load(std::memory_order_relaxed);
    size_t expected = t;
    auto new_top = t + 1;
    if (!top_.compare_exchange_strong(expected, new_top, std::memory_order_release, std::memory_order_relaxed))
    {
        return T{};
    }
    return item;
}

template <typename T> auto WorkStealingDeque<T>::pop_front_half() -> std::vector<T>
{
    size_t t = top_.load(std::memory_order_acquire);
    size_t b = bottom_.load(std::memory_order_acquire);
    if (t >= b)
        return {};

    size_t steal_count = (b - t + 1) / 2;

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
    if (!top_.compare_exchange_strong(expected, new_top, std::memory_order_release, std::memory_order_relaxed))
    {
        return {};
    }
    return out;
}
} // namespace utils