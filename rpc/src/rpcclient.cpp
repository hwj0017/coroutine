
#include "rpc/rpcclient.h"
#include "coroutine/icallable.h"
#include "rpcparser.h"
namespace utils
{
auto RpcClient::write_worker() -> Coroutine<>
{
    while (true)
    {
        auto [req, state] = co_await pending_.recv();
        if (state != State::OK)
        {
            co_return;
        }
        RpcMessage msg;
        // TODO
        msg.header.sequence_id = req.sequence_id;
        msg.method = std::move(req.method);
        msg.payload = std::move(req.payload);
        msg.header.method_length = msg.method.size();
        msg.header.body_length = msg.payload.size();
        if (auto count = co_await socket_.write(msg.string()); count < 0)
        {
            co_return;
        }
    }
}
auto RpcClient::read_worker() -> Coroutine<>
{
    constexpr size_t BufferSize = 1024;
    std::vector<char> buffer;
    RpcParser parser;
    RpcMessage msg;

    while (true)
    {
        // 1. 动态扩容并读取网络数据 (避免局部 temp_buffer 拷贝)
        size_t old_size = buffer.size();
        buffer.resize(old_size + BufferSize);

        auto n = co_await socket_.read({buffer.data() + old_size, BufferSize});
        if (n <= 0)
            break;

        buffer.resize(old_size + n);

        // 2. 循环丢给 Parser 切包
        while (!buffer.empty())
        {
            auto result = parser.parse({buffer.data(), buffer.size()}, msg);

            if (result == RpcParseResult::Error)
            {
                // 协议错误，立刻断开连接
                socket_.close();
                co_return;
            }
            else if (result == RpcParseResult::Incomplete)
            {
                // 半包，跳出内层循环，去外层 co_await 读更多数据
                break;
            }
            else if (result == RpcParseResult::Success)
            {
                ICallable* handle = nullptr;
                {
                    auto guard = std::lock_guard(mutex_);
                    if (auto it = awaiters_.find(msg.header.sequence_id); it == awaiters_.end())
                    {
                        responses_.emplace(msg.header.sequence_id, RpcResponse{std::move(msg.payload), false});
                    }
                    else
                    {
                        handle = it->second->set_value({std::move(msg.payload), false});
                        awaiters_.erase(it);
                    }
                }
                if (handle)
                {
                    co_spawn(handle);
                }
                buffer.erase(buffer.begin(), buffer.begin() + parser.get_consumed_bytes());
                parser.reset();
            }
        }
    }
}

} // namespace utils