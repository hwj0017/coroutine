#pragma once

#include <cassert>
#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace utils
{

/**
 * @brief 高性能环形缓冲区
 * @tparam T 元素类型
 * @tparam Capacity 缓冲区容量，必须是 2 的幂（如 256, 1024）
 */
template <typename T, size_t Capacity> class RingBuffer
{
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity 必须是 2 的幂以进行位运算优化");

  public:
    // 禁止拷贝，支持移动
    RingBuffer() noexcept : head_(0), tail_(0), size_(0) {}

    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;

    RingBuffer(RingBuffer&& other) noexcept : head_(other.head_), tail_(other.tail_), size_(other.size_)
    {
        // 注意：侵入式或原始内存移动通常需要根据具体业务逻辑决定是移动数据还是接管
        // 此处简单清空源对象以防止双重析构
        other.size_ = 0;
        other.head_ = 0;
        other.tail_ = 0;
    }

    ~RingBuffer() { clear(); }

    /**
     * @brief 入队（生产者）
     * 使用完美的转发支持各种构造方式
     */
    template <typename... Args> bool push(Args&&... args)
    {
        if (full())
        {
            return false;
        }

        // 在 tail 位置手动构造对象
        new (get_ptr(tail_)) T(std::forward<Args>(args)...);

        // 使用掩码代替取模：(tail + 1) & Mask
        tail_ = (tail_ + 1) & MASK;
        size_++;
        return true;
    }

    /**
     * @brief 出队（消费者）
     * 返回对象并销毁缓冲区内的副本
     */
    T pop()
    {
        assert(!empty());

        T* p = get_ptr(head_);
        T result = std::move(*p); // 移动出数据

        p->~T(); // 显式调用析构函数

        head_ = (head_ + 1) & MASK;
        size_--;
        return result;
    }

    /**
     * @brief 访问队首元素
     */
    T& front()
    {
        assert(!empty());
        return *get_ptr(head_);
    }

    const T& front() const
    {
        assert(!empty());
        return *get_ptr(head_);
    }

    // --- 状态查询 ---

    bool empty() const noexcept { return size_ == 0; }
    bool full() const noexcept { return size_ == Capacity; }
    size_t size() const noexcept { return size_; }
    static constexpr size_t capacity() noexcept { return Capacity; }

    /**
     * @brief 清空缓冲区并销毁所有现存对象
     */
    void clear()
    {
        while (!empty())
        {
            get_ptr(head_)->~T();
            head_ = (head_ + 1) & MASK;
            size_--;
        }
        head_ = 0;
        tail_ = 0;
    }

  private:
    /**
     * @brief 获取指定索引的内存指针
     */
    inline T* get_ptr(size_t index) noexcept { return reinterpret_cast<T*>(&storage_[index * sizeof(T)]); }

    inline const T* get_ptr(size_t index) const noexcept
    {
        return reinterpret_cast<const T*>(&storage_[index * sizeof(T)]);
    }

  private:
    // 使用掩码加速索引跳转
    static constexpr size_t MASK = Capacity - 1;

    // 原始字节缓冲区，对齐到 T 的要求
    // alignas 确保内存地址符合 T 的对齐标准，避免 CPU 访问异常
    alignas(T) std::byte storage_[Capacity * sizeof(T)];

    size_t head_; // 读指针
    size_t tail_; // 写指针
    size_t size_; // 元素计数
};

/**
 * @brief RingBuffer 的 void 特化版（用于无参数信号同步）
 */
template <size_t Capacity> class RingBuffer<void, Capacity>
{
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");

  public:
    bool push()
    {
        if (size_ >= Capacity)
            return false;
        size_++;
        return true;
    }
    void pop()
    {
        assert(size_ > 0);
        size_--;
    }
    bool empty() const { return size_ == 0; }
    bool full() const { return size_ == Capacity; }
    size_t size() const { return size_; }

  private:
    size_t size_ = 0;
};

} // namespace utils