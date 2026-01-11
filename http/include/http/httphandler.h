#include "coroutine/coroutine.h"
#include <functional>
namespace utils
{
class HttpContext;
using HttpHandler = std::function<Coroutine<>(HttpContext*)>;

} // namespace utils