#include "coroutine/channel.h"
#include "coroutine/coroutine.h"
#include "coroutine/main.h"
#include "coroutine/mutex.h"
#include <cstddef>

utils::Mutex mtx;
size_t count = 0;
auto func(utils::Channel<>& ch) -> utils::Coroutine<>
{
    {
        auto lock = co_await mtx.guard();
        count++;
        if (count == 1000)
        {
            co_await ch.send();
        }
    }
    co_return;
}

auto utils::main_coro() -> MainCoroutine
{
    utils::Channel<> ch;
    for (size_t i = 0; i < 1000; i++)
    {
        co_spawn(func(ch));
    }

    co_await ch.recv();
    co_return 0;
}