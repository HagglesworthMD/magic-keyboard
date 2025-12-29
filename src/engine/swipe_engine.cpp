/**
 * Magic Keyboard - Swipe Typing Engine Implementation
 *
 * Deterministic geometry-based path matching with frequency-weighted scoring.
 * See docs/SWIPE_ENGINE_SPEC.md for algorithm details.
 */

#include "swipe_engine.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <set>
#include <sstream>
#include <unordered_map>

namespace swipe {

// ============================================================================
// Layout Loading
// ============================================================================

bool SwipeEngine::loadLayout(const std::string &layoutPath) {
  keys_.clear();
  keyIndex_.clear();

  std::ifstream f(layoutPath);
  if (!f.is_open()) {
    return false;
  }

  std::string content((std::istreambuf_iterator<char>(f)),
                      std::istreambuf_iterator<char>());

  // Parse layout JSON manually (no external JSON library dependency)
  // Extract keyUnit, keyHeight, keySpacing
  double keyUnit = 60.0;
  double keyHeight = 50.0;
  double spacing = 6.0;

  auto extractDouble = [&](const std::string &field) -> double {
    size_t pos = content.find("\"" + field + "\":");
    if (pos == std::string::npos)
      return 0;
    pos += field.length() + 3;
    return std::stod(content.substr(pos));
  };

  keyUnit = extractDouble("keyUnit");
  if (keyUnit <= 0)
    keyUnit = 60.0;
  keyHeight = extractDouble("keyHeight");
  if (keyHeight <= 0)
    keyHeight = 50.0;
  spacing = extractDouble("keySpacing");
  if (spacing < 0)
    spacing = 6.0;

  // Parse rows
  size_t rowPos = 0;
  while ((rowPos = content.find("\"y\":", rowPos)) != std::string::npos) {
    int rowY = std::stoi(content.substr(rowPos + 4));

    // Find row offset
    double rowOffset = 0.0;
    size_t offsetPos = content.find("\"offset\":", rowPos);
    size_t nextRowPos = content.find("\"y\":", rowPos + 4);
    if (offsetPos != std::string::npos &&
        (nextRowPos == std::string::npos || offsetPos < nextRowPos)) {
      rowOffset = std::stod(content.substr(offsetPos + 9));
    }

    // Find keys array
    size_t keysPos = content.find("\"keys\":", rowPos);
    size_t keysEnd = content.find("]", keysPos);

    size_t keyPos = keysPos;
    while ((keyPos = content.find("{", keyPos)) != std::string::npos &&
           keyPos < keysEnd) {
      size_t objEnd = content.find("}", keyPos);
      std::string obj = content.substr(keyPos, objEnd - keyPos + 1);

      Key k;
      k.isSpecial = false;

      // Extract code
      size_t codePos = obj.find("\"code\":\"");
      if (codePos != std::string::npos) {
        size_t codeEnd = obj.find("\"", codePos + 8);
        k.id = obj.substr(codePos + 8, codeEnd - (codePos + 8));
      }

      // Extract label
      size_t labelPos = obj.find("\"label\":\"");
      if (labelPos != std::string::npos) {
        size_t labelEnd = obj.find("\"", labelPos + 9);
        k.label = obj.substr(labelPos + 9, labelEnd - (labelPos + 9));
      }

      // Extract special flag
      if (obj.find("\"special\":true") != std::string::npos ||
          obj.find("\"action\":true") != std::string::npos) {
        k.isSpecial = true;
      }

      // Extract x, w
      auto getVal = [&](const std::string &field) -> double {
        size_t fp = obj.find("\"" + field + "\":");
        if (fp == std::string::npos)
          return 0.0;
        return std::stod(obj.substr(fp + field.size() + 3));
      };

      double kx = getVal("x") + rowOffset;
      double kw = getVal("w");
      if (kw <= 0)
        kw = 1.0;

      // Convert to pixel coordinates
      k.bounds.x = kx * keyUnit + (kx > 0 ? static_cast<int>(kx) * spacing : 0);
      k.bounds.y = rowY * (keyHeight + spacing);
      k.bounds.w = kw * keyUnit + (kw > 1 ? (kw - 1) * spacing : 0);
      k.bounds.h = keyHeight;

      k.center.x = k.bounds.x + k.bounds.w / 2.0;
      k.center.y = k.bounds.y + k.bounds.h / 2.0;

      keyIndex_[k.id] = keys_.size();
      keys_.push_back(k);

      keyPos = objEnd;
    }
    rowPos = keysEnd;
  }

  buildNeighborMap();
  return !keys_.empty();
}

// ============================================================================
// Dictionary Loading
// ============================================================================

bool SwipeEngine::loadDictionary(const std::string &wordsPath,
                                 const std::string &freqPath) {
  dictionary_.clear();
  for (int i = 0; i < 26; ++i)
    for (int j = 0; j < 26; ++j)
      buckets_[i][j].clear();

  // Load frequencies first
  std::unordered_map<std::string, uint32_t> freqs;
  std::ifstream ff(freqPath);
  std::string line;
  while (std::getline(ff, line)) {
    size_t tab = line.find('\t');
    if (tab != std::string::npos) {
      std::string word = line.substr(0, tab);
      uint32_t freq = std::stoul(line.substr(tab + 1));
      freqs[word] = freq;
    }
  }

  // Load words
  std::ifstream wf(wordsPath);
  if (!wf.is_open()) {
    return false;
  }

  while (std::getline(wf, line)) {
    if (line.empty())
      continue;

    // Skip words with non-alpha characters
    bool valid = true;
    for (char c : line) {
      if (!std::isalpha(c)) {
        valid = false;
        break;
      }
    }
    if (!valid)
      continue;

    DictWord dw;
    dw.word = line;
    dw.freq = freqs.count(line) ? freqs[line] : 1000; // Default low priority
    dw.len = static_cast<int>(line.length());
    dw.first = static_cast<char>(std::tolower(line[0]));
    dw.last = static_cast<char>(std::tolower(line.back()));
    dictionary_.push_back(dw);

    // Index by first/last character
    int fidx = dw.first - 'a';
    int lidx = dw.last - 'a';
    if (fidx >= 0 && fidx < 26 && lidx >= 0 && lidx < 26) {
      buckets_[fidx][lidx].push_back(static_cast<int>(dictionary_.size()) - 1);
    }
  }

  return !dictionary_.empty();
}

// ============================================================================
// Neighbor Map Construction
// ============================================================================

void SwipeEngine::buildNeighborMap() {
  neighbors_.clear();

  // For each alphabetic key, find neighbors within 1.5 key widths
  for (const auto &key : keys_) {
    if (!key.isAlpha())
      continue;

    char c = std::tolower(key.id[0]);
    std::vector<char> neighs;

    for (const auto &other : keys_) {
      if (!other.isAlpha() || other.id == key.id)
        continue;

      double dist = key.center.distanceTo(other.center);
      // 1.5 key widths ≈ 90px with 60px keyUnit
      if (dist < 90.0) {
        neighs.push_back(std::tolower(other.id[0]));
      }
    }

    neighbors_[c] = neighs;
  }
}

// ============================================================================
// Key Finding
// ============================================================================

const Key *SwipeEngine::findBestKey(const Point &pt) const {
  const Key *bestKey = nullptr;
  double bestDistSq = 1e18;

  // Priority 1: Inside bounding rect
  for (const auto &k : keys_) {
    if (k.bounds.contains(pt)) {
      return &k;
    }

    double d2 = pt.distanceSquaredTo(k.center);
    if (d2 < bestDistSq) {
      bestDistSq = d2;
      bestKey = &k;
    }
  }

  // Reject if too far from any key (noise filtering)
  if (bestDistSq > 100 * 100) {
    return nullptr;
  }

  return bestKey;
}

const Key *SwipeEngine::findKeyById(const std::string &id) const {
  auto it = keyIndex_.find(id);
  if (it != keyIndex_.end()) {
    return &keys_[it->second];
  }
  return nullptr;
}

// ============================================================================
// Path → Key Sequence Mapping
// ============================================================================

std::vector<std::string>
SwipeEngine::mapPathToSequence(const std::vector<Point> &path) {
  if (path.empty() || keys_.empty()) {
    return {};
  }

  std::vector<std::string> rawSequence;
  const Key *currentKey = nullptr;
  int consecutiveSamples = 0;

  // Candidate tracking for hysteresis
  const Key *candidateKey = nullptr;
  int candidateCount = 0;

  for (const auto &pt : path) {
    const Key *bestKey = findBestKey(pt);
    if (!bestKey)
      continue;

    if (currentKey == nullptr) {
      // First key
      currentKey = bestKey;
      consecutiveSamples = 1;
      rawSequence.push_back(currentKey->id);
    } else if (bestKey != currentKey) {
      // Potential key change - apply hysteresis
      bool accept = false;

      // Condition 1: Inside new key's rect
      if (bestKey->bounds.contains(pt)) {
        accept = true;
      } else {
        // Condition 2: Strong distance win
        double d2_cur = pt.distanceSquaredTo(currentKey->center);
        double d2_new = pt.distanceSquaredTo(bestKey->center);
        double d_cur = std::sqrt(d2_cur);
        double d_new = std::sqrt(d2_new);

        double ratio = config::DISTANCE_RATIO_THRESHOLD;
        if (d_new < d_cur * ratio &&
            (d_cur - d_new) > config::DISTANCE_GAP_MIN_PX) {
          accept = true;
        }
      }

      // Condition 3: Consecutive samples on new key
      if (!accept) {
        if (bestKey == candidateKey) {
          candidateCount++;
          if (candidateCount >= config::CONSECUTIVE_SAMPLES_THRESHOLD) {
            accept = true;
          }
        } else {
          candidateKey = bestKey;
          candidateCount = 1;
        }
      }

      if (accept) {
        currentKey = bestKey;
        consecutiveSamples = 1;
        rawSequence.push_back(currentKey->id);
        candidateKey = nullptr;
        candidateCount = 0;
      }
    } else {
      consecutiveSamples++;
    }
  }

  if (rawSequence.empty()) {
    return {};
  }

  // ---- Phase 2: Collapse duplicates with dwell tracking ----
  std::vector<std::pair<std::string, int>> dwells;
  for (const auto &s : rawSequence) {
    if (dwells.empty() || s != dwells.back().first) {
      dwells.push_back({s, 1});
    } else {
      dwells.back().second++;
    }
  }

  // ---- Phase 3: Remove A-B-A bounces where B dwell < threshold ----
  std::vector<std::string> filtered;
  for (size_t i = 0; i < dwells.size(); ++i) {
    // Check for A-B-A pattern
    if (i > 0 && i < dwells.size() - 1 &&
        dwells[i - 1].first == dwells[i + 1].first &&
        dwells[i].second < config::MIN_DWELL_FOR_BOUNCE) {
      continue; // Skip B
    }
    filtered.push_back(dwells[i].first);
  }

  // ---- Phase 4: Re-collapse (bounce removal may create new duplicates) ----
  std::vector<std::string> result;
  for (const auto &s : filtered) {
    if (result.empty() || s != result.back()) {
      result.push_back(s);
    }
  }

  return result;
}

// ============================================================================
// Shortlist Generation
// ============================================================================

std::vector<int> SwipeEngine::getShortlist(const std::string &keys) const {
  if (keys.empty()) {
    return {};
  }

  int fidx = std::tolower(keys[0]) - 'a';
  int lidx = std::tolower(keys.back()) - 'a';

  if (fidx < 0 || fidx >= 26 || lidx < 0 || lidx >= 26) {
    return {};
  }

  std::vector<int> result;
  int targetLen = static_cast<int>(keys.length());

  for (int idx : buckets_[fidx][lidx]) {
    const auto &dw = dictionary_[idx];
    if (std::abs(dw.len - targetLen) <= config::LENGTH_TOLERANCE) {
      result.push_back(idx);
    }
  }

  return result;
}

// ============================================================================
// Levenshtein Distance with Early Exit
// ============================================================================

int SwipeEngine::levenshtein(const std::string &s1, const std::string &s2,
                             int limit) {
  int n = static_cast<int>(s1.length());
  int m = static_cast<int>(s2.length());

  if (std::abs(n - m) > limit) {
    return limit + 1;
  }

  std::vector<int> prev(m + 1);
  std::vector<int> curr(m + 1);

  for (int j = 0; j <= m; ++j) {
    prev[j] = j;
  }

  for (int i = 1; i <= n; ++i) {
    curr[0] = i;
    int minRow = curr[0];

    for (int j = 1; j <= m; ++j) {
      int cost = (std::tolower(s1[i - 1]) == std::tolower(s2[j - 1])) ? 0 : 1;
      curr[j] = std::min({curr[j - 1] + 1, prev[j] + 1, prev[j - 1] + cost});
      minRow = std::min(minRow, curr[j]);
    }

    if (minRow > limit) {
      return limit + 1; // Early exit
    }

    std::swap(prev, curr);
  }

  return prev[m];
}

// ============================================================================
// Bigram Overlap
// ============================================================================

int SwipeEngine::countBigramOverlap(const std::string &keys,
                                    const std::string &word) {
  auto getBigrams = [](const std::string &s) {
    std::set<uint16_t> result;
    for (size_t i = 0; i + 1 < s.length(); ++i) {
      if (std::isalpha(s[i]) && std::isalpha(s[i + 1])) {
        result.insert(static_cast<uint16_t>((std::tolower(s[i]) - 'a') * 26 +
                                            (std::tolower(s[i + 1]) - 'a')));
      }
    }
    return result;
  };

  auto b1 = getBigrams(keys);
  auto b2 = getBigrams(word);

  int count = 0;
  for (auto bg : b1) {
    if (b2.count(bg)) {
      count++;
    }
  }
  return count;
}

// ============================================================================
// Spatial Score
// ============================================================================

double SwipeEngine::computeSpatialScore(const std::string &keys,
                                        const std::string &word) {
  // Compute average centroid distance between key sequence and word
  // Uses dynamic alignment to handle length differences

  double totalDist = 0;
  int pairs = 0;

  size_t keyIdx = 0;
  size_t wordIdx = 0;

  while (keyIdx < keys.length() && wordIdx < word.length()) {
    // Find key centroids
    std::string keyId(1, std::tolower(keys[keyIdx]));
    std::string wordId(1, std::tolower(word[wordIdx]));

    const Key *keyKey = findKeyById(keyId);
    const Key *wordKey = findKeyById(wordId);

    if (keyKey && wordKey) {
      double dist = keyKey->center.distanceTo(wordKey->center);
      totalDist += dist;
      pairs++;
    }

    // Advance indices - simple alignment
    size_t keysRemaining = keys.length() - keyIdx;
    size_t wordRemaining = word.length() - wordIdx;

    if (keysRemaining > wordRemaining) {
      keyIdx++;
    } else if (wordRemaining > keysRemaining) {
      wordIdx++;
    } else {
      keyIdx++;
      wordIdx++;
    }
  }

  if (pairs == 0) {
    return 0;
  }

  double avgDist = totalDist / pairs;

  // Normalize: 0px = 1.0, SPATIAL_NORM_DISTANCE = 0.0, beyond = negative
  return std::max(-1.0, 1.0 - avgDist / config::SPATIAL_NORM_DISTANCE);
}

// ============================================================================
// Candidate Scoring
// ============================================================================

double SwipeEngine::scoreCandidate(const std::string &keys,
                                   const DictWord &dw) {
  // Component 1: Edit distance penalty
  int editDist = levenshtein(keys, dw.word, config::EDIT_DISTANCE_LIMIT);

  // Component 2: Bigram overlap bonus
  int bigramOverlap = countBigramOverlap(keys, dw.word);

  // Component 3: Frequency bonus (log scale)
  // Lower freq value = more common = higher score
  // Using inverse: if freq=1 is most common, higher freq = lower priority
  double freqScore = std::log1p(1000.0 / (dw.freq + 1));

  // Component 4: Spatial proximity bonus
  double spatialScore = computeSpatialScore(keys, dw.word);

  // Weighted combination
  return config::W_EDIT_DISTANCE * editDist +
         config::W_BIGRAM_OVERLAP * bigramOverlap +
         config::W_FREQUENCY * freqScore + config::W_SPATIAL * spatialScore;
}

// ============================================================================
// Candidate Generation
// ============================================================================

std::vector<Candidate>
SwipeEngine::generateCandidates(const std::string &keySequence) {
  if (keySequence.length() < config::MIN_KEY_SEQUENCE_LENGTH) {
    return {};
  }

  auto shortlist = getShortlist(keySequence);
  std::vector<Candidate> candidates;
  candidates.reserve(shortlist.size());

  for (int idx : shortlist) {
    const auto &dw = dictionary_[idx];
    double score = scoreCandidate(keySequence, dw);

    if (score >= config::MIN_CANDIDATE_SCORE) {
      Candidate c;
      c.word = dw.word;
      c.score = score;
      c.editDistance =
          levenshtein(keySequence, dw.word, config::EDIT_DISTANCE_LIMIT);
      c.bigramOverlap = countBigramOverlap(keySequence, dw.word);
      c.freqContribution =
          config::W_FREQUENCY * std::log1p(1000.0 / (dw.freq + 1));
      c.spatialContribution =
          config::W_SPATIAL * computeSpatialScore(keySequence, dw.word);
      candidates.push_back(c);
    }
  }

  // Sort by score descending
  std::sort(
      candidates.begin(), candidates.end(),
      [](const Candidate &a, const Candidate &b) { return a.score > b.score; });

  // Limit to max candidates
  if (candidates.size() > static_cast<size_t>(config::MAX_CANDIDATES)) {
    candidates.resize(config::MAX_CANDIDATES);
  }

  return candidates;
}

} // namespace swipe
