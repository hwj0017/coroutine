#include "coroutine/intrusivelist.h"
#include <algorithm> // for std::min
#include <atomic>
#include <cassert>
#include <cstddef>
#include <iostream>
#include <span>
#include <type_traits> // for static_assert
#include <vector>

namespace utils
{

// 多线程读单线程写的双端队列

} // namespace utils