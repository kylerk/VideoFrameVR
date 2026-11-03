// The pointer draw's block, shared by pointer.vert and pointer.frag.

#include "common.glsl"

struct PointerDraw {
  vec4 corners[4];  // xyz, local space
  vec4 color;       // premultiplied, linear
  uvec4 mode;       // x: a round dot filling the quad, else the whole quad
};
DECLARE_PUSH(PointerDraw)
