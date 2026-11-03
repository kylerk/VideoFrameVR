#version 450
#extension GL_GOOGLE_include_directive : require

// A quad given as four corners in local space, drawn as a triangle strip:
// the pointer's beam and dot, a screen corner's handle.

#include "pointer.glsl"

layout(location = 0) out vec2 vLocal;  // -1..1 across the quad

void main() {
  vLocal = vec2((gl_VertexIndex & 1) == 1 ? 1.0 : -1.0, gl_VertexIndex >= 2 ? 1.0 : -1.0);
  gl_Position = eyeClip(currentEye(), pc.draw.corners[gl_VertexIndex].xyz);
}
