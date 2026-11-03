#include "tile_grid.h"

#include <algorithm>
#include <cstdlib>

namespace {

// A GPU's tiling: its bin alignment and largest bin (Mesa's
// freedreno_devices.py) and how many pixels of the eye pass fit in its
// tile memory (fitted to bin dumps on the device).
struct TilingEntry {
  uint32_t vendorId, deviceId;
  VkDriverId driver;
  uint32_t alignWidth, alignHeight, maxWidth, maxHeight;
  uint32_t pixels;
  const char* source;
};

// The Steam Frame's Adreno 750 under Turnip. The pixels are 114 of the
// pass's 6144-pixel blocks: with that, the choice below reproduced every
// bin measured at 1728 to 5400 per eye, with SteamVR's maps and our own
// (Mesa Turnip 26.3.0-devel e07916b45f).
const TilingEntry kEntries[] = {
    {0x5143, 0x43051401, VK_DRIVER_ID_MESA_TURNIP, 96, 32, 2016, 2032, 114 * 6144,
     "Adreno 750, measured on Turnip 26.3"},
};

uint32_t roundUp(uint32_t v, uint32_t step) { return (v + step - 1) / step * step; }

}  // namespace

// Turnip's choice (tu_tiling_config_update_tile_layout in tu_util.cc): of
// the bin widths its alignment allows, the one giving the fewest bins, bins
// no more than twice as long as wide with a density map, then the squarest.
// Upstream judges the shape before evening out the bins' height; the
// Frame's driver matched only when judged after.
TileGrid eyePassTileGrid(const GpuCapabilities& caps, uint32_t width, uint32_t height) {
  const auto entry = std::find_if(std::begin(kEntries), std::end(kEntries), [&](const TilingEntry& e) {
    return e.vendorId == caps.vendorId && e.deviceId == caps.deviceId && e.driver == caps.driverId;
  });
  TileGrid grid;
  if (entry == std::end(kEntries) || width == 0 || height == 0) return grid;
  const TilingEntry& e = *entry;
  const uint32_t widest = std::min(e.maxWidth, roundUp(width, e.alignWidth));
  const uint32_t tallest = std::min(e.maxHeight, roundUp(height, e.alignHeight));
  uint32_t best = ~0u;
  for (uint32_t w = e.alignWidth; w <= widest; w += e.alignWidth) {
    uint32_t h = std::min(e.pixels / w, tallest) / e.alignHeight * e.alignHeight;
    if (h == 0) continue;
    const uint32_t columns = (width + w - 1) / w, rows = (height + h - 1) / h;
    h = roundUp((height + rows - 1) / rows, e.alignHeight);
    const uint32_t penalty = (w > 2 * h || h > 2 * w) ? 1000 : 0;
    const uint32_t bins = columns * rows + penalty;
    if (bins < best ||
        (bins == best && std::abs(static_cast<int>(w) - static_cast<int>(h)) <
                             std::abs(static_cast<int>(grid.binWidth) - static_cast<int>(grid.binHeight)))) {
      best = bins;
      grid.binWidth = w;
      grid.binHeight = h;
      grid.columns = columns;
      grid.rows = rows;
    }
  }
  if (grid.known()) grid.source = e.source;
  return grid;
}
