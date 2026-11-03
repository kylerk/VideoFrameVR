#include "menu_style.h"

// Presets atop Video settings (docs/alpha/presets.md): one row across the
// page's top with the performance presets, Custom while the settings were
// changed by hand, the user's own presets as far as they fit and the button
// to the list of them. The list applies, moves, renames and deletes them;
// saving and renaming take a name over the keyboard.

namespace {

constexpr double kChoiceW = 124;  // a performance preset's button, at least
constexpr double kListW = 640;    // the list of the user's presets, a column of it
constexpr double kListRowH = 56, kListStep = 64;

MenuCommand userPresetCommand(MenuCommand::Type type, const std::string& path, double value = 0) {
  MenuCommand c;
  c.type = type;
  c.path = path;
  c.value = value;
  return c;
}

}  // namespace

double Menu::layoutPresetRow() {
  const PresetStrings& t = text().presets;
  const double y = kPagePad;
  double x = columnX(0) + kGroupPad;
  // On the Frame, Advanced settings has the third column's top.
  const double end = kStandaloneHeadset ? columnX(2) - kGap : kPageX + kPageWidth - kPagePad;
  const double labelW = textWidth(t.label, kTextSize, false) + 36;
  addLabel(x + 12, y, labelW - 12, kSettingRowH, t.label);
  x += labelW;
  const std::vector<UserPreset>* own = view_.userPresetsListed > 0 ? view_.userPresets : nullptr;
  const int shown = own ? view_.userPresetShown : -1;
  auto choice = [&](const std::string& label, bool chosen, std::function<MenuCommand()> action,
                    double minW, double maxW) -> Button& {
    Button b = textButton(label, Icon::None, std::move(action), chosen);
    b.panel = kBrowser;
    b.x = x;
    b.y = y;
    b.w = std::clamp(textWidth(label, 24, false) + 40, minW, maxW);
    b.h = kSettingRowH;
    b.fontSize = 24;
    // As a key: a click redraws only what it changes (the row, the rows
    // whose settings change; Menu::update()), never the whole menu at once
    // while the eye images may be switching.
    b.key = true;
    buttons_.push_back(b);
    x += b.w + 8;
    return buttons_.back();
  };
  for (int i = 0; i < kPerformancePresetCount; ++i) {
    const auto preset = static_cast<PerformancePreset>(i);
    if (!view_.presets || !view_.presets->offered(preset)) continue;
    choice(t.names[i], shown < 0 && view_.performancePreset == preset,
           command(MenuCommand::ApplyPreset, i), kChoiceW, 1e9);
  }
  // Changed by hand: lit as Foveation's Custom is, and picking a preset
  // puts its settings back.
  if (shown < 0 && view_.performancePreset == PerformancePreset::Custom)
    choice(t.custom, true, [] { return MenuCommand{}; }, kChoiceW, 1e9).tooltip = t.tipCustom;
  addRule(x + 4, y + 6, kSettingRowH - 12);
  x += 22;
  if (!own) return kPagePad + kSettingRowH + kGap;  // until they are listed

  if (own->empty()) {
    Button save = textButton(t.saveAs, Icon::Plus, [this] {
      openPresetName({}, {});
      return MenuCommand{};
    });
    save.panel = kBrowser;
    save.bare = save.on = true;  // blue text, as a link
    save.x = x - 24;             // its icon where a button's edge would be
    save.y = y;
    save.w = std::min(save.w, end - save.x);
    save.h = kSettingRowH;
    save.fontSize = 24;
    buttons_.push_back(save);
    return kPagePad + kSettingRowH + kGap;
  }
  // As many as fit before the list button, in the list's order.
  constexpr double listButtonW = 56;
  int count = 0;
  for (const UserPreset& preset : *own) {
    const double w = std::min(240.0, textWidth(preset.name, 24, false) + 40);
    if (x + w + 8 + listButtonW > end) break;
    Button& b = choice(preset.name, count == shown,
                       [file = preset.file] {
                         return userPresetCommand(MenuCommand::ApplyUserPreset, file);
                       },
                       0, 240);
    b.radius = kSettingRowH / 2;
    if (view_.presets) b.tooltip = presetSummary(view_.presets->with(preset, defaultSettings()));
    ++count;
  }
  const int hidden = static_cast<int>(own->size()) - count;
  const bool open = dropdown_ == Dropdown::Presets;
  auto toggleList = [this] {
    const bool opening = dropdown_ != Dropdown::Presets;
    dropdown_ = opening ? Dropdown::Presets : Dropdown::None;
    presetDeleting_.clear();
    MenuCommand c;
    if (opening) c.type = MenuCommand::ListUserPresets;  // as the folder has them now
    return c;
  };
  // "+N" is lit too while the preset shown as chosen is one of those.
  Button list = hidden > 0 ? textButton("+" + std::to_string(hidden), Icon::None, toggleList,
                                        open || shown >= count)
                           : iconButton(Icon::List, toggleList, open);
  list.panel = kBrowser;
  list.x = x;
  list.y = y;
  list.w = listButtonW;
  list.h = kSettingRowH;
  list.fontSize = 24;
  list.tooltip = t.myPresets;
  buttons_.push_back(list);
  if (open) layoutPresetList(list);
  return kPagePad + kSettingRowH + kGap;
}

