#include "flat_screen.h"

#include <algorithm>
#include <cmath>

ScreenSize ScreenSize::clamped() const {
  ScreenSize s;
  s.distance = std::clamp(distance, kMinDistance, kMaxDistance);
  s.height = std::clamp(height, kMinHeight, kMaxHeight);
  s.curve = std::clamp(curve, 0.0f, kMaxCurve);
  return s;
}

ScreenSurface::ScreenSurface(const ScreenPlacement& placement, float aspect)
    : pose_(placement.pose()),
      inverse_(transposedRotation(placement.rotation()) * translation(placement.anchorPosition * -1.0f)),
      height_(placement.size.height),
      distance_(placement.size.distance) {
  width_ = height_ * aspect;
  if (placement.size.curve > 1e-3f) {
    radius_ = distance_ / placement.size.curve;
    const float widest = 2 * kMaxHalfArc * radius_;
    if (width_ > widest) {
      height_ *= widest / width_;
      width_ = widest;
    }
  }
}

Vec3 ScreenSurface::point(float x, float y) const {
  if (radius_ <= 0) return transformPoint(pose_, {x, y, -distance_});
  // The bend's axis is upright, radius_ behind the screen's centre.
  const float angle = x / radius_;
  return transformPoint(pose_, {radius_ * std::sin(angle), y,
                                radius_ - distance_ - radius_ * std::cos(angle)});
}

bool ScreenSurface::hit(Vec3 origin, Vec3 dir, float& x, float& y) const {
  const Vec3 o = transformPoint(inverse_, origin), d = transformDir(inverse_, dir);
  if (radius_ <= 0) {
    if (std::abs(d.z) < 1e-6f) return false;
    const float t = (-distance_ - o.z) / d.z;
    if (t <= 0) return false;
    x = o.x + d.x * t;
    y = o.y + d.y * t;
    return true;
  }
  // The upright cylinder of the bend, seen from inside: its far side first.
  const float axisZ = radius_ - distance_;
  const float qx = o.x, qz = o.z - axisZ;
  const float a = d.x * d.x + d.z * d.z, b = 2 * (qx * d.x + qz * d.z);
  const float c = qx * qx + qz * qz - radius_ * radius_;
  const float disc = b * b - 4 * a * c;
  if (a < 1e-9f || disc < 0) return false;
  const float root = std::sqrt(disc);
  for (const float t : {(-b + root) / (2 * a), (-b - root) / (2 * a)}) {
    if (t <= 0) continue;
    const Vec3 p = o + d * t;
    const float angle = std::atan2(p.x, axisZ - p.z);
    if (std::abs(angle) > kMaxHalfArc) continue;
    x = angle * radius_;
    y = p.y;
    return true;
  }
  return false;
}

namespace {

// A corner handle's measures on the surface: about 3° long and a third of a
// degree thick from the viewer, wherever the screen is, but on a small
// screen short enough that corners keep apart.
struct HandleShape {
  float arm, thick, gap;
  explicit HandleShape(const ScreenSurface& s)
      : arm(std::min(0.05f * s.distance(), 0.4f * std::min(s.width(), s.height()))),
        thick(0.006f * s.distance()),
        gap(thick) {}
};

}  // namespace

ScreenCorner cornerAt(const ScreenSurface& surface, float x, float y) {
  // Within an arm's length of the corner, inside or outside the picture.
  const float reach = HandleShape(surface).arm;
  const float halfW = surface.width() / 2, halfH = surface.height() / 2;
  if (std::abs(std::abs(x) - halfW) > reach || std::abs(std::abs(y) - halfH) > reach) return {};
  return {x < 0 ? -1 : 1, y < 0 ? -1 : 1};
}

std::vector<Quad> cornerHandle(const ScreenSurface& surface, ScreenCorner corner) {
  if (!corner.valid()) return {};
  const HandleShape h(surface);
  const int sx = corner.sx, sy = corner.sy;
  const float arm = h.arm, thick = h.thick;
  const float outX = sx * (surface.width() / 2 + h.gap + thick);
  const float outY = sy * (surface.height() / 2 + h.gap + thick);
  std::vector<Quad> quads;
  // In `pieces` across, so a bent screen's arm follows the bend.
  auto rect = [&](float x0, float x1, float y0, float y1, int pieces) {
    for (int i = 0; i < pieces; ++i) {
      const float a = x0 + (x1 - x0) * i / pieces, b = x0 + (x1 - x0) * (i + 1) / pieces;
      quads.push_back({surface.point(a, y0), surface.point(b, y0), surface.point(a, y1),
                       surface.point(b, y1)});
    }
  };
  rect(outX - sx * arm, outX, outY - sy * thick, outY, 4);  // along the top or bottom
  rect(outX - sx * thick, outX, outY - sy * arm, outY, 1);  // along the side
  return quads;
}
