#pragma once

#include "platform.h"
#include "settings.h"

#include <future>
#include <string>
#include <vector>

// Performance presets and the user's named presets (docs/alpha/presets.md).
// Both are settings.conf lines for the settings they cover (presets.cpp
// lists the keys); a covered setting a preset doesn't name has its default.

// What new profiles start with, Frame : desktop.
constexpr PerformancePreset kDefaultPerformancePreset =
    kStandaloneHeadset ? PerformancePreset::High : PerformancePreset::Balanced;
constexpr int kPerformancePresetCount = 3;  // Low, Balanced, High

// "low", "balanced", "high" or "custom" (an old "battery" is Low); parsing
// anything else gives Custom.
const char* performancePresetName(PerformancePreset preset);
PerformancePreset parsePerformancePreset(const std::string& name);

// A named preset: a file in configDir()/presets with its name, and the
// stereo focus defaults besides the performance settings.
struct UserPreset {
  std::string name;  // as shown
  std::string file;  // its file name in the folder
  Settings values;   // over the defaults
};

// This device's presets, its resolution in the Resolution row's choices
// (`stops`, eyeResolutionStops; none: percent).
class Presets {
 public:
  explicit Presets(const std::vector<int>& stops);
  // Whether this device has it (the desktop has no Low).
  bool offered(PerformancePreset preset) const;
  // `current` with a preset's covered settings, the resolution snapped to
  // this device's choices; the rest as in `current`.
  Settings with(PerformancePreset preset, const Settings& current) const;
  Settings with(const UserPreset& preset, const Settings& current) const;
  // The performance preset the covered settings match,
  // s.performancePreset first; else Custom.
  PerformancePreset matching(const Settings& s) const;
  // At start, after settleEyeResolution: a named preset gets this device's
  // values (a changed table, a profile from another device); otherwise the
  // settings stay and are named by the preset they match. True if changed.
  bool settle(Settings& s) const;
  // The user's preset shown as chosen: `picked` (a name) while the settings
  // match it, else, when no performance preset matches, the first that does;
  // -1: none.
  int shownUserPreset(const std::vector<UserPreset>& presets, const Settings& s,
                      const std::string& picked) const;

 private:
  Settings with(const Settings& preset, const std::vector<std::string>& keys, Settings s) const;
  std::string covered(Settings s, const std::vector<std::string>& keys) const;

  std::vector<int> stops_;
  Settings values_[kPerformancePresetCount];
  std::string lines_[kPerformancePresetCount];  // covered(values_); empty: not offered
};

// A name as kept: one line, without control characters or surrounding
// spaces. Names are the same ignoring case.
std::string presetName(const std::string& name);
bool samePresetName(const std::string& a, const std::string& b);

// Each runs on the config queue (queueConfigWork), after earlier writes,
// and gives every preset in the folder afterwards, in the user's order.
// Saving under a preset's name (ignoring case) replaces it, a new one goes
// last; renaming keeps the file's other lines; moving goes `by` places.
std::future<std::vector<UserPreset>> listUserPresets();
// Whether two listings have the same presets in the same order.
bool sameUserPresets(const std::vector<UserPreset>& a, const std::vector<UserPreset>& b);
std::future<std::vector<UserPreset>> saveUserPreset(const std::string& name, const Settings& s);
std::future<std::vector<UserPreset>> renameUserPreset(const std::string& file, const std::string& name);
std::future<std::vector<UserPreset>> deleteUserPreset(const std::string& file);
std::future<std::vector<UserPreset>> moveUserPreset(const std::string& file, int by);
