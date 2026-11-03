#pragma once

#include <cstdint>
#include <string>

// A file's fingerprint as Stash computes it (pkg/hash/oshash), so the
// library and a Stash database can be matched: the file size plus the sum
// of the little-endian 64-bit words of its first and last 64 KiB, as 16
// lower case hex digits. Reads 128 KiB whatever the size, so slow on a
// network share (tens of ms): never on the render thread.
struct FileFingerprint {
  std::string oshash;  // empty if the file could not be read (or is 8 bytes or less)
  uint64_t size = 0;   // bytes
  int64_t mtime = 0;   // seconds since 1970
};
FileFingerprint fingerprintFile(const std::string& path);
// Read elsewhere (Stash's stream): the bytes read from each end of a file
// of `size` bytes (0 for 8 bytes or less), and the oshash of `ends`, those
// of its start then those of its end.
int64_t oshashChunk(uint64_t size);
std::string oshashOfEnds(uint64_t size, const unsigned char* ends);
