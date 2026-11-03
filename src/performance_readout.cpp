#include "performance_readout.h"

#include "strings.h"
#include "vr_space.h"

#include <cairo.h>
#include <pango/pangocairo.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

constexpr int kWidth = 560, kHeight = 200;  // canvas units (= pixels)
constexpr int kPad = 14;
constexpr int kLineY[5] = {kPad, kPad + 38, kPad + 76, kPad + 118, kPad + 148};
constexpr int kLineSize[5] = {28, 28, 28, 22, 22};
// In front of the eyes: 2 m away (as the loading text), 17° left of and 16°
// below straight ahead, 0.44 m wide (about 12.5°), facing the eyes.
constexpr float kDistance = 2.0f, kLeft = 0.2967f, kDown = 0.2793f, kPanelW = 0.44f;

}  // namespace

bool PerformanceReadout::init(Uploader& uploader) { return canvas_.init(uploader, kWidth, kHeight); }

void PerformanceReadout::restart(double now) {
  frames_ = 0;
  gpuSum_ = 0;
  gpuFrames_ = 0;
  stepDisplay_ = lastDisplay_ = 0;
  stepCount_ = stepAt_ = 0;
  lastDrops_ = -1;
  wasPlaying_ = false;
  dropsSince_ = 0;
  lastAt_ = now;
  nextAt_ = now;  // the first update at once, with nothing measured yet
}

