
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
auto main_coro() -> utils::MainCoroutine
{
    WaitGroup wg;
    for (int i = 0; i < 4; i++)
    {
        wg.add(1);
        co_spawn([&] {
            std::cout << "hello\n";
            wg.done();
        });
    }
    co_await wg.wait();

    co_return 0;
}
} // namespace utils
