#pragma once
#include <span>
#include <vector>

class Buffer
{
  public:
    explicit Buffer(size_t initial_size = 1024) : buffer_(initial_size), read_idx_(0), write_idx_(0) {}

    // 获取可供 Parser 读取的数据视图 (std::span)
    std::span<char> readable_span() { return {buffer_.data() + read_idx_, readable_bytes()}; }

    // Parser 吃掉数据后，只移动 read_idx，不产生内存拷贝！
    void retrieve(size_t bytes)
    {
        if (bytes < readable_bytes())
        {
            read_idx_ += bytes;
        }
        else
        {
            // 数据全吃光了，重置指针，这也是一种零拷贝优化
            read_idx_ = 0;
            write_idx_ = 0;
        }
    }

    // 准备从 Socket 读数据前，获取可写的内存区域
    std::span<char> writable_span(size_t reserve_size = 1024)
    {
        ensure_writable(reserve_size);
        return {buffer_.data() + write_idx_, buffer_.size() - write_idx_};
    }

    // Socket 读完数据后，推进 write_idx
    void commit_write(size_t bytes) { write_idx_ += bytes; }

    bool empty() const { return read_idx_ == write_idx_; }

  private:
    std::vector<char> buffer_;
    size_t read_idx_;
    size_t write_idx_;

    size_t readable_bytes() const { return write_idx_ - read_idx_; }
    size_t writable_bytes() const { return buffer_.size() - write_idx_; }
    size_t prependable_bytes() const { return read_idx_; }

    // 核心逻辑：空间不够时的扩容或内存整理
    void ensure_writable(size_t len)
    {
        if (writable_bytes() >= len)
        {
            return; // 空间够，什么都不用做
        }

        // 空间不够，但如果 "废弃区 + 空闲区" 的总和够用，就做一次内存碎片整理 (Compact)
        if (prependable_bytes() + writable_bytes() >= len)
        {
            size_t readable = readable_bytes();
            // 把现存的有用的数据平移到最前面（这是唯一一次真正发生内存移动的地方，但频率极低）
            std::copy(buffer_.begin() + read_idx_, buffer_.begin() + write_idx_, buffer_.begin());
            read_idx_ = 0;
            write_idx_ = readable;
        }
        else
        {
            // 真不够了，只能重新分配内存扩容
            buffer_.resize(write_idx_ + len);
        }
    }
};