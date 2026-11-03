#pragma once

// How big the VR menu is and where it floats, in one place.
//
// At a menu distance of 150 cm and view arc of 110°, the whole
// menu drawing, 5000 × 3000 layout units, is bent around a vertical cylinder
// whose axis stands 12 cm behind the eyes, so the surface straight ahead is
// 1.5 m away and the drawing spans 110° of the cylinder. Its central page is
// 2880 × 1700 units and the playback strip, centred below it, 2572 units wide.
// Every panel here lies on that cylinder at the same scale, so the browser,
// the playback strip and the format panel share curvature, scale and anchor.
//
// The values marked "not recovered" are layout choices that still need
// checking in the headset.
namespace menu_geometry {

constexpr float kDistance = 1.5f;     // eyes to the surface straight ahead, meters
constexpr float kAxisBehind = 0.12f;  // the cylinder's axis behind the eyes
constexpr float kRadius = kDistance + kAxisBehind;
constexpr float kArcDegrees = 110.0f;  // the whole menu drawing
// Meters of arc per reference layout unit (the drawing is 5000 units wide).
constexpr float kUnit = kRadius * kArcDegrees * 3.14159265f / 180.0f / 5000.0f;

// Layout pixels per meter on the surface: 26 per degree straight ahead. The
// menu is laid out in these.
constexpr float kPixelsPerMeter = 1000.0f;
// Texture pixels per layout pixel: the menu is drawn this much finer than it
// is laid out, 52 pixels per degree, so the eye image samples it from
// above its own resolution (the 5000-wide reference drawing has 42).
constexpr float kRasterScale = 2.0f;
static_assert(kRasterScale == static_cast<int>(kRasterScale) && kRasterScale >= 1,
              "whole: a scrolled list is moved by whole texture pixels, which must land "
              "where drawing it there would");

// Reference sizes in pixels (rounded to whole pixels).
constexpr int kPageWidthPx = 1792;   // central page, 2880 units
constexpr int kPageHeightPx = 1058;  // 1700 units
constexpr int kStripWidthPx = 1600;  // playback strip, 2572 units
static_assert(kPageWidthPx - 2880 * kUnit * kPixelsPerMeter < 1 &&
                  2880 * kUnit * kPixelsPerMeter - kPageWidthPx < 1,
              "page width follows the reference");
static_assert(kPageHeightPx - 1700 * kUnit * kPixelsPerMeter < 1 &&
                  1700 * kUnit * kPixelsPerMeter - kPageHeightPx < 1,
              "page height follows the reference");
static_assert(kStripWidthPx - 2572 * kUnit * kPixelsPerMeter < 1 &&
                  2572 * kUnit * kPixelsPerMeter - kStripWidthPx < 1,
              "strip width follows the reference");

// The playback strip's bottom edge, below eye level (m): the reference menu
// hangs from the bottom of its 3000-unit drawing, centred on the eyes, with
// 184 + 100 units of padding under the strip (inferred, not measured live).
constexpr float kStripBottom = -0.756f;
// Not recovered: the gap up to the browser page, and the sources column.
constexpr float kPageGap = 0.03f;     // from the strip up to the browser page
constexpr int kSourcesWidthPx = 380;  // the sources column left of the page
constexpr int kSourcesGapPx = 16;     // inner border padding

// Panels in front of others sit this much closer to the eyes, so the strip's
// open lists cover the page without flickering.
constexpr float kLayerStep = 0.01f;

}  // namespace menu_geometry
