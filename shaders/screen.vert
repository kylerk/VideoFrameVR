#version 450
#extension GL_GOOGLE_include_directive : require

// The flat or curved screen: 65 columns of two vertices (a triangle strip),
// bent around an upright axis.

#include "screen.glsl"

layout(location = 0) out vec2 vUV;

void main() {
  vec2 aPos = vec2(-0.5 + float(gl_VertexIndex >> 1) / 64.0, (gl_VertexIndex & 1) != 0 ? 0.5 : -0.5);
  vUV = vec2(aPos.x + 0.5, 0.5 - aPos.y);  // mpv stores frames top row first
  if ((pc.draw.mode.y & kFlip) != 0u) vUV.y = 1.0 - vUV.y;
  vec4 size = pc.draw.size;
  vec3 p;
  if (size.w > 0.0) {
    // Width is the arc length around an upright axis size.w behind the centre.
    float angle = aPos.x * size.x / size.w;
    p = vec3(size.w * sin(angle), aPos.y * size.y, size.w - size.z - size.w * cos(angle));
  } else {
    p = vec3(aPos.x * size.x, aPos.y * size.y, -size.z);
  }
  vec4 q = vec4(p, 1.0);
  gl_Position = eyeClip(currentEye(), vec3(dot(pc.draw.pose0, q), dot(pc.draw.pose1, q),
                                           dot(pc.draw.pose2, q)));
}
