#pragma once

#include "gpu_context.h"

#include <cstdint>

// The bins a tiling GPU draws an eye pass in (docs/vulkan-renderer.md, 5.3).
// Turnip reads the density map once per bin, at the bin's centre, so the
// sharp area is made of whole bins; the app's density maps are drawn to fit
// them (density_map.h). No driver reports its bins (VK_QCOM_tile_properties
// is not offered), so they are worked out here the way the driver does,
// for the GPUs measured.
struct TileGrid {
  uint32_t binWidth = 0, binHeight = 0;  // 0: unknown
  uint32_t columns = 0, rows = 0;
  // Where the entry comes from, for --timing.
  const char* source = "unknown";
  bool known() const { return binWidth > 0 && binHeight > 0; }
};

// The bins of EyeTargets' foveated pass (one 4-byte colour attachment at
// one sample, no depth, a density map with offsets) at width × height on
// this GPU; unknown on a GPU without an entry.
TileGrid eyePassTileGrid(const GpuCapabilities& caps, uint32_t width, uint32_t height);
