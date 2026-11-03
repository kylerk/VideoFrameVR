#pragma once

#include "gl_interop.h"

#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

// Shows photos: FFmpeg decodes them in a background thread, then they are
// uploaded into a mipmapped sRGB image shared with Vulkan a few megabytes
// per frame, so the headset's frames never stall (docs/vulkan-renderer.md,
// 3.5). GL uploads them, so automatic focus reads them as it reads video.
// The next photo is decoded ahead, so switching to it (by hand or in a
// slideshow) shows it at once.
class PhotoPreloader {
 public:
  PhotoPreloader() = default;
  PhotoPreloader(const PhotoPreloader&) = delete;
  PhotoPreloader& operator=(const PhotoPreloader&) = delete;
  // With the GL context current: what is left goes to `interop`'s retirement.
  ~PhotoPreloader();

  void start(GlInterop& interop);
  // The photo on screen (decoded first) and the one to have ready next;
  // replaces earlier requests still waiting.
  void request(const std::string& current, const std::string& next);
  // GL thread, every frame inside GL's access window (gl_interop.h): uploads
  // a decoded photo a part at a time. A photo still uploading stays with GL.
  void update();
  // The photo `path` once it is all uploaded and handed to Vulkan, or
  // nullptr. It stays valid until the next update(). GL reads it after
  // GlInterop::acquireForGl().
  SharedImage* image(const std::string& path, int& width, int& height);

 private:
  struct Decoded {
    std::string path;
    int width = 0, height = 0;
    std::vector<uint32_t> pixels;  // RGBA, until uploaded
  };
  struct Slot : Decoded {
    std::unique_ptr<SharedImage> image;  // full mip chain
    int uploadedRows = 0;
    bool ready = false;
  };
  struct Worker;
  static void work(const std::shared_ptr<Worker>& worker, int maxSize);

  GlInterop* interop_ = nullptr;
  Slot slots_[2];  // the current photo and the next one
  int shown_ = -1;
  std::string current_, next_;

  std::thread worker_;
  std::shared_ptr<Worker> state_;
};