// Under its button: the user's presets in their order, each to apply, move,
// rename or delete (asked in its row), then Save as preset…. Columns side by
// side when there are more than the page's height holds.
void Menu::layoutPresetList(const Button& opener) {
  const PresetStrings& t = text().presets;
  const std::vector<UserPreset>& own = *view_.userPresets;
  const int count = static_cast<int>(own.size());
  const double top = opener.y + opener.h + 24;
  const double rowsTop = top + 44;
  const int perColumn = std::max(
      1, static_cast<int>((kBrowserHeight - kPagePad - 12 - (kListRowH + 4) - rowsTop) / kListStep));
  const int columns = std::max(1, (count + perColumn - 1) / perColumn);
  const double total = columns * kListW + (columns - 1) * 2 * kGap;
  const double x0 = std::clamp(opener.x, kPageX + kPagePad + 12.0,
                               kBrowserWidth - kPagePad - 12.0 - total);
  const double radius = glass() ? kListRowH / 2 : 16;
  addLabel(x0 + 12, top, kListW - 24, 34, t.myPresets, kHintSize, true).popup = true;
  if (own.empty()) addLabel(x0 + 12, rowsTop, kListW - 24, 34, t.none, kHintSize, true).popup = true;

  auto popup = [&](Button b, double x, double y, double w) {
    b.panel = kBrowser;
    b.popup = true;
    b.x = x;
    b.y = y;
    b.w = w;
    b.h = kListRowH;
    b.fontSize = 26;
    b.radius = radius;
    buttons_.push_back(b);
    return &buttons_.back();
  };
  auto round = [&](Icon icon, const char* tip, std::function<MenuCommand()> action, double x,
                   double y) {
    Button& b = addRound(icon, tip, std::move(action), x, y, kListRowH);
    b.panel = kBrowser;
    b.popup = true;
    b.bare = true;
  };
  constexpr double roundGap = 4;
  const double nameW = kListW - 4 * kListRowH - 3 * roundGap - 12;
  for (int i = 0; i < count; ++i) {
    const UserPreset& preset = own[i];
    const double x = x0 + (i / perColumn) * (kListW + 2 * kGap);
    const double y = rowsTop + (i % perColumn) * kListStep;
    if (preset.file == presetDeleting_) {
      char ask[256];
      std::snprintf(ask, sizeof(ask), t.deleteAsk, preset.name.c_str());
      constexpr double cancelW = 136, deleteW = 136;
      addLabel(x + 16, y, kListW - 16 - cancelW - deleteW - 16, kListRowH, ask, 24).popup = true;
      popup(textButton(text().cancel, Icon::None,
                       [this] {
                         presetDeleting_.clear();
                         dirty_ = true;
                         return MenuCommand{};
                       }),
            x + kListW - deleteW - 8 - cancelW, y, cancelW);
      popup(textButton(t.deleteButton, Icon::None,
                       [this, file = preset.file] {
                         presetDeleting_.clear();
                         return userPresetCommand(MenuCommand::DeleteUserPreset, file);
                       }),
            x + kListW - deleteW, y, deleteW)
          ->danger = true;
      continue;
    }
    Button* name = popup(textButton(preset.name, Icon::None,
                                    [this, file = preset.file] {
                                      dropdown_ = Dropdown::None;
                                      return userPresetCommand(MenuCommand::ApplyUserPreset, file);
                                    },
                                    i == view_.userPresetShown),
                         x, y, nameW);
    name->alignLeft = true;
    if (view_.presets) name->tooltip = presetSummary(view_.presets->with(preset, defaultSettings()));
    double bx = x + nameW + 12;
    if (i > 0)
      round(Icon::MoveUp, t.tipMoveUp, [file = preset.file] {
        return userPresetCommand(MenuCommand::MoveUserPreset, file, -1);
      }, bx, y);
    bx += kListRowH + roundGap;
    if (i + 1 < count)
      round(Icon::MoveDown, t.tipMoveDown, [file = preset.file] {
        return userPresetCommand(MenuCommand::MoveUserPreset, file, 1);
      }, bx, y);
    bx += kListRowH + roundGap;
    round(Icon::Pencil, t.tipRename, [this, file = preset.file, n = preset.name] {
      openPresetName(file, n);
      return MenuCommand{};
    }, bx, y);
    bx += kListRowH + roundGap;
    round(Icon::Trash, t.tipDelete, [this, file = preset.file] {
      presetDeleting_ = file;
      dirty_ = true;
      return MenuCommand{};
    }, bx, y);
  }
  const int rows = std::min(std::max(count, 1), perColumn);
  Button save = textButton(t.saveAs, Icon::Plus, [this] {
    openPresetName({}, {});
    return MenuCommand{};
  });
  save.bare = save.on = true;
  popup(save, x0, rowsTop + rows * kListStep - (count ? 0 : 20) + 4, kListW);
}

