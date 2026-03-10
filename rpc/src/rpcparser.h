#pragma once
#include "rpc/common.h"
#include <cstddef>
#include <cstdint>
#include <span>
namespace utils
{
class RpcParser
{
  private:
    // 安全防御：限制单次 RPC 调用的最大负载为 16MB，防止恶意发超大包撑爆内存
    static constexpr uint32_t MAX_META_SIZE = 1024;
    static constexpr uint32_t MAX_BODY_SIZE = 16 * 1024 * 1024;

    size_t consumed_bytes_{0}; // 记录上一次成功解析消耗了多少字节

  public:
    RpcParser() = default;
    // 获取消耗的字节数，供外部从缓冲区中抹除
    size_t get_consumed_bytes() const { return consumed_bytes_; }

    // 重置状态
    void reset() { consumed_bytes_ = 0; }

    // 核心解析逻辑
    RpcParseResult parse(std::span<char> buffer, RpcMessage& out_msg)
    {
        // 1. 检查是否连 Header 的长度都不够
        if (buffer.size() < sizeof(RpcHeader))
        {
            return RpcParseResult::Incomplete;
        }

        // 2. 提取 Header 并转换大端序
        const RpcHeader* raw_header = reinterpret_cast<const RpcHeader*>(buffer.data());

        // 校验魔数
        uint16_t magic = be16toh(raw_header->magic);
        if (magic != RpcHeader::EXPECTED_MAGIC)
        {
            return RpcParseResult::Error;
        }

        // 3. 提取两个长度字段
        uint32_t method_len = be32toh(raw_header->method_length);
        uint32_t body_len = be32toh(raw_header->body_length);

        // 安全校验：防止 OOM 攻击
        if (method_len > MAX_META_SIZE || body_len > MAX_BODY_SIZE)
        {
            return RpcParseResult::Error;
        }

        // 4. 计算总长度：Header + Meta + Body
        size_t total_packet_size = sizeof(RpcHeader) + method_len + body_len;

        // 5. 检查缓冲区数据是否足够
        if (buffer.size() < total_packet_size)
        {
            return RpcParseResult::Incomplete;
        }

        // --- 逻辑分水岭：数据已完整 ---

        // 6. 填充 Header 到输出对象
        out_msg.header.magic = magic;
        out_msg.header.version = raw_header->version;
        out_msg.header.msg_type = raw_header->msg_type;
        out_msg.header.serialize_type = raw_header->serialize_type;
        out_msg.header.compress_type = raw_header->compress_type;
        out_msg.header.status_code = be16toh(raw_header->status_code);
        out_msg.header.sequence_id = be64toh(raw_header->sequence_id);
        out_msg.header.method_length = method_len;
        out_msg.header.body_length = body_len;

        // 7. 解析 method
        const char* method_ptr = buffer.data() + sizeof(RpcHeader);
        out_msg.method.assign(method_ptr, method_len);

        // 8. 提取 Body (Payload)
        const char* body_ptr = method_ptr + method_len;
        out_msg.payload.assign(body_ptr, body_len);

        // 9. 更新消耗字节数
        consumed_bytes_ = total_packet_size;

        return RpcParseResult::Success;
    }
};
} // namespace utils
