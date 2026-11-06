#pragma once

#include <cstdint>
#include <vector>

// The light of the room around a passthrough video, from a picture of it, and
// the colour grade that makes the video sit in that light. No I/O: the picture
// comes from room_capture.h.

// A picture, 3 bytes per pixel (R, G, B), sRGB values.
struct RoomImage {
  int width = 0, height = 0;
  std::vector<uint8_t> rgb;
};

struct RoomEstimate {
  bool ok = false;      // enough of the picture shows something
  float luminance = 0;  // typical (geometric mean) linear brightness, 0..1
  float tint[3] = {1, 1, 1};  // average linear colour, the largest channel 1
  float usable = 0;     // the part of the picture measured, 0..1
};

// Leaves out pure black (a picture not shown yet, the corners of the headset
// view) and, for the colour, clipped pixels (lamps, which say little about the
// light on everything else).
RoomEstimate estimateRoom(const RoomImage& image);

// How a room becomes a grade (Video settings, in passthrough; saved in Settings).
struct RoomTuning {
  int tint = 70;        // percent of the room's tint the video takes
  int brightness = 60;  // percent of the room's distance from mid grey, in stops
  int darkest = 300;    // the most a room darkens the video: hundredths of a stop
  int red = 110, green = 110, blue = 110;  // percent, on top of everything
};

// The linear gains for red, green and blue the shader multiplies the picture
// by: part of the room's tint (lowering the other channels rather than
// clipping the strongest), part of how far the room is below a mid grey (a room
// never brightens the video), then the trims. Without a room (!room.ok): no
// grade at all, every gain exactly 1.
void roomGrade(const RoomEstimate& room, const RoomTuning& tuning, float gains[3]);
