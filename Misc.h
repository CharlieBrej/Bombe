#pragma once
#define _USE_MATH_DEFINES
#include <cmath>

#include <iostream>
#include <math.h>
#include <assert.h>
#include <random>

#define BREAKPOINT __asm__ volatile("int $0x03");

enum Direction
{
    DIRECTION_N = 0,
    DIRECTION_E = 1,
    DIRECTION_S = 2,
    DIRECTION_W = 3
};

inline Direction direction_rotate(Direction direction, bool clockwise)
{
    if (clockwise)
        return Direction((int(direction) + 1) % 4);
    else
        return Direction((int(direction) + 4 - 1) % 4);
}

inline Direction direction_rotate(Direction direction, Direction direction2)
{
    return Direction((int(direction) + int(direction2)) % 4);
}

inline Direction direction_rotate_anti(Direction direction, Direction direction2)
{
    return Direction(((int(direction) + 4) - int(direction2)) % 4);
}

inline Direction direction_flip(Direction direction, bool vertically)
{
    if ((int(direction) ^ int(vertically)) & 1)
        return Direction((int(direction) + 2) % 4);
    else
        return direction;
}

class XYPos;
class DirFlip
{
public:
    Direction dir = DIRECTION_N;
    bool flp = false;
    DirFlip() {}
    DirFlip(Direction dir_, bool flp_):
        dir(dir_),
        flp(flp_)
    {}

    DirFlip(int in):
        dir(Direction(in & 0x3)),
        flp((in >> 2)& 0x1)
    {}

    Direction get_n()
    {
        return get_dir(DIRECTION_N);
    }

    Direction get_dir(Direction t)
    {
        t = Direction((int(dir) + int(t)) % 4);
        if (!flp)
            return t;
        if (t == DIRECTION_N)
            return DIRECTION_S;
        if (t == DIRECTION_S)
            return DIRECTION_N;
        return t;
    }

    Direction get_dir_anti(Direction t)
    {
        if (flp)
        {
            if (t == DIRECTION_N)
                t = DIRECTION_S;
            else if (t == DIRECTION_S)
                t = DIRECTION_N;
        }
        t = direction_rotate_anti(t, dir);
        return t;
    }

    unsigned mask(unsigned in)
    {
        in <<= int(dir);
        in |= in >> 4;
        if (flp)
        {
            in = (in & 0xA) | ((in & 1) << 2) | ((in >> 2) & 1);
        }
        return in & 0xF;
    }

    unsigned mask_anti(unsigned in)
    {
        if (flp)
        {
            in = (in & 0xA) | ((in & 1) << 2) | ((in >> 2) & 1);
        }
        in <<= int(dir);
        in |= in >> 4;

        return in & 0xF;
    }

    int as_int()
    {
        return int(dir) + (int(flp) << 2);
    }

    DirFlip rotate(bool clockwise)
    {
        if (clockwise != flp)
            return DirFlip(Direction((int(dir) + 1) % 4), flp);
        else
            return DirFlip(Direction((int(dir) + 3) % 4), flp);
    }

    DirFlip flip(bool vertically)
    {
        if (vertically)
            return DirFlip(dir, !flp);
        return DirFlip(Direction((int(dir) + 2) % 4), !flp);
    }

    DirFlip flip(Direction around)
    {
        return flip(around == DIRECTION_E || around == DIRECTION_W);

    }

    XYPos trans(XYPos pos, int size);
    XYPos trans_inv(XYPos pos, int size);
};

class Rand
{
public:
    std::mt19937 gen;

    Rand()
    {
        std::random_device rd;
        gen.seed(rd());
    };

    Rand(unsigned i)
    {
        gen.seed(i);
    };

    operator unsigned int()
    {
        return gen();
    };
//     unsigned int save()
//     {
//         return value;
//     };
};


class XYPosFloat;

class XYPos
{
public:
    int x;
    int y;

    XYPos():
        x(0),
        y(0)
    {}
    XYPos(XYPosFloat pos);
    XYPos(int x_, int y_):
        x(x_),
        y(y_)
    {}

