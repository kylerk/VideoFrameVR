#include "read_ahead.h"

#include <algorithm>
#include <cstring>

void ReadAhead::seek(uint64_t offset) {
  position = offset;
  auto covers = [&](const Window& w) {
    if (offset >= w.at && offset <= w.next + kBlock) return true;
    return std::any_of(blocks.begin(), blocks.end(), [&](const Block& b) {
      return b.window == w.id && b.generation == generation && offset >= b.offset &&
             offset < b.offset + b.bytes.size();
    });
  };
  auto it = std::find_if(windows.begin(), windows.end(), covers);
  Window w = it != windows.end() ? *it : Window{++windowIds, offset, offset};
  if (it != windows.end()) windows.erase(it);
  else if (windows.size() == kWindows) windows.erase(windows.begin());
  w.at = std::min(w.at, offset);
  if (offset > w.next) w.at = w.next = offset;  // skipped past what it read ahead
  windows.push_back(w);
}

void ReadAhead::retire() {
  blocks.remove_if([&](auto& b) {
    if (b.pending) return false;
    const auto w = std::find_if(windows.begin(), windows.end(),
                                [&](auto& x) { return x.id == b.window; });
    return b.generation != generation || w == windows.end() ||
           b.offset + b.bytes.size() + kBlock <= w->at;
  });
}

void ReadAhead::queue(uint64_t chunk, uint64_t length) {
  for (auto w = windows.rbegin(); w != windows.rend(); ++w) {
    auto ahead = std::count_if(blocks.begin(), blocks.end(), [&](auto& b) {
      return b.window == w->id && b.generation == generation && b.offset + b.bytes.size() > w->at;
    });
    for (; ahead < 2 && w->next < length; ++ahead) {
      const size_t n = std::min(chunk, length - w->next);
      blocks.push_back({this, generation, w->id, w->next, std::vector<uint8_t>(n)});
      w->next += n;
    }
  }
}

size_t ReadAhead::take(void* out, uint64_t count) {
  for (auto& b : blocks) if (b.generation == generation && b.done &&
      position >= b.offset && position < b.offset + b.filled) {
    const size_t at = position - b.offset, n = std::min<uint64_t>(count, b.filled - at);
    memcpy(out, b.bytes.data() + at, n);
    position += n;
    if (!windows.empty()) windows.back().at = position;
    return n;
  }
  return 0;
}
