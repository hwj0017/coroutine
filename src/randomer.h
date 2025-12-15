
#include <random>
namespace utils
{

class Randomer
{
  public:
    Randomer() = default;
    int random(int start, int end)
    {
        std::uniform_int_distribution<int> dist(start, end - 1);
        return dist(rng_);
    }

  private:
    std::mt19937 rng_;
};
} // namespace utils