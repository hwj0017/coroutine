#include "coroutine/main.h"
#include "schedule.h"
#include <cassert>
#include <iostream>

int main()
{
    utils::co_spawn(utils::main_coro());
    utils::schedule();
    return 0;
}
