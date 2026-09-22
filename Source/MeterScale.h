#pragma once
#include <algorithm>
#include <cmath>
#include <limits>

namespace lightHostModern
{
inline double amplitudeDb(double amplitude) noexcept
{
    return std::isfinite(amplitude) && amplitude > 0 ? 20.0 * std::log10(amplitude)
        : -std::numeric_limits<double>::infinity();
}
inline int meterSegments(double db) noexcept
{
    if (!std::isfinite(db)) return 0;
    return static_cast<int>(std::round((std::clamp)((db + 60.0) / 60.0, 0.0, 1.0) * 28.0));
}
}
