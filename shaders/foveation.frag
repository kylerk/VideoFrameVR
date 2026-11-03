#version 450
#extension GL_EXT_fragment_invocation_density : require

layout(push_constant) uniform Push {
  layout(offset = 112) vec4 gaze;  // pixels xy
} pc;
layout(location = 0) out vec4 outColor;

void main() {
  // One colour per rate: full green, up to 2×2 yellow, coarser untinted.
  const int size = max(gl_FragSizeEXT.x, gl_FragSizeEXT.y);
  vec4 tint = size == 1 ? vec4(0.0, 1.0, 0.0, 0.28) : size == 2 ? vec4(1.0, 0.85, 0.0, 0.22) : vec4(0.0);
  vec2 p = gl_FragCoord.xy;
  vec2 d = abs(p - pc.gaze.xy);
  if (min(d.x, d.y) < 2.0 && max(d.x, d.y) < 16.0) {
    tint = vec4(1.0);
  }
  outColor = vec4(tint.rgb * tint.a, tint.a);
}
