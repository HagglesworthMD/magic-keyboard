/**
 * SHARK2 Swipe Typing Algorithm Implementation
 * Based on: Kristensson & Zhai, UIST 2004
 */

#include "shark2.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <sstream>

namespace shark2 {

// ============================================================================
// Constructor
// ============================================================================
Shark2Engine::Shark2Engine() { initializeKeyboard(); }

// ============================================================================
// Keyboard Layout Initialization
// ============================================================================
void Shark2Engine::initializeKeyboard() {
  // KeyboardWindowV2 layout with centered rows
  // Base window: 720px wide
  // Grid unit: 52px, key height: 42px, key gap: 6px
  // Rows are Qt.AlignHCenter within the window

  const double keyW = 52.0;               // gridUnit
  const double keyH = 42.0;               // keyHeight
  const double spacing = 6.0;             // keyGap
  const double keyPitch = keyW + spacing; // 58px per key
  const double windowW = 720.0;           // Base window width

  const double rowSpacing = keyH + spacing; // 48px

  // Row positions (Y)
  const double row0_y = 0;              // QWERTYUIOP
  const double row1_y = 1 * rowSpacing; // ASDFGHJKL
  const double row2_y = 2 * rowSpacing; // ZXCVBNM

  // Row 0: QWERTYUIOP + Backspace (10 + 1.5 = 11.5 units)
  // Row width = 10 * 58 + 1.5 * 58 = 11.5 * 58 = 667px
  double row0_width = 10 * keyPitch + 1.5 * keyW;    // Letters only for swipe
  double row0_startX = (windowW - row0_width) / 2.0; // Centering offset
  const char *row0 = "qwertyuiop";
  for (int i = 0; i < 10; i++) {
    double cx = row0_startX + i * keyPitch + keyW / 2.0;
    double cy = row0_y + keyH / 2.0;
    keyCenters_[row0[i]] = Point(cx, cy);
  }

  // Row 1: ASDFGHJKL + Enter (9 + 1.5 = 10.5 units)
  double row1_width = 9 * keyPitch + 1.5 * keyW;
  double row1_startX = (windowW - row1_width) / 2.0;
  const char *row1 = "asdfghjkl";
  for (int i = 0; i < 9; i++) {
    double cx = row1_startX + i * keyPitch + keyW / 2.0;
    double cy = row1_y + keyH / 2.0;
    keyCenters_[row1[i]] = Point(cx, cy);
  }

  // Row 2: Shift + ZXCVBNM + , + . (1.5 + 7 + 1 + 1 = 10.5 units)
  double row2_width = 1.5 * keyW + 7 * keyPitch + 2 * keyPitch;
  double row2_startX = (windowW - row2_width) / 2.0;
  double row2_lettersX = row2_startX + 1.5 * keyW + spacing; // After 1.5u shift
  const char *row2 = "zxcvbnm";
  for (int i = 0; i < 7; i++) {
    double cx = row2_lettersX + i * keyPitch + keyW / 2.0;
    double cy = row2_y + keyH / 2.0;
    keyCenters_[row2[i]] = Point(cx, cy);
  }
}

void Shark2Engine::setKeyboardSize(int width, int height) {
  keyboardWidth_ = width;
  keyboardHeight_ = height;
  // Could rescale key positions here if needed
}

Point Shark2Engine::getKeyCenter(char c) const {
  c = std::tolower(c);
  auto it = keyCenters_.find(c);
  if (it != keyCenters_.end()) {
    return it->second;
  }
  return Point(0, 0);
}

void Shark2Engine::setKeyCenter(char c, double x, double y) {
  keyCenters_[std::tolower(c)] = Point(x, y);
}

// ============================================================================
// Dictionary Loading
// ============================================================================
bool Shark2Engine::loadDictionary(const std::string &path) {
  std::ifstream file(path);
  if (!file.is_open()) {
    return false;
  }

  templates_.clear();
  for (auto &row : buckets_) {
    for (auto &bucket : row) {
      bucket.clear();
    }
  }

  std::string line;
  uint32_t rank = 1;

  while (std::getline(file, line)) {
    // Trim whitespace
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n' ||
                             line.back() == ' ' || line.back() == '\t')) {
      line.pop_back();
    }
    while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) {
      line.erase(0, 1);
    }

    // Skip empty lines and lines with non-alpha characters
    if (line.empty())
      continue;

    bool valid = true;
    for (char c : line) {
      if (!std::isalpha(c)) {
        valid = false;
        break;
      }
    }
    if (!valid || line.length() < 2) {
      rank++; // Still count for frequency
      continue;
    }

    // Convert to lowercase
    std::string word;
    for (char c : line) {
      word += std::tolower(c);
    }

    // Generate template
    GestureTemplate tmpl = generateTemplate(word, rank);
    size_t idx = templates_.size();
    templates_.push_back(std::move(tmpl));

    // Add to bucket
    int fi = word.front() - 'a';
    int li = word.back() - 'a';
    if (fi >= 0 && fi < 26 && li >= 0 && li < 26) {
      buckets_[fi][li].push_back(idx);
    }

    rank++;
  }

  return !templates_.empty();
}

