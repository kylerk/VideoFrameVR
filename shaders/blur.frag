#version 450
#extension GL_GOOGLE_include_directive : require

// One pass of the glass backdrop's separable Gaussian (sigma 6 texels): each
// pixel averages 31 neighbours along step.

#include "common.glsl"

struct BlurDraw {
  vec4 step;   // xy: one texel along the blur, in texture coordinates
  uvec4 mode;  // x: 1 = key green (the room, in passthrough) becomes smoked glass first
};
DECLARE_PUSH(BlurDraw)

layout(set = 0, binding = 0) uniform sampler2D src;  // linear colour

layout(location = 0) in vec2 vNdc;
layout(location = 0) out vec4 fragColor;

// Passthrough: no key green bleeds into what is next to it, and none is left
// behind the glass for the headset to cut out.
vec3 dekey(vec3 c) {
  float key = clamp(c.g - max(c.r, c.b), 0.0, 1.0);  // key green's share (linear)
  c.g -= key;
  return c + key * vec3(0.010, 0.012, 0.016);
}

void main() {
  vec2 uv = vNdc * 0.5 + 0.5;
  bool dekeyed = pc.draw.mode.x != 0u;
  vec3 sum = vec3(0.0);
  float total = 0.0;
  for (int i = -15; i <= 15; ++i) {
    float weight = exp(-float(i * i) / 72.0);
    vec3 c = texture(src, uv + pc.draw.step.xy * float(i)).rgb;
    sum += (dekeyed ? dekey(c) : c) * weight;
    total += weight;
  }
  fragColor = vec4(sum / total, 1.0);
}
