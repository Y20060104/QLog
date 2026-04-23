

namespace qlog
{
namespace util
{
template<typename T> inline T roundup_pow_of_two(T x)
{
    if ((x & (x - 1)) == 0)
    {
        return x;
    }
    T result = 1;
    while (x > 0)
    {
        x = x >> 1;
        result = result << 1;
    }
    return result;
}
} // namespace util
} // namespace qlog