#pragma once

#include "depth_estimator.h"
#include "focus_settings.h"

// Why the last measurement did not change the focus (None: it could).
enum class FocusReason { None, Blank, Ambiguous, Mismatch, Edge, Clear, Disagree, OutOfRange };

// Focus selection (docs/autofocus.md):
// measurements come from three probes taken in turn (+1, 0, -1, 0, ...; the
// probe directions are spread apart, less so the higher the sensitivity).
// The centre probe's half-convergence angle is accepted when one side probe
// agrees with it within 0.02 rad and both side probes are farther from the
// previously accepted angle than that. The shown angle follows the accepted
// one with a first-order filter of 4 × transition speed per second, and
// holds while the accepted depth is outside the Nearest..Farthest range.
class FocusController {
 public:
  struct State {
    bool locked = false;   // an angle was accepted since the probes were cleared
    bool holding = false;  // the accepted depth is outside the range: the focus holds
    FocusReason reason = FocusReason::None;
    float measured = 0;    // meters, the last probe's depth (0 = none)
    float confidence = 0;
    float distance = 0;    // meters, what the correction uses now
  };

  // The probe the next measurement is for: -1, 0 or +1.
  int phase() const { return phase_; }
  // Forgets the probes (new content); the accepted and shown angles stay.
  void clearProbes();
  // A measurement for probe `phase`.
  void submit(const DepthEstimate& e, int phase);
  // Advances the filter to `now`; returns the distance to correct at (meters).
  // eyes: aimed by the eyes, small changes are followed faster.
  float advance(const FocusSettings& s, double now, bool eyes);
  const State& state() const { return state_; }

 private:
  State state_;
  float probes_[3] = {0, 0, 0};  // -1, 0, +1; 0 = none
  int phase_ = 0, phaseStep_ = 1;
  float accepted_ = 0.001f;  // half-convergence angles (radians), initially about 32 m at 65 mm
  float shown_ = 0.001f;
  double lastAdvance_ = -1;
};
