#pragma once

#include "gpu_context.h"
#include "texture.h"

#include <vulkan/vulkan.h>

class Pipelines;

// The blurred scene behind glass menus, on the desktop (docs/vulkan-renderer.md,
// 7). Before the eye passes, SceneRenderer draws each eye's scene up to the
// first glass panel into a float image at a quarter of the eye's width and
// height; blur() then blurs it across into a second image and down back into
// the first, which the eye's glass panels sample by their place in the eye
// image. Three small passes per eye, only while glass shows.
class GlassBackdrop {
 public:
  static constexpr VkFormat kFormat = VK_FORMAT_R16G16B16A16_SFLOAT;  // linear colour

  GlassBackdrop() = default;
  GlassBackdrop(const GlassBackdrop&) = delete;
  GlassBackdrop& operator=(const GlassBackdrop&) = delete;
  // Retires its images, framebuffers and render passes through GpuContext.
  ~GlassBackdrop();

  // eyeAreas: the eyes' render areas, which it is made for.
  bool init(GpuContext& gpu, int eyes, const VkRect2D* eyeAreas);
  // The scene pass, for the pipelines drawing into it (the blur's is compatible).
  VkRenderPass scenePass() const { return scenePass_; }

  // Made for eye `eye`'s render area `area` (a quarter of its size), before
  // its passes are recorded; false if its images cannot be had.
  bool prepare(int eye, const VkRect2D& area);
  // Begins the eye's scene pass, cleared to `clear`, with viewport and
  // scissor; the area drawn (at the origin).
  VkRect2D beginScene(VkCommandBuffer cmd, int eye, const VkClearColorValue& clear);
  void endScene(VkCommandBuffer cmd);
  // Blurs the eye's scene; dekey: key green (passthrough) becomes smoked
  // glass first.
  void blur(VkCommandBuffer cmd, int eye, const Pipelines& pipelines, bool dekey);
  // What the eye's glass panels show behind them (after blur()).
  const Texture* texture(int eye) const { return &eyes_[eye].result; }

 private:
  struct Eye {
    GpuImage images[2];  // the scene and the vertical blur; the horizontal blur
    VkFramebuffer framebuffers[2] = {};
    Texture result, across;
    VkExtent2D size{};
  };
  bool createEye(Eye& eye, VkExtent2D size);
  void retireEye(Eye& eye);
  void beginPass(VkCommandBuffer cmd, VkRenderPass pass, VkFramebuffer framebuffer,
                 VkExtent2D size, const VkClearColorValue* clear);

  GpuContext* gpu_ = nullptr;
  VkRenderPass scenePass_ = VK_NULL_HANDLE;  // cleared
  VkRenderPass blurPass_ = VK_NULL_HANDLE;   // overwritten
  Eye eyes_[2];
};
