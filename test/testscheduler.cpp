#include "coroutine/channel.h"
#include "coroutine/main.h"
#include <iostream>
#include <mutex>

std::mutex mtx;
auto funcA(int i, utils::Channel<>& c) -> utils::Coroutine
{

    std::cout << "funcA " + std::to_string(i) + "\n";
    co_await c.send();
    co_return;
}
auto utils::main_coro() -> utils::Coroutine
{
    Channel<> c(1000);
    for (int i = 0; i < 1000; ++i)
    {
        funcA(i, c)();
    }
    for (int i = 0; i < 1000; ++i)
    {
        co_await c.recv();
        std::cout << "recv " + std::to_string(i) + "\n";
    }
    co_return;
}