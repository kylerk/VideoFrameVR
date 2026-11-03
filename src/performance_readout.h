#pragma once

#include "cairo_canvas.h"
#include "scene_renderer.h"
#include "video_player.h"
#include "xmath.h"

#include <cstdint>
#include <string>

// A small readout of how smoothly the app runs, head-locked at the bottom
// left of the view, also with the menu closed (Settings → Show performance,
// or --show-performance for one run). Every half second it shows, over the
// last two seconds, the headset's frames drawn against its rate and the new
// video frames against the file's, mpv's dropped frames in the last five
// seconds and since the file opened or the last seek, and what the picture
// costs: eye size, foveation, GPU time per frame and how the video is
// decoded and drawn. Green while the video keeps its rate without drops,
// amber or red otherwise.
//
// It only reads what is counted anyway (VideoPlayer::Counts, the frame
// loop's display times and GPU timestamps); its texture is drawn again only
// when the text changes, at most twice a second, and nothing is allocated
// per frame.
class PerformanceReadout {
 public:
  bool init(Uploader& uploader);

  // Switched on: what was counted before is forgotten.
  void restart(double now);
  // Each headset frame drawn: its predicted display time (seconds).
  void frameDrawn(double displayTime) {
    ++frames_;
    lastDisplay_ = displayTime;
  }
  // A frame's GPU time, both eyes and the rest of the submission (ms).
  void gpuTime(double ms) {
    gpuSum_ += ms;
    ++gpuFrames_;
  }

  // What update() reads.
  struct State {
    double displayRate = 0;  // the headset's frames per second, 0 = not known
    bool video = false;      // a video is open (not a photo)
    bool paused = false;
    bool waiting = false;    // loading, or a seek on its way
    double sourceFps = 0;    // the file's, 0 = not known
    const VideoPlayer::Counts* counts = nullptr;
    const std::string* codec = nullptr;        // mpv's, e.g. "hevc"
    const std::string* pixelFormat = nullptr;  // e.g. "p010"
    bool direct = false;     // the eyes sample decoded frames (mpv does not draw them)
    int eyeWidth = 0;        // eye image pixels across
    int foveation = -1;      // FoveationLevel, -1 = none (desktop)
    float sharpWidth = 0, sharpHeight = 0;
  };
  // True every half second: then update().
  bool due(double now) const { return now >= nextAt_; }
  void update(double now, const State& state);

  // The readout for this frame's scene, in front of the head.
  PanelItem item(Vec3 headPos, Quat headRot);

 private:
  static constexpr double kStep = 0.5;   // seconds per update
  static constexpr int kRateSteps = 4;   // rates over the last 2 s
  static constexpr int kDropSteps = 10;  // drops over the last 5 s
  enum Status { Neutral, Good, Warn, Bad };
  struct Step {
    int frames = 0;
    double displaySpan = 0;
    double gpuSum = 0;
    int gpuFrames = 0;
    bool videoValid = false;  // played through the whole step
    uint64_t videoFrames = 0;
    double videoSpan = 0;
    long long drops = 0;
  };
  void draw();

  CairoCanvas canvas_;
  bool dirty_ = true;
  double nextAt_ = 0, lastAt_ = 0;
  // Counting since the last update.
  int frames_ = 0;
  double lastDisplay_ = 0, stepDisplay_ = 0;
  double gpuSum_ = 0;
  int gpuFrames_ = 0;
  Step steps_[kDropSteps];
  int stepCount_ = 0, stepAt_ = 0;  // filled steps, the next one to write
  uint64_t lastNewFrames_ = 0;
  long long lastDrops_ = -1;
  uint64_t lastRestarts_ = 0;
  bool wasPlaying_ = false;
  long long dropsSince_ = 0;  // since the file opened or the last seek
  bool sinceSeek_ = false;
  // What is shown.
  char lines_[5][96] = {};
  Status status_[5] = {};
};
