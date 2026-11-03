#pragma once

#include <string>

// The menu's accent colour (Settings → Appearance): what is chosen, the
// panels' edge and blue text. One of the presets, or a hue, brightness and
// saturation of the user's own.
constexpr int kAccentPresets = 7;  // blue (the default), teal, green, orange, red, pink, purple
constexpr int kAccentCustom = kAccentPresets;

struct Accent {
  int preset = 0;       // 0..kAccentPresets - 1, or kAccentCustom
  int hue = 160;        // custom: degrees around OKLCH's hue circle, 0..359
  int brightness = 50;  // custom: percent; 50 keeps white text as readable as on the presets
  int saturation = 100;  // custom: percent of the most vivid colour of that hue and brightness
  bool operator==(const Accent& o) const {
    return preset == o.preset && hue == o.hue && brightness == o.brightness &&
           saturation == o.saturation;
  }
  bool operator!=(const Accent& o) const { return !(*this == o); }
};

// sRGB, 0..1.
struct AccentColours {
  double fill[3];  // chosen buttons, switches and sliders; the panels' edge
  double text[3];  // chosen rows and other accent text, on the dark panels
};
AccentColours accentColours(const Accent& accent);

// Kept in settings.conf: "blue" ... "purple", "custom".
const char* accentCode(int preset);
int accentPreset(const std::string& code);  // unknown: blue
