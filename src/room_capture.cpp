#include "room_capture.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

// SteamVR's headset view (a loopback device its steamvr-v4l2cam service feeds).
const char* const kMirrorDevice = "/dev/video99";
// Enough to measure: the less of the picture is copied, the better.
constexpr int kPictureWidth = 256;
// How long what the app draws takes to leave the headset view.
constexpr int kBlankSettleMs = 350;
constexpr int kGrabTimeoutMs = 3000;

// Whether a frame shows the room as it is now. A reader first gets the last
// frame written, however old, and the device can hand out the same frame again
// to keep its frame rate (v4l2loopback): counting frames says nothing, only the
// sequence number moving on does. settled: what the app drew has left the view.
struct FreshFrames {
  bool seen = false;
  uint32_t last = 0;

  bool accept(uint32_t sequence, bool settled) {
    const bool first = !seen;
    const bool moved = static_cast<int32_t>(sequence - last) > 0;
    // A device that numbers nothing: only the wait is left to go by.
    const bool unnumbered = sequence == 0 && last == 0;
    seen = true;
    last = sequence;
    return settled && (first ? unnumbered : moved || unnumbered);
  }
};

double msSince(Clock::time_point start) {
  return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

int xioctl(int fd, unsigned long request, void* arg) {
  int r;
  do r = ioctl(fd, request, arg);
  while (r == -1 && errno == EINTR);
  return r;
}

std::string fourcc(uint32_t f) {
  const char s[5] = {static_cast<char>(f), static_cast<char>(f >> 8), static_cast<char>(f >> 16),
                     static_cast<char>(f >> 24), 0};
  return s;
}

// A frame of packed RGB as a picture about kPictureWidth wide, over what image
// held. False: a format not handled, or too few bytes.
bool convertFrame(const v4l2_pix_format& fmt, const uint8_t* data, size_t size, RoomImage& image) {
  const int w = static_cast<int>(fmt.width), h = static_cast<int>(fmt.height);
  if (w <= 0 || h <= 0) return false;
  int bytes = 0, r = 0, g = 1, b = 2;
  switch (fmt.pixelformat) {
    case V4L2_PIX_FMT_RGB24: bytes = 3; break;
    case V4L2_PIX_FMT_BGR24: bytes = 3; r = 2; b = 0; break;
    case V4L2_PIX_FMT_RGBA32:
    case V4L2_PIX_FMT_RGBX32: bytes = 4; break;
    case V4L2_PIX_FMT_ABGR32:
    case V4L2_PIX_FMT_XBGR32: bytes = 4; r = 2; b = 0; break;
    case V4L2_PIX_FMT_ARGB32:
    case V4L2_PIX_FMT_XRGB32: bytes = 4; r = 1; g = 2; b = 3; break;
    case V4L2_PIX_FMT_BGRA32:
    case V4L2_PIX_FMT_BGRX32: bytes = 4; r = 3; g = 2; b = 1; break;
    default: return false;
  }
  const size_t stride = fmt.bytesperline ? fmt.bytesperline : static_cast<size_t>(w) * bytes;
  if (size < stride * h) return false;
  const int step = std::max(1, w / kPictureWidth);
  image.width = w / step;
  image.height = h / step;
  image.rgb.resize(static_cast<size_t>(image.width) * image.height * 3);
  uint8_t* out = image.rgb.data();
  for (int y = 0; y < image.height; ++y) {
    const uint8_t* row = data + static_cast<size_t>(y * step) * stride;
    for (int x = 0; x < image.width; ++x, out += 3) {
      const uint8_t* p = row + static_cast<size_t>(x * step) * bytes;
      out[0] = p[r];
      out[1] = p[g];
      out[2] = p[b];
    }
  }
  return true;
}

bool isBlack(const RoomImage& image) {
  size_t shown = 0;
  for (size_t i = 0; i + 2 < image.rgb.size(); i += 3)
    if (std::max({image.rgb[i], image.rgb[i + 1], image.rgb[i + 2]}) > 3) ++shown;
  return shown * 20 < image.rgb.size() / 3;
}

// A frame from the headset view, made after the app stopped drawing into it:
// the first with anything in it, else the last in time. Reads from the start,
// as the view may only be fed while it is read. Stops early on quit. Never
// changes the device's format. detail: what went wrong, or what it read.
bool grabFrame(const std::atomic<bool>& quit, RoomImage& image, std::string& detail) {
  const int fd = ::open(kMirrorDevice, O_RDWR | O_NONBLOCK | O_CLOEXEC);
  if (fd < 0) {
    detail = std::string(kMirrorDevice) + ": " + strerror(errno);
    return false;
  }
  struct Mapped {
    void* start;
    size_t size;
  };
  std::vector<Mapped> buffers;
  bool streaming = false, got = false;
  auto finish = [&](bool result) {
    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (streaming) xioctl(fd, VIDIOC_STREAMOFF, &type);
    for (const Mapped& m : buffers) munmap(m.start, m.size);
    if (!buffers.empty()) {
      v4l2_requestbuffers none{};
      none.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      none.memory = V4L2_MEMORY_MMAP;
      xioctl(fd, VIDIOC_REQBUFS, &none);
    }
    ::close(fd);
    return result;
  };
  auto failed = [&](const char* what) {
    detail = std::string(kMirrorDevice) + ": " + what + ": " + strerror(errno);
    return finish(false);
  };
  v4l2_format format{};
  format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (xioctl(fd, VIDIOC_G_FMT, &format) < 0) return failed("no format");
  const v4l2_pix_format& fmt = format.fmt.pix;
  char summary[48];
  std::snprintf(summary, sizeof(summary), "%ux%u %s", fmt.width, fmt.height, fourcc(fmt.pixelformat).c_str());
  v4l2_requestbuffers request{};
  request.count = 2;
  request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  request.memory = V4L2_MEMORY_MMAP;
  if (xioctl(fd, VIDIOC_REQBUFS, &request) < 0 || request.count == 0) return failed("no buffers");
  for (uint32_t i = 0; i < request.count; ++i) {
    v4l2_buffer b{};
    b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    b.memory = V4L2_MEMORY_MMAP;
    b.index = i;
    if (xioctl(fd, VIDIOC_QUERYBUF, &b) < 0) return failed("no buffer");
    void* start = mmap(nullptr, b.length, PROT_READ, MAP_SHARED, fd, b.m.offset);
    if (start == MAP_FAILED) return failed("can't map a buffer");
    buffers.push_back({start, b.length});
    if (xioctl(fd, VIDIOC_QBUF, &b) < 0) return failed("can't queue a buffer");
  }
  v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (xioctl(fd, VIDIOC_STREAMON, &type) < 0) return failed("can't start");
  streaming = true;
  const auto start = Clock::now();
  FreshFrames fresh;
  int frames = 0;
  uint32_t firstSequence = 0;
  bool shows = false;
  while (!shows && !quit && msSince(start) < kGrabTimeoutMs) {
    pollfd p{fd, POLLIN, 0};
    const int ready = ::poll(&p, 1, 100);
    if (ready < 0 && errno != EINTR) return failed("can't wait for a frame");
    if (ready <= 0) continue;
    v4l2_buffer b{};
    b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    b.memory = V4L2_MEMORY_MMAP;
    if (xioctl(fd, VIDIOC_DQBUF, &b) < 0) {
      if (errno == EAGAIN) continue;
      return failed("can't read a frame");
    }
    if (++frames == 1) firstSequence = b.sequence;
    if (fresh.accept(b.sequence, msSince(start) >= kBlankSettleMs) && b.index < buffers.size()) {
      const size_t used = b.bytesused ? b.bytesused : buffers[b.index].size;
      if (!convertFrame(fmt, static_cast<const uint8_t*>(buffers[b.index].start), used, image)) {
        detail = std::string(kMirrorDevice) + ": format not handled: " + summary;
        return finish(false);
      }
      got = true;
      shows = !isBlack(image);
    }
    xioctl(fd, VIDIOC_QBUF, &b);
  }
  char counts[96];
  std::snprintf(counts, sizeof(counts), ", %d frames, sequence %u to %u", frames, firstSequence, fresh.last);
  detail = got ? std::string(summary) + counts : std::string(kMirrorDevice) + ": no new frames in time" + counts;
  return finish(got);
}

}  // namespace

