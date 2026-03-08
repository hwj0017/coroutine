#include "coroutine/syscall.h"
#include <atomic>
#include <cstddef>
#include <netinet/in.h>
#include <span>
#include <string>
namespace utils
{
// TCP 连接句柄（供用户读写）
class TcpConnection
{
  public:
    explicit TcpConnection(int sockfd, sockaddr_in addr);
    ~TcpConnection();
    auto get_id() const { return id_; }
    // 异步读：读取最多 n 字节
    // 返回实际读取字节数，0 表示对端关闭
    auto read(std::span<char> buffer) -> utils::ReadAwaiter;

    // 异步写：写入 n 字节
    // 返回实际写入字节数（通常等于 n，除非出错）
    auto write(std::span<const char> buffer) -> utils::WriteAwaiter;

    // TODO
    // 关闭连接（优雅关闭）
    void close() {}

    // 获取远端地址（可选）
    std::string remote_address() const;

    // 禁止拷贝，允许移动（但通常用 shared_ptr 管理）
    TcpConnection(const TcpConnection&) = delete;
    TcpConnection& operator=(const TcpConnection&) = delete;

  private:
    size_t id_;
    int sockfd_;
    sockaddr_in addr_;
    static std::atomic<size_t> next_id_;
};
} // namespace utils
