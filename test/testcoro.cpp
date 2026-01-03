#include "coroutine/coroutine.h"
#include <iostream>

auto funcA() -> utils::Coroutine<int> { co_return 10; }
auto funcB() -> utils::Coroutine<>
{
    std::cout << co_await funcA() << std::endl;
    co_return;
}

int main() { utils::co_spawn(funcB()); }