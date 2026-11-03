// StashCatalog::query(): the Scenes page's filter, facet counts and order,
// on bit sets of the catalog's scenes.

#include "stash_catalog.h"

#include "stash_text.h"

#include <algorithm>
#include <cstdint>
#include <functional>

namespace {

// The kinds of filter, each a bit set of the scenes it lets through.
enum Kind { Format, TagsIn, TagsOut, PerformersIn, PerformersOut, StudiosIn, StudiosOut, Women,
            Men, Looks, Released, Added, Resolution, Duration, FrameRate, Codec, FileSize, History,
            Text, kKinds };

using Bits = std::vector<uint64_t>;

bool has(const Bits& bits, size_t i) { return (bits[i / 64] >> (i % 64)) & 1; }

// Calls f(i) for each scene in the set.
template <typename F>
void forEach(const Bits& bits, F f) {
  for (size_t w = 0; w < bits.size(); ++w)
    for (uint64_t x = bits[w]; x; x &= x - 1) f(w * 64 + static_cast<size_t>(__builtin_ctzll(x)));
}

int count(const Bits& bits) {
  int n = 0;
  for (const uint64_t x : bits) n += __builtin_popcountll(x);
  return n;
}

bool inMask(unsigned mask, uint8_t value) { return value != kStashNone && ((mask >> value) & 1); }

// The buckets a scene with this many women (up to 3) or men (up to 4) is in.
unsigned womenBuckets(uint8_t women) {
  if (women == kStashNone || women == 0) return 0;
  return women == 1 ? stashBit(StashWomen::One) : women == 2 ? stashBit(StashWomen::Two)
                                                             : stashBit(StashWomen::ThreePlus);
}
unsigned menBuckets(uint8_t men) {
  if (men == kStashNone) return 0;
  if (men == 0) return stashBit(StashMen::None);
  if (men == 1) return stashBit(StashMen::One);
  return stashBit(StashMen::TwoPlus) | (men >= 4 ? stashBit(StashMen::FourPlus) : 0u);
}

uint64_t mix(uint64_t x) {  // splitmix64
  x += 0x9e3779b97f4a7c15ull;
  x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
  x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
  return x ^ (x >> 31);
}

// Those with a count, the most first (ties in index order), sorted as
// single numbers: the count above, the index below.
std::vector<StashFacet> facets(const std::vector<int>& counts) {
  std::vector<uint64_t> keys;
  for (size_t i = 0; i < counts.size(); ++i)
    if (counts[i] > 0) keys.push_back(uint64_t(counts[i]) << 32 | (UINT32_MAX - uint32_t(i)));
  std::sort(keys.begin(), keys.end(), std::greater<uint64_t>());
  std::vector<StashFacet> list(keys.size());
  for (size_t k = 0; k < keys.size(); ++k)
    list[k] = {static_cast<int>(UINT32_MAX - uint32_t(keys[k])), static_cast<int>(keys[k] >> 32)};
  return list;
}

}  // namespace

