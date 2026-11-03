#include "menu_style.h"

#include "menu_setup.h"

#include "smb.h"

#include <algorithm>
#include <thread>

// The browser's Network page (SMB servers found on the network and those
// signed in to) and the form that signs in to one, adds one by its name or
// address, or opens a share a server doesn't list. The shares and folders
// themselves are the file browser's (smb.h locators).

namespace {

std::string lower(std::string text) {
  for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return text;
}

// One "•" per character typed.
std::string hidden(const std::string& text) {
  std::string dots;
  for (char c : text)
    if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) dots += "•";
  return dots;
}

}  // namespace

void Menu::showNetwork() {
  smbForm_ = {};
  showPage(Page::Network);
  discovery_.scan();
}

// The servers to list: those signed in to, then those found that aren't,
// by name.
std::vector<smb::Server> Menu::networkServers() const {
  std::vector<smb::Server> list = smb::servers();
  for (const SmbDiscovery::Server& found : discovery_.servers()) {
    const std::string host = lower(found.address);
    if (std::none_of(list.begin(), list.end(), [&](const smb::Server& s) { return s.host == host; }))
      list.push_back({host, found.name});
  }
  std::sort(list.begin(), list.end(), [](const smb::Server& a, const smb::Server& b) {
    return lower(a.name) < lower(b.name);
  });
  return list;
}

void Menu::openServer(const smb::Server& server) {
  fileKind_ = FileKind::Video;
  openDirectory("smb://" + server.host);
  showPage(Page::Files);
}

// "Network", Add server and Look again; then the servers, each opening its
// shares, those signed in to with Forget at their end.
void Menu::layoutNetworkPage() {
  const NetworkStrings& t = text().network;
  const double x = kPageX + kPagePad, right = kPageX + kPageWidth - kPagePad;
  addLabel(x + 12, kBrowserRowY, 600, kBrowserRowH, t.nav, 30);
  double buttonX = right;
  for (auto [label, action] : {std::pair<const char*, std::function<MenuCommand()>>{
                                   t.addServer,
                                   [this] {
                                     openSmbForm(SmbForm::Kind::AddServer, "");
                                     return MenuCommand{};
                                   }},
                               {t.lookAgain, [this] {
                                  discovery_.scan();
                                  dirty_ = true;
                                  return MenuCommand{};
                                }}}) {
    Button b = textButton(label, Icon::None, action, label == t.addServer);
    b.panel = kBrowser;
    b.x = buttonX - b.w;
    b.y = kBrowserRowY;
    b.h = kBrowserRowH;
    b.disabled = label == t.lookAgain && discovery_.scanning();
    buttons_.push_back(b);
    buttonX = b.x - kGap;
  }
  if (smbForm_.kind != SmbForm::Kind::None) {
    layoutSmbForm(kBrowserToolsY);
    return;
  }
  std::vector<ListItem> items;
  for (const smb::Server& server : networkServers()) {
    ListItem item{server.name, false, [this, server] {
                    openServer(server);
                    return MenuCommand{};
                  }};
    item.folder = true;
    smb::Login login;
    bool remembered = false;
    if (smb::knownLogin(server.host, login, remembered))
      item.trash = [this, host = server.host] {
        smb::forget(host);
        dirty_ = true;
        return MenuCommand{};
      };
    items.push_back(std::move(item));
  }
  listTop_ = kBrowserToolsY;
  listBottom_ = kListBottom;
  listRight_ = right + 6;
  addList(items, x, kFileRowH, kFileRows);
  for (Button& b : buttons_)
    if (b.icon == Icon::Trash && b.scrolls) b.tooltip = t.forget;
}

// Over the page: what an empty Network page means.
const char* Menu::networkState() const {
  if (page_ != Page::Network || smbForm_.kind != SmbForm::Kind::None || listCount_ > 0) return nullptr;
  return discovery_.scanning() ? text().network.looking : text().network.none;
}

// Opens the form; on the Files page it belongs to the server being browsed.
void Menu::openSmbForm(SmbForm::Kind kind, const std::string& host) {
  const NetworkStrings& t = text().network;
  smbForm_ = {};
  smbForm_.kind = kind;
  smbForm_.host = host;
  smb::Login login;
  bool remembered = true;
  const bool known = kind == SmbForm::Kind::SignIn && smb::knownLogin(host, login, remembered);
  smbForm_.text[1] = login.user;
  smbForm_.remember = remembered;
  // A login known but refused: the password has changed.
  if (known) smbForm_.error = t.wrongLogin;
  smbForm_.field = kind == SmbForm::Kind::AddServer ? 0
                   : kind == SmbForm::Kind::Share   ? 0
                   : login.user.empty()            ? 1
                                                   : 2;
  editing_ = true;
  dirty_ = true;
}

