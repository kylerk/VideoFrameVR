#include "eye_resolution.h"

#include "density_map.h"

#include <algorithm>
#include <cmath>

std::vector<int> eyeResolutionStops(const GpuCapabilities& caps) {
  constexpr int low = 1152, high = 4608;  // battery saving to 486 MiB of eye images
  if (!kStandaloneHeadset || !eyePassTileGrid(caps, low, low).known()) return {};
  const int normal = kDefaultVideoResolution * kPanelWidth / 100;
  const int instant = kInstantResolution * kPanelWidth / 100;
  // The default until 2026-10-27 (1.60×): profiles that saved it keep it.
  const int former = 160 * kPanelWidth / 100;
  auto kept = [&](int size) { return size == normal || size == instant || size == former; };
  std::vector<int> stops;
  auto grid = eyePassTileGrid(caps, low, low);
  for (int size = low; size <= high; ++size) {
    const auto next = eyePassTileGrid(caps, size + 1, size + 1);
    if (grid.columns != next.columns || grid.rows != next.rows || kept(size))
      stops.push_back(size);
    grid = next;
  }
  // The default map, including every ring and clipped edge bin.
  auto cost = [&](int size) {
    const auto grid = eyePassTileGrid(caps, size, size);
    const VkExtent2D eye{static_cast<uint32_t>(size), static_cast<uint32_t>(size)};
    return drawDensityMap(FoveationLevel::High, grid, eye, kDefaultSharpBins, kDefaultFoveationRing, 4, {})
        .shadedFragments;
  };
  for (int i = static_cast<int>(stops.size()) - 2; i >= 0; --i)
    if (!kept(stops[i]) && cost(stops[i]) > cost(stops[i + 1]))
      stops.erase(stops.begin() + i);
  return stops;
}

int nextResolutionStep(int percent, int direction) {
  constexpr int step = kResolutionStep;
  if (direction > 0)
    return percent >= kResolutionHigh ? percent
                                      : std::min((percent / step + 1) * step, kResolutionHigh);
  return percent <= kResolutionLow ? percent
                                   : std::max(((percent + step - 1) / step - 1) * step, kResolutionLow);
}

bool settleEyeResolution(Settings& settings, int recommendedWidth, const std::vector<int>& stops) {
  bool changed = false;
  const double base = kPanelWidth > 0 ? kPanelWidth : recommendedWidth;
  if (settings.videoResolution <= 0) {
    const bool converted = settings.legacyRenderScale > 0 && recommendedWidth > 0;
    if (converted) {
      settings.videoResolution = std::clamp(
          static_cast<int>(std::lround(settings.legacyRenderScale * recommendedWidth / base)),
          kMinResolution, kMaxResolution);
      settings.resolutionNotice = kPanelWidth > 0;
    } else {
      settings.videoResolution = kDefaultVideoResolution;
    }
    changed = converted || settings.legacyRenderScale > 0;
    settings.legacyRenderScale = 0;
  }
  if (!stops.empty()) {
    const int pixels = settings.videoResolutionPixels > 0
                           ? settings.videoResolutionPixels
                           : static_cast<int>(std::lround(settings.videoResolution * base / 100.0));
    const int nearest = *std::min_element(stops.begin(), stops.end(), [pixels](int a, int b) {
      return std::abs(a - pixels) < std::abs(b - pixels);
    });
    const int percent = static_cast<int>(std::lround(nearest * 100.0 / base));
    changed |= settings.videoResolutionPixels != nearest || settings.videoResolution != percent;
    settings.videoResolutionPixels = nearest;
    settings.videoResolution = percent;
  }
  return changed;
}