    XYPos(Direction dir)
    {                               //  N       E      S      W
        static const XYPos dirpos[4] = {{0,-1}, {1,0}, {0,1}, {-1,0}};
        *this = dirpos[dir%4];
    }
    Direction get_direction()
    {
        if (x > y)
        {
            if ((-x) > y)
                return DIRECTION_N;
            else
                return DIRECTION_E;
        }
        else
        {
            if ((-x) > y)
                return DIRECTION_W;
            else
                return DIRECTION_S;
        }
    }

    bool operator<(const XYPos& other) const
    {
        return y < other.y || (!(other.y < y) && x < other.x);
    }

//     operator bool() const
//     {
//         return x || y;
//     }
//
    bool operator==(const XYPos& other) const
    {
        return (x == other.x) && (y == other.y);
    }

    bool operator!=(const XYPos& other) const
    {
        return (x != other.x) || (y != other.y);
    }

    XYPos operator-(const XYPos& other) const
    {
        return XYPos(x - other.x, y - other.y);
    }

    XYPos operator-() const
    {
        return XYPos(-x, -y);
    }

    XYPos operator+(const XYPos& other) const
    {
        return XYPos(x + other.x, y + other.y);
    }

    XYPos operator*(const XYPos& other) const
    {
        return XYPos(x * other.x, y * other.y);
    }

    XYPos operator*(const int& other) const
    {
        return XYPos(x * other, y * other);
    }

    XYPos operator*(const double& other) const
    {
        return XYPos(x * other, y * other);
    }


    XYPos operator*(const Direction& other) const
    {
        switch (other)
        {
        case DIRECTION_N:
            return XYPos(x, y);
        case DIRECTION_E:
            return XYPos(-y, x);
        case DIRECTION_S:
            return XYPos(-x,-y);
        case DIRECTION_W:
            return XYPos(y, -x);
        }
        assert(0);
        return XYPos(0,0);
    }

    XYPos operator/(const int& other) const
    {
        return XYPos(x / other, y / other);
    }

    XYPos operator/(const XYPos& other) const
    {
        XYPos rep = XYPos(x / other.x, y / other.y);
        if (x % other.x < 0)
            rep.x--;
        if (y % other.y < 0)
            rep.y--;
        return rep;
    }


    XYPos operator%(const XYPos& other) const
    {
        XYPos rep(x % other.x, y % other.y);
        if (rep.x < 0)
            rep.x += other.x;
        if (rep.y < 0)
            rep.y += other.y;
        return rep;
    }

    void operator+=(const XYPos& other)
    {
        x += other.x;
        y += other.y;
    }

    void operator*=(double mul)
    {
        x *= mul;
        y *= mul;
    }

    void operator*=(int mul)
    {
        x *= mul;
        y *= mul;
    }

    void operator-=(const XYPos& other)
    {
        x -= other.x;
        y -= other.y;
    }

    void operator/=(const int d)
    {
        x /= d;
        y /= d;
    }

    XYPos operator>>(const int d)
    {
        return XYPos(x >> d, y >> d);
    }

    void operator>>=(const int d)
    {
        x >>= d;
        y >>= d;
    }


    bool inside(const XYPos& other)
    {
        return (x >= 0 && y >= 0 && x < other.x && y < other.y);
    }

    void clamp(const XYPos& min, const XYPos& max)
    {
        if (x < min.x) x = min.x;
        if (y < min.y) y = min.y;
        if (x > max.x) x = max.x;
        if (y > max.y) y = max.y;
    }

    void iter_next(const XYPos& other, XYPos reset = XYPos(0,0))
    {
        x++;
        if (x >= other.x)
        {
            x = reset.x;
            y++;
        }
    }

    bool iter_cond(const XYPos& other)
    {
        return y < other.y;
    }
};
#define FOR_XY(NAME, TL, BR) for (XYPos NAME = (TL); NAME.iter_cond(BR); NAME.iter_next(BR, TL))

#define radians(x) (x / (180.0 / M_PI))
#define degrees(x) (x * (180.0 / M_PI))

class Angle
{
public:
    double angle;
    Angle(double angle_):
        angle(angle_)
    {};

    operator double() const
    {
        return angle;
    }

    Angle operator^(const Angle& other) const
    {
        double dif = fmod(angle - other.angle + M_PI, M_PI * 2);
        if (dif < 0)
            dif += M_PI * 2;
        return Angle(dif - M_PI);
    }

    Angle abs() const
    {
        return Angle(fabs(angle));
    }

};

