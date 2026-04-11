#pragma once
#include "coroutine/coroutine.h"
namespace utils
{
// 代替主函数
auto main_coro() -> MainCoroutine;

} // namespace utils