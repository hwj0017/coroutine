#include "coroutine/channel.h"
#include "coroutine/coroutine.h"
#include "coroutine/main.h"
#include <atomic>
#include <cstddef>
#include <iostream>
#include <mutex>

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
std::atomic<size_t> counter{0};
auto funcA(int i, utils::Channel<void, 1000>& c) -> utils::Coroutine<>
{
    std::cout << "funcA " + std::to_string(i) + "\n";
    counter++;
    co_await c.send();

    co_return;
}
auto utils::main_coro() -> MainCoroutine
{
    std::cout << "main\n";
    Channel<void, 1000> c;
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
    std::cout << "new count: " + std::to_string(new_count.load()) + "\n";
    std::cout << "delete count: " + std::to_string(delete_count.load()) + "\n";
    co_return 0;
}