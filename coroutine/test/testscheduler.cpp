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
    co_await c.send();
    counter++;
    co_return;
}
auto utils::main_coro() -> MainCoroutine
{
    Channel<> c(1000);
    for (int i = 0; i < 1; ++i)
    {
        co_spawn(funcA(i, c));
    }
    release();
    for (int i = 0; i < 1; ++i)
    {
        co_await c.recv();
        std::cout << "recv " + std::to_string(i) + "\n";
        if (i == 55)
        {
            co_yield {};
        }
    }
    co_return 0;
}