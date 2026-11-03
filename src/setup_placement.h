#pragma once

#include "scene_renderer.h"
#include "vr_space.h"
#include "xmath.h"

#include <vector>

class Menu;

// Where first-run setup's panel floats (docs/alpha/first-run.md, "Placement
// and gaze"). Nothing is placed until someone wears the headset
// (XR_EXT_user_presence, else a head that has moved as a head does; on the
// Frame only tracking) and has held still for 0.5 s, or a button is pressed;
// then it is level, 1.5 m ahead, and stays put. Looked away from, an arrow at
// the edge of view points to it, and after 2 s a line says B brings it here:
// it fades out and back in in front. Looking up for 2 s tilts it to face
// them; sitting up levels it again.
class SetupPlacement {
 public:
  struct Frame {
    double now = 0;  // seconds
    bool headValid = false;
    Vec3 headPos;
    Quat headRot;
    int presence = -1;     // XrContext::userPresence()
    bool pressed = false;  // any button or key: while waiting, they're wearing it
    bool bring = false;    // B, Tab or recenter: in front of them
  };
  // Places and moves `panel`, opening it once placed.
  void update(const Frame& f, MenuPanels& panel);
  bool waiting() const { return !placed_; }
  // Fading out or in: nothing to point at meanwhile.
  bool fading() const { return fade_ != Fade::None; }
  float opacity() const;
  // The arrow toward it, its label and, after a while, the line about B.
  void addCues(std::vector<OverlayDraw>& overlays, Menu& menu, Vec3 headPos, Quat headRot,
               double now) const;

 private:
  enum class Fade { None, Out, In };
  void place(const Frame& f, MenuPanels& panel) const;

  bool placed_ = false;
  bool started_ = false;  // a head pose has come
  bool moved_ = false;    // ... and turned like a head since
  Quat first_, last_;
  double lastAt_ = 0, stillSince_ = 0;
  bool tilted_ = false;     // facing someone lying back
  double pitchSince_ = -1;  // looking up (or, tilted, ahead) since
  bool away_ = false;
  double awaySince_ = 0;
  Vec3 front_;  // the panel's, as of the last update
  Fade fade_ = Fade::None;
  double fadeAt_ = 0, now_ = 0;
};
