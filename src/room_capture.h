#pragma once

#include "room_light.h"

#include <atomic>
#include <string>
#include <thread>

// Reads the room's light for the colour grade of passthrough video
// (room_light.h) from SteamVR's headset view, a video device on the Steam Frame.
// One frame on a worker thread, never the render loop. The picture stays in
// memory only until it is measured, then it is wiped: nothing of it is written
// anywhere, and only the measurement stays.
//
// The headset view shows what the app draws too, so while it is looked at
// (blanking()) the app must draw nothing, leaving only the room. Main thread only.
class RoomCapture {
 public:
  enum class Failure { None, Black, Device };

  RoomCapture() = default;
  RoomCapture(const RoomCapture&) = delete;
  RoomCapture& operator=(const RoomCapture&) = delete;
  ~RoomCapture();

  // Ignored while one is running.
  void start();
  bool busy() const { return running_; }
  bool blanking() const { return blank_; }
  // A finished capture, once: room if it worked, else what went wrong
  // (Device: detail says what, in plain words for the log and the menu).
  bool take(RoomEstimate& room, Failure& failure, std::string& detail);

 private:
  void work();

  std::thread worker_;
  std::atomic<bool> running_{false}, blank_{false}, quit_{false}, done_{false};
  // The worker's until done_.
  RoomEstimate room_;
  Failure failure_ = Failure::None;
  std::string detail_;
};
