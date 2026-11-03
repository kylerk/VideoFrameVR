#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require

// A Q10C frame's plane copied texel by texel into a plain 16-bit one
// (DirectVideo::still), as the eye passes sample it: the luma or the
// chroma, from its TP10 descriptors (TP10, video.glsl).

#include "video.glsl"

uint drawFlags() { return 0u; }

layout(set = 0, binding = 1) uniform sampler2D chromaTex;

// DirectVideo's StillPush.
layout(push_constant) uniform Push {
  vec2 size;  // the plane's, in texels
  int chroma;
} u;

layout(location = 0) out vec4 fragColor;

void main() {
  vec2 p = gl_FragCoord.xy / u.size;
  fragColor = u.chroma != 0 ? vec4(textureLod(chromaTex, p, 0.0).rg, 0.0, 0.0)
                            : vec4(tp10Luma(chromaTex, p));
}
