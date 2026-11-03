#pragma once

#include <libavutil/hwcontext_drm.h>
#include "video/out/opengl/common.h"

struct frame_ubwc;
struct mp_image;
// gl NULL: export mode (frame_ubwc_export), no GL involved.
struct frame_ubwc *frame_ubwc_create(GL *gl, struct mp_log *log, int w, int h,
                                      uint32_t drm_format);
const AVDRMFrameDescriptor *frame_ubwc_convert(struct frame_ubwc *p,
                                             const AVDRMFrameDescriptor *src,
                                             struct mp_image *source);
void frame_ubwc_release(struct frame_ubwc *p);
// Export mode: converts into a buffer held for the caller until it unholds
// id (unique across converter replacements, supplied by the caller);
// *fence_fd (the caller's, -1 if already done) signals the conversion.
const AVDRMFrameDescriptor *frame_ubwc_export(struct frame_ubwc *p,
                                            const AVDRMFrameDescriptor *src,
                                            struct mp_image *source,
                                            int *fence_fd, uint64_t id);
void frame_ubwc_unhold(struct frame_ubwc *p, uint64_t id);
void frame_ubwc_destroy(struct frame_ubwc *p);
// Any thread, after the decoder closed a V4L2 session (vd_lavc.c): drops
// every reference to its buffers kept by any instance, and tells the app
// watched, if any, to let go of its imports (video_export.h, session).
void frame_ubwc_decoder_closed(void);
// With no decoder submitting new pictures, poll outstanding GPU reads and
// drop completed inputs. Busy locks or unfinished reads return false;
// neither is waited for, and unfinished inputs stay referenced.
bool frame_ubwc_decoder_idle(void);
struct video_export;
// The app's struct, once it was handed decoder buffers; NULL when it stops.
void frame_ubwc_watch(struct video_export *e);
