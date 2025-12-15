#include "coroutine/main.h"
#include "schedulerinterface.h"
#include <iostream>
#include <memory>

auto listen_main() -> utils::Coroutine
{
    co_await utils::main_coro();
    std::cout << "main coroutine finished" << std::endl;
    std::exit(0);
}

int main()
{
    listen_main()();
    utils::SchedulerInterface::instance().schedule();
    return 0;
}
