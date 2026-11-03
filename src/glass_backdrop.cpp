#include "glass_backdrop.h"

#include "pipelines.h"
#include "scene_renderer.h"

#include <algorithm>

namespace {

// shaders/blur.frag, BlurDraw.
struct BlurPush {
  float step[4];
  uint32_t mode[4];  // x: dekey
};
static_assert(sizeof(BlurPush) == 32 && offsetof(BlurPush, mode) == 16,
              "BlurPush must match shaders/blur.frag");
static_assert(kDrawPushOffset + sizeof(BlurPush) <= kPushConstantBytes, "push constants");

// A quarter of the eye's size each way: the backdrop is blurred anyway.
VkExtent2D quarter(const VkExtent2D& extent) {
  return {std::max(1u, (extent.width + 3) / 4), std::max(1u, (extent.height + 3) / 4)};
}

}  // namespace

GlassBackdrop::~GlassBackdrop() {
  if (!gpu_) return;
  for (Eye& eye : eyes_) retireEye(eye);
  VkDevice d = gpu_->device();
  VkRenderPass scene = scenePass_, blur = blurPass_;
  gpu_->retire([d, scene, blur] {
    if (scene) vkDestroyRenderPass(d, scene, nullptr);
    if (blur) vkDestroyRenderPass(d, blur, nullptr);
  });
}

bool GlassBackdrop::init(GpuContext& gpu, int eyes, const VkRect2D* eyeAreas) {
  gpu_ = &gpu;
  for (int pass = 0; pass < 2; ++pass) {
    VkAttachmentDescription2 color{VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2};
    color.format = kFormat;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    // The scene is cleared; a blur writes every pixel.
    color.loadOp = pass == 0 ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkAttachmentReference2 ref{VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2};
    ref.attachment = 0;
    ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    ref.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    VkSubpassDescription2 subpass{VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &ref;
    // In: earlier reads of the image (the blur before, or the last frame's
    // panels) are done before it is written. Out: the writes are seen by the
    // next pass's (or the eye pass's) sampling.
    VkSubpassDependency2 deps[2] = {{VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2},
                                    {VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2}};
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass = 0;
    deps[0].srcStageMask =
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[0].srcAccessMask = 0;
    deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
    deps[1].srcSubpass = 0;
    deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    VkRenderPassCreateInfo2 info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2};
    info.attachmentCount = 1;
    info.pAttachments = &color;
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = 2;
    info.pDependencies = deps;
    if (!gpu.check(vkCreateRenderPass2(gpu.device(), &info, nullptr,
                                       pass == 0 ? &scenePass_ : &blurPass_),
                   "glass render pass"))
      return false;
  }
  for (int i = 0; i < eyes && i < 2; ++i)
    if (!createEye(eyes_[i], quarter(eyeAreas[i].extent))) return false;
  return true;
}

bool GlassBackdrop::createEye(Eye& eye, VkExtent2D size) {
  eye.size = size;
  Texture* textures[2] = {&eye.result, &eye.across};
  for (int i = 0; i < 2; ++i) {
    GpuImage& image = eye.images[i];
    if (!gpu_->createImage(size.width, size.height, 1, kFormat,
                           VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, image,
                           "glass backdrop"))
      return false;
    VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fb.renderPass = scenePass_;
    fb.attachmentCount = 1;
    fb.pAttachments = &image.view;
    fb.width = size.width;
    fb.height = size.height;
    fb.layers = 1;
    if (!gpu_->check(vkCreateFramebuffer(gpu_->device(), &fb, nullptr, &eye.framebuffers[i]),
                     "glass framebuffer"))
      return false;
    textures[i]->image = image.image;
    textures[i]->view = image.view;
    textures[i]->width = size.width;
    textures[i]->height = size.height;
  }
  return true;
}

void GlassBackdrop::retireEye(Eye& eye) {
  VkDevice d = gpu_->device();
  VkFramebuffer framebuffers[2] = {eye.framebuffers[0], eye.framebuffers[1]};
  gpu_->retire([d, framebuffers] {
    for (VkFramebuffer f : framebuffers)
      if (f) vkDestroyFramebuffer(d, f, nullptr);
  });
  for (GpuImage& image : eye.images) gpu_->retire(image);
  eye = Eye{};
}

void GlassBackdrop::beginPass(VkCommandBuffer cmd, VkRenderPass pass, VkFramebuffer framebuffer,
                              VkExtent2D size, const VkClearColorValue* clear) {
  VkClearValue value{};
  if (clear) value.color = *clear;
  VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
  begin.renderPass = pass;
  begin.framebuffer = framebuffer;
  begin.renderArea = {{0, 0}, size};
  begin.clearValueCount = clear ? 1 : 0;
  begin.pClearValues = clear ? &value : nullptr;
  VkSubpassBeginInfo subpass{VK_STRUCTURE_TYPE_SUBPASS_BEGIN_INFO};
  subpass.contents = VK_SUBPASS_CONTENTS_INLINE;
  vkCmdBeginRenderPass2(cmd, &begin, &subpass);
  const VkViewport viewport{0, 0, static_cast<float>(size.width), static_cast<float>(size.height),
                            0, 1};
  vkCmdSetViewport(cmd, 0, 1, &viewport);
  vkCmdSetScissor(cmd, 0, 1, &begin.renderArea);
}

bool GlassBackdrop::prepare(int index, const VkRect2D& area) {
  Eye& eye = eyes_[index];
  // The eye's render area changed size: made anew (a rare event, not per frame).
  const VkExtent2D size = quarter(area.extent);
  if (size.width != eye.size.width || size.height != eye.size.height || !eye.framebuffers[1]) {
    retireEye(eye);
    if (!createEye(eye, size)) {
      retireEye(eye);
      return false;
    }
  }
  return true;
}

VkRect2D GlassBackdrop::beginScene(VkCommandBuffer cmd, int index, const VkClearColorValue& clear) {
  const Eye& eye = eyes_[index];
  beginPass(cmd, scenePass_, eye.framebuffers[0], eye.size, &clear);
  return {{0, 0}, eye.size};
}

void GlassBackdrop::endScene(VkCommandBuffer cmd) {
  VkSubpassEndInfo end{VK_STRUCTURE_TYPE_SUBPASS_END_INFO};
  vkCmdEndRenderPass2(cmd, &end);
}

void GlassBackdrop::blur(VkCommandBuffer cmd, int index, const Pipelines& pipelines, bool dekey) {
  const Eye& eye = eyes_[index];
  const EyeData none{};
  for (int pass = 0; pass < 2; ++pass) {
    // Across from the scene into the second image, then down back into the first.
    BlurPush p{};
    if (pass == 0) p.step[0] = 1.0f / eye.size.width;
    else p.step[1] = 1.0f / eye.size.height;
    p.mode[0] = pass == 0 && dekey ? 1 : 0;
    beginPass(cmd, blurPass_, eye.framebuffers[pass == 0 ? 1 : 0], eye.size, nullptr);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.blur());
    pipelines.bindTextures(cmd, pass == 0 ? &eye.result : &eye.across, Pipelines::kGlass);
    const VkShaderStageFlags stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    vkCmdPushConstants(cmd, pipelines.layout(), stages, 0, sizeof(none), &none);
    vkCmdPushConstants(cmd, pipelines.layout(), stages, kDrawPushOffset, sizeof(p), &p);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    endScene(cmd);
  }
}
