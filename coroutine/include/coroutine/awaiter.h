
#include <atomic>
#include <coroutine>
#include <tuple>
#include <utility>
namespace utils
{
// 1. 先定义基础的 await_suspend 返回值约束
template <typename T>
concept await_suspend_result = std::same_as<T, void> || std::same_as<T, bool> ||
                               requires(T t) { std::coroutine_handle<>{t}; }; // 检查是否能隐式转为 handle

// 2. 定义核心 Concept
template <typename T>
concept cancellable_awaiter = requires(T t, std::coroutine_handle<> h) {
    // Awaitable 标准接口约束
    { t.await_ready() } -> std::convertible_to<bool>;
    { t.await_suspend(h) } -> await_suspend_result;
    t.await_resume();

    // 取消接口约束：必须拥有 cancel 成员函数
    { t.cancel() } noexcept;
};
template <cancellable_awaiter L, cancellable_awaiter R> class SelectAwaiter
{
  public:
    SelectAwaiter(L left, R right) : left_(left), right_(right) {}
    bool await_ready() { return left_.await_ready() && right_.await_ready(); }
    auto await_resume()
    {
        if (left_.await_resume())
        {
            return left_;
        }
        return right_;
    }

    L left_;
    R right_;
};
} // namespace utils
