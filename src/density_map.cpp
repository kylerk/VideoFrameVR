#include "density_map.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace {

// Leave a coarse bin beyond both the eye and the widest ring.
int anchorBin(uint32_t image, uint32_t bin) {
  return std::max(3u, (image + 2 * bin - 1) / (2 * bin) + 1);
}

}  // namespace

VkExtent2D densityMapExtent(const TileGrid& grid, VkExtent2D eye, uint32_t texel) {
  if (grid.known()) {
    eye.width = (2 * anchorBin(eye.width, grid.binWidth) + 1) * grid.binWidth;
    eye.height = (2 * anchorBin(eye.height, grid.binHeight) + 1) * grid.binHeight;
  }
  return {(eye.width + texel - 1) / texel, (eye.height + texel - 1) / texel};
}

DensityMapLayout drawDensityMap(FoveationLevel level, const TileGrid& grid, VkExtent2D eye,
                                int sharpBins, bool ring, int outside, const DensityMapPixels& map,
                                std::vector<uint8_t>* binRates) {
  DensityMapLayout layout;
  layout.anchor = {static_cast<int32_t>(eye.width / 2), static_cast<int32_t>(eye.height / 2)};
  layout.sharp = {{0, 0}, eye};
  layout.shadedFragments = double(eye.width) * eye.height;
  if (!grid.known()) {
    for (uint32_t y = 0; y < map.size.height; ++y) std::memset(map.texels + y * map.pitch, 255, map.size.width * 2);
    return layout;
  }

  const int tw = grid.binWidth, th = grid.binHeight;
  const int column = anchorBin(eye.width, tw), row = anchorBin(eye.height, th);
  const int half = sharpBins / 2;
  layout.anchor = {column * tw + tw / 2, row * th + th / 2};
  layout.sharp = {{(column - half) * tw, row * th}, {static_cast<uint32_t>(sharpBins * tw), grid.binHeight}};
  auto rate = [&](int i, int j) {
    if (level == FoveationLevel::Off) return 1;
    const int x = std::abs(i - column), y = std::abs(j - row);
    if (x <= half && y == 0) return 1;
    if (ring && x <= half + 1 && y <= 1) return 2;
    return outside;
  };
  layout.shadedFragments = 0;
  for (int j = 0; j <= 2 * row; ++j) {
    const float y0 = std::max(0.f, eye.height * 0.5f + (j - row - 0.5f) * th);
    const float y1 = std::min(float(eye.height), eye.height * 0.5f + (j - row + 0.5f) * th);
    if (y0 >= y1) continue;
    for (int i = 0; i <= 2 * column; ++i) {
      const float x0 = std::max(0.f, eye.width * 0.5f + (i - column - 0.5f) * tw);
      const float x1 = std::min(float(eye.width), eye.width * 0.5f + (i - column + 0.5f) * tw);
      if (x0 >= x1) continue;
      const int r = rate(i, j);
      if (binRates) binRates->push_back(static_cast<uint8_t>(r));
      ++layout.bins[r / 2];
      layout.shadedFragments += double(x1 - x0) * (y1 - y0) / (r * r);
    }
  }
  if (level == FoveationLevel::Off) {
    layout.anchor = {static_cast<int32_t>(eye.width / 2), static_cast<int32_t>(eye.height / 2)};
    layout.sharp = {{0, 0}, eye};
  }
  for (uint32_t y = 0; y < map.size.height; ++y) {
    uint8_t* line = map.texels + y * map.pitch;
    const int j = (y * map.texel + map.texel / 2) / th;
    for (uint32_t x = 0; x < map.size.width; ++x) {
      const int i = (x * map.texel + map.texel / 2) / tw;
      line[x * 2] = line[x * 2 + 1] = 255 / rate(i, j);
    }
  }
  return layout;
}
