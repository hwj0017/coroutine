#include <atomic>
#include <immintrin.h>

namespace utils
{

class SpinLock
{
    std::atomic<bool> locked_{false};

  public:
    SpinLock() = default;
    // 自旋锁不可拷贝、不可移动
    SpinLock(const SpinLock&) = delete;
    SpinLock& operator=(const SpinLock&) = delete;

    void lock() noexcept
    {
        while (true)
        {
            // 1. 乐观路径：先尝试读取，不触发 Cache 一致性协议的写独占
            if (!locked_.load(std::memory_order_relaxed))
            {
                // 2. 只有看到锁空闲，才尝试昂贵的 CAS 操作
                if (!locked_.exchange(true, std::memory_order_acquire))
                {
                    return;
                }
            }
            // 3. 核心：降低 CPU 功耗，优化多核同步速度
            _mm_pause();
        }
    }

    bool try_lock() noexcept
    {
        // 尝试一次，抢不到就走，不阻塞
        return !locked_.load(std::memory_order_relaxed) && !locked_.exchange(true, std::memory_order_acquire);
    }

    void unlock() noexcept { locked_.store(false, std::memory_order_release); }
};

} // namespace utils