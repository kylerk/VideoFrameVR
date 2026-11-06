#version 450
#extension GL_GOOGLE_include_directive : require
#ifdef TP10
#extension GL_EXT_nonuniform_qualifier : require
#endif
#ifdef MASK_FIRST
#extension GL_EXT_control_flow_attributes : require
// The neighbour loops kept as loops: smaller code, and cheaper on the Frame.
#define NEIGHBOURS [[dont_unroll]] for
#else
#define NEIGHBOURS for
#endif

// The video around the viewer (180°/360°, fisheye, equi-angular cubemap) or
// the room-fixed sky, computed per pixel from the view direction.

#include "video.glsl"
#include "projection.glsl"

struct SurroundDraw {
  // xyz: columns of the rotation from local space into the picture's axes;
  // w: the first three of neutralRange()
  vec4 toPicture0;
  vec4 toPicture1;
  vec4 toPicture2;
  vec4 params;  // x fisheye field of view (radians), y zoom (> 1 magnifies); zw Kr, Kb (kYuv)
  vec4 adjust;  // brightness, contrast, saturation (-1..1), w sharpening strength (0 = off)
  // Green screen: x = how green is cut away, y = half its soft edge, z =
  // spill removal 0..2, w = shrink: how many texels the cut reaches past
  // what looks green.
  vec4 chroma;
  vec4 alpha;   // x unused; y the last of neutralRange();
                // zw the chroma lookup's offset (kYuv)
  uvec4 mode;   // x projection: 0 = 180, 1 = 360, 2 = fisheye, 3 = equi-angular
                // cubemap, 4 = cubemap turned to a 2x3 grid; y flags (video.glsl);
                // z footprint taps per side at most (footprintSample)
  vec4 yuv;     // kYuv: the planes' range (videoTap)
};
DECLARE_PUSH(SurroundDraw)
uint drawFlags() { return pc.draw.mode.y; }

#ifdef TP10
#define tex tp10Bands[0]  // a Q10C frame's luma (video.glsl), always kYuv
#else
layout(set = 0, binding = 0) uniform sampler2D tex;
#endif
layout(set = 0, binding = 1) uniform sampler2D chromaTex;  // kYuv

#ifdef TP10
bool isYuv() { return true; }
#else
bool isYuv() { return (pc.draw.mode.y & kYuv) != 0u; }
#endif
vec4 yuvMatrix() { return vec4(pc.draw.params.zw, pc.draw.alpha.zw); }
// A decoded frame's range without brightness, contrast and saturation
// (YuvColor::neutralRange): the push constants are full, so it sits in their
// unused parts.
vec4 neutralRange() {
  return vec4(pc.draw.toPicture0.w, pc.draw.toPicture1.w, pc.draw.toPicture2.w, pc.draw.alpha.y);
}
// The frame as it came at p (texture coordinates), full-size level, linear
// colour: for the mask and the key, which the picture settings must not move.
vec3 tap(vec2 p) { return videoTap(tex, chromaTex, p, isYuv(), neutralRange(), yuvMatrix()); }

layout(location = 0) in vec2 vNdc;
layout(location = 0) out vec4 fragColor;

// The picture at uv (0..1 in this eye's part, kept within lo..hi) as this
// pixel (at fragCoord) shows it, sampled with the gradients dx, dy: through
// the mipmaps, or without them over the pixel's footprint.
vec3 picture(vec2 uv, vec2 lo, vec2 hi, vec2 dx, vec2 dy, EyeData e, uint flags, vec2 fragCoord) {
  vec3 c;
  if (baseLevel(flags)) {
    c = footprintSample(tex, chromaTex, isYuv(), pc.draw.yuv, yuvMatrix(), uv, dx, dy, e.stereo, lo,
                        hi, int(pc.draw.mode.z));
  } else {
    vec2 eyeUv = e.stereo.zw + clamp(uv, lo, hi) * e.stereo.xy;
    c = textureGrad(tex, eyeUv, dx * e.stereo.xy, dy * e.stereo.xy).rgb;
  }
  return clamp(applyLook(adjustPicture(c, pc.draw.adjust.xyz), e, flags, fragCoord), 0.0, 1.0);
}

// DeoVR's packed alpha at uv: each eye's mask, at 40% of its part of the
// frame, sits centred on the middle of the top edge (left eye) or on the
// corners (right eye) and wraps around the frame's edges (into the corners and
// gaps next to the fisheye circles). Its red channel is opacity.
float packedAlpha(vec2 uv, EyeData e, vec2 dx, vec2 dy) {
  vec2 alphaScale = 0.4 * e.stereo.xy;
  vec2 alphaCenter = vec2(e.position.w < 0.5 ? 0.5 : 0.0, 0.0);
  vec2 m = uv - 0.5;
  if (length(m) > 0.5) return 0.0;
  vec2 maskUv = fract(alphaCenter + m * alphaScale);
  vec3 mask = isYuv() ? tap(maskUv) : textureGrad(tex, maskUv, dx * alphaScale, dy * alphaScale).rgb;
  float red = linearToSrgb(mask).r;
  // Compression leaves black a little above 0 and full red below 1.
  return smoothstep(0.08, 0.92, red);
}

