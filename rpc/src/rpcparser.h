#pragma once
#include "rpc/common.h"
#include <cstddef>
#include <cstdint>
#include <span>

namespace utils
{
class RpcParser
{
  public:
    enum class State
    {
        ExpectHeader,
        ExpectBody
    };

  private:
    static constexpr uint32_t MAX_META_SIZE = 1024;
    static constexpr uint32_t MAX_BODY_SIZE = 16 * 1024 * 1024;

    State state_{State::ExpectHeader};
    // 记录当前正在解析的包的长度信息，避免重复计算
    uint32_t method_len_{0};
    uint32_t body_len_{0};
    size_t consumed_bytes_{0};

  public:
    RpcParser() = default;

    auto get_state() const -> State { return state_; }
    size_t get_consumed_bytes() const { return consumed_bytes_; }
    size_t get_expected_bytes() const
    {
        if (state_ == State::ExpectHeader)
        {
            return sizeof(RpcHeader);
        }
        else
        {
            // 如果 Header 已经解析完成，期望长度就是整个包的真实长度
            return sizeof(RpcHeader) + method_len_ + body_len_;
        }
    }
    void reset()
    {
        state_ = State::ExpectHeader;
        consumed_bytes_ = 0;
        method_len_ = 0;
        body_len_ = 0;
    }

    RpcParseResult parse(std::span<char> buffer, RpcMessage& out_msg)
    {
        consumed_bytes_ = 0;

        // --- 阶段 1：解析并填充 Header ---
        if (state_ == State::ExpectHeader)
        {
            if (buffer.size() < sizeof(RpcHeader))
            {
                return RpcParseResult::Incomplete;
            }

            const RpcHeader* raw_header = reinterpret_cast<const RpcHeader*>(buffer.data());

            // 1. 校验魔数
            if (raw_header->get_magic() != RpcHeader::EXPECTED_MAGIC)
            {
                return RpcParseResult::Error;
            }

            // 2. 直接写入目标对象 out_msg
            out_msg.header = *raw_header;
            method_len_ = out_msg.header.get_method_length();
            body_len_ = out_msg.header.get_body_length();

            // 3. 安全校验
            if (method_len_ > MAX_META_SIZE || body_len_ > MAX_BODY_SIZE)
            {
                return RpcParseResult::Error;
            }

            state_ = State::ExpectBody;
        }

        // --- 阶段 2：基于已填充的 Header 解析 Body ---
        if (state_ == State::ExpectBody)
        {
            size_t total_packet_size = sizeof(RpcHeader) + method_len_ + body_len_;

            if (buffer.size() < total_packet_size)
            {
                // 虽然 Header 已经填进去了，但数据没够，告诉外部还需要更多数据
                return RpcParseResult::Incomplete;
            }

            // 4. 解析 Method (Payload 1)
            const char* method_ptr = buffer.data() + sizeof(RpcHeader);
            out_msg.method.assign(method_ptr, method_len_);

            // 5. 解析 Body (Payload 2)
            const char* body_ptr = method_ptr + method_len_;
            out_msg.payload.assign(body_ptr, body_len_);

            consumed_bytes_ = total_packet_size;

            // 成功解析完一个整包，重置状态以便解析下一个
            state_ = State::ExpectHeader;
            return RpcParseResult::Success;
        }

        return RpcParseResult::Incomplete;
    }
};
} // namespace utils