RoomCapture::~RoomCapture() {
  quit_ = true;
  if (worker_.joinable()) worker_.join();
}

void RoomCapture::start() {
  if (running_) return;
  if (worker_.joinable()) worker_.join();
  done_ = false;
  running_ = true;
  // At once, so the frames the worker waits for already leave the app out.
  blank_ = true;
  worker_ = std::thread([this] { work(); });
}

bool RoomCapture::take(RoomEstimate& room, Failure& failure, std::string& detail) {
  if (!done_) return false;
  done_ = false;
  room = room_;
  failure = failure_;
  detail = detail_;
  return true;
}

void RoomCapture::work() {
  const auto started = Clock::now();
  RoomImage image;
  std::string detail;
  const bool grabbed = grabFrame(quit_, image, detail);
  blank_ = false;
  room_ = grabbed ? estimateRoom(image) : RoomEstimate{};
  // The picture is done with: only the measurement stays.
  if (!image.rgb.empty()) explicit_bzero(image.rgb.data(), image.rgb.size());
  image = RoomImage{};
  failure_ = !grabbed ? Failure::Device : !room_.ok ? Failure::Black : Failure::None;
  detail_ = detail;
  std::printf("Room match: %.0f ms, %s: %s, brightness %.1f%%, tint %.2f %.2f %.2f\n", msSince(started),
              detail.c_str(), failure_ == Failure::None ? "read" : "failed", room_.luminance * 100.0f,
              room_.tint[0], room_.tint[1], room_.tint[2]);
  // In this order: whoever sees it done also sees it no longer busy.
  running_ = false;
  done_ = true;
}