// How much of the frame stays at p (texture coordinates) under the green
// screen key, measured on the frame as it came: dx, dy as sampled there.
float keyAlpha(vec2 p, vec2 dx, vec2 dy, vec4 chroma) {
  vec3 t = clamp(isYuv() ? tap(p) : textureGrad(tex, p, dx, dy).rgb, 0.0, 1.0);
  return chromaAlpha(linearToSrgb(t), chroma);
}

void main() {
  EyeData e = currentEye();
  uint flags = pc.draw.mode.y;
  int projection = int(pc.draw.mode.x);
  float zoom = pc.draw.params.y;
  vec2 eyeScale = e.stereo.xy, eyeOffset = e.stereo.zw;

  // The view ray through this pixel; the top row (t.y = 0) looks along the
  // upper edge of the field of view.
  vec2 t = vNdc * 0.5 + 0.5;
  vec3 ray = vec3(mix(e.tangents.x, e.tangents.y, t.x) / zoom,
                  mix(e.tangents.w, e.tangents.z, t.y) / zoom, -1.0);
  mat3 toPicture = mat3(pc.draw.toPicture0.xyz, pc.draw.toPicture1.xyz, pc.draw.toPicture2.xyz);
  vec3 dir = normalize(toPicture * rotateByQuat(e.orientation, ray));
  // Stereo correction (stereo_correction.h): intersect a sphere of the focus
  // distance's radius around this eye's source camera. Both the ray origin
  // and the resulting lookup direction are relative to that camera.
  if ((flags & kCorrect) != 0u && e.viewer.w > 0.0) {
    vec3 viewer = e.viewer.xyz - e.camera.xyz;
    float along = dot(viewer, dir);
    float reach = along * along - dot(viewer, viewer) + e.viewer.w * e.viewer.w;
    dir = normalize(viewer + dir * (sqrt(max(reach, 0.0)) - along));
  }
  bool inside;
  vec2 uv = pictureUv(dir, projection, pc.draw.params.x, inside);
  if ((flags & kFlip) != 0u) uv.y = 1.0 - uv.y;

  // Mip selection from derivatives; for 360 pick the version without the
  // jump at the back seam so no line appears there.
  vec2 dx = dFdx(uv), dy = dFdy(uv);
  vec2 shifted = vec2(fract(uv.x + 0.5), uv.y);
  vec2 dxs = dFdx(shifted), dys = dFdy(shifted);
  bool acrossSeam = abs(dxs.x) + abs(dys.x) < abs(dx.x) + abs(dy.x);
  if (acrossSeam) {
    dx = dxs;
    dy = dys;
  }
  // Sharpening's neighbours: one eye pixel away along the same gradients,
  // per pixel rather than per 2x2 block.
  vec2 fineX = dFdxFine(uv), fineY = dFdyFine(uv);
  vec2 fineXs = dFdxFine(shifted), fineYs = dFdyFine(shifted);
  vec2 stepX = acrossSeam ? fineXs : fineX, stepY = acrossSeam ? fineYs : fineY;
  // Cube face edges jump across the frame: through mipmaps the full-size
  // level avoids seams there and roughly matches the headset's resolution
  // for 8K video; without them, taps stay on the face.
  bool cube = projection == 3 || projection == 4;
  if (cube && (flags & kBaseLevel) == 0u) {
    dx = vec2(0.0);
    dy = vec2(0.0);
  }

  // Bilinear alone reaches half a texel out: keep it inside this eye's part,
  // and a cubemap's face.
  vec2 margin = vec2(0.0005);
  if (baseLevel(flags))
    margin = max(margin, 0.5 / (videoSize(tex, chromaTex) * eyeScale));
  vec2 eyeUv = eyeOffset + clamp(uv, margin, 1.0 - margin) * eyeScale;
  vec2 lo = margin, hi = 1.0 - margin;
  if (cube) {
    vec2 grid = projection == 3 ? vec2(3.0, 2.0) : vec2(2.0, 3.0);
    vec2 cell = min(floor(uv * grid), grid - 1.0);
    vec2 texel = 0.5 / (videoSize(tex, chromaTex) * eyeScale);
    lo = cell / grid + texel;
    hi = (cell + 1.0) / grid - texel;
  }
#ifdef MASK_FIRST
  // Passthrough with a packed mask: the mask first, as it doesn't depend on
  // the picture. Where it cuts everything away the output is fixed, and the
  // picture (most of the cost: its taps and conversion) is never sampled.
  // Only in the pipeline for that case (Pipelines::kSurroundMask): in the
  // one for everything else this code, though never run, made every video
  // draw dearer on the Frame (docs/vulkan-renderer.md, 7).
  bool maskFirst = (flags & kPackedAlpha) != 0u && (flags & (kKeyGreen | kAlphaOut)) != 0u;
  float maskAlpha = maskFirst && inside ? packedAlpha(uv, e, dx, dy) : 0.0;
  if (maskFirst && maskAlpha == 0.0) {
    fragColor = (flags & kAlphaOut) != 0u ? vec4(0.0) : vec4(kKeyColor, 1.0);
    return;
  }
#endif
  vec3 c = inside ? picture(uv, lo, hi, dx, dy, e, flags, gl_FragCoord.xy) : vec3(0.0);

  // Sharpening: the picture one eye pixel to each side, as those pixels
  // would sample it. Taps stay on this eye's part, a cubemap's face, or wrap
  // around 360 video.
  // Only in the pipelines for it (SHARPEN): in the others this code, though
  // never run, made every video draw about three times dearer on the Frame
  // (docs/vulkan-renderer.md, 7).
#ifdef SHARPEN
  float strength = pc.draw.adjust.w;
#else
  const float strength = 0.0;
#endif
  bool sharpened = strength > 0.0 && inside;
  vec2 nUv[4];
  vec3 n[4];
  if (sharpened) {
    vec2 steps[4] = vec2[](-stepX, stepX, -stepY, stepY);
    vec2 pixels[4] = vec2[](vec2(-1.0, 0.0), vec2(1.0, 0.0), vec2(0.0, -1.0), vec2(0.0, 1.0));
    NEIGHBOURS (int i = 0; i < 4; ++i) {
      vec2 p = uv + steps[i];
      if (projection == 1) p.x = fract(p.x);
      p = clamp(p, lo, hi);
      nUv[i] = p;
      n[i] = picture(p, lo, hi, dx, dy, e, flags, gl_FragCoord.xy + pixels[i]);
    }
  }

  bool keyGreen = (flags & kKeyGreen) != 0u, alphaOut = (flags & kAlphaOut) != 0u;
  if (!keyGreen && !alphaOut) {
    fragColor = vec4(sharpened ? sharpen(c, n[0], n[1], n[2], n[3], strength) : c, 1.0);
    return;
  }
  // Passthrough: how much of the picture stays here.
  float alpha = inside ? 1.0 : 0.0;
  vec4 chroma = pc.draw.chroma;
  bool packed = (flags & kPackedAlpha) != 0u && inside;
  bool keyed = !packed && (flags & kChromaKey) != 0u && inside;
  vec2 eyeDx = dx * eyeScale, eyeDy = dy * eyeScale;
  if (packed) {
#ifdef MASK_FIRST
    alpha = maskAlpha;
#else
    alpha = packedAlpha(uv, e, dx, dy);
#endif
  } else if (keyed) {
    alpha = keyAlpha(eyeUv, eyeDx, eyeDy, chroma);
    if (chroma.w > 0.0 && alpha > 0.0) {
      // Shrink: cut where any neighbour that far away is cut.
      vec2 texel = chroma.w / videoSize(tex, chromaTex);
      NEIGHBOURS (int i = 0; i < 4; ++i) {
        vec2 o = texel * vec2(i == 0 ? 1.0 : i == 1 ? -1.0 : 0.0, i == 2 ? 1.0 : i == 3 ? -1.0 : 0.0);
        alpha = min(alpha, keyAlpha(eyeUv + o, eyeDx, eyeDy, chroma));
      }
    }
  }
  // Near what is cut away nothing is sharpened, so the cut gets no fringes;
  // the coverage itself never is.
  if (sharpened && (packed || keyed)) {
    bool edge = alpha < 1.0;
    NEIGHBOURS (int i = 0; i < 4; ++i)
      edge = edge || (packed ? packedAlpha(nUv[i], e, dx, dy)
                             : keyAlpha(eyeOffset + nUv[i] * eyeScale, eyeDx, eyeDy, chroma)) < 1.0;
    sharpened = !edge;
  }
  if (sharpened) c = sharpen(c, n[0], n[1], n[2], n[3], strength);
  if (packed) {
    if (keyGreen) {
      // The picture itself must never look like the key colour, or the
      // headset would cut holes into it (a green top, say): the key channel
      // is capped just above the larger other one, in display values, below
      // what the headset's keyer reacts to.
      vec3 s = linearToSrgb(c);
      vec3 rest = s * (vec3(1.0) - kKeyColor);
      float keyed = dot(s, kKeyColor);
      s += kKeyColor * (min(keyed, max(max(rest.r, rest.g), rest.b) + 0.02) - keyed);
      c = srgbToLinear(s);
    }
  } else if (keyed) {
    c = srgbToLinear(removeSpill(linearToSrgb(c), chroma.z));
  }
  fragColor = alphaOut ? vec4(c * alpha, alpha) : vec4(mix(kKeyColor, c, alpha), 1.0);
}
