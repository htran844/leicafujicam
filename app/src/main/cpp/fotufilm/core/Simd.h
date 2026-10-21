// Port of Swift's SIMD2/3/4<Float> and Swift.min/max semantics used across FotufilmCore.
#ifndef FOTUFILM_CORE_SIMD_H
#define FOTUFILM_CORE_SIMD_H

#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

namespace fotufilm {

// Swift.min(x, y) is `y < x ? y : x`; Swift.max(x, y) is `y >= x ? y : x`. Arguments keep their
// upstream order because NaN handling depends on it.
template <typename T>
inline T swiftMin(T x, T y) { return y < x ? y : x; }
template <typename T>
inline T swiftMax(T x, T y) { return y >= x ? y : x; }
template <typename T, typename... Rest>
inline T swiftMin(T x, T y, T z, Rest... rest) { return swiftMin(swiftMin(x, y), z, rest...); }
template <typename T, typename... Rest>
inline T swiftMax(T x, T y, T z, Rest... rest) { return swiftMax(swiftMax(x, y), z, rest...); }

// Math.swift `clamp`: min(max(x, lo), hi).
template <typename T>
inline T clamp(T x, T lo, T hi) { return swiftMin(swiftMax(x, lo), hi); }

// ClosedRange.contains.
template <typename T>
inline bool inClosedRange(T value, T lo, T hi) { return lo <= value && value <= hi; }

// Sequence.max() / min(): first element wins ties and NaN is never adopted after the first.
inline bool sequenceMax(const std::vector<float>& values, float& out) {
    if (values.empty()) return false;
    float result = values[0];
    for (size_t i = 1; i < values.size(); ++i) {
        if (result < values[i]) result = values[i];
    }
    out = result;
    return true;
}
inline bool sequenceMin(const std::vector<float>& values, float& out) {
    if (values.empty()) return false;
    float result = values[0];
    for (size_t i = 1; i < values.size(); ++i) {
        if (values[i] < result) result = values[i];
    }
    out = result;
    return true;
}
// `values.max() ?? fallback`
inline float maxOr(const std::vector<float>& values, float fallback) {
    float out;
    return sequenceMax(values, out) ? out : fallback;
}

inline void preconditionCheck(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(std::string("precondition failed: ") + message);
}

constexpr float kFloatPi = 3.14159265358979323846f;

struct Float2 {
    float x = 0, y = 0;
    Float2() = default;
    constexpr Float2(float x_, float y_) : x(x_), y(y_) {}
    static Float2 repeating(float v) { return Float2(v, v); }
    float& operator[](int i) { return i == 0 ? x : y; }
    float operator[](int i) const { return i == 0 ? x : y; }
    float sum() const { return (-0.0f + x) + y; }
    float max() const { return swiftMax(x, y); }
    float min() const { return swiftMin(x, y); }
};

struct Float3 {
    float x = 0, y = 0, z = 0;
    Float3() = default;
    constexpr Float3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    static Float3 repeating(float v) { return Float3(v, v, v); }
    static Float3 one() { return Float3(1, 1, 1); }
    float& operator[](int i) { return i == 0 ? x : (i == 1 ? y : z); }
    float operator[](int i) const { return i == 0 ? x : (i == 1 ? y : z); }
    float sum() const { return ((-0.0f + x) + y) + z; }
    float max() const { return swiftMax(swiftMax(x, y), z); }
    float min() const { return swiftMin(swiftMin(x, y), z); }
};

struct Float4 {
    float x = 0, y = 0, z = 0, w = 0;
    Float4() = default;
    constexpr Float4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
    static Float4 repeating(float v) { return Float4(v, v, v, v); }
    float& operator[](int i) { return i == 0 ? x : (i == 1 ? y : (i == 2 ? z : w)); }
    float operator[](int i) const { return i == 0 ? x : (i == 1 ? y : (i == 2 ? z : w)); }
    float sum() const { return (((-0.0f + x) + y) + z) + w; }
    float max() const { return swiftMax(swiftMax(swiftMax(x, y), z), w); }
    float min() const { return swiftMin(swiftMin(swiftMin(x, y), z), w); }
};

#define FOTUFILM_SIMD_OPS(T, APPLY)                                                         \
    inline T operator+(T a, T b) { return APPLY(a, b, +); }                                 \
    inline T operator-(T a, T b) { return APPLY(a, b, -); }                                 \
    inline T operator*(T a, T b) { return APPLY(a, b, *); }                                 \
    inline T operator/(T a, T b) { return APPLY(a, b, /); }                                 \
    inline T operator+(T a, float s) { return a + T::repeating(s); }                        \
    inline T operator-(T a, float s) { return a - T::repeating(s); }                        \
    inline T operator*(T a, float s) { return a * T::repeating(s); }                        \
    inline T operator/(T a, float s) { return a / T::repeating(s); }                        \
    inline T operator+(float s, T a) { return T::repeating(s) + a; }                        \
    inline T operator-(float s, T a) { return T::repeating(s) - a; }                        \
    inline T operator*(float s, T a) { return T::repeating(s) * a; }                        \
    inline T operator/(float s, T a) { return T::repeating(s) / a; }                        \
    inline T& operator+=(T& a, T b) { return a = a + b; }                                   \
    inline T& operator-=(T& a, T b) { return a = a - b; }                                   \
    inline T& operator*=(T& a, T b) { return a = a * b; }                                   \
    inline T& operator/=(T& a, T b) { return a = a / b; }                                   \
    inline T& operator+=(T& a, float s) { return a = a + s; }                               \
    inline T& operator-=(T& a, float s) { return a = a - s; }                               \
    inline T& operator*=(T& a, float s) { return a = a * s; }                               \
    inline T& operator/=(T& a, float s) { return a = a / s; }

#define FOTUFILM_APPLY2(a, b, op) Float2((a).x op (b).x, (a).y op (b).y)
#define FOTUFILM_APPLY3(a, b, op) Float3((a).x op (b).x, (a).y op (b).y, (a).z op (b).z)
#define FOTUFILM_APPLY4(a, b, op) \
    Float4((a).x op (b).x, (a).y op (b).y, (a).z op (b).z, (a).w op (b).w)

FOTUFILM_SIMD_OPS(Float2, FOTUFILM_APPLY2)
FOTUFILM_SIMD_OPS(Float3, FOTUFILM_APPLY3)
FOTUFILM_SIMD_OPS(Float4, FOTUFILM_APPLY4)

#undef FOTUFILM_SIMD_OPS
#undef FOTUFILM_APPLY2
#undef FOTUFILM_APPLY3
#undef FOTUFILM_APPLY4

inline Float2 operator-(Float2 a) { return Float2(-a.x, -a.y); }
inline Float3 operator-(Float3 a) { return Float3(-a.x, -a.y, -a.z); }
inline Float4 operator-(Float4 a) { return Float4(-a.x, -a.y, -a.z, -a.w); }

inline bool operator==(Float2 a, Float2 b) { return a.x == b.x && a.y == b.y; }
inline bool operator==(Float3 a, Float3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
inline bool operator==(Float4 a, Float4 b) {
    return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
}
inline bool operator!=(Float2 a, Float2 b) { return !(a == b); }
inline bool operator!=(Float3 a, Float3 b) { return !(a == b); }
inline bool operator!=(Float4 a, Float4 b) { return !(a == b); }

// Pointwise Swift.min/max (SIMD pointwiseMin/pointwiseMax), clamp, and the scalar maps the
// upstream code spells element by element.
inline Float3 pointwiseMin(Float3 a, Float3 b) {
    return Float3(swiftMin(a.x, b.x), swiftMin(a.y, b.y), swiftMin(a.z, b.z));
}
inline Float3 pointwiseMax(Float3 a, Float3 b) {
    return Float3(swiftMax(a.x, b.x), swiftMax(a.y, b.y), swiftMax(a.z, b.z));
}
inline Float3 clamp(Float3 v, Float3 lo, Float3 hi) { return pointwiseMin(pointwiseMax(v, lo), hi); }
inline Float2 pointwiseMin(Float2 a, Float2 b) { return Float2(swiftMin(a.x, b.x), swiftMin(a.y, b.y)); }
inline Float2 pointwiseMax(Float2 a, Float2 b) { return Float2(swiftMax(a.x, b.x), swiftMax(a.y, b.y)); }

// Swift `stride(from:through:by:)` / `stride(from:to:by:)` over Float: element i is
// `start.addingProduct(Float(i), step)` (a fused multiply-add, so no rounding accumulates), and
// the end is included only when an element lands on it exactly.
inline std::vector<float> strideThrough(float start, float end, float step) {
    std::vector<float> values;
    for (int i = 0;; ++i) {
        float value = i == 0 ? start : std::fma(static_cast<float>(i), step, start);
        if (step > 0 ? value >= end : value <= end) {
            if (value == end) values.push_back(value);
            break;
        }
        values.push_back(value);
    }
    return values;
}
inline std::vector<float> strideTo(float start, float end, float step) {
    std::vector<float> values;
    for (int i = 0;; ++i) {
        float value = i == 0 ? start : std::fma(static_cast<float>(i), step, start);
        if (step > 0 ? value >= end : value <= end) break;
        values.push_back(value);
    }
    return values;
}

}  // namespace fotufilm

#endif  // FOTUFILM_CORE_SIMD_H