// The form's fields (the one typed into outlined blue), Remember password,
// the error if any, then Cancel and Sign in (or Open); the keyboard under it.
void Menu::layoutSmbForm(double top) {
  const NetworkStrings& t = text().network;
  const double x0 = kPageX + kPagePad, w = kPageWidth - 2 * kPagePad;
  const double x = x0 + kGroupPad, inner = w - 2 * kGroupPad;
  constexpr double labelW = 300, h = 56, step = 66;
  const bool share = smbForm_.kind == SmbForm::Kind::Share;
  const bool signing = smbForm_.signing != nullptr;
  pageScrollMax_ = 0;  // drawGroups() mustn't cut it off as a scrolled settings page
  double y = firstRow(top);
  if (share) {
    addLabel(x + 12, y, inner - 12, 0, t.sharesRefused, kHintSize, true, 1);
    y += hintHeight(1) + 8;
  }
  for (const int i : smbFields()) {
    const bool active = smbForm_.field == i && editing_;
    const std::string& typed = smbForm_.text[i];
    const char* placeholder = i == 0 && !share ? t.serverHint : i == 1 ? t.userHint : "";
    addLabel(x + 12, y, labelW - 12, h, share ? t.shareField : t.fields[i]);
    std::string shown = i == 2 && !smbForm_.showPassword ? hidden(typed) : typed;
    Button b = textButton(typed.empty() && !active ? std::string(placeholder) : shown,
                          Icon::None, [this, i] {
                            smbForm_.field = i;
                            editing_ = true;
                            return MenuCommand{};
                          });
    b.panel = kBrowser;
    b.x = x + labelW;
    b.y = y;
    b.w = inner - labelW - (i == 2 ? 130 : 0);
    b.h = h;
    b.field = true;
    b.placeholder = typed.empty() && !active;
    b.on = active;
    b.caret = active ? caret() : -1;
    b.radius = 12;
    b.fontSize = kTextSize;
    buttons_.push_back(b);
    if (i == 2) {
      Button eye = textButton(smbForm_.showPassword ? text().hide : t.show, Icon::None, [this] {
        smbForm_.showPassword = !smbForm_.showPassword;
        return MenuCommand{};
      });
      eye.panel = kBrowser;
      eye.x = x + inner - 120;
      eye.y = y;
      eye.w = 120;
      eye.h = h;
      buttons_.push_back(eye);
    }
    y += step;
  }
  if (!share) {
    addToggleRow(x, y, inner, t.remember, smbForm_.remember, [this] {
      smbForm_.remember = !smbForm_.remember;
      return MenuCommand{};
    });
    y += kSettingStep;
    addLabel(x + 12, y, inner - 12, 0, t.rememberHint, kHintSize, true, 1);
    y += hintHeight(1);
  }
  if (!smbForm_.error.empty()) {
    addLabel(x + 12, y, inner - 12, 0, smbForm_.error, kHintSize, false, 1);
    y += hintHeight(1);
  }
  y += 8;
  Button go = textButton(signing ? t.signingIn : share ? t.open : t.signIn, Icon::None,
                         [this] { return submitSmbForm(); }, true);
  go.panel = kBrowser;
  go.x = x + inner - go.w;
  go.y = y;
  go.h = h;
  go.disabled = signing;
  buttons_.push_back(go);
  Button cancel = textButton(strings().cancel, Icon::None, [this] {
    const bool adding = smbForm_.kind == SmbForm::Kind::AddServer;
    smbForm_ = {};
    editing_ = false;
    if (!adding) showNetwork();
    dirty_ = true;
    return MenuCommand{};
  });
  cancel.panel = kBrowser;
  cancel.x = go.x - kGap - cancel.w;
  cancel.y = y;
  cancel.h = h;
  buttons_.push_back(cancel);
  y += h + 16;
  std::string title = share ? t.shareTitle : t.addTitle;
  if (smbForm_.kind == SmbForm::Kind::SignIn) {
    char line[256];
    std::snprintf(line, sizeof(line), t.signInTitle, serverName(smbForm_.host).c_str());
    title = line;
  }
  addGroup(x0, top, w, y - top, title);
  if (editing_) layoutKeyboard();
}

std::vector<int> Menu::smbFields() const {
  switch (smbForm_.kind) {
    case SmbForm::Kind::AddServer: return {0, 1, 2};
    case SmbForm::Kind::SignIn: return {1, 2};
    case SmbForm::Kind::Share: return {0};
    default: return {};
  }
}

