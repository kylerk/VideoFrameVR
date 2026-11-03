#pragma once

#include "curved_panel.h"
#include "xmath.h"

class Menu;

// Geometry in the room: angles, poses, and where the menu panels float.

constexpr float kPi = 3.14159265f;

// An angle brought into -pi..pi.
float wrapAngle(float a);
// The horizontal direction a heading (yaw) looks along.
Vec3 forwardFromYaw(float yaw);
// The frame things are placed in front of: the head's heading.
Mat4 headFrame(Quat headOrientation);
// The frame the menu opens in: the head's heading and pitch with its roll
// taken out.
Mat4 menuFrame(Quat headOrientation);
// A tracked pose as a matrix, and its inverse.
Mat4 poseMatrix(Vec3 position, Quat orientation);
Mat4 poseInverse(Vec3 position, Quat orientation);
// Rotation whose +Z axis points along `z` and whose Y axis is as close to
// `up` as possible. False if `z` points (nearly) straight along `up`.
bool facingBasis(Vec3 z, Vec3 up, Mat4& out);

// The menu: the playback strip in front of the user when the menu opens,
// and the browser page above it, both on one
// cylinder around where the eyes were (see menu_geometry.h).
struct MenuPanels {
  bool open = false;
  Vec3 anchor;       // the cylinder's axis at eye level (tilted with the menu)
  Mat4 orientation;  // Y along the axis, -Z straight ahead

  // In front of the eyes, aligned with `frame` (see menuFrame).
  void placeInFrontOf(Vec3 headPosition, const Mat4& frame);
  // The surface straight ahead at eye level.
  Vec3 front() const;
  // Puts the front at `point`, facing `headPosition` with no roll about `up`.
  void moveFront(Vec3 point, Vec3 headPosition, Vec3 up);
  // Where a menu panel (Menu::PanelId) is, with the rows it shows now.
  CurvedPanel panel(const Menu& menu, int id) const;
};
