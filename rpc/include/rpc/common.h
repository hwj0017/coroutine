#pragma once
#include <cstdint>
#include <endian.h> // Linux 下用于网络字节序转换: be32toh, be64toh
#include <span>
#include <string>
#include <string_view>
#include <vector>
namespace utils
{
// 1. 强制 1 字节对齐的协议头
#pragma pack(push, 1)
struct RpcHeader
{
    static constexpr uint16_t EXPECTED_MAGIC = 0x4252; // 'B' 'R'
    uint16_t magic;                                    // 魔数 (预期 0x4252)
    uint8_t version;                                   // 版本号
    uint8_t msg_type;                                  // 消息类型
    uint8_t serialize_type;                            // 序列化类型
    uint8_t compress_type;                             // 压缩类型
    uint16_t status_code;                              // 状态码
    uint64_t sequence_id;                              // 请求序列号 (非常重要)
    uint32_t method_length;                            // method的长度
    uint32_t body_length;                              // 实际业务数据 (Payload) 的长度
    RpcHeader() : magic(0x4252) {}
};
#pragma pack(pop)

// 2. 解析出来的完整 RPC 消息对象
struct RpcMessage
{
    RpcHeader header;
    std::string method;
    std::string payload;
    RpcMessage() = default;
    auto string()
    {
        header.magic = RpcHeader::EXPECTED_MAGIC;
        header.method_length = method.size();
        header.body_length = payload.size();
        return std::string(reinterpret_cast<const char*>(&header), sizeof(RpcHeader)) + method + payload;
    }
};

// 3. 解析器状态机返回值
enum class RpcParseResult
{
    Success,    // 成功解析出一个完整的包
    Incomplete, // 数据不够，继续等
    Error       // 数据损坏或不合法（魔数错误、包太长等）
};
} // namespace utils
