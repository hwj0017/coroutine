#include "coroutine/main.h"
#include "coroutine/coroutine.h"
#include "schedule.h"
#include <cassert>
#include <csignal>
#include <cstdlib>
#include <iostream>

int main()
{
    std::signal(SIGPIPE, SIG_IGN);
    utils::co_spawn(utils::main_coro());
    utils::schedule();
    return 0;
}
