#include "coroutine/coroutine.h"
#include "coroutine/main.h"
#include "coroutine/syscall.h"
#include <atomic>
#include <cstddef>
#include <iostream>
#include <mutex>

auto utils::main_coro() -> MainCoroutine
{
    co_await delay(5);
    co_return 0;
}