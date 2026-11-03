#include "eye_targets.h"

#include "xr_context.h"

EyeTargets::~EyeTargets() {
  if (!gpu_) return;
  retireFramebuffers();
  VkDevice d = gpu_->device();
  const VkRenderPass passes[2] = {renderPass_, plainPass_};
  gpu_->retire([d, passes] {
    for (VkRenderPass pass : passes)
      if (pass) vkDestroyRenderPass(d, pass, nullptr);
  });
}

void EyeTargets::retireFramebuffers() {
  std::vector<VkFramebuffer> framebuffers;
  for (auto* set : {framebuffers_, plainFramebuffers_}) {
    for (int eye = 0; eye < 2; ++eye) {
      framebuffers.insert(framebuffers.end(), set[eye].begin(), set[eye].end());
      set[eye].clear();
    }
  }
  if (framebuffers.empty()) return;
  VkDevice d = gpu_->device();
  gpu_->retire([d, framebuffers] {
    for (VkFramebuffer f : framebuffers) vkDestroyFramebuffer(d, f, nullptr);
  });
}

VkRenderPass EyeTargets::createPass(GpuContext& gpu, VkFormat format, bool density) {
  VkAttachmentDescription2 attachments[2] = {{VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2},
                                             {VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2}};
  VkAttachmentDescription2& color = attachments[0];
  color.format = format;
  color.samples = VK_SAMPLE_COUNT_1_BIT;
  color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  // What OpenXR expects the image in when it is released.
  color.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  // Foveated: the density map, only read (by the GPU's density processing,
  // never by shaders).
  VkAttachmentDescription2& map = attachments[1];
  map.format = VK_FORMAT_R8G8_UNORM;
  map.samples = VK_SAMPLE_COUNT_1_BIT;
  map.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  map.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  map.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  map.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  map.initialLayout = VK_IMAGE_LAYOUT_FRAGMENT_DENSITY_MAP_OPTIMAL_EXT;
  map.finalLayout = VK_IMAGE_LAYOUT_FRAGMENT_DENSITY_MAP_OPTIMAL_EXT;

  VkAttachmentReference2 colorRef{VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2};
  colorRef.attachment = 0;
  colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  colorRef.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  VkSubpassDescription2 subpass{VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &colorRef;

  // In: the clear waits for the runtime's last use of the image (its
  // semaphore wait on the queue covers it at this stage). Out: the writes
  // are made available for the compositor's copy after release.
  VkSubpassDependency2 deps[2] = {{VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2},
                                  {VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2}};
  deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
  deps[0].dstSubpass = 0;
  deps[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  deps[0].srcAccessMask = 0;
  deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
  if (density) {
    deps[0].dstStageMask |= VK_PIPELINE_STAGE_FRAGMENT_DENSITY_PROCESS_BIT_EXT;
    deps[0].dstAccessMask |= VK_ACCESS_FRAGMENT_DENSITY_MAP_READ_BIT_EXT;
  }
  deps[1].srcSubpass = 0;
  deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
  deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  deps[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
  deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  deps[1].dstAccessMask = 0;

  VkRenderPassFragmentDensityMapCreateInfoEXT densityInfo{
      VK_STRUCTURE_TYPE_RENDER_PASS_FRAGMENT_DENSITY_MAP_CREATE_INFO_EXT};
  densityInfo.fragmentDensityMapAttachment = {1, VK_IMAGE_LAYOUT_FRAGMENT_DENSITY_MAP_OPTIMAL_EXT};
  VkRenderPassCreateInfo2 info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2};
  if (density) info.pNext = &densityInfo;
  info.attachmentCount = density ? 2 : 1;
  info.pAttachments = attachments;
  info.subpassCount = 1;
  info.pSubpasses = &subpass;
  info.dependencyCount = 2;
  info.pDependencies = deps;
  VkRenderPass pass = VK_NULL_HANDLE;
  gpu.check(vkCreateRenderPass2(gpu.device(), &info, nullptr, &pass), "eye render pass");
  return pass;
}

bool EyeTargets::init(GpuContext& gpu, const XrContext& xr) {
  gpu_ = &gpu;
  xr_ = &xr;
  foveated_ = xr.foveated();
  renderPass_ = createPass(gpu, xr.eyeFormat(), foveated_);
  if (!renderPass_) return false;
  if (foveated_) {
    plainPass_ = createPass(gpu, xr.eyeFormat(), false);
    if (!plainPass_) return false;
  }
  return makeFramebuffers();
}

// At the size in use, not the images' (a rectangle at their origin): Turnip
// bins a pass by its framebuffer's size, so the bins are those of an image of
// that size (docs/vulkan-renderer.md, 5.5).
bool EyeTargets::makeFramebuffers() {
  const XrContext& xr = *xr_;
  version_ = xr.eyeTargetsVersion();
  auto make = [&](VkRenderPass pass, bool density, std::vector<VkFramebuffer>* out) {
    for (int eye = 0; eye < xr.eyeCount() && eye < 2; ++eye) {
      const std::vector<VkImageView>& views = xr.eyeViews(eye);
      if (density && xr.densityMapViews(eye).size() < views.size()) continue;  // no maps (yet)
      for (size_t i = 0; i < views.size(); ++i) {
        const VkImageView both[2] = {views[i], density ? xr.densityMapViews(eye)[i] : VK_NULL_HANDLE};
        VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fb.renderPass = pass;
        fb.attachmentCount = density ? 2 : 1;
        fb.pAttachments = both;
        fb.width = static_cast<uint32_t>(xr.eyeWidth(eye));
        fb.height = static_cast<uint32_t>(xr.eyeHeight(eye));
        fb.layers = 1;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        if (!gpu_->check(vkCreateFramebuffer(gpu_->device(), &fb, nullptr, &framebuffer),
                         "eye framebuffer"))
          return false;
        out[eye].push_back(framebuffer);
      }
    }
    return true;
  };
  return make(renderPass_, foveated_, framebuffers_) &&
         (!plainPass_ || make(plainPass_, false, plainFramebuffers_));
}

bool EyeTargets::update() {
  if (version_ == xr_->eyeTargetsVersion()) return true;
  retireFramebuffers();
  return makeFramebuffers();
}

void EyeTargets::beginEye(VkCommandBuffer cmd, int eye, const EyeImage& image,
                          const VkClearColorValue& clear) {
  const bool plain = plainPass_ && !image.densityMap;
  VkClearValue value{};
  value.color = clear;
  VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
  begin.renderPass = plain ? plainPass_ : renderPass_;
  begin.framebuffer = (plain ? plainFramebuffers_ : framebuffers_)[eye][image.index];
  begin.renderArea = image.area;
  begin.clearValueCount = 1;
  begin.pClearValues = &value;
  VkSubpassBeginInfo subpass{VK_STRUCTURE_TYPE_SUBPASS_BEGIN_INFO};
  subpass.contents = VK_SUBPASS_CONTENTS_INLINE;
  vkCmdBeginRenderPass2(cmd, &begin, &subpass);

  VkViewport viewport{};
  viewport.x = static_cast<float>(image.area.offset.x);
  viewport.y = static_cast<float>(image.area.offset.y);
  viewport.width = static_cast<float>(image.area.extent.width);
  viewport.height = static_cast<float>(image.area.extent.height);
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;
  vkCmdSetViewport(cmd, 0, 1, &viewport);
  vkCmdSetScissor(cmd, 0, 1, &image.area);
}

void EyeTargets::endEye(VkCommandBuffer cmd, const EyeImage& image) {
  VkSubpassEndInfo end{VK_STRUCTURE_TYPE_SUBPASS_END_INFO};
  // The density map's offset applies to the whole pass, given at its end.
  VkSubpassFragmentDensityMapOffsetEndInfoQCOM offset{
      VK_STRUCTURE_TYPE_SUBPASS_FRAGMENT_DENSITY_MAP_OFFSET_END_INFO_QCOM};
  if (foveated_ && image.densityMap) {
    offset.fragmentDensityOffsetCount = 1;
    offset.pFragmentDensityOffsets = &image.densityOffset;
    end.pNext = &offset;
  }
  vkCmdEndRenderPass2(cmd, &end);
}
