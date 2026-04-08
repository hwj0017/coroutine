#include "coroutine/main.h"
#include "schedule.h"
#include <cassert>
#include <csignal>
#include <cstdlib>
#include <iostream>
namespace utils
{
class ListenCoroutine : public CoroutineBase
{
  public:
    class promise_type : public Promise
    {
      public:
        auto get_return_object() -> ListenCoroutine { return ListenCoroutine(this); }
        void return_void() {}
    };
    ListenCoroutine(promise_type* promise) : CoroutineBase(promise) {}
};

auto listen() -> ListenCoroutine
{
    auto ret = co_await main_coro();
    std::exit(ret);
}
} // namespace utils

int main()
{
    std::signal(SIGPIPE, SIG_IGN);
    utils::co_spawn(utils::listen());
    utils::schedule();
    return 0;
}
