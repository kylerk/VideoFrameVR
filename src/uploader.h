#pragma once

#include "gpu_context.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

class CairoCanvas;

// The staging memory canvases reach the GPU through: a persistently mapped
// ring with one region per frame slot, reused once the GPU has finished that
// slot's last frame, and for an exceptional frame that needs more, an
// overflow buffer retired with it (docs/vulkan-renderer.md, 2.7 and 3.6).
// It also keeps the canvases whose operations wait for the next frame.
// All calls on the main thread.
class Uploader {
 public:
  static constexpr VkDeviceSize kRegionBytes = VkDeviceSize(48) << 20;

  // Where record() lets a canvas write a copy's bytes.
  struct Space {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceSize offset = 0;
    uint8_t* data = nullptr;
  };

  Uploader() = default;
  Uploader(const Uploader&) = delete;
  Uploader& operator=(const Uploader&) = delete;
  // Retires the staging memory through GpuContext.
  ~Uploader();

  bool init(GpuContext& gpu);
  GpuContext& gpu() const { return *gpu_; }

  // A canvas has operations for the next frame (CairoCanvas); it forgets
  // itself when destroyed.
  void queue(CairoCanvas* canvas);
  void forget(CairoCanvas* canvas);

  // Records every queued canvas's operations into the command buffer of
  // frame slot `slot` (before its eye passes), in the order they came.
  void record(VkCommandBuffer cmd, int slot);
  // During record(): staging space for `bytes`, aligned for a buffer-to-image
  // copy. Always succeeds unless memory runs out (false, logged).
  bool reserve(VkDeviceSize bytes, Space& out);

 private:
  GpuContext* gpu_ = nullptr;
  VkDeviceSize alignment_ = 16;
  GpuBuffer ring_;  // kFramesInFlight regions of kRegionBytes
  VkDeviceSize regionStart_ = 0, used_ = 0;
  GpuBuffer overflow_;  // this frame's, if the region is not enough
  VkDeviceSize overflowUsed_ = 0;
  std::vector<CairoCanvas*> queued_;
  VkDeviceSize largestOverflow_ = 0;  // a frame's uploads, logged when it grows
};
