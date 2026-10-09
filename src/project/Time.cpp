#include "Project.h"
#include <intrin.h>
#include <climits>
#include <limits>
#include <numeric>

namespace editor::project {
namespace {
bool valid(Rational rate, int sampleRate) {
    return rate.numerator > 0 && rate.numerator <= INT_MAX && rate.denominator > 0 &&
        rate.denominator <= INT_MAX && std::gcd(rate.numerator, rate.denominator) == 1 &&
        sampleRate >= 8000 && sampleRate <= 384000;
}
bool validRate(Rational rate) {
    return rate.numerator > 0 && rate.numerator <= INT_MAX && rate.denominator > 0 &&
        rate.denominator <= INT_MAX && std::gcd(rate.numerator, rate.denominator) == 1;
}
std::optional<qint64> scale(qint64 value, unsigned __int64 multiplier, unsigned __int64 divisor, Rounding rounding) {
    if (value < 0) return {};
    unsigned __int64 high = 0, remainder = 0;
    const auto low = _umul128(static_cast<unsigned __int64>(value), multiplier, &high);
    if (high >= divisor) return {}; // _udiv128 quotient would overflow.
    auto result = _udiv128(high, low, divisor, &remainder);
    const bool up = rounding == Rounding::Ceil ? remainder != 0 :
        rounding == Rounding::Nearest && remainder >= (divisor / 2 + divisor % 2);
    const auto max = static_cast<unsigned __int64>(std::numeric_limits<qint64>::max());
    if (result > max || (up && result == max)) return {};
    if (up) ++result;
    return static_cast<qint64>(result);
}
}
std::optional<qint64> framesToSamples(qint64 frames, Rational fps, int sampleRate, Rounding rounding) {
    if (!valid(fps, sampleRate)) return {};
    return scale(frames, static_cast<unsigned __int64>(fps.denominator) * sampleRate, fps.numerator, rounding);
}
std::optional<qint64> samplesToFrames(qint64 samples, Rational fps, int sampleRate, Rounding rounding) {
    if (!valid(fps, sampleRate)) return {};
    return scale(samples, fps.numerator, static_cast<unsigned __int64>(fps.denominator) * sampleRate, rounding);
}
std::optional<qint64> framesToTicks(qint64 frames, Rational fps, Rational timeBase, Rounding rounding) {
    if (!validRate(fps) || !validRate(timeBase)) return {};
    return scale(frames, static_cast<unsigned __int64>(fps.denominator) * timeBase.denominator,
        static_cast<unsigned __int64>(fps.numerator) * timeBase.numerator, rounding);
}
std::optional<qint64> ticksToFrames(qint64 ticks, Rational timeBase, Rational fps, Rounding rounding) {
    if (!validRate(fps) || !validRate(timeBase)) return {};
    return scale(ticks, static_cast<unsigned __int64>(timeBase.numerator) * fps.numerator,
        static_cast<unsigned __int64>(timeBase.denominator) * fps.denominator, rounding);
}
std::optional<qint64> scaleTime(qint64 value, qint64 numerator, qint64 denominator, Rounding rounding) {
    if (numerator < 0 || denominator <= 0) return {};
    return scale(value, static_cast<unsigned __int64>(numerator), static_cast<unsigned __int64>(denominator), rounding);
}
}
