#include "accent.h"

#include <algorithm>
#include <cmath>

// Colours are made in OKLCH, where lightness looks the same across hues: the
// fill of every hue is as light as keeps white text on it as readable as on
// today's blue (4.05:1), and as vivid as sRGB allows at that lightness.

namespace {

constexpr const char* kCodes[kAccentPresets + 1] = {"blue", "teal",  "green",  "orange",
                                                    "red",  "pink", "purple", "custom"};
constexpr double kPresetHues[kAccentPresets] = {259.4, 200, 145, 50, 25, 350, 305};

struct Linear {
  double r, g, b;
};

Linear oklchToLinear(double L, double C, double hue) {
  const double h = hue * M_PI / 180;
  const double a = C * std::cos(h), b = C * std::sin(h);
  const double l = std::pow(L + 0.3963377774 * a + 0.2158037573 * b, 3);
  const double m = std::pow(L - 0.1055613458 * a - 0.0638541728 * b, 3);
  const double s = std::pow(L - 0.0894841775 * a - 1.2914855480 * b, 3);
  return {4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
          -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
          -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s};
}

bool inGamut(const Linear& c) {
  constexpr double e = 1e-6;
  return c.r >= -e && c.r <= 1 + e && c.g >= -e && c.g <= 1 + e && c.b >= -e && c.b <= 1 + e;
}

// The most vivid colour of this hue and lightness sRGB can show, or this
// share of it.
Linear vivid(double L, double hue, double share = 1) {
  double lo = 0, hi = 0.4;
  for (int i = 0; i < 24; ++i) {
    const double mid = (lo + hi) / 2;
    (inGamut(oklchToLinear(L, mid, hue)) ? lo : hi) = mid;
  }
  return oklchToLinear(L, lo * share, hue);
}

double whiteContrast(const Linear& c) {
  const double y = 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b;
  return 1.05 / (std::max(0.0, y) + 0.05);
}

// The lightness at which white text on the hue's fill has this contrast.
double lightnessFor(double hue, double contrast) {
  double lo = 0.3, hi = 0.9;
  for (int i = 0; i < 24; ++i) {
    const double mid = (lo + hi) / 2;
    (whiteContrast(vivid(mid, hue)) > contrast ? lo : hi) = mid;
  }
  return lo;
}

void toSrgb(const Linear& c, double out[3]) {
  int i = 0;
  for (double v : {c.r, c.g, c.b}) {
    v = std::clamp(v, 0.0, 1.0);
    out[i++] = v <= 0.0031308 ? 12.92 * v : 1.055 * std::pow(v, 1 / 2.4) - 0.055;
  }
}

// brightness 50: as readable as the presets; 0 dark; 100 as light as white
// text still reads on (3:1). saturation: percent of the most vivid.
AccentColours make(double hue, int brightness, int saturation) {
  const double share = std::clamp(saturation, 0, 100) / 100.0;
  const double normal = lightnessFor(hue, 4.05);
  const double t = std::clamp(brightness, 0, 100) / 50.0 - 1;
  const double L = t < 0 ? normal + t * (normal - 0.40) : normal + t * (lightnessFor(hue, 3.0) - normal);
  AccentColours c;
  toSrgb(vivid(L, hue, share), c.fill);
  // Text on the dark panels: lighter than the fill, following it a little.
  toSrgb(vivid(std::clamp(0.711 + 0.6 * (L - normal), 0.64, 0.84), hue, share), c.text);
  return c;
}

}  // namespace

AccentColours accentColours(const Accent& accent) {
  if (accent.preset == kAccentCustom) return make(accent.hue, accent.brightness, accent.saturation);
  if (accent.preset <= 0 || accent.preset >= kAccentPresets)
    return {{0.118, 0.471, 1.0}, {0.36, 0.64, 1.0}};  // blue, as it always was
  static AccentColours presets[kAccentPresets] = {};
  static bool made[kAccentPresets] = {};
  if (!made[accent.preset]) {
    presets[accent.preset] = make(kPresetHues[accent.preset], 50, 100);
    made[accent.preset] = true;
  }
  return presets[accent.preset];
}

const char* accentCode(int preset) {
  return kCodes[std::clamp(preset, 0, kAccentCustom)];
}

int accentPreset(const std::string& code) {
  for (int i = 0; i <= kAccentCustom; ++i)
    if (code == kCodes[i]) return i;
  return 0;
}
