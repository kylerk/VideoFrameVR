#pragma once

#include "video_format.h"
#include "video_memory.h"

#include <string>

// Whether the video brings its own packed mask: as chosen for it by hand,
// else as its file name says (whatever the format).
bool packedMask(const std::string& path, const VideoSettings& video);
// The one layout a packed mask comes in: side-by-side fisheye.
bool maskFormat(const VideoFormat& format);

// What passthrough shows the room through, as the scene draws it: the
// packed alpha mask (side-by-side fisheye only), else a keyed green screen.
SeeThrough seeThrough(bool passthrough, bool alphaPacked, const VideoFormat& format,
                      const GreenScreen& key);

// Why passthrough switches itself on for a video made for it (Settings →
// Automatic passthrough), if its passthrough was never set by hand: it
// brings its own mask (packedMask(), and in the one layout a mask comes in: a
// name alone also matches titles such as "…-alpha-turbo"), or the green
// screen check found one (green_screen_check.h).
enum class AutoPassthrough { None, Mask, GreenScreen };
AutoPassthrough autoPassthrough(const std::string& path, const VideoFormat& format,
                                const VideoSettings& video);
// The green screen check has yet to look at this video (or only an older one did).
bool greenCheckWanted(const std::string& path, const VideoFormat& format,
                      const VideoSettings& video);
