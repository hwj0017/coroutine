#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <list>
#include <vector>

using Clock = std::chrono::steady_clock;
using MS = std::chrono::milliseconds;

struct TimerTask
{
    uint64_t expire_tick;
    void* data;
};

struct WheelLevel
{
    std::vector<std::list<TimerTask>> slots;
    std::vector<uint64_t> bitmap; // 核心：增加位图，每个 bit 对应一个槽位
    uint64_t slot_mask;
    uint64_t shift;
    uint64_t total_range;
    WheelLevel(size_t bits, uint64_t accumulated_shift)
        : slots(1ULL << bits),
          // 如果槽位 < 64，只需要 1 个 uint64_t；如果是 256，需要 4 个 uint64_t
          bitmap((1ULL << bits) > 64 ? (1ULL << (bits - 6)) : 1, 0), slot_mask((1ULL << bits) - 1),
          shift(accumulated_shift), total_range(1ULL << (accumulated_shift + bits))
    {
    }

    // 设置第 slot 个位为 1
    inline void set_bit(size_t slot) { bitmap[slot >> 6] |= (1ULL << (slot & 63)); }

    // 清除第 slot 个位（置为 0）
    inline void clear_bit(size_t slot) { bitmap[slot >> 6] &= ~(1ULL << (slot & 63)); }
};

class BitwiseTimerWheel
{
  public:
    BitwiseTimerWheel(MS interval, const std::vector<size_t>& level_bits)
        : tick_interval_ms(interval.count()), current_tick(0)
    {
        assert(!level_bits.empty());
        last_tick_time = current_ms();

        uint64_t accumulated_shift = 0;
        for (size_t bits : level_bits)
        {
            levels.emplace_back(bits, accumulated_shift);
            accumulated_shift += bits;
        }
        assert(accumulated_shift <= 64);
        max_range = (accumulated_shift == 64) ? ~0ULL : (1ULL << accumulated_shift);
    }

    std::vector<void*> update()
    {
        std::vector<void*> results;
        uint64_t now = current_ms();
        uint64_t ticks_to_process = (now - last_tick_time) / tick_interval_ms;

        for (uint64_t i = 0; i < ticks_to_process; ++i)
        {
            current_tick++;

            size_t l0_slot = current_tick & levels[0].slot_mask;

            // 齿轮传动：底层转满一圈时，推动高层降级
            if (l0_slot == 0)
            {
                for (size_t lvl = 1; lvl < levels.size(); ++lvl)
                {
                    size_t slot = (current_tick >> levels[lvl].shift) & levels[lvl].slot_mask;

                    if (!levels[lvl].slots[slot].empty())
                    {
                        std::list<TimerTask> tasks;
                        tasks.splice(tasks.end(), levels[lvl].slots[slot]);

                        // 🌟 槽位被清空，立刻清除位图标志
                        levels[lvl].clear_bit(slot);

                        for (auto& task : tasks)
                        {
                            insert_task(std::move(task));
                        }
                    }

                    if (slot != 0)
                        break;
                }
            }

            // 执行底层到期任务
            if (!levels[0].slots[l0_slot].empty())
            {
                for (auto& task : levels[0].slots[l0_slot])
                {
                    results.push_back(task.data);
                }
                levels[0].slots[l0_slot].clear();

                // 🌟 底层任务执行完毕，清除位图标志
                levels[0].clear_bit(l0_slot);
            }
        }

        last_tick_time += ticks_to_process * tick_interval_ms;
        return results;
    }

    void add_timer(MS delay, void* data)
    {
        uint64_t delay_ticks = delay.count() / tick_interval_ms;
        if (delay_ticks == 0)
            delay_ticks = 1;
        if (delay_ticks >= max_range)
            delay_ticks = max_range - 1;

        TimerTask task{current_tick + delay_ticks, data};
        insert_task(std::move(task));
    }

    // 🌟 终极位图加速查找
    int64_t get_next_timeout() const
    {
        uint64_t min_expire = UINT64_MAX;
        bool found = false;

        // 从最低层开始找（低层任务永远比高层任务先到期）
        for (size_t i = 0; i < levels.size(); ++i)
        {
            // 扫描该层的位图数组
            for (size_t w = 0; w < levels[i].bitmap.size(); ++w)
            {
                uint64_t word = levels[i].bitmap[w];

                // 只要这个 word 不为 0，说明里面有槽位包含任务
                while (word != 0)
                {
                    // 硬件级指令：获取最低位连续的 0 的个数 (即最低位的 1 的索引)
                    // 注意: __builtin_ctzll 是 GCC/Clang 特有指令
                    int bit_idx = __builtin_ctzll(word);

                    size_t slot = (w << 6) + bit_idx; // 还原真实的槽位索引

                    // 遍历该槽位，寻找真实的最小 expire_tick
                    for (const auto& task : levels[i].slots[slot])
                    {
                        if (task.expire_tick < min_expire)
                        {
                            min_expire = task.expire_tick;
                            found = true;
                        }
                    }

                    // 极速清除最低位的 1，继续寻找这个 word 里下一个非空的槽位
                    word &= word - 1;
                }
            }

            // 只要在这一层找到了任务，就绝对不用再去高层找了！
            if (found)
                break;
        }

        if (!found)
            return -1; // 整个轮子是空的

        if (min_expire <= current_tick)
            return 0; // 已经有任务过期，应该立刻 update

        // 返回还有多少毫秒到期
        return (min_expire - current_tick) * tick_interval_ms;
    }

  private:
    void insert_task(TimerTask task)
    {
        uint64_t diff = task.expire_tick - current_tick;

        for (size_t i = 0; i < levels.size(); ++i)
        {
            if (diff < levels[i].total_range || i == levels.size() - 1)
            {
                size_t slot = (task.expire_tick >> levels[i].shift) & levels[i].slot_mask;
                levels[i].slots[slot].push_back(std::move(task));

                // 🌟 新增任务，设置位图标志
                levels[i].set_bit(slot);
                return;
            }
        }
    }

    uint64_t current_ms() const { return std::chrono::duration_cast<MS>(Clock::now().time_since_epoch()).count(); }

    uint64_t tick_interval_ms;
    uint64_t last_tick_time;
    uint64_t current_tick;
    uint64_t max_range;
    std::vector<WheelLevel> levels;
};