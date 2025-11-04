#include "coroutine/channel.h"
#include "coroutine/coroutine.h"
#include <iostream>
#include <thread>
auto func(utils::Channel<> *channel) -> utils::Coroutine {
  co_await channel->send();
  std::cout << "func send" << std::endl;
  co_await channel->recv();
  std::cout << "func recv" << std::endl;
  co_return;
}
auto func_A() -> utils::Coroutine {
  utils::Channel<> chan;
  func(&chan);
  co_await chan.recv();
  std::cout << "func recv" << std::endl;
  co_await chan.send();
  std::cout << "func send" << std::endl;
}
auto utils::main_coro() -> utils::Coroutine {
  func_A();
  co_return;
}