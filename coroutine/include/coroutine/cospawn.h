#pragma once
#include "coroutine/icallable.h"
namespace utils
{
// 2. 模板派生类：负责存储具体的 Lambda/仿函数 F

template <std::invocable F> void co_spawn(F&& func, bool yield = false)
{
    using DecayedF = std::decay_t<F>;

    // 1. 直接将类定义在函数内部！
    class CallableHolder final : public ICallable
    {
      public:
        // 接收退化后的类型
        explicit CallableHolder(DecayedF f_val) : f(std::move(f_val)) {}

        void invoke() override
        {
            f();
            delete this;
        }
        void destroy() override { delete this; }

      private:
        DecayedF f;
    };
    auto call = new CallableHolder(std::forward<F>(func));
    co_spawn(call);
}
void co_spawn(ICallable* call, bool yield = false);
} // namespace utils