bool Shark2Engine::loadDictionaryWithFrequency(
    const std::vector<std::pair<std::string, uint32_t>> &words) {

  templates_.clear();
  for (auto &row : buckets_) {
    for (auto &bucket : row) {
      bucket.clear();
    }
  }

  for (const auto &[word, freq] : words) {
    if (word.length() < 2)
      continue;

    // Validate all alpha
    bool valid = true;
    for (char c : word) {
      if (!std::isalpha(c)) {
        valid = false;
        break;
      }
    }
    if (!valid)
      continue;

    // Lowercase
    std::string lword;
    for (char c : word) {
      lword += std::tolower(c);
    }

    GestureTemplate tmpl = generateTemplate(lword, freq);
    size_t idx = templates_.size();
    templates_.push_back(std::move(tmpl));

    int fi = lword.front() - 'a';
    int li = lword.back() - 'a';
    if (fi >= 0 && fi < 26 && li >= 0 && li < 26) {
      buckets_[fi][li].push_back(idx);
    }
  }

  return !templates_.empty();
}

// ============================================================================
// Template Generation
// ============================================================================
GestureTemplate Shark2Engine::generateTemplate(const std::string &word,
                                               uint32_t freq) {
  GestureTemplate tmpl;
  tmpl.word = word;
  tmpl.frequencyRank = freq;
  tmpl.firstChar = word.front();
  tmpl.lastChar = word.back();

  // Generate raw points by connecting letter centers
  for (char c : word) {
    Point p = getKeyCenter(c);
    if (p.x != 0 || p.y != 0) { // Valid key
      tmpl.rawPoints.push_back(p);
    }
  }

  if (tmpl.rawPoints.empty()) {
    return tmpl;
  }

  tmpl.startPoint = tmpl.rawPoints.front();
  tmpl.endPoint = tmpl.rawPoints.back();

  // Uniform sampling
  tmpl.sampledPoints = uniformSample(tmpl.rawPoints, config::SAMPLE_POINTS);

  // Normalize for shape channel
  tmpl.normalizedShape = normalizeShape(tmpl.sampledPoints);

  return tmpl;
}

// ============================================================================
// Path Utilities
// ============================================================================
double Shark2Engine::pathLength(const std::vector<Point> &points) {
  if (points.size() < 2)
    return 0;

  double len = 0;
  for (size_t i = 1; i < points.size(); i++) {
    len += points[i - 1].distance(points[i]);
  }
  return len;
}

Point Shark2Engine::centroid(const std::vector<Point> &points) {
  if (points.empty())
    return Point(0, 0);

  double sumX = 0, sumY = 0;
  for (const auto &p : points) {
    sumX += p.x;
    sumY += p.y;
  }
  return Point(sumX / points.size(), sumY / points.size());
}

