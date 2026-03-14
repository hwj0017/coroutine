#include <atomic>
#include <cstddef>
#include <iostream>
#include <new>
#include <print>

// 1. 轻量级自旋锁 (SpinLock)
// 在内存池这种临界区极小的场景，自旋锁比 std::mutex 陷入内核睡眠的开销小得多
class SpinLock
{
    std::atomic_flag flag = ATOMIC_FLAG_INIT;

  public:
    void lock()
    {
        while (flag.test_and_set(std::memory_order_acquire))
        {
            // 自旋等待，这里可以加入 _mm_pause() 优化 CPU 流水线
        }
    }
    void unlock() { flag.clear(std::memory_order_release); }
};

// 内存块的侵入式链表节点（复用空闲内存本身，不额外占空间）
struct FreeNode
{
    FreeNode* next;
};

// 2. 全局内存池 (Global Pool)
// 按 Size-Class (尺寸等级) 划分，每种尺寸一个池子
class GlobalSizeClassPool
{
    SpinLock spin_lock;
    FreeNode* head = nullptr;

  public:
    // 从全局池批量获取节点到本地 TLS
    FreeNode* fetch_batch(size_t batch_size, size_t& fetched_count)
    {
        std::lock_guard<SpinLock> lock(spin_lock);
        if (!head)
            return nullptr;

        FreeNode* current = head;
        fetched_count = 1;
        // 截取最多 batch_size 个节点
        while (current->next && fetched_count < batch_size)
        {
            current = current->next;
            fetched_count++;
        }

        FreeNode* batch_head = head;
        head = current->next; // 断开链表
        current->next = nullptr;
        return batch_head;
    }

    // 将 TLS 中多余的节点批量归还给全局池
    void return_batch(FreeNode* batch_head, FreeNode* batch_tail)
    {
        if (!batch_head)
            return;
        std::lock_guard<SpinLock> lock(spin_lock);
        batch_tail->next = head;
        head = batch_head;
    }
};

// 尺寸梯度的数量和最大池化大小
constexpr size_t NUM_CLASSES = 6;
constexpr size_t MAX_POOLED_SIZE = 512;

// 预定义的尺寸等级：16, 32, 64, 128, 256, 512
constexpr size_t SIZE_CLASSES[NUM_CLASSES] = {16, 32, 64, 128, 256, 512};

// 全局静态的资源池数组
static GlobalSizeClassPool global_pools[NUM_CLASSES];

// 3. 线程局部缓存 (Thread-Local Storage Cache)
// 每个线程独享，无锁分配！
struct ThreadLocalCache
{
    FreeNode* head = nullptr;
    size_t count = 0;
    static constexpr size_t BATCH_SIZE = 32;   // 批量向全局池借/还的数量
    static constexpr size_t MAX_CAPACITY = 64; // 本地缓存上限，防止跨线程释放导致内存泄漏
};

// 使用 thread_local 关键字实现 TLS
thread_local ThreadLocalCache tls_caches[NUM_CLASSES];

// 4. 核心分配器
class CoroMemoryPool
{
    // 将实际大小映射到尺寸等级索引
    static int get_class_index(size_t size)
    {
        if (size <= 16)
            return 0;
        if (size <= 32)
            return 1;
        if (size <= 64)
            return 2;
        if (size <= 128)
            return 3;
        if (size <= 256)
            return 4;
        if (size <= 512)
            return 5;
        return -1; // 超过 512 字节
    }

  public:
    static void* allocate(std::size_t size)
    {
        int index = get_class_index(size);
        if (index == -1)
        {
            // 超大协程帧，退化为操作系统调用
            return ::operator new(size);
        }

        ThreadLocalCache& tls = tls_caches[index];

        // Fast-path: 线程本地有缓存，直接无锁拿取 (O(1))
        if (tls.head)
        {
            FreeNode* node = tls.head;
            tls.head = tls.head->next;
            tls.count--;
            return node;
        }

        // Slow-path: 线程本地为空，向全局池批量“进货”
        size_t fetched = 0;
        FreeNode* batch = global_pools[index].fetch_batch(tls.BATCH_SIZE, fetched);

        if (batch)
        {
            // 留下一个给自己用，剩下的挂入 TLS
            FreeNode* result = batch;
            tls.head = batch->next;
            tls.count = fetched - 1;
            return result;
        }

        // 终极 Fallback: 全局池也空了，向系统申请。
        // （在真正的工业级实现中，这里会一次性 malloc 一大块 Block 然后切分成小块，这里简化处理）
        return ::operator new(SIZE_CLASSES[index]);
    }

    static void deallocate(void* ptr, std::size_t size)
    {
        if (!ptr)
            return;

        int index = get_class_index(size);
        if (index == -1)
        {
            ::operator delete(ptr, size);
            return;
        }

        ThreadLocalCache& tls = tls_caches[index];
        FreeNode* node = static_cast<FreeNode*>(ptr);

        // Fast-path: 无锁挂入线程本地缓存 (O(1))
        node->next = tls.head;
        tls.head = node;
        tls.count++;

        // 跨线程释放防御机制：如果某个线程一直在释放（比如专门处理 I/O 唤醒的线程）
        // TLS 缓存会爆满，需要批量归还给全局池，供其他线程复用
        if (tls.count >= tls.MAX_CAPACITY)
        {
            FreeNode* batch_head = tls.head;
            FreeNode* batch_tail = tls.head;

            // 找到前 BATCH_SIZE 个节点
            for (size_t i = 1; i < tls.BATCH_SIZE; ++i)
            {
                batch_tail = batch_tail->next;
            }

            // 将前一半保留在 TLS
            tls.head = batch_tail->next;
            tls.count -= tls.BATCH_SIZE;

            // 将截取出来的链表归还给全局池
            batch_tail->next = nullptr;
            global_pools[index].return_batch(batch_head, batch_tail);
        }
    }
};