void Menu::openPresetName(const std::string& file, const std::string& name) {
  presetName_ = {true, file, name, name};
  presetDeleting_.clear();
  dropdown_ = Dropdown::None;
  editing_ = true;
  dirty_ = true;
}

Menu::PresetNameCheck Menu::checkPresetName() const {
  PresetNameCheck check;
  const std::string name = presetName(presetName_.text);
  if (name.empty()) return check;
  const PresetStrings& t = text().presets;
  // Built-in names in English (what other languages' presets are saved
  // with) and as shown.
  for (const char* builtIn : {"Low", "Balanced", "High", "Custom", t.names[0], t.names[1],
                              t.names[2], t.custom})
    if (samePresetName(name, builtIn)) {
      check.kind = PresetNameCheck::BuiltIn;
      return check;
    }
  check.kind = PresetNameCheck::Fine;
  if (!view_.userPresets) return check;
  for (size_t i = 0; i < view_.userPresets->size(); ++i) {
    const UserPreset& p = (*view_.userPresets)[i];
    if (p.file != presetName_.file && samePresetName(p.name, name)) {
      check.kind = PresetNameCheck::Other;
      check.preset = static_cast<int>(i);
      break;
    }
  }
  return check;
}

// Save (Replace over the user's preset of that name) or Rename; back to the
// list.
MenuCommand Menu::submitPresetName() {
  const PresetNameCheck check = checkPresetName();
  const bool renaming = !presetName_.file.empty();
  if (check.kind == PresetNameCheck::Empty || check.kind == PresetNameCheck::BuiltIn ||
      (renaming && check.kind == PresetNameCheck::Other))
    return {};
  MenuCommand c = renaming ? userPresetCommand(MenuCommand::RenameUserPreset,
                                               presetName_.file + "\n" + presetName_.text)
                           : userPresetCommand(MenuCommand::SaveUserPreset, presetName_.text);
  presetName_ = {};
  editing_ = false;
  dropdown_ = Dropdown::Presets;
  dirty_ = true;
  return c;
}

