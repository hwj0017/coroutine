
// 你的库定义的 main 协程入口
#include "coroutine/channel.h"
#include "coroutine/coroutine.h"
#include "coroutine/waitgroup.h"
#include <chrono>
#include <cstdlib>
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

auto main_coro() -> utils::MainCoroutine
{
    void* ptr = malloc(100);
    std::cout << "--- Starting Benchmarks (C++ Coroutines) ---" << std::endl;

    // 1. 测试延迟
    co_await benchmark_ping_pong(10000);
    exit(0);
    co_return 0;
}
} // namespace utils
