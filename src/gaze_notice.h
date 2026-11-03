#pragma once

#include "scene_renderer.h"
#include "subtitle_overlay.h"
#include "xmath.h"

#include <string>
#include <vector>

// A short message just in front of the viewer, following the gaze: the
// recenter countdown, the viewpoint as the stick moves it. Added last, it is
// drawn over everything (the overlays have no depth test), and nearer than
// the menu and the screen, so that both eyes see it in front of them rather
// than through them; the size it would have at 2 m. Messages shown at once
// take rows, the first highest. The text is drawn into the texture again
// only when it changes.
class GazeNotice {
 public:
  explicit GazeNotice(int row) : row_(row) {}
  bool init(Uploader& uploader) { return text_.init(uploader); }

  void addTo(std::vector<OverlayDraw>& overlays, const std::string& text, Vec3 headPos,
             Quat headRot) {
    constexpr float kNear = 0.7f, kShrink = kNear / 2.0f;
    constexpr float kWidth = 1.4f * kShrink, kHeight = kWidth / SubtitleOverlay::kAspect;
    constexpr float kTop = 0.25f * kShrink, kRowStep = 0.45f * kShrink;
    text_.setText(text);
    const Mat4 gaze = rotation(headRot);
    const Vec3 center = headPos + transformDir(gaze, {0.0f, kTop - row_ * kRowStep, -kNear});
    PanelItem item;
    item.texture = text_.texture();
    item.model = translation(center) * gaze * scaling(kWidth, kHeight, 1);
    overlays.push_back(item);
  }

 private:
  SubtitleOverlay text_;
  int row_;
};
