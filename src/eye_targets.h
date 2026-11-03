#pragma once

#include "gpu_context.h"

#include <vulkan/vulkan.h>

#include <vector>

class XrContext;
struct EyeImage;

// The eye render passes (render pass 2: the swapchain image cleared and
// stored, one subpass, no depth) and a framebuffer for every eye swapchain
// image (at the eye size in use). Foveated eye images have two: with their density map, and without
// one for foveation Off, so that full rate costs no more than unfoveated.
// Each eye is drawn in one pass, begun and ended here
// (docs/vulkan-renderer.md, 2.4).
class EyeTargets {
 public:
  EyeTargets() = default;
  EyeTargets(const EyeTargets&) = delete;
  EyeTargets& operator=(const EyeTargets&) = delete;
  // Retires the render passes and framebuffers through GpuContext.
  ~EyeTargets();

  // Once the eye images exist (XrContext::createEyeImages).
  bool init(GpuContext& gpu, const XrContext& xr);
  // Framebuffers anew after a change of the eye images, their size in use
  // or their density maps (XrContext::eyeTargetsVersion); the old ones are
  // retired. Before beginEye().
  bool update();
  // The pass of images drawn with a density map (EyeImage::densityMap), and
  // the one without; the same one where the eye images have no maps.
  VkRenderPass renderPass() const { return renderPass_; }
  VkRenderPass plainRenderPass() const { return plainPass_ ? plainPass_ : renderPass_; }

  // Begins eye `eye`'s pass on its acquired image, cleared to `clear`, with
  // viewport and scissor on the image's render area; with the image's
  // density map if it has one.
  void beginEye(VkCommandBuffer cmd, int eye, const EyeImage& image, const VkClearColorValue& clear);
  // Ends it, with the image's density map offset if it has a map.
  void endEye(VkCommandBuffer cmd, const EyeImage& image);
  bool foveated() const { return foveated_; }

 private:
  VkRenderPass createPass(GpuContext& gpu, VkFormat format, bool density);
  bool makeFramebuffers();
  void retireFramebuffers();

  GpuContext* gpu_ = nullptr;
  const XrContext* xr_ = nullptr;
  uint64_t version_ = 0;  // of the eye targets the framebuffers were made for
  bool foveated_ = false;
  VkRenderPass renderPass_ = VK_NULL_HANDLE;
  VkRenderPass plainPass_ = VK_NULL_HANDLE;  // foveated images without their map
  std::vector<VkFramebuffer> framebuffers_[2];       // per eye, by swapchain image
  std::vector<VkFramebuffer> plainFramebuffers_[2];  // ... for plainPass_
};
