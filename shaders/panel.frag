#version 450
#extension GL_GOOGLE_include_directive : require

#include "panel.glsl"

layout(set = 0, binding = 0) uniform sampler2D tex;  // sRGB: sampling returns linear colour
layout(set = 0, binding = 1) uniform sampler2D backdrop;  // the scene behind, blurred (glass)

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 fragColor;

// Signed distance to a rounded rectangle (negative inside).
float roundedRectDistance(vec2 p, vec4 rect, float radius) {
  vec2 center = 0.5 * (rect.xy + rect.zw);
  vec2 q = abs(p - center) - 0.5 * (rect.zw - rect.xy) + radius;
  return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
}

void main() {
  uint flags = pc.draw.flags;
  bool glass = (flags & kGlass) != 0u;
  float pixelScale = pc.draw.shape.y;
  vec4 c = texture(tex, vUV, pc.draw.lodBias);  // premultiplied alpha
  // The button's icon or text: opaque light strokes in the panel texture
  // (glass buttons themselves are translucent, Material ones dark).
  float lum = dot(c.rgb / max(c.a, 1e-4), vec3(0.2126, 0.7152, 0.0722));
  float ink = glass ? smoothstep(0.6, 0.9, c.a) * smoothstep(0.5, 0.8, lum)
                    : smoothstep(0.15, 0.35, lum);
  if (glass) {
    // Frosted glass: behind the panel's translucent parts, the scene blurred.
    EyeData e = currentEye();
    float inside = clamp(c.a / pc.draw.shape.w, 0.0, 1.0);
    vec2 screenUV = (gl_FragCoord.xy - e.area.xy) / e.area.zw;
    vec3 back = texture(backdrop, screenUV).rgb;
    if ((flags & kPassthrough) != 0u) {
      // Seen through the glass, key green would turn the panel green and the
      // headset would cut it out, showing the room right through the menu.
      // The blur already made the room smoked glass; this makes sure nothing
      // behind the glass is left green enough to key.
      vec3 s = linearToSrgb(clamp(back, 0.0, 1.0));
      s.g = min(s.g, max(s.r, s.b) + 0.02);
      back = srgbToLinear(s);
    }
    c = vec4(c.rgb + (1.0 - c.a) * back * inside, max(c.a, inside));
  }
  if ((flags & kHover) != 0u) {
    vec2 p = vUV * vec2(textureSize(tex, 0)) / pixelScale;
    float d = roundedRectDistance(p, pc.draw.hover, pc.draw.shape.z);
    // A row partly scrolled out of its list shows its highlight only where
    // the row shows, open at the cut edge.
    vec4 clip = pc.draw.hoverClip;
    float outside = max(max(clip.x - p.x, p.x - clip.z), max(clip.y - p.y, p.y - clip.w));
    float inside = (1.0 - smoothstep(-1.0, 0.0, d)) * (1.0 - smoothstep(-1.0, 0.0, outside));
    float rim = (1.0 - smoothstep(1.0, 2.5, abs(d + 2.0))) * inside;  // ring inside the edge
    if ((flags & kHoverDanger) != 0u) {
      // Red glass (or a red Material button); the icon turns white and stays readable.
      vec3 red = glass ? vec3(0.42, 0.02, 0.02) : vec3(0.60, 0.05, 0.04);
      c.rgb = mix(c.rgb, red * c.a, inside * (1.0 - ink) * (glass ? 0.75 : 0.95));
      c.rgb = mix(c.rgb, vec3(1.0) * c.a, inside * ink);
      if (glass) c.rgb = mix(c.rgb, vec3(1.0, 0.62, 0.58) * c.a, rim * 0.5);
    } else {
      // A little lighter, with a rim in the menu's accent.
      c.rgb += vec3(0.04) * c.a * inside * (1.0 - ink);
      vec3 accent = srgbToLinear(vec3((uvec3(flags) >> uvec3(8u, 16u, 24u)) & 255u) / 255.0);
      c.rgb = mix(c.rgb, accent * c.a, rim);
    }
  }
  fragColor = c * pc.draw.opacity;
}
