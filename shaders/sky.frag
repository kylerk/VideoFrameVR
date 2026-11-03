#version 450

// The background behind the flat screen: a calm night sky with a faint
// aurora, rendered once at startup into an equirectangular texture (row 0 =
// the zenith).

layout(location = 0) in vec2 vNdc;
layout(location = 0) out vec4 fragColor;

const float PI = 3.14159265359;

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float noise(vec2 p) {
  vec2 i = floor(p), f = fract(p);
  f = f * f * (3.0 - 2.0 * f);
  return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), f.x),
             mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), f.x), f.y);
}
float fbm(vec2 p) {
  float v = 0.0, a = 0.5;
  for (int i = 0; i < 5; ++i) {
    v += a * noise(p);
    p *= 2.03;
    a *= 0.5;
  }
  return v;
}
// The sky at a direction above the horizon: gradient, stars, aurora, and a
// dark mountain ridge along the horizon.
vec3 sky(float lon, float lat) {
  // Noise along the horizon on a circle, so there is no seam behind you.
  vec2 around = vec2(cos(lon), sin(lon));
  float ridge = 0.025 + 0.06 * fbm(around * 2.2 + 21.0) + 0.02 * fbm(around * 9.0 + 4.0);
  if (lat < ridge) {
    // Mountains: nearly black, a faint cold rim light along the crest.
    return mix(vec3(0.003, 0.004, 0.007), vec3(0.012, 0.022, 0.030),
               smoothstep(ridge - 0.02, ridge, lat));
  }
  vec3 color = mix(vec3(0.010, 0.018, 0.034), vec3(0.002, 0.004, 0.010), smoothstep(0.0, 1.1, lat));
  // Stars.
  vec2 grid = vec2(lon * cos(lat), lat) * 140.0;
  vec2 cell = floor(grid);
  float h = hash(cell);
  if (h > 0.986) {
    vec2 offset = vec2(hash(cell + 3.1), hash(cell + 7.7)) - 0.5;
    float d = length(fract(grid) - 0.5 - offset * 0.6);
    color += vec3(0.8, 0.85, 1.0) * smoothstep(0.14, 0.0, d) * (h - 0.986) / 0.014 * 0.5;
  }
  // Aurora: curtains along a wavy band, with fine vertical rays.
  float band = 0.40 + 0.08 * sin(lon * 2.0 + 0.7) + 0.08 * (fbm(around * 1.5 + 3.0) - 0.5);
  float above = lat - band;
  float curtain = exp(-max(-above, 0.0) * 16.0) * exp(-max(above, 0.0) * 3.0);
  float rays = 0.45 + 0.55 * pow(noise(around * 45.0), 2.0);
  float strength = smoothstep(0.38, 0.72, fbm(around * 1.2 + 11.0));
  float glow = curtain * rays * strength * (0.6 + 0.4 * fbm(around * 4.0 + 5.0));
  vec3 hue = mix(vec3(0.10, 0.85, 0.45), vec3(0.45, 0.20, 0.75), smoothstep(0.0, 0.35, above));
  color += hue * glow * 0.2 * smoothstep(0.0, 0.12, lat);
  return color;
}

void main() {
  // The top row (vNdc.y = -1) is the zenith.
  float lon = vNdc.x * PI;
  float lat = -vNdc.y * 0.5 * PI;
  vec3 color;
  if (lat >= 0.0) {
    color = sky(lon, lat);
  } else {
    // A frozen lake mirroring the sky faintly (a little rippled), turning
    // into dark snow towards your feet.
    float ripple = (noise(vec2(lon * 40.0, lat * 300.0)) - 0.5) * 0.01;
    vec3 mirror = sky(lon + ripple, -lat) * 0.3;
    vec3 snow = vec3(0.004, 0.006, 0.010);
    color = mix(mirror, snow, smoothstep(-0.05, -0.7, lat));
  }
  fragColor = vec4(color, 1.0);
}