void PerformanceReadout::update(double now, const State& s) {
  const Strings& t = strings();
  const VideoPlayer::Counts& c = *s.counts;
  Step step;
  step.frames = frames_;
  step.displaySpan = stepDisplay_ > 0 ? lastDisplay_ - stepDisplay_ : 0;
  step.gpuSum = gpuSum_;
  step.gpuFrames = gpuFrames_;
  frames_ = 0;
  gpuSum_ = 0;
  gpuFrames_ = 0;
  stepDisplay_ = lastDisplay_;

  // mpv's drops: the output's and the decoder's, as far as known.
  long long drops = -1;
  if (c.voDrops >= 0 || c.decoderDrops >= 0)
    drops = std::max<int64_t>(c.voDrops, 0) + std::max<int64_t>(c.decoderDrops, 0);
  if (c.restarts != lastRestarts_) {  // a new file or a seek: what was before does not count
    lastRestarts_ = c.restarts;
    sinceSeek_ = c.seek;
    dropsSince_ = 0;
    for (Step& old : steps_) old.videoValid = false;
    wasPlaying_ = false;
  }
  const bool playing = s.video && !s.paused && !s.waiting;
  // Only steps played through from start to end count: one that began paused
  // or seeking would show a rate too low.
  if (playing && wasPlaying_) {
    step.videoValid = true;
    step.videoFrames = c.newFrames - lastNewFrames_;
    step.videoSpan = now - lastAt_;
    if (drops >= 0 && lastDrops_ >= 0 && drops >= lastDrops_) step.drops = drops - lastDrops_;
    dropsSince_ += step.drops;
  }
  wasPlaying_ = playing;
  lastNewFrames_ = c.newFrames;
  lastDrops_ = drops;
  lastAt_ = now;
  nextAt_ = now + kStep;
  steps_[stepAt_] = step;
  stepAt_ = (stepAt_ + 1) % kDropSteps;
  stepCount_ = std::min(stepCount_ + 1, kDropSteps);

  // Sums over the latest steps.
  int frames = 0, gpuFrames = 0, videoSteps = 0;
  double displaySpan = 0, gpuSum = 0, videoSpan = 0;
  uint64_t videoFrames = 0;
  long long recentDrops = 0;
  for (int i = 0; i < stepCount_; ++i) {
    const Step& p = steps_[(stepAt_ - 1 - i + kDropSteps) % kDropSteps];
    if (i < kRateSteps) {
      frames += p.frames;
      displaySpan += p.displaySpan;
      gpuSum += p.gpuSum;
      gpuFrames += p.gpuFrames;
      if (p.videoValid) {
        ++videoSteps;
        videoFrames += p.videoFrames;
        videoSpan += p.videoSpan;
      }
    }
    if (p.videoValid) recentDrops += p.drops;
  }

  char lines[5][96] = {};
  Status status[5] = {};
  // The headset.
  const double fps = displaySpan > 0 ? frames / displaySpan : 0;
  std::snprintf(lines[0], sizeof(lines[0]), t.perfHeadset, fps, s.displayRate);
  if (s.displayRate > 0 && displaySpan > 0)
    status[0] = fps >= 0.97 * s.displayRate ? Good : fps >= 0.85 * s.displayRate ? Warn : Bad;
  // The video and its drops.
  if (!s.video) {
    std::snprintf(lines[1], sizeof(lines[1]), "%s", t.perfNoVideo);
  } else if (s.waiting) {
    std::snprintf(lines[1], sizeof(lines[1]), "%s", t.perfWaiting);
  } else if (s.paused) {
    std::snprintf(lines[1], sizeof(lines[1]), "%s", t.perfPaused);
  } else if (videoSteps == 0) {
    std::snprintf(lines[1], sizeof(lines[1]), "%s", t.perfWaiting);
  } else {
    const double videoFps = videoFrames / videoSpan;
    if (s.sourceFps > 0)
      std::snprintf(lines[1], sizeof(lines[1]), t.perfVideo, videoFps, s.sourceFps);
    else
      std::snprintf(lines[1], sizeof(lines[1]), t.perfVideoUnknown, videoFps);
    const double ratio = s.sourceFps > 0 ? videoFps / s.sourceFps : 1.0;
    status[1] = recentDrops == 0 && ratio >= 0.985              ? Good
                : recentDrops <= kDropSteps / 2 && ratio >= 0.9 ? Warn
                                                                : Bad;
  }
  if (s.video) {
    std::snprintf(lines[2], sizeof(lines[2]), t.perfDropped, recentDrops,
                  static_cast<int>(kDropSteps * kStep), dropsSince_,
                  sinceSeek_ ? t.perfSinceSeek : t.perfSinceOpen);
    status[2] = recentDrops == 0 ? (dropsSince_ == 0 ? Good : Neutral)
                : recentDrops <= kDropSteps / 2 ? Warn : Bad;
  }
  // What it costs: eye size, foveation, GPU time.
  int n = std::snprintf(lines[3], sizeof(lines[3]), t.perfSize, s.eyeWidth);
  if (s.foveation >= 0 && s.foveation < 5) {
    n += std::snprintf(lines[3] + n, sizeof(lines[3]) - n, " · ");
    n += std::snprintf(lines[3] + n, sizeof(lines[3]) - n, "%s", t.foveationLevels[s.foveation]);
  }
  if (gpuFrames > 0) {
    n += std::snprintf(lines[3] + n, sizeof(lines[3]) - n, " · ");
    n += std::snprintf(lines[3] + n, sizeof(lines[3]) - n, t.perfGpu, gpuSum / gpuFrames);
  }
  if (s.sharpWidth > 0)
    std::snprintf(lines[3] + n, sizeof(lines[3]) - n, t.perfSharp, s.sharpWidth, s.sharpHeight);
  // How the video is decoded and drawn: "HEVC 10-bit · v4l2m2m · direct".
  if (s.video && s.codec && !s.codec->empty()) {
    char codec[24] = {};
    for (size_t i = 0; i < s.codec->size() && i + 1 < sizeof(codec); ++i)
      codec[i] = static_cast<char>(std::toupper(static_cast<unsigned char>((*s.codec)[i])));
    const bool tenBit = (s.pixelFormat && s.pixelFormat->find("10") != std::string::npos) ||
                        c.hwPixelFormat.find("10") != std::string::npos;
    const std::string& decoder = c.decoder;
    const char* decoderText = decoder == "no" || decoder.empty() ? t.perfSoftware : decoder.c_str();
    std::snprintf(lines[4], sizeof(lines[4]), "%s%s · %s · %s", codec, tenBit ? " 10-bit" : "",
                  decoderText, s.direct ? t.perfDirect : t.perfMpv);
  }

  for (int i = 0; i < 5; ++i) {
    if (status[i] != status_[i] || std::strcmp(lines[i], lines_[i]) != 0) dirty_ = true;
    status_[i] = status[i];
    std::memcpy(lines_[i], lines[i], sizeof(lines[i]));
  }
}