// Enter: on to the next field, or from the last one, Sign in (or Open).
MenuCommand Menu::submitSmbForm() {
  const std::vector<int> fields = smbFields();
  const auto at = std::find(fields.begin(), fields.end(), smbForm_.field);
  if (editing_ && at != fields.end() && at + 1 != fields.end()) {
    smbForm_.field = *(at + 1);
    dirty_ = true;
    return {};
  }
  if (smbForm_.signing) return {};
  if (smbForm_.kind == SmbForm::Kind::Share) {
    std::string name = smbForm_.text[0];
    name.erase(0, name.find_first_not_of("/\\ "));
    if (name.empty()) return {};
    const std::string host = smbForm_.host;
    smbForm_ = {};
    editing_ = false;
    openDirectory("smb://" + host + "/" + name);
    return {};
  }
  // "smb://nas/media", "\\nas\media" and "nas/media" all name nas, then
  // open the share (and folder) after it.
  std::string server = smbForm_.kind == SmbForm::Kind::AddServer ? smbForm_.text[0] : smbForm_.host;
  std::replace(server.begin(), server.end(), '\\', '/');
  if (server.rfind("smb://", 0) == 0) server.erase(0, 6);
  server.erase(0, server.find_first_not_of("/ "));
  const size_t slash = server.find('/');
  smbForm_.host = lower(server.substr(0, slash));
  smbForm_.then = slash == std::string::npos ? "" : server.substr(slash + 1);
  while (!smbForm_.then.empty() && smbForm_.then.back() == '/') smbForm_.then.pop_back();
  if (smbForm_.host.empty()) return {};
  auto result = std::make_shared<std::atomic<int>>(-1);
  smbForm_.signing = result;
  smbForm_.error.clear();
  // --first-run's setup saves it only when it ends (SetupPages::signedIn()).
  const bool remember = smbForm_.remember && !(setup_ && setup_->holdWrites);
  std::thread([result, host = smbForm_.host, login = smb::Login{smbForm_.text[1], smbForm_.text[2]},
               remember] {
    *result = static_cast<int>(smb::signIn(host, login, remember));
  }).detach();
  dirty_ = true;
  return {};
}

// A sign-in finished, or the folder being browsed asks for one (or doesn't
// list its shares).
void Menu::updateNetwork() {
  if (discovery_.poll() && (page_ == Page::Network || setup_)) dirty_ = true;
  if (smbForm_.signing && *smbForm_.signing >= 0) {
    const auto status = static_cast<smb::Status>(smbForm_.signing->load());
    const NetworkStrings& t = text().network;
    smbForm_.signing.reset();
    dirty_ = true;
    if (status == smb::Status::Ok) {
      const std::string where = "smb://" + smbForm_.host + (smbForm_.then.empty() ? "" : "/" + smbForm_.then);
      smb::setName(smbForm_.host, serverName(smbForm_.host));
      if (setup_) {
        SetupPages::signedIn(*this);
        return;
      }
      smbForm_ = {};
      editing_ = false;
      fileKind_ = FileKind::Video;
      if (where == directory())
        refreshDirectory();
      else
        openDirectory(where);
      showPage(Page::Files);
      return;
    }
    smbForm_.error = status == smb::Status::Offline ? t.offline
                     : status == smb::Status::Denied ? t.denied
                                                     : t.wrongLogin;
    if (setup_) SetupPages::signInFailed(*this);
  }
}

// The folder just read: an SMB server that wants a login gets the form, one
// that doesn't list its shares the share's field.
void Menu::smbFolderRead() {
  if (page_ != Page::Files || !smb::isSmb(directory()) || smbForm_.kind != SmbForm::Kind::None) return;
  const std::string host = smb::host(directory());
  if (listing_.status() == smb::Status::NeedsLogin)
    openSmbForm(SmbForm::Kind::SignIn, host);
  else if (listing_.status() == smb::Status::Denied && smb::parent(directory()).empty())
    openSmbForm(SmbForm::Kind::Share, host);
}

// Its name as found on the network, else as signed in to (or typed).
std::string Menu::serverName(const std::string& host) const {
  for (const SmbDiscovery::Server& found : discovery_.servers())
    if (lower(found.address) == host) return found.name;
  for (const smb::Server& s : smb::servers())
    if (s.host == host) return s.name;
  return host;
}

// A locator as the browser shows it: the server's name, then the share and
// folders ("NAS/media/films").
std::string Menu::shownLocation(const std::string& dir) const {
  if (!smb::isSmb(dir)) return dir;
  const std::string host = smb::host(dir);
  return serverName(host) + dir.substr(6 + host.size());
}
