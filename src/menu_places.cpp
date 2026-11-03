#include "menu_style.h"

// Edit places: the navigation's places in a group, each with its name (to
// rename it), where it is, a switch for whether it shows and its moves; under
// the group, back to the default order and Done.
void Menu::layoutPlacesPage() {
  if (!placeRename_.path.empty()) {
    layoutPlaceRename();
    return;
  }
  const PlacesStrings& t = text().places;
  const double x0 = kPageX + kPagePad, w = kPageWidth - 2 * kPagePad;
  const double x = x0 + kGroupPad, inner = w - 2 * kGroupPad;
  constexpr double nameW = 560, switchW = 110, moveW = kSettingRowH;
  const double downX = x + inner - moveW, upX = downX - 6 - moveW, switchX = upX - 16 - switchW;
  const double locationX = x + nameW + kGap;
  const double top = kPagePad - 4;
  double y = firstRow(top);
  addLabel(x + 12, y, inner - 12, 0, t.hint, kHintSize, true, 1);
  y += hintHeight(1) + 6;
  addLabel(x + 12, y, nameW - 12, 30, t.name, kHintSize, true);
  addLabel(locationX, y, switchX - locationX, 30, t.location, kHintSize, true);
  addLabel(switchX, y, switchW, 30, t.show, kHintSize, true);
  y += 38;

  auto place = [this](std::function<void(Places&)> change) {
    return [this, change] {
      change(places_);
      dirty_ = true;
      return MenuCommand{};
    };
  };
  const auto& places = places_.places();
  for (size_t i = 0; i < places.size(); ++i) {
    const Places::Place& p = places[i];
    Button name = textButton(p.label, Icon::None, [this, path = p.path, own = p.ownName,
                                                   label = p.label] {
      placeRename_ = {path, own, label};
      editing_ = true;
      dirty_ = true;
      return MenuCommand{};
    });
    name.panel = kBrowser;
    name.x = x;
    name.y = y;
    name.w = nameW;
    name.h = kSettingRowH;
    name.field = true;
    name.radius = 10;
    name.fontSize = 24;
    name.tooltip = t.tipRename;
    buttons_.push_back(name);
    addLabel(locationX, y, switchX - kGap - locationX, kSettingRowH, shownLocation(p.path),
             kHintSize, true);
    addToggleRow(switchX, y, switchW, "", !p.hidden,
                 place([path = p.path, hidden = p.hidden](Places& all) {
                   all.setHidden(path, !hidden);
                 }));
    for (const auto& [by, icon, tip, at] : {std::tuple{-1, Icon::MoveUp, t.tipMoveUp, upX},
                                           std::tuple{1, Icon::MoveDown, t.tipMoveDown, downX}}) {
      if ((by < 0 && i == 0) || (by > 0 && i + 1 == places.size())) continue;
      Button& move = addRound(icon, tip, place([path = p.path, by = by](Places& all) {
                                all.move(path, by);
                              }), at, y, moveW);
      move.panel = kBrowser;
      move.bare = true;
    }
    y += kSettingStep;
  }
  addGroup(x0, top, w, y + 8 - top, t.title);

  y += 8 + kGap;
  Button done = textButton(strings().done, Icon::None, [this] {
    showBrowser();
    return MenuCommand{};
  }, true);
  done.panel = kBrowser;
  done.x = x0 + w - done.w;
  done.y = y;
  done.h = kBrowserRowH;
  buttons_.push_back(done);
  Button reset = textButton(t.reset, Icon::None, place([](Places& all) { all.resetOrder(); }));
  reset.panel = kBrowser;
  reset.x = done.x - kGap - reset.w;
  reset.y = y;
  reset.h = kBrowserRowH;
  reset.tooltip = t.tipReset;
  buttons_.push_back(reset);
}

// One place's name in its field, over the keyboard: Enter or Save keeps it,
// empty gives the place its own name back.
void Menu::layoutPlaceRename() {
  const PlacesStrings& t = text().places;
  const double x0 = kPageX + kPagePad, w = kPageWidth - 2 * kPagePad;
  const double x = x0 + kGroupPad, inner = w - 2 * kGroupPad;
  constexpr double h = 56;
  const double top = kPagePad - 4;
  double y = firstRow(top);
  const std::string& typed = placeRename_.text;
  Button field = textButton(typed.empty() && !editing_ ? placeRename_.ownName
                                                       : typed,
                            Icon::None, [this] {
                              editing_ = true;
                              return MenuCommand{};
                            });
  field.panel = kBrowser;
  field.x = x;
  field.y = y;
  field.w = inner;
  field.h = h;
  field.field = true;
  field.placeholder = typed.empty() && !editing_;
  field.on = editing_;
  field.caret = editing_ ? caret() : -1;
  field.radius = 12;
  field.fontSize = kTextSize;
  buttons_.push_back(field);
  y += h + 10;
  addLabel(x + 12, y, inner - 12, 0, t.renameHint, kHintSize, true, 1);
  y += hintHeight(1) + 8;
  Button save = textButton(t.save, Icon::None, [this] {
    savePlaceName();
    return MenuCommand{};
  }, true);
  save.panel = kBrowser;
  save.x = x + inner - save.w;
  save.y = y;
  save.h = h;
  buttons_.push_back(save);
  Button cancel = textButton(strings().cancel, Icon::None, [this] {
    placeRename_ = {};
    editing_ = false;
    dirty_ = true;
    return MenuCommand{};
  });
  cancel.panel = kBrowser;
  cancel.x = save.x - kGap - cancel.w;
  cancel.y = y;
  cancel.h = h;
  buttons_.push_back(cancel);
  y += h + 16;
  char title[256];
  std::snprintf(title, sizeof(title), t.renameTitle, placeRename_.ownName.c_str());
  addGroup(x0, top, w, y - top, title);
  if (editing_) layoutKeyboard();
}

void Menu::savePlaceName() {
  places_.rename(placeRename_.path, placeRename_.text);
  placeRename_ = {};
  editing_ = false;
  dirty_ = true;
}
