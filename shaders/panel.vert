#version 450
#extension GL_GOOGLE_include_directive : require

// A textured panel: the unit quad (4 vertices) or, curved, 65 columns of two
// (a triangle strip).

#include "panel.glsl"

layout(location = 0) out vec2 vUV;

void main() {
  bool curved = (pc.draw.flags & kCurved) != 0u;
  vec2 aPos = curved ? vec2(-0.5 + float(gl_VertexIndex >> 1) / 64.0,
                            (gl_VertexIndex & 1) != 0 ? 0.5 : -0.5)
                     : vec2((gl_VertexIndex & 1) != 0 ? 0.5 : -0.5,
                            (gl_VertexIndex & 2) != 0 ? 0.5 : -0.5);
  vec2 local = vec2(aPos.x + 0.5, 0.5 - aPos.y);  // texture rows are stored top first
  vec4 uv = pc.draw.uv;
  vUV = vec2(mix(uv.x, uv.y, local.x), mix(uv.z, uv.w, local.y));
  vec3 p = vec3(aPos, 0.0);
  if (curved) {
    vec4 arc = pc.draw.arc;
    float radius = pc.draw.shape.x;
    float angle = mix(arc.x, arc.y, local.x);
    p = vec3(radius * sin(angle), mix(arc.z, arc.w, local.y), -radius * cos(angle));
  }
  vec4 q = vec4(p, 1.0);
  gl_Position = eyeClip(currentEye(), vec3(dot(pc.draw.model0, q), dot(pc.draw.model1, q),
                                           dot(pc.draw.model2, q)));
}