void PerformanceReadout::draw() {
  cairo_t* cr = canvas_.cr();
  canvas_.clear();
  // A dark box under the text keeps it readable on bright and dark video.
  const double r = 14, w = kWidth, h = kHeight;
  cairo_new_path(cr);
  cairo_arc(cr, w - r, r, r, -M_PI / 2, 0);
  cairo_arc(cr, w - r, h - r, r, 0, M_PI / 2);
  cairo_arc(cr, r, h - r, r, M_PI / 2, M_PI);
  cairo_arc(cr, r, r, r, M_PI, 3 * M_PI / 2);
  cairo_close_path(cr);
  cairo_set_source_rgba(cr, 0.05, 0.05, 0.06, 0.72);
  cairo_fill(cr);

  // The worst of the headset and the video, as a bar down the left edge.
  Status worst = Neutral;
  for (const Status s : status_) worst = std::max(worst, s);
  auto colour = [cr](Status s, double alpha) {
    switch (s) {
      case Good: cairo_set_source_rgba(cr, 0.42, 0.90, 0.48, alpha); break;
      case Warn: cairo_set_source_rgba(cr, 1.00, 0.76, 0.22, alpha); break;
      case Bad: cairo_set_source_rgba(cr, 1.00, 0.40, 0.36, alpha); break;
      case Neutral: cairo_set_source_rgba(cr, 0.92, 0.92, 0.92, alpha); break;
    }
  };
  if (worst != Neutral) {
    colour(worst, 1.0);
    cairo_rectangle(cr, 10, 16, 6, h - 32);
    cairo_fill(cr);
  }

  PangoLayout* layout = pango_cairo_create_layout(cr);
  PangoAttrList* attrs = pango_attr_list_new();
  pango_attr_list_insert(attrs, pango_attr_font_features_new("tnum"));  // steady digits
  pango_layout_set_attributes(layout, attrs);
  PangoFontDescription* font = pango_font_description_new();
  pango_font_description_set_family(font, "Sans");
  for (int i = 0; i < 5; ++i) {
    if (!lines_[i][0]) continue;
    pango_font_description_set_weight(font, i < 3 ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL);
    pango_font_description_set_absolute_size(font, kLineSize[i] * PANGO_SCALE);
    pango_layout_set_font_description(layout, font);
    pango_layout_set_text(layout, lines_[i], -1);
    pango_layout_set_width(layout, (kWidth - 30 - kPad) * PANGO_SCALE);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
    if (i < 3)
      colour(status_[i], 1.0);
    else
      cairo_set_source_rgba(cr, 0.80, 0.80, 0.82, 1.0);
    cairo_move_to(cr, 30, kLineY[i]);
    pango_cairo_show_layout(cr, layout);
  }
  pango_font_description_free(font);
  pango_attr_list_unref(attrs);
  g_object_unref(layout);
  canvas_.upload();
}

PanelItem PerformanceReadout::item(Vec3 headPos, Quat headRot) {
  if (dirty_) {
    dirty_ = false;
    draw();
  }
  // Head-locked: where it is in the view never changes; turned to face the eyes.
  const Vec3 at = Vec3{-kLeft, -kDown, -1.0f} * (kDistance / std::sqrt(1 + kLeft * kLeft + kDown * kDown));
  Mat4 facing;
  if (!facingBasis(normalize(at) * -1.0f, {0.0f, 1.0f, 0.0f}, facing)) facing = Mat4();
  PanelItem item;
  item.texture = canvas_.texture();
  item.model = translation(headPos) * rotation(headRot) * translation(at) * facing *
               scaling(kPanelW, kPanelW * kHeight / kWidth, 1);
  return item;
}
