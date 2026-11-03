// Shared by the scene shaders: the eye's part of every push-constant block,
// clip space and colour conversions.
//
// The push-constant block (256 bytes) is the eye's data at offset 0, then
// the draw's own block at offset 112. scene_renderer.h holds the matching
// C++ structs; keep both in step.

struct EyeData {
  vec4 orientation;  // quaternion x, y, z, w: from the eye's axes to local space
  vec4 position;     // local space; w = the eye's index, 0 left, 1 right
  vec4 tangents;     // tan of the field of view's left, right, down and up angles
  vec4 area;         // the render area in pixels: x, y, width, height
  vec4 stereo;       // this eye's part of a stereo frame: scale xy, offset xy
  vec4 viewer;       // stereo correction: this eye from the head centre, w = focus distance (0 = off)
  vec4 camera;       // ... and its camera from the rig centre
};

// Declares the push-constant block with a draw block of type T, and
// currentEye(), the one place shaders get their eye's data from.
#define DECLARE_PUSH(T)                              \
  layout(push_constant) uniform Push {               \
    EyeData eye;                                     \
    T draw;                                          \
  } pc;                                              \
  EyeData currentEye() { return pc.eye; }

const float PI = 3.14159265359;
// The clip planes of EyeMatrices::projection (xr_context.cpp).
const float kNearZ = 0.05;
const float kFarZ = 100.0;

vec3 linearToSrgb(vec3 c) {
  return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));
}
vec3 srgbToLinear(vec3 c) {
  return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}

// v turned by the unit quaternion q.
vec3 rotateByQuat(vec4 q, vec3 v) {
  vec3 t = 2.0 * cross(q.xyz, v);
  return v + q.w * t + cross(q.xyz, t);
}

// A point in local space as Vulkan clip coordinates for this eye: the
// OpenGL-style projection of xmath.h with y flipped (row 0 is the top) and
// depth mapped to 0..1.
vec4 eyeClip(EyeData e, vec3 world) {
  vec3 v = rotateByQuat(vec4(-e.orientation.xyz, e.orientation.w), world - e.position.xyz);
  float l = e.tangents.x, r = e.tangents.y, d = e.tangents.z, u = e.tangents.w;
  float x = (2.0 * v.x + (r + l) * v.z) / (r - l);
  float y = (2.0 * v.y + (u + d) * v.z) / (u - d);
  float zGl = -(kFarZ + kNearZ) / (kFarZ - kNearZ) * v.z - 2.0 * kFarZ * kNearZ / (kFarZ - kNearZ);
  float w = -v.z;
  return vec4(x, -y, 0.5 * (zGl + w), w);
}
