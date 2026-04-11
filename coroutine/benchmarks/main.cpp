// 你的库定义的 main 协程入口
#include "coroutine/channel.h"
#include "coroutine/coroutine.h"
#include "coroutine/waitgroup.h"
#include <atomic>
#include <chrono>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <vector>

using namespace std::chrono;

std::atomic<size_t> new_count{0};
std::atomic<size_t> delete_count{0};
void* operator new(std::size_t size)
{
    ++new_count;
    void* p = std::malloc(size);
    return p;
}

void operator delete(void* ptr) noexcept
{
    ++delete_count;
    std::free(ptr);
}

namespace utils
{
// 防止编译器优化掉读取结果
template <class T> void do_not_optimize(T&& val) { asm volatile("" : : "g"(val) : "memory"); }

// =====================================================================
// --- 实验 1: 海量协程创建与频繁 Yield (测试纯粹的状态机切换开销) ---
// =====================================================================
auto benchmark_yield(int num_routines, int num_yields) -> Coroutine<>
{
    WaitGroup wg;
    auto start = high_resolution_clock::now();

    for (int i = 0; i < num_routines; ++i)
    {
        wg.add(1);
        co_spawn([](int yields, WaitGroup& wg) -> Coroutine<> {
            for (int j = 0; j < yields; ++j)
            {
                co_yield {};
            }
            wg.done();
        }(num_yields, wg));
    }

    co_await wg.wait();
    auto end = high_resolution_clock::now();
    auto total_ns = duration_cast<nanoseconds>(end - start).count();
    long long total_switches = (long long)num_routines * num_yields;

    std::cout << "[1] Yield Context Switch Benchmark\n";
    std::cout << "    Coroutines created : " << num_routines << "\n";
    std::cout << "    Yields per routine : " << num_yields << "\n";
    std::cout << "    Total switches     : " << total_switches << "\n";
    std::cout << "    Time per switch    : " << total_ns / total_switches << " ns\n\n";
}

// =====================================================================
// --- 实验 2: Ping-Pong 延迟 (1v1 强同步) ---
// =====================================================================
auto benchmark_ping_pong(int n) -> Coroutine<>
{
    auto chan_a = Channel<>(0); // 无缓冲 channel
    auto chan_b = Channel<>(0);

    // 启动对端协程
    co_spawn([](int n, Channel<>& a, Channel<>& b) -> Coroutine<> {
        for (int i = 0; i < n; ++i)
        {
            co_await a.recv();
            co_await b.send(); // 注意：如果你的 send 需要参数，请填入假数据如 send(1)
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

    std::cout << "[2] Ping-Pong Latency Benchmark\n";
    std::cout << "    Iterations       : " << n << "\n";
    std::cout << "    Ping-Pong latency: " << total_ns / (n * 2) << " ns/op (per switch)\n\n";
}

// =====================================================================
// --- 实验 3: MPMC 吞吐量 (多生产者多消费者) ---
// =====================================================================
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
                co_await ch.send(); // 同理，如果 send 需参，传入伪数据
            }
            wg.done();
        }(ch, p_num, wg));
    }

    co_await wg.wait();
    auto end = high_resolution_clock::now();

    double time_sec = std::chrono::duration<double>(end - start).count();
    double msgs_per_sec = total_msgs / (time_sec == 0 ? 1.0 : time_sec);

    std::cout << "[3] MPMC Throughput Benchmark\n";
    std::cout << "    Producers : " << p_count << "\n";
    std::cout << "    Consumers : " << c_count << "\n";
    std::cout << "    Total Msgs: " << total_msgs << "\n";
    std::cout << "    Throughput: " << std::fixed << std::setprecision(0) << msgs_per_sec << " msgs/sec\n\n";
}

auto main_coro() -> utils::Coroutine<int>
{
    std::cout << "=== C++ Stackless Coroutine Benchmark Suite ===\n\n";

    // 1. 测试基础上下文切换性能 (100万协程，各切换100次)
    co_await benchmark_yield(1000000, 100);

    // 2. 测试延迟 (1000万次互相发送接收)
    co_await benchmark_ping_pong(10000000);

    // 3. 测试吞吐 (16 生产者, 16 消费者)
    // 注意：如果是单线程调度器，测试结果仅代表无锁情况下的吞吐；
    // 如果支持多线程 Work-Stealing 调度，则会对标 Go 的 GOMAXPROCS>1 场景。
    co_await benchmark_throughput(10000000, 16, 16);
    std::cout << "new count: " << new_count.load() << ", delete count: " << delete_count.load() << "\n";
    co_return 0; // 或者不返回，取决于 Coroutine<int> 的实现
}
} // namespace utils