// The Name field alone on the page, as Stash's server fields: what the
// preset holds, why a name can't be used, and Save, Replace or Rename.
void Menu::layoutPresetName() {
  const PresetStrings& t = text().presets;
  const double x0 = columnX(0), w = 3 * kColumnW + 2 * kGap;
  const double x = x0 + kGroupPad, inner = w - 2 * kGroupPad;
  constexpr double h = 56, labelW = 180;
  const double top = kPagePad;
  const bool renaming = !presetName_.file.empty();
  double y = firstRow(top);
  addLabel(x + 12, y, labelW - 12, h, t.name);
  Button field = textButton(presetName_.text, Icon::None, [this] {
    editing_ = true;
    return MenuCommand{};
  });
  field.panel = kBrowser;
  field.x = x + labelW;
  field.y = y;
  field.w = inner - labelW;
  field.h = h;
  field.field = true;
  field.on = editing_;
  field.caret = editing_ ? caret() : -1;
  field.radius = 12;
  field.fontSize = kTextSize;
  buttons_.push_back(field);
  y += h + 10;

  // What it holds: the settings now, or the preset's.
  Settings values;
  bool known = !renaming;
  if (!renaming) {
    values.videoResolution = view_.resolutionStops ? 0 : view_.videoResolution;
    values.videoResolutionPixels = view_.resolutionStops ? view_.videoResolution : 0;
    values.foveation = view_.foveation;
    values.videoMipmaps = view_.videoMipmaps;
    values.antiShimmer = view_.antiShimmer;
    values.sharpen = view_.sharpen;
    values.toneLook = view_.toneLook;
    values.tvGamma = view_.tvGamma;
    values.refreshRate = view_.refreshRate;
  } else if (view_.userPresets && view_.presets) {
    for (const UserPreset& p : *view_.userPresets)
      if (p.file == presetName_.file) {
        values = view_.presets->with(p, defaultSettings());
        known = true;
      }
  }
  char line[1024];
  if (known) {
    std::snprintf(line, sizeof(line), renaming ? t.itsSettings : t.savesThis,
                  presetSummary(values).c_str());
    addLabel(x + 12, y, inner - 12, 0, line, kHintSize, true, 2);
  }
  y += hintHeight(2);
  const PresetNameCheck check = checkPresetName();
  const std::string typed = presetName(presetName_.text);
  const char* note = renaming ? t.renameHint : t.saveHint;
  bool warning = true;
  switch (check.kind) {
    case PresetNameCheck::BuiltIn:
      std::snprintf(line, sizeof(line), t.builtInName, typed.c_str());
      break;
    case PresetNameCheck::Other:
      std::snprintf(line, sizeof(line), renaming ? t.nameTaken : t.replaceWarning,
                    (*view_.userPresets)[check.preset].name.c_str());
      break;
    default:
      std::snprintf(line, sizeof(line), "%s", note);
      warning = false;
  }
  addLabel(x + 12, y, inner - 12, 0, line, kHintSize, !warning, 1);
  y += hintHeight(1) + 12;

  const bool replace = !renaming && check.kind == PresetNameCheck::Other;
  const bool fine = check.kind == PresetNameCheck::Fine || replace;
  Button ok = textButton(renaming ? t.rename : replace ? t.replace : t.save, Icon::None,
                         [this] { return submitPresetName(); }, fine);
  ok.panel = kBrowser;
  ok.disabled = !fine;
  ok.x = x + inner - ok.w;
  ok.y = y;
  ok.h = h;
  buttons_.push_back(ok);
  Button cancel = textButton(text().cancel, Icon::None, [this] {
    presetName_ = {};
    editing_ = false;
    dropdown_ = Dropdown::Presets;
    dirty_ = true;
    return MenuCommand{};
  });
  cancel.panel = kBrowser;
  cancel.x = ok.x - kGap - cancel.w;
  cancel.y = y;
  cancel.h = h;
  buttons_.push_back(cancel);
  y += h + 16;
  char title[256];
  if (renaming)
    std::snprintf(title, sizeof(title), t.renameTitle, presetName_.name.c_str());
  else
    std::snprintf(title, sizeof(title), "%s", t.saveTitle);
  addGroup(x0, top, w, y - top, title);
  if (editing_) layoutKeyboard();
}

// "Resolution: 1.75× · 3780, Foveation: High, Video mipmaps: Off, …": what
// a preset sets, as the page's rows name it.
std::string Menu::presetSummary(const Settings& s) const {
  const Strings& t = text();
  const bool stops = view_.resolutionStops != nullptr;
  const int base = view_.resolutionBase;
  std::string out;
  auto add = [&](const char* label, const std::string& value) {
    char item[256];
    std::snprintf(item, sizeof(item), t.presets.item, label, value.c_str());
    if (!out.empty()) out += t.presets.separator;
    out += item;
  };
  add(t.eyeResolution,
      resolutionText(stops && base > 0 ? s.videoResolutionPixels * 100.0 / base : s.videoResolution,
                     base));
  if (kStandaloneHeadset && view_.foveationAvailable)
    add(t.foveation, t.foveationLevels[std::clamp(static_cast<int>(s.foveation), 0, 4)]);
  if (view_.refreshRate > 0) {
    char hz[32];
    std::snprintf(hz, sizeof(hz), t.refreshRateChoice, s.refreshRate);
    add(t.refreshRate, hz);
  }
  add(t.videoMipmaps, s.videoMipmaps ? t.on : t.off);
  if (!s.videoMipmaps)
    add(t.antiShimmer, t.antiShimmerLevels[std::clamp(static_cast<int>(s.antiShimmer), 0, 2)]);
  char sharpen[16];
  std::snprintf(sharpen, sizeof(sharpen), "%.2f", s.sharpen / 100.0);
  add(t.sharpen, s.sharpen > 0 ? sharpen : t.off);
  add(t.toneLook, s.toneLook ? t.on : t.off);
  add(t.tvGamma, s.tvGamma ? t.on : t.off);
  return out;
}