// ============================================================================
// Uniform Sampling (SHARK2 Stage 1)
// ============================================================================
std::vector<Point> Shark2Engine::uniformSample(const std::vector<Point> &points,
                                               int n) {
  if (points.empty())
    return {};
  if (points.size() == 1) {
    return std::vector<Point>(n, points[0]);
  }

  std::vector<Point> result;
  result.reserve(n);

  double totalLen = pathLength(points);
  if (totalLen < 1e-9) {
    // All points same location
    return std::vector<Point>(n, points[0]);
  }

  double interval = totalLen / (n - 1);
  double accumulated = 0;

  result.push_back(points[0]);
  size_t j = 1;

  for (size_t i = 1;
       i < points.size() && result.size() < static_cast<size_t>(n); i++) {
    double segLen = points[i - 1].distance(points[i]);

    while (accumulated + segLen >= interval * j &&
           result.size() < static_cast<size_t>(n)) {
      double t = (interval * j - accumulated) / segLen;
      t = std::max(0.0, std::min(1.0, t));

      Point interp;
      interp.x = points[i - 1].x + t * (points[i].x - points[i - 1].x);
      interp.y = points[i - 1].y + t * (points[i].y - points[i - 1].y);
      result.push_back(interp);
      j++;
    }
    accumulated += segLen;
  }

  // Ensure we have exactly n points
  while (result.size() < static_cast<size_t>(n)) {
    result.push_back(points.back());
  }

  return result;
}

// ============================================================================
// Shape Normalization (SHARK2 Shape Channel)
// ============================================================================
std::vector<Point>
Shark2Engine::normalizeShape(const std::vector<Point> &points) {
  if (points.empty())
    return {};

  // 1. Translate centroid to origin
  Point c = centroid(points);
  std::vector<Point> centered;
  centered.reserve(points.size());
  for (const auto &p : points) {
    centered.push_back(p - c);
  }

  // 2. Scale to unit size (using bounding box diagonal or path length)
  double maxDist = 0;
  for (const auto &p : centered) {
    double d = std::sqrt(p.x * p.x + p.y * p.y);
    maxDist = std::max(maxDist, d);
  }

  if (maxDist < 1e-9) {
    return centered; // Degenerate case
  }

  std::vector<Point> normalized;
  normalized.reserve(centered.size());
  for (const auto &p : centered) {
    normalized.push_back(p / maxDist);
  }

  return normalized;
}

// ============================================================================
// Distance Metrics
// ============================================================================
double Shark2Engine::shapeDistance(const std::vector<Point> &a,
                                   const std::vector<Point> &b) {
  if (a.size() != b.size() || a.empty()) {
    return std::numeric_limits<double>::max();
  }

  // Average Euclidean distance between corresponding points
  double sum = 0;
  for (size_t i = 0; i < a.size(); i++) {
    sum += a[i].distance(b[i]);
  }
  return sum / a.size();
}

double Shark2Engine::locationDistance(const std::vector<Point> &a,
                                      const std::vector<Point> &b) {
  if (a.size() != b.size() || a.empty()) {
    return std::numeric_limits<double>::max();
  }

  // Average Euclidean distance (non-normalized)
  double sum = 0;
  for (size_t i = 0; i < a.size(); i++) {
    sum += a[i].distance(b[i]);
  }
  return sum / a.size();
}

