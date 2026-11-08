// Fixed represents a world coordinate as a signed 64-bit count of 1/65536 of a block. Conversions to integer blocks divide by 65536; floating conversions scale by the same factor. Products divide their raw result by 65536.
#pragma once
#include "plasma/Vector.h"

namespace cube {

// Fixed-point conversion scales, in raw units per block.
const double FixedUnitsPerBlock = 65536.0;
const float FixedUnitsPerBlockF = 65536.0f;

struct Fixed {
    // Leaves the value unset.
    Fixed() {}
    Fixed &operator=(const Fixed &other)
    {
        raw = other.raw;
        return *this;
    }
    explicit Fixed(long long raw)
        : raw(raw)
    {}
    Fixed(int blocks)
        : raw((long long)blocks * 65536)
    {}
    Fixed &operator=(int blocks)
    {
        raw = (long long)blocks * 65536;
        return *this;
    }
    Fixed(double blocks)
        : raw((long long)(blocks * FixedUnitsPerBlock))
    {}
    Fixed(float blocks)
        : raw((long long)(blocks * FixedUnitsPerBlockF))
    {}

    Fixed operator+(const Fixed &other) const { return Fixed(raw + other.raw); }
    Fixed operator-(const Fixed &other) const { return Fixed(raw - other.raw); }
    // Negation.
    Fixed operator-() const { return Fixed(-raw); }
    Fixed operator*(const Fixed &other) const { return Fixed(raw * other.raw / 65536); }
    Fixed &operator+=(const Fixed &other)
    {
        raw += other.raw;
        return *this;
    }
    Fixed &operator-=(const Fixed &other)
    {
        raw -= other.raw;
        return *this;
    }
    Fixed &operator*=(const Fixed &other)
    {
        // Multiplication may overflow the signed intermediate.
        raw = raw * other.raw / 65536;
        return *this;
    }
    bool operator!=(const Fixed &other) const { return raw != other.raw; }
    bool operator>(const Fixed &other) const { return raw > other.raw; }
    bool operator<(const Fixed &other) const { return raw < other.raw; }
    bool operator>=(const Fixed &other) const { return raw >= other.raw; }

    bool operator<=(const Fixed &other) const { return raw <= other.raw; }
    // Scaled by a factor: the raw value times the factor, in float.
    Fixed operator*(float scale) const { return Fixed((long long)(raw * scale)); }
    // Divided by a factor, in the same way.
    Fixed operator/(float divisor) const { return Fixed((long long)(raw / divisor)); }
    // Divided by a whole number, in double.
    Fixed operator/(int divisor) const { return Fixed((long long)(raw / (double)divisor)); }
    // Compared with a number of blocks.
    bool operator>=(float blocks) const { return raw >= Fixed(blocks).raw; }

    // With a whole number of blocks. Adds or subtracts a float number of blocks, truncated to a raw
    // value.
    Fixed operator+(float blocks) const { return Fixed((long long)(blocks * FixedUnitsPerBlockF) + raw); }
    Fixed operator-(float blocks) const { return Fixed(raw - (long long)(blocks * FixedUnitsPerBlockF)); }

    Fixed operator-(double blocks) const { return Fixed(raw - (long long)(blocks * FixedUnitsPerBlock)); }
    // Subtracts a negated offset when stepping upwards or applying a half-block displacement.
    Fixed operator+(double blocks) const { return *this - -blocks; }
    Fixed operator+(int blocks) const { return Fixed((long long)blocks * 65536 + raw); }
    Fixed operator-(int blocks) const { return Fixed(raw - (long long)blocks * 65536); }
    bool operator>(int blocks) const { return raw > (long long)blocks * 65536; }

    bool operator<(int blocks) const { return raw < (long long)blocks * 65536; }

    // Compares after converting the float to fixed point, avoiding rounding the coordinate to float.
    bool operator<(float blocks) const { return raw < (long long)(blocks * FixedUnitsPerBlockF); }
    bool operator>(float blocks) const { return raw > (long long)(blocks * FixedUnitsPerBlockF); }

    operator float() const { return (float)raw * (1.0f / 65536); }
    operator int() const { return (int)(raw / 65536); }

    long long raw; // 1/65536 blocks
};

// Returns the containing block by dividing by 65536 and subtracting one for negative coordinates. A floating block count can also be subtracted from a coordinate.
inline Fixed operator-(float blocks, const Fixed &coordinate)
{
    return Fixed((long long)(blocks * FixedUnitsPerBlockF) - coordinate.raw);
}

inline int toBlock(const Fixed &coordinate)
{
    if (coordinate.raw < 0)
        return (int)(coordinate.raw / 65536) - 1;
    return (int)(coordinate.raw / 65536);
}

// The block that contains a world position.
inline plasma::Vector<3, int> toBlock(const plasma::Vector<3, Fixed> &position)
{
    return plasma::Vector<3, int>(toBlock(position[0]), toBlock(position[1]), toBlock(position[2]));
}

// A coordinate in blocks as a double (fild, then a multiplication by 1/65536 in double precision;
// World::wind, World::buildTree).
inline double toBlocks(const Fixed &value)
{
    return value.raw / FixedUnitsPerBlock;
}

// A coordinate scaled in fixed point: the raw value times a double factor, truncated (the noise
// inputs of World::wind and Region:Record: shapeDistance, which subtract the coordinate scaled by a
// negative factor from a constant offset).
inline Fixed scaled(const Fixed &value, double scale)
{
    return Fixed((long long)(value.raw * scale));
}

// Integral fixed-coordinate conversions retain signed integer division.
enum {
    FixedIntegerUnitsPerBlock = 65536
};

// Whether the squared length of an offset can be taken without overflow (not in the original). From 16384
// blocks on an axis the raw products can pass 2^63 and wrap, so the original's reach tests come out true at
// random for creatures far away (ghost hits from traps and blasts on a server). Such an offset is beyond
// every reach; for shorter ones the tests give what they always did.
template<unsigned N> inline bool isShortOffset(const plasma::Vector<N, Fixed> &offset)
{
    for (unsigned i = 0; i < N; i++) {
        if (offset[i].raw >= (1LL << 30) || offset[i].raw <= -(1LL << 30))
            return false;
    }
    return true;
}

}
