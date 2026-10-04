#include "cloudscope/common/units.hpp"

#include <cmath>

namespace cloudscope {

Degrees wrap_360(Degrees angle)
{
    double wrapped = std::fmod(angle.value(), 360.0);
    if (wrapped < 0.0) {
        wrapped += 360.0;
    }
    // fmod of a tiny negative number plus 360 can round to exactly 360.
    return Degrees(wrapped >= 360.0 ? 0.0 : wrapped);
}

Degrees wrap_180(Degrees angle)
{
    const double wrapped = wrap_360(Degrees(angle.value() + 180.0)).value() - 180.0;
    return Degrees(wrapped);
}

Degrees angular_difference(Degrees from, Degrees to)
{
    return wrap_180(to - from);
}

}  // namespace cloudscope
