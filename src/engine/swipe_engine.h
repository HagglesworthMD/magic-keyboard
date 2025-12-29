#pragma once

/**
 * Magic Keyboard - Swipe Typing Engine
 *
 * Deterministic geometry-based path matching with frequency-weighted scoring.
 * See docs/SWIPE_ENGINE_SPEC.md for algorithm details.
 */

#include <cctype>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace swipe {

// ============================================================================
// Tuning Constants
// ============================================================================
namespace config {
// Hysteresis parameters
constexpr double DISTANCE_RATIO_THRESHOLD = 0.72;
constexpr double DISTANCE_GAP_MIN_PX = 6.0;
constexpr int CONSECUTIVE_SAMPLES_THRESHOLD = 2;
constexpr int MIN_DWELL_FOR_BOUNCE = 2;

// Shortlist filtering
constexpr int LENGTH_TOLERANCE = 3;

// Scoring weights (see SWIPE_ENGINE_SPEC.md for rationale)
constexpr double W_EDIT_DISTANCE = -2.2;
constexpr double W_BIGRAM_OVERLAP = 1.0;
constexpr double W_FREQUENCY = 0.8;
constexpr double W_SPATIAL = 1.5;

// Thresholds
constexpr double MIN_CANDIDATE_SCORE = -5.0;
constexpr int MAX_CANDIDATES = 8;
constexpr int MIN_KEY_SEQUENCE_LENGTH = 2;

// Levenshtein
constexpr int EDIT_DISTANCE_LIMIT = 7;

// Spatial normalization (distance where score = 0)
constexpr double SPATIAL_NORM_DISTANCE = 60.0;
} // namespace config

// ============================================================================
// Geometry Types
// ============================================================================
struct Point {
  double x = 0;
  double y = 0;

  double distanceSquaredTo(const Point &other) const {
    double dx = x - other.x;
    double dy = y - other.y;
    return dx * dx + dy * dy;
  }

  double distanceTo(const Point &other) const {
    return std::sqrt(distanceSquaredTo(other));
  }
};

struct Rect {
  double x = 0;
  double y = 0;
  double w = 0;
  double h = 0;

  bool contains(const Point &p) const {
    return p.x >= x && p.x <= x + w && p.y >= y && p.y <= y + h;
  }
};

// ============================================================================
// Key Model
// ============================================================================
struct Key {
  std::string id;    // Key code (e.g., "a", "space", "backspace")
  std::string label; // Display label
  Rect bounds;       // Hit area rectangle
  Point center;      // Centroid for distance calculations
  bool isSpecial;    // Non-letter key (shift, enter, etc.)

  // Check if this is an alphabetic key
  bool isAlpha() const { return id.length() == 1 && std::isalpha(id[0]); }
};

// ============================================================================
// Dictionary Model
// ============================================================================
struct DictWord {
  std::string word;
  uint32_t freq; // Frequency rank (lower = more common)
  char first;    // First character (lowercase)
  char last;     // Last character (lowercase)
  int len;       // Word length

  DictWord() : freq(0), first(0), last(0), len(0) {}
  DictWord(const std::string &w, uint32_t f)
      : word(w), freq(f), first(std::tolower(w.front())),
        last(std::tolower(w.back())), len(static_cast<int>(w.length())) {}
};

// ============================================================================
// Candidate Result
// ============================================================================
struct Candidate {
  std::string word;
  double score;

  // Score components (for debugging/tuning)
  int editDistance = 0;
  int bigramOverlap = 0;
  double freqContribution = 0;
  double spatialContribution = 0;

  Candidate() : score(0) {}
  Candidate(const std::string &w, double s) : word(w), score(s) {}
};

// ============================================================================
// Confidence Level
// ============================================================================
enum class Confidence { LOW, MEDIUM, HIGH };

inline Confidence getConfidence(const std::vector<Candidate> &candidates) {
  if (candidates.empty())
    return Confidence::LOW;

  double top = candidates[0].score;
  double gap =
      (candidates.size() > 1) ? top - candidates[1].score : std::abs(top);

  if (gap > 5.0 && top > 0)
    return Confidence::HIGH;
  if (gap > 2.0 && top > -3.0)
    return Confidence::MEDIUM;
  return Confidence::LOW;
}

// ============================================================================
// Swipe Engine Class
// ============================================================================
class SwipeEngine {
public:
  SwipeEngine() = default;

  // Load keyboard layout from JSON file
  bool loadLayout(const std::string &layoutPath);

  // Load dictionary from words.txt and freq.tsv
  bool loadDictionary(const std::string &wordsPath,
                      const std::string &freqPath);

  // Convert raw path points to key sequence
  // Returns collapsed sequence like ["h", "e", "l", "o"]
  std::vector<std::string> mapPathToSequence(const std::vector<Point> &path);

  // Generate word candidates from key sequence string
  // Input: "helo" (collapsed keys joined)
  // Output: Sorted candidates with scores
  std::vector<Candidate> generateCandidates(const std::string &keySequence);

  // Accessors
  size_t getDictionarySize() const { return dictionary_.size(); }
  size_t getKeyCount() const { return keys_.size(); }
  const std::vector<Key> &getKeys() const { return keys_; }

private:
  // Layout data
  std::vector<Key> keys_;
  std::unordered_map<std::string, size_t> keyIndex_; // id -> keys_ index

  // Dictionary data
  std::vector<DictWord> dictionary_;
  std::vector<int> buckets_[26][26]; // [first][last] -> dictionary indices

  // Neighbor map for spatial tolerance (precomputed)
  std::unordered_map<char, std::vector<char>> neighbors_;

  // ---- Internal algorithms ----

  // Find best matching key for a point
  const Key *findBestKey(const Point &pt) const;
  const Key *findKeyById(const std::string &id) const;

  // Get shortlist of dictionary indices matching first/last char
  std::vector<int> getShortlist(const std::string &keys) const;

  // Scoring components
  int levenshtein(const std::string &s1, const std::string &s2, int limit);
  int countBigramOverlap(const std::string &keys, const std::string &word);
  double computeSpatialScore(const std::string &keys, const std::string &word);
  double scoreCandidate(const std::string &keys, const DictWord &dw);

  // Build neighbor map from layout
  void buildNeighborMap();
};

} // namespace swipe