StashResult StashCatalog::query(const StashFilter& f, const StashOrder& order,
                                const std::vector<StashHistory>* history, bool withFacets) const {
  StashResult result;
  const size_t n = scenes_.size(), words = words_;
  if (history && history->size() != n) history = nullptr;

  // All scenes, as a bit set.
  Bits everything(words, ~uint64_t(0));
  if (n % 64) everything.back() = (uint64_t(1) << (n % 64)) - 1;
  if (n == 0) everything.clear();
  // Away from home, what can't be played isn't there.
  if (history)
    for (size_t i = 0; i < n; ++i)
      if ((*history)[i].hidden) everything[i / 64] &= ~(uint64_t(1) << (i % 64));

  // VR, with the formats chosen in the player since the catalog was made.
  Bits vr = vrBits_;
  if (history)
    for (size_t i = 0; i < n; ++i)
      if ((*history)[i].vr >= 0) {
        const uint64_t bit = uint64_t(1) << (i % 64);
        vr[i / 64] = (*history)[i].vr ? vr[i / 64] | bit : vr[i / 64] & ~bit;
      }

  std::array<Bits, kKinds> masks;
  std::array<bool, kKinds> active{};
  auto start = [&](Kind k, bool full) -> Bits& {
    active[k] = true;
    masks[k] = full ? everything : Bits(words, 0);
    return masks[k];
  };
  auto setIf = [&](Kind k, auto test) {
    Bits& m = start(k, false);
    for (size_t i = 0; i < n; ++i)
      if (test(i)) m[i / 64] |= uint64_t(1) << (i % 64);
  };
  auto row = [words](const std::vector<uint64_t>& bits, int index) { return bits.data() + index * words; };
  auto pick = [&](const StashPick& p, const std::vector<uint64_t>& bits, size_t rows, Kind in, Kind out,
                  bool all) {
    std::vector<int> include, exclude;
    for (const int i : p.include)
      if (i >= 0 && static_cast<size_t>(i) < rows) include.push_back(i);
    for (const int i : p.exclude)
      if (i >= 0 && static_cast<size_t>(i) < rows) exclude.push_back(i);
    if (!include.empty()) {
      Bits& m = start(in, all);
      for (const int i : include) {
        const uint64_t* r = row(bits, i);
        for (size_t w = 0; w < words; ++w) m[w] = all ? m[w] & r[w] : m[w] | r[w];
      }
    }
    if (!exclude.empty()) {
      Bits& m = start(out, true);
      for (const int i : exclude) {
        const uint64_t* r = row(bits, i);
        for (size_t w = 0; w < words; ++w) m[w] &= ~r[w];
      }
    }
  };

  if (f.format != StashFormat::All) {
    Bits& m = start(Format, false);
    for (size_t w = 0; w < words; ++w) m[w] = (f.format == StashFormat::VR ? vr[w] : ~vr[w]) & everything[w];
  }
  pick(f.tags, tagBits_, tags_.size(), TagsIn, TagsOut, f.tags.all);
  pick(f.performers, performerBits_, performers_.size(), PerformersIn, PerformersOut, f.performers.all);
  pick(f.studios, studioBits_, studios_.size(), StudiosIn, StudiosOut, false);
  if (f.women) setIf(Women, [&](size_t i) { return (womenBuckets(women_[i]) & f.women) != 0; });
  if (f.men) setIf(Men, [&](size_t i) { return (menBuckets(men_[i]) & f.men) != 0; });

  // Looks: one performer with all of them. Per kind, each value's choice
  // (1 included, 2 left out) and whether any is included.
  std::array<std::vector<uint8_t>, kStashLookKinds> chosen;
  std::array<bool, kStashLookKinds> including{}, lookActive{};
  for (int k = 0; k < kStashLookKinds; ++k) {
    const size_t values = k < kStashNamedLooks ? lookNames_[k].size()
                                               : size_t(kStashLookBuckets[k - kStashNamedLooks]);
    chosen[k].assign(values, 0);
    for (int out = 0; out < 2; ++out)
      for (const int v : out ? f.looks[k].exclude : f.looks[k].include)
        if (v >= 0 && static_cast<size_t>(v) < values) {
          chosen[k][v] = static_cast<uint8_t>(1 + out);
          including[k] = including[k] || !out;
          lookActive[k] = true;
        }
  }
  // The kinds of look a performer misses, as bits: an included value she
  // hasn't (bit k), a left-out one she has (bit 16 + k).
  auto lookMisses = [&](const Look& l) {
    unsigned misses = 0;
    for (int k = 0; k < kStashLookKinds; ++k) {
      if (!lookActive[k]) continue;
      uint8_t choices = 0;
      stashEachLookValue(static_cast<StashLookKind>(k), l.values[k], [&](int v) {
        choices |= chosen[k][v];
      });
      if (including[k] && !(choices & 1)) misses |= 1u << k;
      if (choices & 2) misses |= 1u << (16 + k);
    }
    return misses;
  };
  const bool looks = std::any_of(lookActive.begin(), lookActive.end(), [](bool on) { return on; });
  if (looks)
    setIf(Looks, [&](size_t i) {
      for (uint32_t l = lookStart_[i]; l < lookStart_[i + 1]; ++l)
        if (!lookMisses(looks_[l])) return true;
      return false;
    });

  if (f.releasedFrom != kStashNoDay || f.releasedTo != kStashNoDay)
    setIf(Released, [&](size_t i) {
      const int32_t day = scenes_[i].day;
      return day != kStashNoDay && (f.releasedFrom == kStashNoDay || day >= f.releasedFrom) &&
             (f.releasedTo == kStashNoDay || day <= f.releasedTo);
    });
  if (f.addedFrom || f.addedTo)
    setIf(Added, [&](size_t i) {
      const int64_t t = scenes_[i].createdAt;
      return t != 0 && (!f.addedFrom || t >= f.addedFrom) && (!f.addedTo || t <= f.addedTo);
    });
  // Each scene's resolution in its format, as `vr` has it.
  std::vector<uint8_t> resolution;
  if (f.resolutions || withFacets) {
    resolution.resize(n);
    for (size_t i = 0; i < n; ++i) resolution[i] = resolutions_[has(vr, i)][i];
  }
  if (f.resolutions) setIf(Resolution, [&](size_t i) { return inMask(f.resolutions, resolution[i]); });
  if (f.durations) setIf(Duration, [&](size_t i) { return inMask(f.durations, duration_[i]); });
  if (f.frameRates) setIf(FrameRate, [&](size_t i) { return inMask(f.frameRates, frameRate_[i]); });
  if (f.codecs) setIf(Codec, [&](size_t i) { return inMask(f.codecs, codec_[i]); });
  if (f.sizes) setIf(FileSize, [&](size_t i) { return inMask(f.sizes, size_[i]); });

  auto historyFits = [&](size_t i, StashHistoryOption option) {
    const StashHistory h = history ? (*history)[i] : StashHistory{};
    switch (option) {
      case StashHistoryOption::Unplayed: return h.plays == 0;
      case StashHistoryOption::Played: return h.plays > 0;
      case StashHistoryOption::Rated4: return h.rating >= 4;
      case StashHistoryOption::Unrated: return h.rating <= 0;
      case StashHistoryOption::PlayedLongAgo: return h.plays > 0 && h.lastPlayedAt < f.longAgo;
      default: return true;
    }
  };
  constexpr int kOptions = static_cast<int>(StashHistoryOption::kCount);
  if (f.history)
    setIf(History, [&](size_t i) {
      for (int o = 0; o < kOptions; ++o)
        if ((f.history >> o) & 1 && !historyFits(i, static_cast<StashHistoryOption>(o))) return false;
      return true;
    });

  if (!f.text.empty()) {
    std::vector<std::string> wordsOf;
    const std::string folded = stashFold(f.text);
    for (size_t at = 0; at < folded.size();) {
      const size_t space = folded.find_first_of(" \t", at);
      const size_t end = space == std::string::npos ? folded.size() : space;
      if (end > at) wordsOf.push_back(folded.substr(at, end - at));
      at = end + 1;
    }
    if (!wordsOf.empty())
      setIf(Text, [&](size_t i) {
        for (const std::string& w : wordsOf)
          if (haystack_[i].find(w) == std::string::npos) return false;
        return true;
      });
  }

  // The scenes all kinds let through, or all but one.
  auto allBut = [&](int skip) {
    Bits bits = everything;
    for (int k = 0; k < kKinds; ++k)
      if (active[k] && k != skip)
        for (size_t w = 0; w < words; ++w) bits[w] &= masks[k][w];
    return bits;
  };
  const Bits matching = allBut(-1);

  // The order.
  auto sortIndex = [](StashSort by) {
    switch (by) {
      case StashSort::Released: return 0;
      case StashSort::Added: return 1;
      case StashSort::Title: return 2;
      case StashSort::Duration: return 3;
      case StashSort::Resolution: return 4;
      case StashSort::CastSize: return 5;
      case StashSort::Modified: return 6;
      case StashSort::Size: return 7;
      default: return -1;
    }
  };
  StashSort by = order.by;
  const bool historySort = by == StashSort::Rating || by == StashSort::Plays || by == StashSort::LastPlayed;
  if (historySort && !history) by = StashSort::Released;
  if (const int fixed = sortIndex(by); fixed >= 0) {
    const Order& o = orders_[fixed];
    result.scenes.reserve(count(matching));
    auto take = [&](size_t from, size_t to, bool backwards) {
      for (size_t r = 0; r < to - from; ++r) {
        const int i = o.scenes[backwards ? to - 1 - r : from + r];
        if (has(matching, i)) result.scenes.push_back(i);
      }
    };
    take(0, o.known, order.descending);
    take(o.known, o.scenes.size(), false);
  } else {
    forEach(matching, [&](size_t i) { result.scenes.push_back(static_cast<int>(i)); });
    std::vector<int64_t> key(n, 0);  // 0 = none, last either way
    for (const int i : result.scenes) {
      if (by == StashSort::Random) {
        key[i] = static_cast<int64_t>(mix(static_cast<uint64_t>(scenes_[i].id) ^ mix(order.seed)) >> 1) | 1;
      } else {
        const StashHistory& h = (*history)[i];
        key[i] = by == StashSort::Rating ? static_cast<int64_t>(h.rating * 2)
                 : by == StashSort::Plays ? h.plays : h.lastPlayedAt;
      }
    }
    const bool down = by != StashSort::Random && order.descending;
    std::sort(result.scenes.begin(), result.scenes.end(), [&](int a, int b) {
      const int64_t ka = key[a], kb = key[b];
      if ((ka == 0) != (kb == 0)) return kb == 0;
      if (ka != kb) return down ? ka > kb : ka < kb;
      return titleRank_[a] < titleRank_[b];
    });
  }
  if (!withFacets) return result;

  // Facets.
  {
    const Bits base = allBut(Format);
    for (size_t w = 0; w < words; ++w) {
      result.formats[1] += __builtin_popcountll(base[w] & vr[w]);
      result.formats[0] += __builtin_popcountll(base[w]);
    }
    result.formats[2] = result.formats[0] - result.formats[1];
  }
  // Tags, performers and studios: how many scenes of `base` have each.
  auto countLists = [&](const Lists& lists, const Bits& base, size_t size, const uint8_t* hidden) {
    std::vector<int> counts(size, 0);
    forEach(base, [&](size_t i) {
      for (uint32_t at = lists.start[i]; at < lists.start[i + 1]; ++at) ++counts[lists.items[at]];
    });
    if (hidden)
      for (size_t i = 0; i < size; ++i)
        if (hidden[i]) counts[i] = 0;
    return facets(counts);
  };
  result.tags = countLists(sceneTags_, f.tags.all ? matching : allBut(TagsIn), tags_.size(), nullptr);
  result.performers =
      countLists(scenePerformers_, f.performers.all ? matching : allBut(PerformersIn),
                 performers_.size(), f.malePerformers ? nullptr : hidden_.data());
  result.studios = countLists(sceneStudios_, allBut(StudiosIn), studios_.size(), nullptr);

  auto histogram = [&](Kind k, const std::vector<uint8_t>& values, auto& counts) {
    forEach(allBut(k), [&](size_t i) {
      if (values[i] < counts.size()) ++counts[values[i]];
    });
  };
  auto buckets = [&](Kind k, const std::vector<uint8_t>& values, unsigned (*of)(uint8_t),
                     auto& counts) {
    forEach(allBut(k), [&](size_t i) {
      const unsigned in = of(values[i]);
      for (size_t b = 0; b < counts.size(); ++b) counts[b] += (in >> b) & 1;
    });
  };
  buckets(Women, women_, womenBuckets, result.women);
  buckets(Men, men_, menBuckets, result.men);
  histogram(Resolution, resolution, result.resolutions);
  histogram(Duration, duration_, result.durations);
  histogram(FrameRate, frameRate_, result.frameRates);
  histogram(Codec, codec_, result.codecs);
  histogram(FileSize, size_, result.sizes);
  {
    // Each kind of look counted with the others kept (and its own left-out
    // values), in one pass: a performer counts for a kind if she misses no
    // other kind. A scene counts once per value.
    std::array<std::vector<int>, kStashLookKinds> counts;
    std::array<std::vector<uint32_t>, kStashLookKinds> seen;
    for (int k = 0; k < kStashLookKinds; ++k) {
      counts[k].assign(chosen[k].size(), 0);
      seen[k].assign(chosen[k].size(), UINT32_MAX);
    }
    forEach(allBut(Looks), [&](size_t scene) {
      const uint32_t i = static_cast<uint32_t>(scene);
      for (uint32_t l = lookStart_[i]; l < lookStart_[i + 1]; ++l) {
        const Look& look = looks_[l];
        const unsigned fails = lookMisses(look);
        for (int k = 0; k < kStashLookKinds; ++k) {
          if (fails & ~(1u << k)) continue;
          stashEachLookValue(static_cast<StashLookKind>(k), look.values[k], [&](int v) {
            if (seen[k][v] == i) return;
            seen[k][v] = i;
            ++counts[k][v];
          });
        }
      }
    });
    for (int k = 0; k < kStashLookKinds; ++k) result.looks[k] = facets(counts[k]);
  }
  {
    std::vector<int> years(lastYear_ - firstYear_ + 1, 0);
    forEach(allBut(Released), [&](size_t i) {
      if (!year_[i]) ++result.undated;
      else ++years[year_[i] - firstYear_];
    });
    for (int y = lastYear_; y >= firstYear_ && y > 0; --y)
      if (years[y - firstYear_]) result.years.emplace_back(y, years[y - firstYear_]);
  }
  if (!f.addedSince.empty()) {
    result.addedSince.assign(f.addedSince.size(), 0);
    forEach(allBut(Added), [&](size_t i) {
      for (size_t t = 0; t < f.addedSince.size(); ++t)
        result.addedSince[t] += scenes_[i].createdAt >= f.addedSince[t];
    });
  }
  forEach(matching, [&](size_t i) {
    for (int o = 0; o < kOptions; ++o)
      result.history[o] += historyFits(i, static_cast<StashHistoryOption>(o));
  });
  return result;
}
