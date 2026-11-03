#include "focus_marker.h"

#include "vr_space.h"

#include <cairo.h>

#include <algorithm>
#include <cmath>

namespace {

constexpr float kRingRadius = 0.0253f;  // per meter of distance: 1.45°, the measured patch
constexpr float kTextWidth = 0.3f;      // per meter of distance
constexpr float kGazeDotDistance = 2.0f;
// The gaze dot's canvas, per meter of distance: its ring (60 of 64 pixels)
// is 3° around the gaze, about the eye tracker's error.
constexpr float kGazeDotSize = 2 * 0.0524f * 64 / 60;

}  // namespace

bool FocusMarker::init(Uploader& uploader) {
  if (!ring_.init(uploader, 128, 128) || !text_.init(uploader)) return false;
  cairo_t* cr = ring_.cr();
  ring_.clear();
  cairo_arc(cr, 64, 64, 56, 0, 2 * 3.14159265);
  cairo_set_line_width(cr, 10);
  cairo_set_source_rgba(cr, 0, 0, 0, 0.7);
  cairo_stroke_preserve(cr);
  cairo_set_line_width(cr, 5);
  cairo_set_source_rgba(cr, 1.0, 0.85, 0.2, 1.0);
  cairo_stroke(cr);
  cairo_arc(cr, 64, 64, 4, 0, 2 * 3.14159265);
  cairo_fill(cr);
  ring_.upload();

  if (!dot_.init(uploader, 128, 128)) return false;
  cr = dot_.cr();
  dot_.clear();
  cairo_arc(cr, 64, 64, 60, 0, 2 * 3.14159265);
  cairo_set_line_width(cr, 3);
  cairo_set_source_rgba(cr, 0, 0, 0, 0.25);
  cairo_stroke_preserve(cr);
  cairo_set_line_width(cr, 1.5);
  cairo_set_source_rgba(cr, 0.2, 0.9, 1.0, 0.35);
  cairo_stroke(cr);
  cairo_arc(cr, 64, 64, 5, 0, 2 * 3.14159265);
  cairo_set_source_rgba(cr, 0, 0, 0, 0.7);
  cairo_fill(cr);
  cairo_arc(cr, 64, 64, 3.5, 0, 2 * 3.14159265);
  cairo_set_source_rgba(cr, 0.2, 0.9, 1.0, 1.0);
  cairo_fill(cr);
  dot_.upload();
  return true;
}

void FocusMarker::updateGaze(bool show, Vec3 origin, Vec3 direction, float distance) {
  showDot_ = show;
  if (!show) return;
  distance = distance > 0 ? std::min(distance, 50.0f) : kGazeDotDistance;
  const Vec3 at = origin + direction * distance;
  Mat4 basis;
  if (!facingBasis(direction * -1.0f, {0, 1, 0}, basis)) basis = Mat4{};
  const float d = kGazeDotSize * distance;
  dotModel_ = translation(at) * basis * scaling(d, d, 1);
}

void FocusMarker::update(bool show, Vec3 headCenter, Quat head, Vec3 direction, float distance,
                         const std::string& text) {
  show_ = show && distance > 0;
  if (!show_) return;
  distance = std::min(distance, 50.0f);
  const Mat4 facing = rotation(head);
  const Vec3 up = transformDir(facing, {0, 1, 0});
  const Vec3 at = headCenter + direction * distance;
  // Facing the head, upright with it.
  Mat4 basis;
  if (!facingBasis(normalize(headCenter - at), up, basis)) basis = facing;
  const float r = kRingRadius * distance, w = kTextWidth * distance;
  ringModel_ = translation(at) * basis * scaling(2 * r, 2 * r, 1);
  textModel_ = translation(at - up * (1.2f * r)) * basis *
               scaling(w, w / SubtitleOverlay::kAspect, 1);
  text_.setText(text);
}

void FocusMarker::addTo(std::vector<OverlayDraw>& overlays) {
  auto add = [&](const Texture* texture, const Mat4& model) {
    PanelItem item;
    item.texture = texture;
    item.model = model;
    overlays.push_back(item);
  };
  if (showDot_) add(dot_.texture(), dotModel_);
  if (!show_) return;
  add(ring_.texture(), ringModel_);
  add(text_.texture(), textModel_);
}
