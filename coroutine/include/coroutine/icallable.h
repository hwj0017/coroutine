#pragma once
#include <atomic>
#include <cstddef>
#include <utility>
namespace utils
{
class ICallable
{
  public:
    ICallable() : id_(next_id_++) {}
    virtual void invoke() = 0;
    virtual void destroy() = 0;
    size_t id() const { return id_; }

  private:
    size_t id_;
    static std::atomic<size_t> next_id_;
};
inline std::atomic<size_t> ICallable::next_id_{0};

class Task
{
  private:
    // 2. 模板派生类：负责存储具体的 Lambda/仿函数 F
    template <typename F> struct CallableHolder final : public ICallable
    {
        F f;
        explicit CallableHolder(F&& func) : f(std::forward<F>(func)) {}

        void invoke() override { f(); }
    };

    // 唯一的成员：指向堆上对象的指针
    ICallable* callable_;

  public:
    Task() = default;

    // 3. 构造函数：接受任何符合 void() 签名的对象
    // 使用 std::decay_t 去掉引用和 const 修饰符
    template <typename F> Task(F&& f) : callable_(new CallableHolder<std::decay_t<F>>(std::forward<F>(f))) {}

    // 4. 移动构造与移动赋值 (std::unique_ptr 默认支持)
    Task(Task&&) noexcept = default;
    Task& operator=(Task&&) noexcept = default;

    // 5. 执行接口
    void operator()() const
    {
        if (callable_)
        {
            callable_->invoke();
        }
    }

    // 显式检查是否有效
    explicit operator bool() const { return static_cast<bool>(callable_); }

    // 禁用拷贝（Task 通常作为独占资源在协程间传递）
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
};
} // namespace utils
