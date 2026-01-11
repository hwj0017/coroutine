
#include <atomic>
#include <coroutine>
#include <tuple>
#include <utility>
namespace utils
{
class AwaiterBase
{
  public:
    virtual void ready(AwaiterBase* child) = 0;
    void set_parent(AwaiterBase* parent) { parent_ = parent; }
    AwaiterBase* parent_;
};
template <typename T> class Awaiter : public AwaiterBase
{
  public:
    using ValueType = T;
    Awaiter() = default;
    virtual bool await_ready() = 0;
    virtual void await_suspend(std::coroutine_handle<> handle) = 0;
    auto await_resume() -> T { return std::move(value_); }

    auto set_value(T&& value) -> std::coroutine_handle<>
    {
        value_ = std::move(value);
        if (parent_->ready(this))
        {
            return handle;
        }
    }
    virtual void cancel() = 0;

  private:
    ValueType value_;
    std::coroutine_handle<> handle;
};

template <typename T>
concept IsAwaiter = std::is_base_of_v<utils::Awaiter<typename T::ValueType>, T>;
// all
template <IsAwaiter L, IsAwaiter R> class Select
{
  public:
    Select(L l, R r) : l_(l), r_(r)
    {
        l_.set_parent(this);
        r_.set_parent(this);
    }
    bool ready(AwaiterBase* child)
    {
        // 已经准备好了
        if (ready_.exchange(true))
        {
            return false;
        }
        if (child == l_)
        {
            r_.cancel();
        }
        else
        {
            l_.cancel();
        }
        return true;
    }
    bool await_ready() { return l_->await_ready() || r_->await_ready(); }
    bool await_suspend(std::coroutine_handle<> handle)
    {
        if (!l_.await_suspend(handle))
        {
            return false;
        }
        return r_.await_suspend(handle);
    }
    auto await_resume() { return std::make_tuple(l_.await_resume(), r_.await_resume()); }

  private:
    std::atomic<bool> ready_{false};
    L l_;
    R r_;
};

template <typename L, typename R> auto operator|(L l, R r) { return Select<L, R>(l, r); }
} // namespace utils
