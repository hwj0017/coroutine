#pragma once
#include <atomic>
#include <coroutine>
namespace utils
{
using Handle = std::coroutine_handle<>;
class Promise
{
  public:
    Promise() : id_(next_id_++) {}
    size_t get_id() { return id_; }

  protected:
    size_t id_{};
    static std::atomic<size_t> next_id_;
};
inline std::atomic<size_t> Promise::next_id_{0};
inline auto& promise(Handle handle) { return std::coroutine_handle<Promise>::from_address(handle.address()).promise(); }

} // namespace utils