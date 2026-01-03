#include "coroutine/main.h"
#include "schedule.h"
#include <cassert>
#include <iostream>
namespace utils
{
auto listen_main() -> utils::Coroutine<>
{
    co_await utils::main_coro();
    std::cout << "main coroutine finished" << std::endl;
    std::exit(0);
}
} // namespace utils

int main()
{
    utils::co_spawn(utils::listen_main());
    utils::schedule();
    return 0;
}
