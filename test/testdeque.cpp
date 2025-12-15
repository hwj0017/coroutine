#include "concurrentdeque.h" // 你的 WorkStealingDeque 头文件
#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <random>
#include <thread>
#include <vector>

using namespace utils;

// 用于验证任务完整性
std::atomic<size_t> g_executed{0};
std::atomic<size_t> g_expected{0};

// 生成随机任务 ID
thread_local std::mt19937 rng(std::random_device{}());

// === 测试 1: 单线程基本操作 ===
void test_single_thread()
{
    std::cout << "=== Test 1: Single-thread push/pop ===\n";
    WorkStealingDeque<int> deque(8); // capacity=8 (2^3)

    // Push 5 elements
    for (int i = 0; i < 5; ++i)
    {
        deque.push_back(i);
    }
    assert(deque.size() == 5);
    assert(!deque.empty());

    // Pop 5 elements (LIFO)
    for (int i = 4; i >= 0; --i)
    {
        int val = deque.pop_back();
        assert(val == i);
    }
    assert(deque.empty());

    // Batch push
    std::vector<int> batch = {10, 20, 30};
    deque.push_back(batch);
    assert(deque.size() == 3);

    // Batch pop
    auto popped = deque.pop_back(3);
    assert(popped.size() == 3);
    assert(popped[0] == 30 && popped[1] == 20 && popped[2] == 10); // LIFO

    std::cout << "✅ Single-thread test passed.\n\n";
}

// === 测试 2: Work Stealing (Owner + Thieves) ===
void test_work_stealing()
{
    std::cout << "=== Test 2: Work Stealing ===\n";
    const size_t num_tasks = 256;
    const size_t num_thieves = 3;
    WorkStealingDeque<int> deque(256);

    g_expected = num_tasks;
    g_executed = 0;

    // Owner: 提交任务（ID 从 1 开始，避免 0）
    std::thread owner([&]() {
        for (size_t i = 0; i < num_tasks; ++i)
        {
            deque.push_back(static_cast<int>(i + 1)); // ✅ ID=1,2,...,256
        }
    });

    std::vector<std::thread> thieves;
    for (size_t t = 0; t < num_thieves; ++t)
    {
        thieves.emplace_back([&]() {
            while (g_executed.load() < g_expected.load())
            {
                // 单任务窃取（0 表示空）
                if (int task = deque.pop_front(); task != 0)
                {
                    g_executed.fetch_add(1, std::memory_order_relaxed);
                }
                else
                {
                    // 批量窃取
                    auto tasks = deque.pop_front(10);
                    for (int task : tasks)
                    {
                        if (task != 0)
                        {
                            g_executed.fetch_add(1, std::memory_order_relaxed);
                        }
                    }
                    if (tasks.empty())
                    {
                        std::this_thread::yield();
                    }
                }
            }
        });
    }

    owner.join();
    for (auto& t : thieves)
        t.join();

    // Owner 消费剩余任务
    while (deque.pop_back() != 0)
    {
        g_executed.fetch_add(1, std::memory_order_relaxed);
    }

    assert(g_executed == g_expected);
    std::cout << "✅ Work stealing test passed (" << g_executed << " tasks executed).\n\n";
}

// === 测试 3: 队列满/空边界 ===
void test_boundaries()
{
    std::cout << "=== Test 3: Capacity boundaries ===\n";
    WorkStealingDeque<int> deque(4); // capacity=4

    // Fill to full
    for (int i = 0; i < 4; ++i)
    {
        deque.push_back(i);
    }
    assert(deque.full());
    assert(deque.size() == 4);

    // Push more should assert (but we can't test assert in unit test)
    // Instead, verify pop works
    for (int i = 3; i >= 0; --i)
    {
        assert(deque.pop_back() == i);
    }
    assert(deque.empty());

    // Batch push exactly capacity
    std::vector<int> batch = {1, 2, 3, 4};
    deque.push_back(batch);
    assert(deque.full());

    auto popped = deque.pop_back(10);
    assert(popped.size() == 4);
    assert(popped[0] == 4 && popped[1] == 3 && popped[2] == 2 && popped[3] == 1);

    std::cout << "✅ Boundary test passed.\n\n";
}

// === 测试 4: 竞争检测（模拟 owner/thief 竞争）===
void test_competition()
{
    std::cout << "=== Test 4: Competition handling ===\n";
    WorkStealingDeque<int> deque(2); // 小容量，易竞争

    // 一个任务
    deque.push_back(42);

    // Owner 尝试 pop
    int owner_val = deque.pop_back();
    // Thief 尝试 steal（应该失败，因为 owner 已取走）
    int thief_val = deque.pop_front();

    // 只能有一个成功
    size_t success_count = (owner_val == 42) + (thief_val == 42);
    assert(success_count == 1);

    std::cout << "✅ Competition test passed.\n\n";
}

// === 测试 5: 指针类型 ===
void test_pointer_type()
{
    std::cout << "=== Test 5: Pointer type ===\n";
    struct Task
    {
        int id;
    };
    WorkStealingDeque<Task*> deque(8);

    Task t1{1}, t2{2};
    deque.push_back(&t1);
    deque.push_back(&t2);

    // Owner pop
    Task* p1 = deque.pop_back();
    assert(p1 == &t2);

    // Thief steal
    Task* p2 = deque.pop_front();
    assert(p2 == &t1);

    std::cout << "✅ Pointer type test passed.\n\n";
}

int main()
{
    try
    {
        test_single_thread();
        test_boundaries();
        test_competition();
        test_pointer_type();
        test_work_stealing(); // 放最后，因耗时较长

        std::cout << "🎉 All tests passed!\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Test failed: " << e.what() << "\n";
        return 1;
    }
}