#version 450
#extension GL_GOOGLE_include_directive : require

#include "pointer.glsl"

layout(location = 0) in vec2 vLocal;
layout(location = 0) out vec4 fragColor;

void main() {
  vec4 c = pc.draw.color;
  if (pc.draw.mode.x != 0u) {
    float r = length(vLocal);
    c *= 1.0 - smoothstep(1.0 - fwidth(r), 1.0, r);
  }
  fragColor = c;
}
