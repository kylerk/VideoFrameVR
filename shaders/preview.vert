#version 450
#extension GL_GOOGLE_include_directive : require

// The seek preview's frame: the unit quad (4 vertices, a triangle strip).

#include "preview.glsl"

layout(location = 0) out vec2 vPos;  // across the quad, 0..1, row 0 at the top

void main() {
  vec2 aPos = vec2((gl_VertexIndex & 1) != 0 ? 0.5 : -0.5, (gl_VertexIndex & 2) != 0 ? 0.5 : -0.5);
  vPos = vec2(aPos.x + 0.5, 0.5 - aPos.y);
  vec4 q = vec4(aPos, 0.0, 1.0);
  gl_Position = eyeClip(currentEye(), vec3(dot(pc.draw.model0, q), dot(pc.draw.model1, q),
                                           dot(pc.draw.model2, q)));
}
