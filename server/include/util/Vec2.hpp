#pragma once

#include <cmath>
#include <ostream>

namespace orbital::util {

struct Vec2 {
    double x{0.0};
    double y{0.0};

    Vec2() = default;
    Vec2(double x_, double y_) : x(x_), y(y_) {}

    Vec2 operator+(const Vec2& other) const { return {x + other.x, y + other.y}; }
    Vec2 operator-(const Vec2& other) const { return {x - other.x, y - other.y}; }
    Vec2 operator*(double s) const { return {x * s, y * s}; }
    Vec2 operator/(double s) const { return {x / s, y / s}; }

    Vec2& operator+=(const Vec2& other) {
        x += other.x;
        y += other.y;
        return *this;
    }

    Vec2& operator-=(const Vec2& other) {
        x -= other.x;
        y -= other.y;
        return *this;
    }

    Vec2& operator*=(double s) {
        x *= s;
        y *= s;
        return *this;
    }

    Vec2& operator/=(double s) {
        x /= s;
        y /= s;
        return *this;
    }
};

inline Vec2 operator*(double s, const Vec2& v) { return {v.x * s, v.y * s}; }

inline double dot(const Vec2& a, const Vec2& b) { return a.x * b.x + a.y * b.y; }

inline double lengthSquared(const Vec2& v) { return dot(v, v); }

inline double length(const Vec2& v) { return std::sqrt(lengthSquared(v)); }

inline Vec2 normalize(const Vec2& v) {
    double len = length(v);
    if (len < 1e-8) {
        return {0.0, 0.0};
    }
    return v / len;
}

inline double cross(const Vec2& a, const Vec2& b) { return a.x * b.y - a.y * b.x; }

inline Vec2 perpendicular(const Vec2& v) { return {-v.y, v.x}; }

inline std::ostream& operator<<(std::ostream& os, const Vec2& v) {
    os << "(" << v.x << ", " << v.y << ")";
    return os;
}

} // namespace orbital::util