// ============================================================================
// Pruning (SHARK2 Stage 2)
// ============================================================================
std::vector<size_t> Shark2Engine::pruneByStartEnd(const Point &start,
                                                  const Point &end,
                                                  int inputLen) {
  std::vector<size_t> candidates;

  // Find keys near start and end points
  std::vector<char> startKeys;
  std::vector<char> endKeys;

  // Debug: find closest key to start and end
  char closestStart = '?';
  char closestEnd = '?';
  double minStartDist = 1e9;
  double minEndDist = 1e9;

  for (const auto &[c, center] : keyCenters_) {
    double distToStart = start.distance(center);
    double distToEnd = end.distance(center);

    if (distToStart < minStartDist) {
      minStartDist = distToStart;
      closestStart = c;
    }
    if (distToEnd < minEndDist) {
      minEndDist = distToEnd;
      closestEnd = c;
    }

    if (distToStart <= config::PRUNING_RADIUS) {
      startKeys.push_back(c);
    }
    if (distToEnd <= config::PRUNING_RADIUS) {
      endKeys.push_back(c);
    }
  }

  // If no keys found near start/end, use closest
  if (startKeys.empty()) {
    startKeys.push_back(closestStart);
  }

  if (endKeys.empty()) {
    endKeys.push_back(closestEnd);
  }

  // Add neighbor keys to expand search for better accuracy
  // QWERTY adjacency map
  static const std::unordered_map<char, std::string> neighbors = {
      {'q', "wa"},     {'w', "qase"},   {'e', "wsdr"},   {'r', "edft"},
      {'t', "rfgy"},   {'y', "tghu"},   {'u', "yhji"},   {'i', "ujko"},
      {'o', "iklp"},   {'p', "ol"},     {'a', "qwsz"},   {'s', "awedxz"},
      {'d', "serfcx"}, {'f', "drtgvc"}, {'g', "ftyhbv"}, {'h', "gyujnb"},
      {'j', "huikmn"}, {'k', "jiolm"},  {'l', "kop"},    {'z', "asx"},
      {'x', "zsdc"},   {'c', "xdfv"},   {'v', "cfgb"},   {'b', "vghn"},
      {'n', "bhjm"},   {'m', "njk"}};

  std::vector<char> expandedStart = startKeys;
  std::vector<char> expandedEnd = endKeys;

  for (char c : startKeys) {
    if (neighbors.count(c)) {
      for (char n : neighbors.at(c)) {
        if (std::find(expandedStart.begin(), expandedStart.end(), n) ==
            expandedStart.end()) {
          expandedStart.push_back(n);
        }
      }
    }
  }

  for (char c : endKeys) {
    if (neighbors.count(c)) {
      for (char n : neighbors.at(c)) {
        if (std::find(expandedEnd.begin(), expandedEnd.end(), n) ==
            expandedEnd.end()) {
          expandedEnd.push_back(n);
        }
      }
    }
  }

  // Use expanded lists for better recall
  startKeys = expandedStart;
  endKeys = expandedEnd;

  // Collect templates from matching buckets
  std::vector<bool> seen(templates_.size(), false);

  for (char sc : startKeys) {
    int fi = sc - 'a';
    if (fi < 0 || fi >= 26)
      continue;

    for (char ec : endKeys) {
      int li = ec - 'a';
      if (li < 0 || li >= 26)
        continue;

      for (size_t idx : buckets_[fi][li]) {
        if (seen[idx])
          continue;

        const auto &tmpl = templates_[idx];
        int lenDiff = std::abs(static_cast<int>(tmpl.word.length()) - inputLen);
        if (lenDiff <= config::LENGTH_TOLERANCE) {
          candidates.push_back(idx);
          seen[idx] = true;
        }
      }
    }
  }

  return candidates;
}

// ============================================================================
// Frequency Score
// ============================================================================
double Shark2Engine::frequencyToScore(uint32_t rank) {
  // Log scale: rank 1 -> high score, rank 10000 -> low score
  if (rank == 0)
    rank = 1;
  return 1.0 / std::log2(rank + 1);
}

