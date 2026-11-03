#include "uploader.h"

#include "cairo_canvas.h"

#include <algorithm>
#include <cstdio>

Uploader::~Uploader() {
  if (!gpu_) return;
  gpu_->retire(ring_);
  if (overflow_.buffer) gpu_->retire(overflow_);
}

bool Uploader::init(GpuContext& gpu) {
  gpu_ = &gpu;
  alignment_ = std::max<VkDeviceSize>(16, gpu.caps().bufferCopyOffsetAlignment);
  // Canvases are B8G8R8A8_SRGB, copied, scrolled and mipmapped by linear blits.
  VkFormatProperties props;
  vkGetPhysicalDeviceFormatProperties(gpu.physicalDevice(), VK_FORMAT_B8G8R8A8_SRGB, &props);
  const VkFormatFeatureFlags need =
      VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT |
      VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT |
      VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
  if ((props.optimalTilingFeatures & need) != need) {
    std::fprintf(stderr, "Vulkan: B8G8R8A8_SRGB images can't be filtered and blitted here\n");
    return false;
  }
  return gpu.createBuffer(kRegionBytes * GpuContext::kFramesInFlight,
                          VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true, ring_, "staging ring");
}

void Uploader::queue(CairoCanvas* canvas) {
  if (std::find(queued_.begin(), queued_.end(), canvas) == queued_.end())
    queued_.push_back(canvas);
}

void Uploader::forget(CairoCanvas* canvas) {
  queued_.erase(std::remove(queued_.begin(), queued_.end(), canvas), queued_.end());
}

void Uploader::record(VkCommandBuffer cmd, int slot) {
  // The slot's last frame is finished (GpuContext::beginFrame): its region is free.
  regionStart_ = kRegionBytes * static_cast<VkDeviceSize>(slot);
  used_ = 0;
  overflowUsed_ = 0;
  if (queued_.empty()) return;

  // A frame needing more than its region gets an overflow buffer, retired
  // with it: an exceptional content change, not a frame cost.
  VkDeviceSize total = 0;
  for (const CairoCanvas* canvas : queued_) total += canvas->pendingBytes(alignment_);
  // (Sized for all of it: what doesn't fit the rest of the region goes there.)
  if (total > kRegionBytes) {
    if (gpu_->createBuffer(total, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true, overflow_,
                           "staging overflow") &&
        total > largestOverflow_) {
      largestOverflow_ = total;
      std::printf("Vulkan: canvas uploads of %.1f MiB in one frame, more than the %.0f MiB "
                  "staging region\n",
                  total / 1048576.0, kRegionBytes / 1048576.0);
    }
  }

  std::vector<CairoCanvas*> canvases;
  canvases.swap(queued_);
  for (CairoCanvas* canvas : canvases) canvas->record(cmd, *this);

  gpu_->flush(ring_, regionStart_, used_);
  if (overflow_.buffer) {
    gpu_->flush(overflow_, 0, overflowUsed_);
    gpu_->retire(overflow_);
    overflow_ = GpuBuffer{};
  }
}

bool Uploader::reserve(VkDeviceSize bytes, Space& out) {
  const VkDeviceSize start = (used_ + alignment_ - 1) / alignment_ * alignment_;
  if (start + bytes <= kRegionBytes) {
    used_ = start + bytes;
    out.buffer = ring_.buffer;
    out.offset = regionStart_ + start;
    out.data = static_cast<uint8_t*>(ring_.mapped) + out.offset;
    return true;
  }
  const VkDeviceSize at = (overflowUsed_ + alignment_ - 1) / alignment_ * alignment_;
  if (!overflow_.buffer || at + bytes > overflow_.size) {
    std::fprintf(stderr, "Vulkan: no staging space for a canvas upload of %.1f MiB\n",
                 bytes / 1048576.0);
    return false;
  }
  overflowUsed_ = at + bytes;
  out.buffer = overflow_.buffer;
  out.offset = at;
  out.data = static_cast<uint8_t*>(overflow_.mapped) + at;
  return true;
}
