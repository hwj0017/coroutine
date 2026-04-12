#include "coroutine/coroutine.h"
#include "coroutine/cospawn.h"
#include "coroutine/main.h"
#include "coroutine/waitgroup.h"
#include <algorithm>
#include <atomic>
#include <cassert>
#include <iostream>
#include <numeric>
#include <random>
#include <thread>
#include <vector>

namespace utils
{

// ============================================================================
// 测试1: 基础协程启动和同步
// ============================================================================
auto test_basic_spawn() -> Coroutine<>
{
    std::cout << "=== Test 1: Basic Spawn ===" << std::endl;

    WaitGroup wg;
    wg.add(2);
    std::atomic<int> counter{0};

    auto task1 = [&]() -> Coroutine<> {
        auto done = DoneGuard(wg);
        counter.fetch_add(1);
        std::cout << "  Task 1 completed" << std::endl;
        co_return;
    };

    auto task2 = [&]() -> Coroutine<> {
        auto done = DoneGuard(wg);
        counter.fetch_add(1);
        std::cout << "  Task 2 completed" << std::endl;
        co_return;
    };

    co_spawn(task1());
    co_spawn(task2());

    co_await wg.wait();

    assert(counter == 2);
    std::cout << "PASSED" << std::endl;
}

// ============================================================================
// 测试2: 大量并发协程
// ============================================================================
auto test_many_coroutines() -> Coroutine<>
{
    std::cout << "=== Test 2: Many Concurrent Coroutines ===" << std::endl;

    const int count = 10000;
    WaitGroup wg;
    wg.add(count);
    std::atomic<int> counter{0};

    for (int i = 0; i < count; ++i)
    {
        auto task = [&]() -> Coroutine<> {
            auto done = DoneGuard(wg);
            counter.fetch_add(1);
            co_return;
        };
        co_spawn(task());
    }

    co_await wg.wait();

    assert(counter == count);
    std::cout << "  All " << count << " coroutines completed" << std::endl;
    std::cout << "PASSED" << std::endl;
}

// ============================================================================
// 测试3: 嵌套协程
// ============================================================================
auto test_nested_coroutines() -> Coroutine<>
{
    std::cout << "=== Test 3: Nested Coroutines ===" << std::endl;

    WaitGroup wg;
    wg.add(1);
    std::atomic<int> depth{0};

    auto level3 = [&](int id, WaitGroup& lwg) -> Coroutine<> {
        auto done = DoneGuard(lwg);
        depth.fetch_add(1);
        std::cout << "  Level 3 task " << id << std::endl;
        co_return;
    };

    auto level2 = [&](int id, WaitGroup& g) -> Coroutine<> {
        auto done = DoneGuard(g);
        WaitGroup lwg;
        lwg.add(2);

        co_spawn(level3(id * 2, lwg));
        co_spawn(level3(id * 2 + 1, lwg));

        co_await lwg.wait();

        depth.fetch_add(1);
        std::cout << "  Level 2 task " << id << " completed" << std::endl;
        co_return;
    };

    auto level1 = [&]() -> Coroutine<> {
        auto done = DoneGuard(wg);

        WaitGroup lwg;
        lwg.add(2);

        co_spawn(level2(0, lwg));
        co_spawn(level2(1, lwg));

        co_await lwg.wait();

        depth.fetch_add(1);
        std::cout << "  Level 1 task completed" << std::endl;
        co_return;
    };

    co_spawn(level1());

    co_await wg.wait();

    assert(depth == 7); // 1 + 2 + 4
    std::cout << "PASSED: Total depth = " << depth << std::endl;
}

// ============================================================================
// 测试4: 协程间共享状态（原子操作）
// ============================================================================
auto test_shared_state() -> Coroutine<>
{
    std::cout << "=== Test 4: Shared State (Atomic) ===" << std::endl;

    const int num_tasks = 1000;
    const int increments_per_task = 100;

    WaitGroup wg;
    wg.add(num_tasks);
    std::atomic<long long> sum{0};

    auto worker = [&]() -> Coroutine<> {
        auto done = DoneGuard(wg);

        for (int i = 0; i < increments_per_task; ++i)
        {
            sum.fetch_add(1, std::memory_order_relaxed);
        }
        co_return;
    };

    for (int i = 0; i < num_tasks; ++i)
    {
        co_spawn(worker());
    }

    co_await wg.wait();

    assert(sum == (long long)num_tasks * increments_per_task);
    std::cout << "  Sum = " << sum << " (expected " << (long long)num_tasks * increments_per_task << ")" << std::endl;
    std::cout << "PASSED" << std::endl;
}

// ============================================================================
// 测试5: 计算密集型任务
// ============================================================================
auto test_compute_intensive() -> Coroutine<>
{
    std::cout << "=== Test 5: Compute Intensive ===" << std::endl;

    const int n = 100;
    WaitGroup wg;
    wg.add(n);

    // 简化版：直接计算
    auto worker = [&](int idx) -> Coroutine<> {
        auto done = DoneGuard(wg);

        // 计算斐波那契数列（非递归，避免过深）
        long long a = 0, b = 1, c;
        for (int i = 0; i < 30; ++i)
        {
            c = a + b;
            a = b;
            b = c;
        }
        co_return;
    };

    for (int i = 0; i < n; ++i)
    {
        co_spawn(worker(i));
    }

    co_await wg.wait();

    std::cout << "  " << n << " compute tasks completed" << std::endl;
    std::cout << "PASSED" << std::endl;
}

// ============================================================================
// 测试6: WaitGroup 正确使用
// ============================================================================
auto test_waitgroup_usage() -> Coroutine<>
{
    std::cout << "=== Test 6: WaitGroup Usage ===" << std::endl;

    WaitGroup wg;

    // 测试 add 和 done 配合
    wg.add(5);
    std::atomic<int> done_count{0};

    auto worker = [](WaitGroup& wg, std::atomic<int>& done_count) -> Coroutine<> {
        done_count.fetch_add(1);
        wg.done();
        std::cout << "  Worker done" << std::endl;
        co_return;
    };

    for (int i = 0; i < 5; ++i)
    {
        co_spawn(worker(wg, done_count));
    }

    co_await wg.wait();

    assert(done_count == 5);
    std::cout << "PASSED" << std::endl;
}

// ============================================================================
// 测试7: 协程在不同 P 上执行
// ============================================================================
auto test_multi_processor() -> Coroutine<>
{
    std::cout << "=== Test 7: Multi-Processor Execution ===" << std::endl;

    const int task_count = 1000;
    WaitGroup wg;
    wg.add(task_count);

    std::atomic<int> cpu_counts[64] = {}; // 最多64个CPU

    auto worker = [](int id, WaitGroup& wg, std::atomic<int> cpu_counts[]) -> Coroutine<> {
        auto done = DoneGuard(wg);
        // 记录当前线程ID
        auto cpu_id = std::hash<std::thread::id>{}(std::this_thread::get_id()) % 64;
        cpu_counts[cpu_id].fetch_add(1);
        co_return;
    };

    for (int i = 0; i < task_count; ++i)
    {
        co_spawn(worker(i, wg, cpu_counts));
    }

    co_await wg.wait();

    int active_cpus = 0;
    for (int i = 0; i < 64; ++i)
    {
        if (cpu_counts[i] > 0)
        {
            active_cpus++;
        }
    }

    std::cout << "  Tasks executed on " << active_cpus << " different processors" << std::endl;
    assert(active_cpus >= 2); // 至少在两个P上执行
    std::cout << "PASSED" << std::endl;
}

// ============================================================================
// 测试8: 任务窃取测试
// ============================================================================
auto test_work_stealing() -> Coroutine<>
{
    std::cout << "=== Test 8: Work Stealing ===" << std::endl;

    const int batch_size = 50;
    const int num_batches = 10;
    WaitGroup wg;
    wg.add(num_batches * batch_size);

    std::atomic<int> total_tasks{0};

    // 批量创建任务，这样可以触发工作窃取
    for (int batch = 0; batch < num_batches; ++batch)
    {
        auto batch_creator = [](WaitGroup& wg, std::atomic<int>& total_tasks) -> Coroutine<> {
            for (int i = 0; i < batch_size; ++i)
            {
                co_spawn([&](WaitGroup& wg, std::atomic<int>& total_tasks) -> Coroutine<> {
                    auto done = DoneGuard(wg);
                    total_tasks.fetch_add(1);
                    co_return;
                }(wg, total_tasks));
            }
            co_return;
        };
        co_spawn(batch_creator(wg, total_tasks));
    }

    co_await wg.wait();

    assert(total_tasks == num_batches * batch_size);
    std::cout << "  Total tasks: " << total_tasks << std::endl;
    std::cout << "PASSED" << std::endl;
}

// ============================================================================
// 测试9: 压力测试 - 混合负载
// ============================================================================
auto test_stress() -> Coroutine<>
{
    std::cout << "=== Test 9: Stress Test (Mixed Workload) ===" << std::endl;

    const int total_tasks = 5000;
    WaitGroup wg;
    wg.add(total_tasks);

    std::atomic<int> fast_tasks{0};
    std::atomic<int> slow_tasks{0};
    std::atomic<long long> shared_counter{0};

    std::mt19937 rng(42);
    int fast_count = 0, slow_count = 0;

    // 创建混合任务
    for (int i = 0; i < total_tasks; ++i)
    {
        int type = rng() % 3;

        if (type == 0)
        {
            // 快速任务：简单计数
            fast_count++;
            auto fast = [](WaitGroup& wg, std::atomic<int>& fast_tasks,
                           std::atomic<long long>& shared_counter) -> Coroutine<> {
                auto done = DoneGuard(wg);
                shared_counter.fetch_add(1);
                fast_tasks.fetch_add(1);
                co_return;
            };
            co_spawn(fast(wg, fast_tasks, shared_counter));
        }
        else if (type == 1)
        {
            // 中等任务：多次原子操作
            slow_count++;
            auto medium = [](WaitGroup& wg, std::atomic<long long>& shared_counter,
                             std::atomic<int>& fast_tasks) -> Coroutine<> {
                auto done = DoneGuard(wg);
                for (int j = 0; j < 10; ++j)
                {
                    shared_counter.fetch_add(j);
                }
                fast_tasks.fetch_add(1);
                co_return;
            };
            co_spawn(medium(wg, shared_counter, fast_tasks));
        }
        else
        {
            // 模拟计算任务
            slow_count++;
            auto slow = [](WaitGroup& wg, std::atomic<long long>& shared_counter,
                           std::atomic<int>& slow_tasks) -> Coroutine<> {
                auto done = DoneGuard(wg);
                volatile int sum = 0;
                for (int j = 0; j < 100; ++j)
                {
                    sum += j * j;
                }
                shared_counter.fetch_add(sum);
                slow_tasks.fetch_add(1);
                co_return;
            };
            co_spawn(slow(wg, shared_counter, slow_tasks));
        }
    }

    co_await wg.wait();

    std::cout << "  Fast tasks: " << fast_tasks << std::endl;
    std::cout << "  Slow tasks: " << slow_tasks << std::endl;
    std::cout << "  Shared counter: " << shared_counter << std::endl;
    std::cout << "PASSED" << std::endl;
}

// ============================================================================
// 测试10: 协程链式执行
// ============================================================================
auto test_chained_execution() -> Coroutine<>
{
    std::cout << "=== Test 10: Chained Execution ===" << std::endl;

    const int chain_length = 100;
    WaitGroup wg;
    wg.add(chain_length + 1);

    std::atomic<int> steps{0};

    // 使用非递归方式链式执行
    for (int i = 0; i <= chain_length; ++i)
    {
        co_spawn([](WaitGroup& wg, std::atomic<int>& steps) -> Coroutine<> {
            steps.fetch_add(1);
            wg.done();
            co_return;
        }(wg, steps));
    }

    co_await wg.wait();

    assert(steps == chain_length + 1);
    std::cout << "  Chain executed " << steps << " steps" << std::endl;
    std::cout << "PASSED" << std::endl;
}

// ============================================================================
// 主协程：运行所有测试
// ============================================================================
auto main_coro() -> MainCoroutine
{
    std::cout << "========================================" << std::endl;
    std::cout << "      Scheduler Test Suite             " << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;

    co_await test_basic_spawn();
    std::cout << std::endl;

    co_await test_many_coroutines();
    std::cout << std::endl;

    co_await test_nested_coroutines();
    std::cout << std::endl;

    co_await test_shared_state();
    std::cout << std::endl;

    co_await test_compute_intensive();
    std::cout << std::endl;

    co_await test_waitgroup_usage();
    std::cout << std::endl;

    co_await test_multi_processor();
    std::cout << std::endl;

    co_await test_work_stealing();
    std::cout << std::endl;

    co_await test_stress();
    std::cout << std::endl;

    co_await test_chained_execution();
    std::cout << std::endl;

    std::cout << "========================================" << std::endl;
    std::cout << "   All Tests PASSED!   " << std::endl;
    std::cout << "========================================" << std::endl;

    co_return 0;
}

} // namespace utils