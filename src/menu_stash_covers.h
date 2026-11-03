#pragma once

#include "folder_listing.h"
#include "stash.h"

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

typedef struct _cairo_surface cairo_surface_t;
class Offline;

// What the menu's tiles and rows show of a video Stash knows (docs/stash.md):
// its cover instead of the frame thumbnail, its title instead of the file's
// name, its performers and studio, and the studio's logo on its tile. Shared
// by the file browser, the library view and the Stash pages; made by the
// menu only while Stash is on. GL thread only.
class StashCovers {
 public:
  explicit StashCovers(Stash& stash);
  ~StashCovers();
  StashCovers(const StashCovers&) = delete;
  StashCovers& operator=(const StashCovers&) = delete;

  Stash& stash() const { return stash_; }
  bool covers() const { return stash_.settings().covers; }
  bool titles() const { return stash_.settings().titles; }
  // True once after each new catalog or switch: what tiles show changed.
  bool changed();

  // The scene with a file at `path` (or a stream's locator,
  // stashSceneLocation()), or whose file it is a copy of on this device
  // (offline downloads, by oshash); null if Stash doesn't know it (or
  // has no catalog yet). Valid until the next Stash::poll().
  const StashScene* scene(const std::string& path) const;
  int sceneIndex(const std::string& path) const;  // ... in the catalog, -1 if none
  void setOffline(const Offline* offline) { offline_ = offline; }
  // Its performers as a row or tile names them (women only, unless the
  // male performers setting is on), and its studio.
  std::string performers(const StashScene& scene) const;
  std::string studio(const StashScene& scene) const;
  // The title a video is listed by: Stash's, while titles are on and it
  // has one; else empty (its file's name).
  std::string title(const std::string& path) const;

  // The cover to draw `width` texture pixels wide, once it is there; null
  // while it comes, if there is none or it failed, and with covers off:
  // show the frame thumbnail then.
  cairo_surface_t* cover(const std::string& path, int width) const;
  // Whether the video's cover is shown or on its way, so its own frame
  // thumbnail is not needed.
  bool coverComing(const std::string& path) const;
  // The studio's mark for a tile, while covers are on: its logo fitted
  // into w × h texture pixels with a soft shadow (`pad` pixels of it on
  // each side), to paint as it is; null if there is none yet. `name` is
  // the studio's, to write instead when it has no logo. `top` and
  // `bottom`: the browser's rows it is drawn in. `coming`: null for now,
  // but there will be one.
  cairo_surface_t* logo(const std::string& path, int w, int h, int& pad, std::string& name,
                        double top, double bottom, bool& coming);
  // A studio's logo, the same way; null with `none` set if it has none (or
  // it failed), else while it comes.
  cairo_surface_t* studioLogo(const StashStudio& studio, int w, int h, int& pad, bool& none,
                              double top, double bottom);
  // Once a frame: whether logos were left out of the last one (only a few
  // shadows are made a frame), and the rows to draw again for them.
  bool nextFrame(double& top, double& bottom) {
    madeThisFrame_ = 0;
    top = waitingTop_;
    bottom = waitingBottom_;
    waitingTop_ = waitingBottom_ = 0;
    return std::exchange(logosWaiting_, false);
  }

  // The covers of `paths` (most urgent first), `coverWidth` texture pixels
  // wide, and the logos of the first `shown` of them, `logoWidth` wide.
  void want(const std::vector<std::string>& paths, int coverWidth, size_t shown, int logoWidth);
  // A page's own pictures (a performer's portrait), asked for before the
  // covers by the next want() only, whether covers are on or not.
  void wantFirst(std::vector<StashImages::Request> requests) { first_ = std::move(requests); }
  bool wantsFirst() const { return !first_.empty(); }
  // ... and those not asked for yet, taken back.
  std::vector<StashImages::Request> takeFirst() {
    std::vector<StashImages::Request> requests = std::move(first_);
    first_.clear();
    return requests;
  }

  // A folder or library search: every word in the file's name, or with
  // titles on in its title, performers and studio.
  bool matches(const FolderListing::Entry& entry, const std::string& query) const;

 private:
  struct Logo {
    cairo_surface_t* surface = nullptr;
    uint64_t used = 0;
  };
  const StashCatalog* catalog() const { return stash_.catalog().get(); }

  Stash& stash_;
  const Offline* offline_ = nullptr;
  uint64_t generation_ = 0;
  // Logos with their shadows, made once each (by URL and size), the
  // longest unused dropped past a few screens' worth.
  std::map<std::string, Logo> logos_;
  uint64_t clock_ = 0;
  int madeThisFrame_ = 0;
  bool logosWaiting_ = false;
  double waitingTop_ = 0, waitingBottom_ = 0;
  std::vector<StashImages::Request> first_;
};
