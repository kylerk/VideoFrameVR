#include "stash_catalog.h"

#include "stash_text.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <tuple>
#include <unordered_set>

// The scene workbench's Improved v1 defaults, with the studio bonus set to
// 1.75. No tag-name rules, performer characteristics or gender overrides.
void StashCatalog::indexSimilarity() {
  for (int vr = 0; vr < 2; ++vr) {
    std::vector<uint64_t> bits = tagBits_;
    std::vector<int> counts(tags_.size(), 0), active;
    int n = 0;
    for (const StashScene& s : scenes_) n += s.vr == bool(vr);
    for (size_t t = 0; t < tags_.size(); ++t) {
      for (size_t w = 0; w < words_; ++w) {
        uint64_t& word = bits[t * words_ + w];
        word &= vr ? vrBits_[w] : ~vrBits_[w];
        counts[t] += __builtin_popcountll(word);
      }
      if (counts[t] >= 4 && counts[t] < n) active.push_back(static_cast<int>(t));
    }
    // Directional association A->B = (P(B|A)-P(B))/(1-P(B)). Only
    // supported associations contribute; constant tags cannot contribute.
    std::vector<double> density(tags_.size(), 0);
    auto add = [&](int target, int other, int shared) {
      const double probability = double(counts[other]) / n;
      const double association = (double(shared) / counts[target] - probability) / (1 - probability);
      if (association > .35) {
        const double strength = (association - .35) / .65;
        density[target] += strength * strength * shared / (shared + 10);
      }
    };
    for (size_t a = 0; a < active.size(); ++a) {
      for (size_t b = a + 1; b < active.size(); ++b) {
        const int ta = active[a], tb = active[b];
        int shared = 0;
        for (size_t w = 0; w < words_; ++w)
          shared += __builtin_popcountll(bits[ta * words_ + w] & bits[tb * words_ + w]);
        if (shared < 4) continue;
        add(ta, tb, shared);
        add(tb, ta, shared);
      }
    }
    std::vector<double>& weights = similarityWeights_[vr];
    weights.assign(tags_.size(), 0);
    for (size_t t = 0; t < tags_.size(); ++t) {
      if (!counts[t]) continue;
      // Match the six-decimal density exported by the workbench.
      const double redundancy = std::round(density[t] * 1e6) / 1e6;
      weights[t] = (1 + .15 * std::min(4.0, std::log(double(n + 1) / (counts[t] + 1)))) /
                   (1 + redundancy);
    }
  }
  similarityMass_.assign(scenes_.size(), 0);
  similarityGroup_.resize(scenes_.size());
  std::iota(similarityGroup_.begin(), similarityGroup_.end(), 0);
  auto root = [&](int i) {
    while (similarityGroup_[i] != i) {
      similarityGroup_[i] = similarityGroup_[similarityGroup_[i]];
      i = similarityGroup_[i];
    }
    return i;
  };
  auto join = [&](int a, int b) {
    a = root(a);
    b = root(b);
    similarityGroup_[std::max(a, b)] = std::min(a, b);
  };
  std::unordered_map<std::string, int> hashes;
  std::map<std::tuple<std::string, int, std::vector<int>>, int> titles;
  for (int i = 0; i < static_cast<int>(scenes_.size()); ++i) {
    const StashScene& s = scenes_[i];
    for (const int t : s.tags) similarityMass_[i] += similarityWeights_[s.vr][t];
    for (const std::string& hash : s.oshashes) {
      if (hash.empty()) continue;
      const auto [it, added] = hashes.emplace(hash, i);
      if (!added) join(i, it->second);
    }
    const std::string title = stashMergeKey(s.displayTitle());
    if (!title.empty() && s.studio >= 0 && !s.performers.empty()) {
      const auto [it, added] = titles.emplace(std::make_tuple(title, s.studio, s.performers), i);
      if (!added) join(i, it->second);
    }
  }
  for (int& group : similarityGroup_) group = root(group);
}

std::vector<int> StashCatalog::similarScenes(int of, bool malePerformers, size_t limit) const {
  if (of < 0 || of >= static_cast<int>(scenes_.size()) || !limit) return {};
  const StashScene& source = scenes_[of];
  std::vector<char> tagged(tags_.size(), 0), shown(performers_.size(), 0);
  for (const int t : source.tags) tagged[t] = 1;
  size_t sourceShown = 0;
  for (const int p : source.performers) {
    shown[p] = stashShown(performers_[p].gender, malePerformers);
    sourceShown += shown[p];
  }
  // Unlike the browsing buckets, these are exact counts, not capped counts.
  // An empty cast or any unknown gender makes the cast-size term unavailable.
  auto castCounts = [&](const StashScene& s) {
    int women = 0, men = 0;
    bool known = !s.performers.empty();
    for (const int p : s.performers) {
      const StashGender gender = performers_[p].gender;
      if (gender == StashGender::Unknown) known = false;
      else if (gender == StashGender::Male) ++men;
      else ++women;
    }
    return std::make_tuple(women, men, known);
  };
  const auto [women, men, known] = castCounts(source);
  std::vector<std::pair<double, int>> ranked;
  for (int i = 0; i < static_cast<int>(scenes_.size()); ++i) {
    const StashScene& s = scenes_[i];
    if (s.vr != source.vr || similarityGroup_[i] == similarityGroup_[of]) continue;
    double sharedMass = 0;
    bool sharedTag = false;
    for (const int t : s.tags) {
      if (!tagged[t]) continue;
      sharedTag = true;
      sharedMass += similarityWeights_[source.vr][t];
    }
    if (!sharedTag) continue;
    const double mass = similarityMass_[of] + similarityMass_[i];
    double score = mass ? 10 * 2 * sharedMass / mass : 0;
    const auto [otherWomen, otherMen, otherKnown] = castCounts(s);
    if (known && otherKnown)
      score += 3 * (.85 * std::exp(-std::abs(women - otherWomen)) +
                    .15 * std::exp(-std::abs(men - otherMen)));
    size_t candidateShown = 0, sharedPerformers = 0;
    for (const int p : s.performers) {
      candidateShown += stashShown(performers_[p].gender, malePerformers);
      sharedPerformers += shown[p];
    }
    score += double(sharedPerformers) / std::max({size_t(1), sourceShown, candidateShown});
    if (s.studio >= 0 && s.studio == source.studio) score += 1.75;
    if (score > 0) ranked.emplace_back(score, i);
  }
  std::sort(ranked.begin(), ranked.end(), [&](const auto& a, const auto& b) {
    return a.first != b.first ? a.first > b.first : scenes_[a.second].id < scenes_[b.second].id;
  });
  std::unordered_set<int> seen;
  std::vector<int> result;
  for (const auto& [score, i] : ranked) {
    if (!seen.insert(similarityGroup_[i]).second) continue;
    result.push_back(i);
    if (result.size() == limit) break;
  }
  return result;
}