class XYPosFloat
{
public:
    double x;
    double y;

    XYPosFloat():
        x(0),
        y(0)
    {}
    XYPosFloat(XYPos pos):
        x(pos.x),
        y(pos.y)
    {}
    XYPosFloat(XYPos pos, double mul):
        x(pos.x * mul),
        y(pos.y * mul)
    {}
    XYPosFloat(double x_, double y_):
        x(x_),
        y(y_)
    {}

    XYPosFloat(Angle a, double d):
        x(cos(a.angle) * d),
        y(sin(a.angle) * d)
    {}

    bool operator<(const XYPosFloat& other) const
    {
        return y < other.y || (!(other.y < y) && x < other.x);
    }

    operator bool() const
    {
        return (x != 0) || (y != 0);
    }

    bool operator==(const XYPosFloat& other) const
    {
        return (x == other.x) && (y == other.y);
    }

    bool operator!=(const XYPos& other) const
    {
        return (x != other.x) || (y != other.y);
    }

    XYPosFloat operator-(const XYPosFloat& other) const
    {
        return XYPosFloat(x - other.x, y - other.y);
    }

    XYPosFloat operator+(const XYPosFloat& other) const
    {
        return XYPosFloat(x + other.x, y + other.y);
    }

    XYPosFloat operator*(const XYPosFloat& other) const
    {
        return XYPosFloat(x * other.x, y * other.y);
    }

    XYPosFloat operator*(const double& other) const
    {
        return XYPosFloat(x * other, y * other);
    }

    XYPosFloat operator*(const int& other) const
    {
        return XYPosFloat(x * other, y * other);
    }

    XYPosFloat operator/(const double& other) const
    {
        return XYPosFloat(x / other, y / other);
    }

    void operator+=(const XYPosFloat& other)
    {
        x += other.x;
        y += other.y;
    }

    void operator*=(double mul)
    {
        x *= mul;
        y *= mul;
    }

    void operator-=(const XYPosFloat& other)
    {
        x -= other.x;
        y -= other.y;
    }

    void operator/=(const double d)
    {
        x /= d;
        y /= d;
    }

    double distance(const XYPosFloat& other) const
    {
        return sqrt((x - other.x)*(x - other.x) +
                    (y - other.y)*(y - other.y));
    }

    double distance() const
    {
        return sqrt(x * x + y * y);
    }

    double angle(const XYPosFloat& other) const
    {
        return atan2(other.y - y, other.x - x);
    }

    double angle() const
    {
        return atan2(y, x);
    }

    XYPosFloat rotate(Angle delta) const
    {
        double r = distance();
        Angle feta = angle() + delta;
        return XYPosFloat(feta, r);
    }

};

inline XYPos::XYPos(XYPosFloat pos):
        x(floor(pos.x)),
        y(floor(pos.y))
{}

class XYRect
{
public:
    XYPos pos;
    XYPos size;
    XYRect(int x, int y, int w, int h) :
        pos(x,y),
        size(w,h)
    {}
    XYRect(XYPos pos_, XYPos size_) :
        pos(pos_),
        size(size_)
    {}
    XYRect() {}
};


inline std::ostream& operator<<(std::ostream& os, const XYPosFloat& obj)
{
      os << "(" << obj.x << ", " << obj.y << ")";
      return os;
}

inline unsigned popcount(unsigned in)
{
    unsigned count = 0;
    while (in)
    {
        count += in & 1;
        in >>= 1;
    }
    return count;
}

inline bool is_leading_utf8_byte(char c)
{
    return (c & 0xC0) != 0x80;
}

inline unsigned scramble(unsigned s)
{
    return (s * 77) ^ (s << 2) ^ (s << 4) ^ (s << 8) ^ (s << 16);
}

uint64_t checksum(std::string s);

template<class _container, class _Ty> inline
    bool contains(_container _C, const _Ty& _Val)
    {return std::find(_C.begin(), _C.end(), _Val) != _C.end(); }

class Colour
{
public:
    uint8_t r,g,b;
    Colour(uint8_t r_, uint8_t g_, uint8_t b_):
        r(r_),g(g_),b(b_) {}
    bool operator==(const Colour& other) const
    {
        return (r == other.r) && (g == other.g) && (b == other.b);
    }
};
