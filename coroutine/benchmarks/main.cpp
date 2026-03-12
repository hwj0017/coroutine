
// 你的库定义的 main 协程入口
#include "coroutine/channel.h"
#include "coroutine/coroutine.h"
#include "coroutine/waitgroup.h"
#include <chrono>
#include <iostream>
#include <vector>

using namespace std::chrono;
namespace utils
{
// 防止编译器优化掉读取结果
template <class T> void do_not_optimize(T&& val) { asm volatile("" : : "g"(val) : "memory"); }

// --- 实验 1: Ping-Pong 延迟 (1v1 强同步) ---
// 测试单次上下文切换 + Channel 发送/接收的开销
auto benchmark_ping_pong(int n) -> Coroutine<>
{
    auto chan_a = Channel<>(0); // 无缓冲 channel
    auto chan_b = Channel<>(0);

    // 启动对端协程
    co_spawn([](int n, Channel<>& a, Channel<>& b) -> Coroutine<> {
        for (int i = 0; i < n; ++i)
        {
            co_await a.recv();
            co_await b.send();
        }
    }(n, chan_a, chan_b));

    auto start = high_resolution_clock::now();

    for (int i = 0; i < n; ++i)
    {
        co_await chan_a.send();
        auto res = co_await chan_b.recv();
        do_not_optimize(res);
    }

    auto end = high_resolution_clock::now();
    auto total_ns = duration_cast<nanoseconds>(end - start).count();

    std::cout << "Ping-Pong: " << total_ns / (n * 2) << " ns/op (per switch)" << std::endl;
}

// --- 实验 2: MPMC 吞吐量 (多生产者多消费者) ---
// 测试 Channel 在高竞争下的锁竞争或无锁性能
auto benchmark_throughput(int total_msgs, int p_count, int c_count) -> Coroutine<>
{
    auto ch = Channel<>(1024); // 有缓冲 channel

    auto start = high_resolution_clock::now();
    auto c_num = total_msgs / c_count;
    auto p_num = total_msgs / p_count;
    WaitGroup wg;
    // 启动消费者
    for (int i = 0; i < c_count; ++i)
    {
        wg.add(1);
        co_spawn([](Channel<>& ch, int num, WaitGroup& wg) -> Coroutine<> {
            for (int j = 0; j < num; ++j)
            {
                auto v = co_await ch.recv();
                do_not_optimize(v);
            }
            wg.done();
        }(ch, c_num, wg));
    }

    // 启动生产者
    for (int i = 0; i < p_count; ++i)
    {
        wg.add(1);
        co_spawn([](Channel<>& ch, int p_num, WaitGroup& wg) -> Coroutine<> {
            for (int j = 0; j < p_num; ++j)
            {
                co_await ch.send();
            }
            wg.done();
        }(ch, p_num, wg));
    }

    // 假设你有某种机制等待所有协程结束，类似 WaitGroup
    // co_await all_tasks_done();

    co_await wg.wait();
    auto end = high_resolution_clock::now();
    // 计算每秒处理的消息数...
    double time = std::chrono::duration<double>(end - start).count();
    std::cout << "Processed " << (total_msgs / (time == 0 ? 1 : time)) << " msgs/sec" << std::endl;
}
auto main_coro() -> utils::MainCoroutine
{
    std::cout << "--- Starting Benchmarks (C++ Coroutines) ---" << std::endl;

    // 1. 测试延迟
    co_await benchmark_ping_pong(10000000);

    // 2. 测试吞吐 (4 生产者, 4 消费者)
    // 注意：确保你的调度器已经开启了多线程 Worker
    co_await benchmark_throughput(10000000, 16, 16);

    co_return 0;
}
} // namespace utils
