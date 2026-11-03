#pragma once

#include "cairo_canvas.h"
#include "scene_renderer.h"
#include "subtitle_overlay.h"
#include "xmath.h"

#include <string>
#include <vector>

// Autofocus diagnostics in the view: a ring where the focus measures, placed
// at the corrected distance (so it sits on what is lined up), with the
// focus state under it.
class FocusMarker {
 public:
  bool init(Uploader& uploader);
  // This frame's placement; hidden unless `show`. direction: unit, local space.
  void update(bool show, Vec3 headCenter, Quat head, Vec3 direction, float distance,
              const std::string& text);
  // A small dot where the eyes look (the focus target's corrected, smoothed
  // gaze) in a faint 3° ring, for checking eye tracking. Hidden unless `show`. direction: unit,
  // local space. At the focus distance, so it fuses with the ring instead of
  // showing double.
  void updateGaze(bool show, Vec3 origin, Vec3 direction, float distance);
  // What is shown this frame, added to the scene's overlays: the dot, the
  // ring and its text.
  void addTo(std::vector<OverlayDraw>& overlays);

 private:
  CairoCanvas ring_, dot_;
  SubtitleOverlay text_;
  bool show_ = false, showDot_ = false;
  Mat4 ringModel_, textModel_, dotModel_;
};
