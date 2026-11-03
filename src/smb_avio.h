#pragma once

#include "smb.h"

#include <mpv/stream_cb.h>

extern "C" {
#include <libavformat/avio.h>
}

struct AVFormatContext;

// Background SMB input for FFmpeg, or a Stash scene's stream
// (stash_stream.h). Keep it alive until after close_input().
class SmbAvio {
 public:
  ~SmbAvio();
  // Local paths need no custom IO and return true too.
  bool attach(AVFormatContext* format, const std::string& path);

 private:
  static int read(void* opaque, uint8_t* out, int count);
  static int64_t seek(void* opaque, int64_t offset, int whence);
  int64_t size() const;
  std::unique_ptr<smb::File> file_;
  mpv_stream_cb_info stash_{};  // a scene's, with its cookie
  AVIOContext* io_ = nullptr;
  AVIOInterruptCB interrupt_{};
  int64_t position_ = 0;
};
