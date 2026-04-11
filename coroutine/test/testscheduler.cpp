#include "coroutine/channel.h"
#include "coroutine/coroutine.h"
#include "coroutine/main.h"
#include <atomic>
#include <cstddef>
#include <iostream>
#include <mutex>

std::atomic<size_t> counter{0};
auto funcA(int i, utils::Channel<>& c) -> utils::Coroutine<>
{
    std::cout << "funcA " + std::to_string(i) + "\n";
    counter++;
    co_await c.send();

    co_return;
}
auto utils::main_coro() -> MainCoroutine
{
    std::cout << "main\n";
    Channel<> c(1000);
    for (int i = 0; i < 1000; ++i)
    {
        co_spawn(funcA(i, c));
    }
    for (int i = 0; i < 1000; ++i)
    {
        co_await c.recv();
        // std::cout << "recv " + std::to_string(i) + "\n";
    }
    std::cout << "counter: " + std::to_string(counter.load()) + "\n";
    co_return 0;
}