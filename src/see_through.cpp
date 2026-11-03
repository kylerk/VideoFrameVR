#include "see_through.h"

#include "green_screen_check.h"

bool packedMask(const std::string& path, const VideoSettings& video) {
  return video.packedMask.value_or(hasPackedAlpha(path));
}

bool maskFormat(const VideoFormat& format) {
  return format.stereo == StereoLayout::SideBySide &&
         (format.projection == Projection::Fisheye180 ||
          format.projection == Projection::Fisheye190 ||
          format.projection == Projection::Fisheye200);
}

namespace {

bool maskShown(bool alphaPacked, const VideoFormat& format) {
  return alphaPacked && maskFormat(format);
}

}  // namespace

SeeThrough seeThrough(bool passthrough, bool alphaPacked, const VideoFormat& format,
                      const GreenScreen& key) {
  SeeThrough s;
  if (!passthrough) return s;
  s.packedAlpha = maskShown(alphaPacked, format);
  if (!s.packedAlpha && key.on) s.greenKey = 0.55f - 0.45f * key.strength / 100.0f;
  return s;
}

AutoPassthrough autoPassthrough(const std::string& path, const VideoFormat& format,
                                const VideoSettings& video) {
  if (video.passthroughChosen) return AutoPassthrough::None;
  if (maskShown(packedMask(path, video), format)) return AutoPassthrough::Mask;
  if (video.greenCheckVersion > 0 && video.greenScreenFound) return AutoPassthrough::GreenScreen;
  return AutoPassthrough::None;
}

bool greenCheckWanted(const std::string& path, const VideoFormat& format,
                      const VideoSettings& video) {
  return !video.passthroughChosen && !maskShown(packedMask(path, video), format) &&
         video.greenCheckVersion < GreenScreenCheck::kVersion;
}
