#pragma once

#include <string>

// With Video mipmaps off, how each eye pixel samples the video's full-size
// level (shaders/video.glsl, footprintSample): Off one bilinear tap, On up to
// two taps along each side of its footprint, Strong up to four. Strong costs
// more than the Frame can spare where pixels span many texels (an 8K flat
// screen, foveation's coarse pixels). The video draws push the limit, so a
// change applies from the next frame. Persisted by name (settings.cpp).
enum class AntiShimmer { Off, On, Strong };
constexpr int kAntiShimmerCount = 3;

// Taps along each side of a pixel at most (SurroundPush/ScreenPush mode[2]).
constexpr int antiShimmerTaps(AntiShimmer a) {
  return a == AntiShimmer::Off ? 1 : a == AntiShimmer::On ? 2 : 4;
}

inline const char* antiShimmerName(AntiShimmer a) {
  static const char* const names[kAntiShimmerCount] = {"off", "on", "strong"};
  return names[static_cast<int>(a)];
}

inline bool parseAntiShimmer(const std::string& name, AntiShimmer& a) {
  for (int i = 0; i < kAntiShimmerCount; ++i) {
    if (name == antiShimmerName(static_cast<AntiShimmer>(i))) {
      a = static_cast<AntiShimmer>(i);
      return true;
    }
  }
  return false;
}
