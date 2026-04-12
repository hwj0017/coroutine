#pragma once
#include <cassert>
#include <cstddef>
#include <utility>

namespace utils
{

/**
 * @brief 侵入式单向链表节点基类
 *
 * 派生类可以直接作为链表节点使用，无需额外的包装结构。
 * 链表操作通过静态方法提供，避免虚函数开销。
 */
class IntrusiveListNode
{
  public:
    IntrusiveListNode() noexcept : next_(nullptr) {}
    IntrusiveListNode(const IntrusiveListNode&) = delete;
    IntrusiveListNode& operator=(const IntrusiveListNode&) = delete;

    // 检查节点是否在链表中
    bool is_linked() const noexcept { return next_ != nullptr; }

  protected:
    mutable IntrusiveListNode* next_;

    friend class IntrusiveList;
};

/**
 * @brief 侵入式单向链表管理类
 */
class IntrusiveList
{
  public:
    IntrusiveList() noexcept : head_(nullptr), tail_(nullptr), size_(0) {}
    IntrusiveList(IntrusiveListNode* node) noexcept : head_(node), tail_(node), size_(1)
    {
        assert(node);
        node->next_ = nullptr;
    }
    // 禁止拷贝
    IntrusiveList(const IntrusiveList&) = delete;
    IntrusiveList& operator=(const IntrusiveList&) = delete;

    // 支持移动
    IntrusiveList(IntrusiveList&& other) noexcept : head_(other.head_), tail_(other.tail_), size_(other.size_)
    {
        other.head_ = nullptr;
        other.tail_ = nullptr;
        other.size_ = 0;
    }

    IntrusiveList& operator=(IntrusiveList&& other) noexcept
    {
        if (this != &other)
        {
            head_ = other.head_;
            tail_ = other.tail_;
            size_ = other.size_;
            other.head_ = nullptr;
            other.tail_ = nullptr;
            other.size_ = 0;
        }
        return *this;
    }

    /**
     * @brief 将节点添加到链表尾部
     */
    void push_back(IntrusiveListNode* node) noexcept
    {
        assert(node);
        if (tail_)
        {
            tail_->next_ = node;
        }
        else
        {
            head_ = node;
        }
        tail_ = node;
        node->next_ = nullptr;
        ++size_;
    }

    void push_back(IntrusiveList other) noexcept
    {
        if (other.empty())
        {
            return;
        }
        if (head_ == nullptr)
        {
            *this = std::move(other);
            return;
        }
        tail_->next_ = other.head_;
        tail_ = other.tail_;
        size_ += other.size_;
    }

    void push_front(IntrusiveListNode* node) noexcept
    {
        assert(node);
        node->next_ = head_;
        head_ = node;
        if (!tail_)
        {
            tail_ = node;
        }
        ++size_;
    }

    /**
     * @brief 从链表头部移除节点
     * @return 移除的节点，如果链表为空则返回nullptr
     */
    IntrusiveListNode* pop_front() noexcept
    {
        if (!head_)
        {
            return nullptr;
        }
        IntrusiveListNode* node = head_;
        head_ = head_->next_;
        if (!head_)
        {
            tail_ = nullptr;
        }
        node->next_ = nullptr;
        --size_;

        return node;
    }
    /**
     * @brief 从链表头部移除链表
     * @return 移除的链表
     */
    IntrusiveList pop_front(size_t count) noexcept
    {
        IntrusiveList result;
        for (size_t i = 0; i < count && !empty(); ++i)
        {
            result.push_back(pop_front());
        }
        return result;
    }

    /**
     * @brief 从链表中移除指定节点（需要O(n)查找）
     * @return 如果找到并移除成功返回true，否则返回false
     */
    bool remove(IntrusiveListNode* node) noexcept
    {
        if (!node || !node->is_linked())
        {
            return false;
        }

        if (head_ == node)
        {
            pop_front();
            return true;
        }

        IntrusiveListNode* current = head_;
        while (current && current->next_ != node)
        {
            current = current->next_;
        }

        if (current)
        {
            current->next_ = node->next_;
            if (tail_ == node)
            {
                tail_ = current;
            }
            node->next_ = nullptr;
            --size_;

            return true;
        }
        return false;
    }

    /**
     * @brief 清空链表（不释放节点内存）
     */
    void clear() noexcept
    {
        head_ = tail_ = nullptr;
        size_ = 0;
        //  // clear后状态总是有效的
    }

    bool empty() const noexcept { return size_ == 0; }
    size_t size() const noexcept { return size_; }
    IntrusiveListNode* front() const noexcept { return head_; }
    IntrusiveListNode* back() const noexcept { return tail_; }

  private:
    IntrusiveListNode* head_;
    IntrusiveListNode* tail_;
    size_t size_;
};

/**
 * @brief 侵入式双向链表节点基类
 */
class IntrusiveListDNode
{
  public:
    IntrusiveListDNode() noexcept : next_(nullptr), prev_(nullptr) {}
    IntrusiveListDNode(const IntrusiveListDNode&) = delete;
    IntrusiveListDNode& operator=(const IntrusiveListDNode&) = delete;

