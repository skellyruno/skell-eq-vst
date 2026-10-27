#pragma once
#include <algorithm>
#include <cmath>
namespace juce {
 template <typename T> constexpr T jlimit(T low, T high, T value) noexcept {
   return value < low ? low : value > high ? high : value;
 }
 template <typename T> struct MathConstants;
 template <> struct MathConstants<double> {
   static constexpr double pi=3.14159265358979323846264338327950288;
   static constexpr double twoPi=6.28318530717958647692528676655900576;
 };
}
