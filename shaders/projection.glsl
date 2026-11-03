// The surround projections: where a view direction falls in the picture
// (surround.frag, and preview.frag's view straight ahead).
//
// Needs PI (common.glsl).

// YouTube's equi-angular cubemap, laid out like ffmpeg's v360 "eac" input:
// a 3x2 grid with left, front, right on top and down, back, up below, the
// bottom row turned by 270, 90 and 270 degrees.
vec2 eacUv(vec3 dir) {
  vec3 v = vec3(dir.x, -dir.y, -dir.z);  // v360 axes: x right, y down, z forward
  float phi = atan(v.x, v.z);
  float theta = asin(clamp(v.y, -1.0, 1.0));
  const float Q = PI / 4.0;
  int face;  // grid position: 0 left, 1 front, 2 right, 3 down, 4 back, 5 up
  float phiNorm;
  if (phi >= -Q && phi < Q) {
    face = 1;
    phiNorm = phi;
  } else if (phi >= -3.0 * Q && phi < -Q) {
    face = 0;
    phiNorm = phi + PI / 2.0;
  } else if (phi >= Q && phi < 3.0 * Q) {
    face = 2;
    phiNorm = phi - PI / 2.0;
  } else {
    face = 4;
    phiNorm = phi + (phi > 0.0 ? -PI : PI);
  }
  float threshold = atan(cos(phiNorm));
  if (theta > threshold) face = 3;
  else if (theta < -threshold) face = 5;

  vec2 f;
  if (face == 0)      f = vec2(-v.z / v.x, -v.y / v.x);
  else if (face == 1) f = vec2( v.x / v.z,  v.y / v.z);
  else if (face == 2) f = vec2(-v.z / v.x,  v.y / v.x);
  else if (face == 3) f = vec2( v.x / v.y, -v.z / v.y);
  else if (face == 4) f = vec2( v.x / v.z, -v.y / v.z);
  else                f = vec2(-v.x / v.y, -v.z / v.y);
  if (face == 4) f = vec2(-f.y, f.x);                    // turned 90 degrees
  else if (face == 3 || face == 5) f = vec2(f.y, -f.x);  // turned 270 degrees

  f = (2.0 / PI) * atan(f) + 0.5;  // equi-angular: equal angle per pixel
  float column = float(face - 3 * (face / 3));
  float row = float(face / 3);
  return vec2((column + f.x) / 3.0, (row + f.y) / 2.0);
}

// The point of one eye's picture (0..1, row 0 at the top) seen in direction
// `dir` (unit, in the picture's axes). projection: 0 = 180, 1 = 360, 2 =
// fisheye of fisheyeFov radians, 3 = equi-angular cubemap, 4 = cubemap turned
// to a 2x3 grid. inside: false where the picture has nothing.
vec2 pictureUv(vec3 dir, int projection, float fisheyeFov, out bool inside) {
  inside = true;
  if (projection == 3) return eacUv(dir);
  if (projection == 4) {
    // The same cubemap turned 90 degrees counterclockwise (2x3 grid), as
    // YouTube packs each eye of stereo 360 video.
    vec2 s = eacUv(dir);
    return vec2(s.y, 1.0 - s.x);
  }
  if (projection == 2) {
    // Equidistant fisheye: distance from the center grows linearly with the
    // angle from the forward axis.
    float theta = acos(clamp(-dir.z, -1.0, 1.0));
    float r = theta / fisheyeFov;  // 0.5 at the edge of the lens circle
    vec2 d = dir.xy;
    float len = length(d);
    d = len > 1e-6 ? d / len : vec2(0.0);
    inside = r <= 0.5;
    return vec2(0.5 + r * d.x, 0.5 - r * d.y);
  }
  float lon = atan(dir.x, -dir.z);  // 0 = front
  float lat = asin(clamp(dir.y, -1.0, 1.0));
  float span = projection == 0 ? PI : 2.0 * PI;
  inside = abs(lon) <= 0.5 * span;
  return vec2(0.5 + lon / span, 0.5 - lat / PI);
}
