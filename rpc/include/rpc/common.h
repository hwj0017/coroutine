#pragma once
#include <algorithm>
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
class RpcHeader
{
  public:
    static constexpr uint16_t EXPECTED_MAGIC = 0x4252; // 'B' 'R'
  private:
    uint16_t magic_{htobe16(EXPECTED_MAGIC)}; // 魔数 (预期 0x4252)
    uint8_t version_;                         // 版本号
    uint8_t msg_type_;                        // 消息类型
    uint8_t serialize_type_;                  // 序列化类型
    uint8_t compress_type_;                   // 压缩类型
    uint16_t status_code_;                    // 状态码
    uint64_t sequence_id_;                    // 请求序列号 (非常重要)
    uint32_t method_length_;                  // method的长度
    uint32_t body_length_;                    // 实际业务数据 (Payload) 的长度
  public:
    RpcHeader() {}

    void set_version(uint8_t version) { version_ = version; }
    void set_msg_type(uint8_t type) { msg_type_ = type; }
    void set_serialize_type(uint8_t type) { serialize_type_ = type; }
    void set_compress_type(uint8_t type) { compress_type_ = type; }

    void set_status_code(uint16_t status_code) { status_code_ = htobe16(status_code); }
    void set_sequence_id(uint64_t seq_id) { sequence_id_ = htobe64(seq_id); }
    void set_method_length(uint32_t len) { method_length_ = htobe32(len); }
    void set_body_length(uint32_t len) { body_length_ = htobe32(len); }

    // --- Getters (内部进行网络字节序到主机序的转换) ---

    uint16_t get_magic() const { return be16toh(magic_); }
    uint8_t get_version() const { return version_; }
    uint8_t get_msg_type() const { return msg_type_; }
    uint8_t get_serialize_type() const { return serialize_type_; }
    uint8_t get_compress_type() const { return compress_type_; }

    uint16_t get_status_code() const { return be16toh(status_code_); }
    uint64_t get_sequence_id() const { return be64toh(sequence_id_); }
    uint32_t get_method_length() const { return be32toh(method_length_); }
    uint32_t get_body_length() const { return be32toh(body_length_); }

    // 校验魔数是否合法
    bool check_magic() const { return get_magic() == EXPECTED_MAGIC; }
};
#pragma pack(pop)

// 2. 解析出来的完整 RPC 消息对象
class RpcMessage
{
  public:
    RpcHeader header;
    std::string method;
    std::string payload;
    RpcMessage() = default;
    auto string() { return std::string(reinterpret_cast<const char*>(&header), sizeof(RpcHeader)) + method + payload; }
};

// 3. 解析器状态机返回值
enum class RpcParseResult
{
    Success,    // 成功解析出一个完整的包
    Incomplete, // 数据不够，继续等
    Error       // 数据损坏或不合法（魔数错误、包太长等）
};
} // namespace utils