// ============================================================================
// Main Recognition (SHARK2 Integration)
// ============================================================================
std::vector<Candidate>
Shark2Engine::recognize(const std::vector<Point> &inputPoints,
                        int maxCandidates) {

  if (inputPoints.size() < 2 || templates_.empty()) {
    return {};
  }

  // Get start/end points for quick checks
  Point start = inputPoints.front();
  Point end = inputPoints.back();

  // Common word fast path - bypass complex matching for very common words
  static const std::vector<std::string> commonWords = {
      "the",  "be",   "to",   "of",   "and",  "a",    "in",   "that",
      "have", "i",    "it",   "for",  "not",  "on",   "with", "he",
      "as",   "you",  "do",   "at",   "this", "but",  "his",  "by",
      "from", "they", "we",   "say",  "her",  "she",  "or",   "an",
      "will", "my",   "one",  "all",  "would", "there", "their"};

  std::vector<Candidate> quickMatches;
  for (const auto &word : commonWords) {
    if (word.length() < 2)
      continue;

    Point expectedStart = getKeyCenter(word[0]);
    Point expectedEnd = getKeyCenter(word.back());

    double startDist = start.distance(expectedStart);
    double endDist = end.distance(expectedEnd);

    // Quick accept if start/end are close
    if (startDist < 60.0 && endDist < 60.0) {
      Candidate c;
      c.word = word;
      c.score = 0.75 - (startDist + endDist) / 300.0; // High base score
      quickMatches.push_back(c);
    }
  }

  // Estimate input word length from gesture
  // Rough heuristic: count direction changes or use path length
  int estimatedLen = std::max(2, static_cast<int>(inputPoints.size() / 10));

  // Stage 1: Uniform sampling
  std::vector<Point> sampled =
      uniformSample(inputPoints, config::SAMPLE_POINTS);

  // Normalize for shape channel
  std::vector<Point> normalizedInput = normalizeShape(sampled);

  // Stage 2: Prune by start/end
  std::vector<size_t> candidateIndices =
      pruneByStartEnd(start, end, estimatedLen);

  // If pruning is too aggressive, expand search
  if (candidateIndices.size() < 10) {
    // Also try with doubled radius or all templates for short words
    estimatedLen = std::max(2, estimatedLen - 1);
    candidateIndices = pruneByStartEnd(start, end, estimatedLen);
  }

  // Stage 3 & 4: Compute distances
  std::vector<Candidate> results;
  results.reserve(candidateIndices.size());

  for (size_t idx : candidateIndices) {
    const auto &tmpl = templates_[idx];
    if (tmpl.normalizedShape.empty())
      continue;

    Candidate cand;
    cand.word = tmpl.word;

    // Shape channel distance
    cand.shapeDistance = shapeDistance(normalizedInput, tmpl.normalizedShape);

    // Location channel distance
    cand.locationDistance = locationDistance(sampled, tmpl.sampledPoints);

    // Frequency score
    cand.frequencyScore = frequencyToScore(tmpl.frequencyRank);

    // Combine scores (lower distance = better, higher freq = better)
    // Convert distances to similarity scores
    double shapeScore = 1.0 / (1.0 + cand.shapeDistance * 10);
    double locationScore = 1.0 / (1.0 + cand.locationDistance / 50.0);

    // Start/end match bonus - reward when input clearly lands on correct keys
    double startEndBonus = 0.0;
    if (tmpl.word.length() > 0) {
      char firstChar = std::tolower(tmpl.word[0]);
      char lastChar = std::tolower(tmpl.word.back());

      double startDist = start.distance(getKeyCenter(firstChar));
      double endDist = end.distance(getKeyCenter(lastChar));

      // Bonus for landing close to expected keys
      if (startDist < 40.0)
        startEndBonus += 0.15;
      if (endDist < 40.0)
        startEndBonus += 0.15;
    }

    // Length-based bonus - longer words are easier to distinguish
    double lengthBonus = std::min(0.2, tmpl.word.length() * 0.03);

    cand.score = config::SHAPE_WEIGHT * shapeScore +
                 config::LOCATION_WEIGHT * locationScore +
                 config::FREQUENCY_WEIGHT * cand.frequencyScore + startEndBonus +
                 lengthBonus;

    results.push_back(cand);
  }

  // Merge quick matches with full results
  for (const auto &qm : quickMatches) {
    // Check if this word is already in results
    bool found = false;
    for (auto &r : results) {
      if (r.word == qm.word) {
        // Keep the better score
        r.score = std::max(r.score, qm.score);
        found = true;
        break;
      }
    }
    if (!found) {
      results.push_back(qm);
    }
  }

  // Sort by score (descending)
  std::sort(
      results.begin(), results.end(),
      [](const Candidate &a, const Candidate &b) { return a.score > b.score; });

  // Return top N
  if (results.size() > static_cast<size_t>(maxCandidates)) {
    results.resize(maxCandidates);
  }

  return results;
}

// Alternative API
std::vector<std::pair<std::string, float>>
Shark2Engine::recognize(const std::vector<std::pair<float, float>> &points,
                        int maxCandidates) {

  std::vector<Point> pts;
  pts.reserve(points.size());
  for (const auto &[x, y] : points) {
    pts.emplace_back(x, y);
  }

  auto candidates = recognize(pts, maxCandidates);

  std::vector<std::pair<std::string, float>> result;
  result.reserve(candidates.size());
  for (const auto &c : candidates) {
    result.emplace_back(c.word, static_cast<float>(c.score));
  }
  return result;
}

} // namespace shark2
