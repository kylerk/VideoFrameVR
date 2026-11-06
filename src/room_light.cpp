#include "room_light.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace {

constexpr float kMidGrey = 0.18f;
// Less of the picture than this showing something: it says nothing about the room.
constexpr float kLeastUsable = 0.05f;
constexpr int kBlack = 3, kClipped = 250;  // sRGB values

const std::array<float, 256>& linearValues() {
  static const std::array<float, 256> table = [] {
    std::array<float, 256> t{};
    for (int i = 0; i < 256; ++i) {
      const float e = static_cast<float>(i) / 255.0f;
      t[i] = e <= 0.04045f ? e / 12.92f : std::pow((e + 0.055f) / 1.055f, 2.4f);
    }
    return t;
  }();
  return table;
}

}  // namespace

RoomEstimate estimateRoom(const RoomImage& image) {
  RoomEstimate room;
  if (image.width <= 0 || image.height <= 0 ||
      image.rgb.size() < static_cast<size_t>(image.width) * image.height * 3)
    return room;
  const std::array<float, 256>& linear = linearValues();
  const int step = std::max(1, std::min(image.width, image.height) / 128);
  double logSum = 0, colour[3] = {0, 0, 0};
  size_t total = 0, shown = 0, coloured = 0;
  for (int y = 0; y < image.height; y += step) {
    const uint8_t* row = image.rgb.data() + static_cast<size_t>(y) * image.width * 3;
    for (int x = 0; x < image.width; x += step) {
      const uint8_t* p = row + static_cast<size_t>(x) * 3;
      ++total;
      const int most = std::max({p[0], p[1], p[2]});
      if (most <= kBlack) continue;
      const float r = linear[p[0]], g = linear[p[1]], b = linear[p[2]];
      logSum += std::log(std::max(0.2126f * r + 0.7152f * g + 0.0722f * b, 1e-4f));
      ++shown;
      if (most >= kClipped) continue;
      colour[0] += r;
      colour[1] += g;
      colour[2] += b;
      ++coloured;
    }
  }
  if (!total) return room;
  room.usable = static_cast<float>(shown) / static_cast<float>(total);
  if (room.usable < kLeastUsable) return room;
  room.luminance = static_cast<float>(std::exp(logSum / static_cast<double>(shown)));
  const double most = std::max({colour[0], colour[1], colour[2]});
  if (coloured && most > 0)
    for (int c = 0; c < 3; ++c) room.tint[c] = static_cast<float>(colour[c] / most);
  room.ok = true;
  return room;
}

void roomGrade(const RoomEstimate& room, const RoomTuning& tuning, float gains[3]) {
  if (!room.ok) {
    std::fill(gains, gains + 3, 1.0f);
    return;
  }
  const int trims[3] = {tuning.red, tuning.green, tuning.blue};
  const float stops = std::log2(std::max(room.luminance, 1e-4f) / kMidGrey) *
                      (static_cast<float>(tuning.brightness) / 100.0f);
  const float brightness = std::exp2(std::clamp(stops, -static_cast<float>(tuning.darkest) / 100.0f, 0.0f));
  for (int c = 0; c < 3; ++c) {
    const float tint = std::pow(std::clamp(room.tint[c], 0.01f, 1.0f), static_cast<float>(tuning.tint) / 100.0f);
    gains[c] = tint * brightness * (static_cast<float>(trims[c]) / 100.0f);
  }
}
