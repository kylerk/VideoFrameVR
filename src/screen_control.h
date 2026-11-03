#pragma once

#include "flat_screen.h"
#include "xr_context.h"

#include <vector>

// The controllers' hold on the flat screen:
//  - Grip with the pointer on a corner: the screen grows or shrinks about
//    its centre, the corner following the pointer. A corner's handle shows
//    only while a pointer is on it or it is held.
//  - Grip elsewhere: the screen goes along with the hand, upright and facing
//    the viewer; pulling or pushing changes its distance. Holding the
//    trigger meanwhile (the volume gesture) keeps it still.
//  - Stick up/down: its distance; with one grip held, its height.
class ScreenControl {
 public:
  // What else is going on this frame.
  struct Frame {
    Vec3 up;           // the room's up
    float aspect = 1;  // the picture's width over its height
    float dt = 0;      // seconds since the last frame
    bool onMenu[2] = {false, false};  // each hand's pointer is on the menu
    bool menuHeld = false;            // a grip holds the menu
    bool sticksFree = true;           // stick up/down isn't scrolling a list
  };

  // Each frame while the flat screen shows a picture. True if the screen's
  // size changed.
  bool update(const ControllerState& input, const Frame& frame, ScreenPlacement& screen);
  // Lets go and hides the handle: no flat screen (or no picture) now.
  void reset();

  bool holding() const { return hand_ >= 0; }
  int hand() const { return hand_; }  // -1 = none
  // The corner handle to draw, empty for none.
  const std::vector<Quad>& handle() const { return handle_; }

 private:
  // Where a pointer meets the screen's surface (ScreenSurface coordinates).
  struct Pointing {
    bool onScreen = false;
    float x = 0, y = 0;
    ScreenCorner corner;
  };

  void takeHold(int hand, const HandState& h, const ScreenSurface& surface,
                const ScreenPlacement& screen);
  void carry(const HandState& h, Vec3 up, ScreenPlacement& screen);
  void resize(const HandState& h, float aspect, ScreenPlacement& screen) const;

  int hand_ = -1;
  // Carrying: the hand's pose and the screen's centre at the start (or
  // since the trigger was let go).
  Mat4 startInverse_;
  Vec3 startCentre_;
  // Resizing: the corner, the place it was taken at, the corner's place
  // and the height then.
  ScreenCorner corner_;
  Pointing start_;
  float startCornerX_ = 0, startCornerY_ = 0, startHeight_ = 0;
  Pointing pointing_[2];
  std::vector<Quad> handle_;
};