    // 检查节点是否在链表中
    bool is_linked() const noexcept { return next_ != nullptr || prev_ != nullptr; }

    /**
     * @brief 将当前节点从链表中删除
     * 如果节点不在链表中，则什么也不做
     */
    void remove_from_list() noexcept
    {
        if (!is_linked())
        {
            return;
        }

        if (prev_)
        {
            prev_->next_ = next_;
        }
        if (next_)
        {
            next_->prev_ = prev_;
        }

        // 重置当前节点的指针
        next_ = nullptr;
        prev_ = nullptr;
    }

  protected:
    mutable IntrusiveListDNode* next_;
    mutable IntrusiveListDNode* prev_;

    friend class IntrusiveDList;
};

/**
 * @brief 侵入式双向链表管理类
 */
class IntrusiveDList
{
  public:
    IntrusiveDList() noexcept : head_(nullptr), tail_(nullptr), size_(0) {}

    // 禁止拷贝
    IntrusiveDList(const IntrusiveDList&) = delete;
    IntrusiveDList& operator=(const IntrusiveDList&) = delete;

    /**
     * @brief 将节点添加到链表尾部
     */
    void push_back(IntrusiveListDNode* node) noexcept
    {
        node->next_ = nullptr;
        node->prev_ = tail_;
        if (tail_)
        {
            tail_->next_ = node;
        }
        else
        {
            head_ = node;
        }
        tail_ = node;
        ++size_;
    }

    /**
     * @brief 将节点添加到链表头部
     */
    void push_front(IntrusiveListDNode* node) noexcept
    {
        node->prev_ = nullptr;
        node->next_ = head_;
        if (head_)
        {
            head_->prev_ = node;
        }
        else
        {
            tail_ = node;
        }
        head_ = node;
        ++size_;
    }

    /**
     * @brief 从链表头部移除节点
     */
    IntrusiveListDNode* pop_front() noexcept
    {
        if (!head_)
        {
            return nullptr;
        }
        IntrusiveListDNode* node = head_;
        head_ = head_->next_;
        if (head_)
        {
            head_->prev_ = nullptr;
        }
        else
        {
            tail_ = nullptr;
        }
        node->next_ = node->prev_ = nullptr; // 清除链接状态
        --size_;

        return node;
    }

    /**
     * @brief 从链表尾部移除节点
     */
    IntrusiveListDNode* pop_back() noexcept
    {
        if (!tail_)
        {
            return nullptr;
        }
        IntrusiveListDNode* node = tail_;
        tail_ = tail_->prev_;
        if (tail_)
        {
            tail_->next_ = nullptr;
        }
        else
        {
            head_ = nullptr;
        }
        node->next_ = node->prev_ = nullptr; // 清除链接状态
        --size_;

        return node;
    }

    /**
     * @brief 从链表中移除指定节点（O(1)操作）
     */
    void remove(IntrusiveListDNode* node) noexcept
    {
        if (!node->is_linked())
        {
            return;
        }

        if (node->prev_)
        {
            node->prev_->next_ = node->next_;
        }
        else
        {
            head_ = node->next_;
        }

        if (node->next_)
        {
            node->next_->prev_ = node->prev_;
        }
        else
        {
            tail_ = node->prev_;
        }

        node->next_ = node->prev_ = nullptr;
        --size_;
    }

    /**
     * @brief 在指定节点后插入新节点
     */
    void insert_after(IntrusiveListDNode* pos, IntrusiveListDNode* node) noexcept
    {
        if (!pos)
        {
            push_front(node);
            return;
        }
        node->next_ = pos->next_;
        node->prev_ = pos;
        if (pos->next_)
        {
            pos->next_->prev_ = node;
        }
        else
        {
            tail_ = node;
        }
        pos->next_ = node;
        ++size_;
    }

    /**
     * @brief 在指定节点前插入新节点
     */
    void insert_before(IntrusiveListDNode* pos, IntrusiveListDNode* node) noexcept
    {
        if (!pos)
        {
            push_back(node);
            return;
        }
        node->prev_ = pos->prev_;
        node->next_ = pos;
        if (pos->prev_)
        {
            pos->prev_->next_ = node;
        }
        else
        {
            head_ = node;
        }
        pos->prev_ = node;
        ++size_;
    }

    /**
     * @brief 清空链表（不释放节点内存）
     */
    void clear() noexcept
    {
        head_ = tail_ = nullptr;
        size_ = 0;
    }

    bool empty() const noexcept { return size_ == 0; }
    size_t size() const noexcept { return size_; }
    IntrusiveListDNode* front() const noexcept { return head_; }
    IntrusiveListDNode* back() const noexcept { return tail_; }

  private:
    IntrusiveListDNode* head_;
    IntrusiveListDNode* tail_;
    size_t size_;
};

} // namespace utils