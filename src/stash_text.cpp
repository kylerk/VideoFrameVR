#include "stash_text.h"

#include <glib.h>

#include <cctype>

namespace {

bool ascii(const std::string& text) {
  for (const unsigned char c : text)
    if (c >= 0x80) return false;
  return true;
}

// Takes over a string glib allocated.
std::string take(gchar* text) {
  std::string s = text ? text : "";
  g_free(text);
  return s;
}

}  // namespace

std::string stashFold(const std::string& text) {
  if (ascii(text)) {
    std::string s = text;
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
  }
  const std::string valid = take(g_utf8_make_valid(text.data(), static_cast<gssize>(text.size())));
  return take(g_utf8_casefold(valid.data(), static_cast<gssize>(valid.size())));
}

std::string stashMergeKey(const std::string& text) {
  const std::string folded = stashFold(text);
  std::string key;
  for (const char* at = folded.c_str(); *at; at = g_utf8_next_char(at)) {
    const gunichar c = g_utf8_get_char(at);
    if (g_unichar_isalnum(c)) key.append(at, g_utf8_next_char(at) - at);
  }
  return key;
}

std::string stashCollateKey(const std::string& text) {
  const std::string folded = stashFold(text);
  return take(g_utf8_collate_key(folded.data(), static_cast<gssize>(folded.size())));
}
