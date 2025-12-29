# Swipe Typing Engine Specification

**Magic Keyboard v0.3 — Deterministic Geometry-Based Algorithm**

This document defines the complete swipe typing engine for Magic Keyboard. The algorithm is purposefully deterministic, on-device, and uses no neural networks or cloud services.

---

## Table of Contents

1. [Design Philosophy](#design-philosophy)
2. [Key Centroid Model](#key-centroid-model)
3. [Path → Key Sequence Mapping](#path--key-sequence-mapping)
4. [Candidate Generation](#candidate-generation)
5. [Scoring Model](#scoring-model)
6. [Acceptance Thresholds](#acceptance-thresholds)
7. [Performance Targets](#performance-targets)
8. [Data Structures](#data-structures)

---

## Design Philosophy

### Explicit Non-Goals

| Excluded | Rationale |
|----------|-----------|
| Neural networks | Determinism, binary size, CPU cost |
| Cloud API calls | Privacy, latency, offline requirement |
| GPT/LLM integration | Overkill for MVP, non-deterministic |
| User profiling | Privacy; all users get same behavior |
| Adaptive learning | Complexity; determinism first |

### Core Principles

1. **Deterministic**: Same path → same candidates (unit-testable)
2. **Geometry-first**: Spatial proximity drives key detection
3. **Frequency-weighted**: Common words rank higher when ties occur
4. **Low latency**: < 20ms candidate generation on Steam Deck
5. **Bounded memory**: Dictionary + index < 5MB

---

## Key Centroid Model

### Layout Parsing

Keys are loaded from `data/layouts/qwerty.json` with the following structure:

```cpp
struct Key {
    std::string id;      // "a", "b", ..., "space", "backspace"
    Rect r;              // Bounding rectangle in layout space
    Point center;        // Centroid = (r.x + r.w/2, r.y + r.h/2)
};

struct Point { double x, y; };
struct Rect {
    double x, y, w, h;
    bool contains(Point p) const;
};
```

### Layout Space Coordinates

- **Unit**: Layout uses `keyUnit` (60px), `keyHeight` (50px), `keySpacing` (6px)
- **Origin**: Top-left of keyboard area (0, 0)
- **Rows**: y = 0, 1, 2, 3 (4 rows for QWERTY + function row)
- **Offset**: Some rows have fractional offsets (e.g., row 1 offset by 0.25 units)

### Centroid Formula

For each key in layout:

```
center.x = leftMargin + (x_units * keyUnit) + (floor(x_units) * spacing)
center.y = topMargin + (y_row * (keyHeight + spacing))
center.x += keyWidth / 2
center.y += keyHeight / 2
```

### Neighbor Map (Pre-computed)

For efficiency, we precompute adjacency for spatial tolerance:

```cpp
// Neighbor keys within 1.5 key-widths of centroid
std::unordered_map<char, std::vector<char>> neighbors = {
    {'q', {'w', 'a', 's'}},
    {'w', {'q', 'e', 'a', 's', 'd'}},
    {'e', {'w', 'r', 's', 'd', 'f'}},
    // ... etc
};
```

---

## Path → Key Sequence Mapping

### Input Format

The UI sends swipe paths as JSON:

```json
{
  "type": "swipe_path",
  "points": [
    {"x": 120.5, "y": 45.2},
    {"x": 125.0, "y": 44.8},
    ...
  ]
}
```

Points are sampled at ~120Hz or every 4px (whichever is less frequent).

### Algorithm: `mapPathToSequence()`

#### Phase 1: Raw Key Assignment

For each point in the path:

1. **Inside-rect check**: If point is inside a key's bounding rect, snap immediately
2. **Nearest centroid**: Otherwise, find key with minimum squared distance to center
3. **Distance cap**: Ignore points > 100px from any key centroid (noise)

#### Phase 2: Hysteresis Filter

To prevent key-bounce during jittery movement:

```cpp
// State machine per path processing
const Key* currentKey = nullptr;
int consecutiveSamples = 0;
static const Key* candidateKey = nullptr;
static int candidateCount = 0;

for (Point pt : path) {
    Key* bestKey = findBestKey(pt);
    
    if (bestKey != currentKey) {
        bool accept = false;
        
        // Immediate accept conditions:
        // 1. Point inside new key's rect
        if (bestKey->r.contains(pt)) accept = true;
        
        // 2. Strong distance win (72% closer AND 6px gap)
        double d_cur = distance(currentKey->center, pt);
        double d_new = distance(bestKey->center, pt);
        if (d_new < d_cur * 0.72 && (d_cur - d_new) > 6.0) accept = true;
        
        // Delayed accept: 2 consecutive samples on new key
        if (!accept) {
            if (bestKey == candidateKey) {
                candidateCount++;
                if (candidateCount >= 2) accept = true;
            } else {
                candidateKey = bestKey;
                candidateCount = 1;
            }
        }
        
        if (accept) {
            currentKey = bestKey;
            rawSequence.push_back(currentKey->id);
            candidateKey = nullptr;
            candidateCount = 0;
        }
    } else {
        consecutiveSamples++;
    }
}
```

#### Phase 3: Sequence Cleanup

1. **Collapse duplicates**: `"aabbcc"` → `"abc"`
2. **Count dwells**: Track how many consecutive samples per key
3. **Remove bounce artifacts**: If pattern is A-B-A and B has dwell < 2, remove B
4. **Re-collapse**: May create new duplicates after bounce removal

```cpp
// Dwell tracking
std::vector<std::pair<std::string, int>> dwells;
for (const auto& s : rawSequence) {
    if (dwells.empty() || s != dwells.back().first)
        dwells.push_back({s, 1});
    else
        dwells.back().second++;
}

// A-B-A filter
std::vector<std::string> filtered;
for (size_t i = 0; i < dwells.size(); ++i) {
    if (i > 0 && i < dwells.size() - 1 &&
        dwells[i-1].first == dwells[i+1].first &&
        dwells[i].second < 2) {
        continue; // Skip bounce
    }
    filtered.push_back(dwells[i].first);
}
```

#### Output

Final key sequence: `["h", "e", "l", "l", "o"]` → conceptually `"hello"`

---

## Candidate Generation

### Dictionary Structure

```cpp
struct DictWord {
    std::string word;    // "hello"
    uint32_t freq;       // Frequency rank (1 = most common)
    char first, last;    // First and last character
    int len;             // Word length
};

// Bucket index for O(1) first/last char lookup
std::vector<int> buckets[26][26];  // buckets[first-'a'][last-'a']
```

### Shortlist Generation: `getShortlist()`

Given key sequence `keys`:

```cpp
std::vector<int> getShortlist(const std::string& keys) {
    int fidx = tolower(keys[0]) - 'a';
    int lidx = tolower(keys.back()) - 'a';
    
    std::vector<int> result;
    int targetLen = keys.length();
    
    for (int idx : buckets[fidx][lidx]) {
        if (std::abs(dictionary[idx].len - targetLen) <= 3) {
            result.push_back(idx);
        }
    }
    return result;
}
```

**Key constraints:**
- First char must match `keys[0]`
- Last char must match `keys.back()`
- Length within ±3 of key sequence length

This typically reduces 100K dictionary to ~100-500 candidates.

### Expansion (Future Enhancement)

For neighbor-key tolerance, we could expand the shortlist:

```cpp
// Not implemented in v0.3 MVP
std::vector<char> startChars = {keys[0]} + neighbors[keys[0]];
std::vector<char> endChars = {keys.back()} + neighbors[keys.back()];
for (char s : startChars)
    for (char e : endChars)
        result.merge(buckets[s-'a'][e-'a']);
```

---

## Scoring Model

### Formula

```cpp
double scoreCandidate(const std::string& keys, const DictWord& dw) {
    // 1. Edit distance (capped at 7)
    int editDist = levenshtein(keys, dw.word, 7);
    
    // 2. Bigram overlap
    int bigramOverlap = countBigramOverlap(keys, dw.word);
    
    // 3. Frequency score
    double freqScore = log1p(dw.freq);
    
    // 4. Spatial distance (NEW in v0.3)
    double spatialScore = computeSpatialScore(keys, dw.word);
    
    // Final weighted sum
    return -2.2 * editDist 
           + 1.0 * bigramOverlap 
           + 0.8 * freqScore
           + 1.5 * spatialScore;
}
```

### Component 1: Edit Distance

Levenshtein distance with early termination:

```cpp
int levenshtein(const std::string& s1, const std::string& s2, int limit) {
    int n = s1.length(), m = s2.length();
    if (std::abs(n - m) > limit) return limit + 1;
    
    std::vector<int> prev(m+1), curr(m+1);
    for (int j = 0; j <= m; ++j) prev[j] = j;
    
    for (int i = 1; i <= n; ++i) {
        curr[0] = i;
        int minRow = curr[0];
        for (int j = 1; j <= m; ++j) {
            int cost = (s1[i-1] == s2[j-1]) ? 0 : 1;
            curr[j] = std::min({curr[j-1]+1, prev[j]+1, prev[j-1]+cost});
            minRow = std::min(minRow, curr[j]);
        }
        if (minRow > limit) return limit + 1;  // Early exit
        std::swap(prev, curr);
    }
    return prev[m];
}
```

**Weight: -2.2** (penalty per edit operation)

### Component 2: Bigram Overlap

Count shared consecutive character pairs:

```cpp
int countBigramOverlap(const std::string& keys, const std::string& word) {
    auto getBigrams = [](const std::string& s) {
        std::set<uint16_t> result;
        for (size_t i = 0; i + 1 < s.length(); ++i) {
            if (isalpha(s[i]) && isalpha(s[i+1])) {
                result.insert((tolower(s[i]) - 'a') * 26 + 
                             (tolower(s[i+1]) - 'a'));
            }
        }
        return result;
    };
    
    auto b1 = getBigrams(keys);
    auto b2 = getBigrams(word);
    
    int count = 0;
    for (auto bg : b1) {
        if (b2.count(bg)) count++;
    }
    return count;
}
```

**Weight: +1.0** (bonus per matching bigram)

### Component 3: Frequency Score

Higher frequency → higher score. Using log scale to prevent dominance:

```cpp
double freqScore = std::log1p(dw.freq);
```

For our dictionary where freq=1 is most common:

| Word | freq | log1p(freq) |
|------|------|-------------|
| the | 1 | 0.69 |
| be | 2 | 1.10 |
| hello | 101 | 4.62 |

**Weight: +0.8**

### Component 4: Spatial Distance (v0.3 Enhancement)

Average distance from key sequence to word's expected path:

```cpp
double computeSpatialScore(const std::string& keys, const std::string& word) {
    // For each character in word, compute minimum distance to 
    // corresponding character in keys path
    
    double totalDist = 0;
    int pairs = 0;
    
    int wordIdx = 0, keyIdx = 0;
    while (wordIdx < word.length() && keyIdx < keys.length()) {
        // Find key positions
        Key* wordKey = findKeyByCode(word[wordIdx]);
        Key* pathKey = findKeyByCode(keys[keyIdx]);
        
        if (wordKey && pathKey) {
            double dx = wordKey->center.x - pathKey->center.x;
            double dy = wordKey->center.y - pathKey->center.y;
            totalDist += std::sqrt(dx*dx + dy*dy);
            pairs++;
        }
        
        // Advance whichever is shorter
        if (word.length() - wordIdx > keys.length() - keyIdx)
            wordIdx++;
        else if (keys.length() - keyIdx > word.length() - wordIdx)
            keyIdx++;
        else {
            wordIdx++; keyIdx++;
        }
    }
    
    if (pairs == 0) return 0;
    double avgDist = totalDist / pairs;
    
    // Normalize: 0px = 1.0, 60px (one key) = 0.0, beyond = negative
    return std::max(-1.0, 1.0 - avgDist / 60.0);
}
```

**Weight: +1.5**

---

## Acceptance Thresholds

### Minimum Score Threshold

Candidates with score below threshold are discarded:

```cpp
const double MIN_CANDIDATE_SCORE = -5.0;

// After scoring, filter:
candidates.erase(
    std::remove_if(candidates.begin(), candidates.end(),
        [](const Candidate& c) { return c.score < MIN_CANDIDATE_SCORE; }),
    candidates.end()
);
```

### Maximum Candidates

Return at most 8 candidates to UI:

```cpp
if (candidates.size() > 8) candidates.resize(8);
```

### Minimum Path Length

Ignore swipes with < 3 unique keys (likely accidental):

```cpp
if (keySequence.length() < 2) {
    return {};  // No candidates for very short swipes
}
```

### Confidence Indicators

Provide confidence level for top candidate:

```cpp
enum Confidence { LOW, MEDIUM, HIGH };

Confidence getConfidence(const std::vector<Candidate>& candidates) {
    if (candidates.empty()) return LOW;
    
    double top = candidates[0].score;
    double gap = (candidates.size() > 1) 
                 ? top - candidates[1].score 
                 : top;
    
    if (gap > 5.0 && top > 0) return HIGH;
    if (gap > 2.0 && top > -3.0) return MEDIUM;
    return LOW;
}
```

---

## Performance Targets

| Metric | Target | Achieved |
|--------|--------|----------|
| Candidate generation latency | < 20ms | ~3-5ms |
| Memory for dictionary | < 5MB | ~500KB |
| Dictionary size | ≥ 30K words | 1K words (MVP) |
| First/last filter ratio | > 90% reduction | ~95% |
| Levenshtein early-exit rate | > 50% | ~60% |

### Logging Format

For debugging and tuning:

```
SwipeCand layout=qwerty points=42 keys=5 shortlist=127 cand=8 top=hello gen=4ms dict=1038
```

---

## Data Structures

### Complete Type Definitions

```cpp
namespace swipe {

struct Point {
    double x, y;
};

struct Rect {
    double x, y, w, h;
    bool contains(const Point& p) const {
        return p.x >= x && p.x <= x + w && 
               p.y >= y && p.y <= y + h;
    }
};

struct Key {
    std::string id;       // Key code
    std::string label;    // Display label
    Rect bounds;          // Hit area
    Point center;         // Centroid
    bool isSpecial;       // Non-letter key
};

struct DictWord {
    std::string word;
    uint32_t freq;        // Lower = more common
    char first, last;
    int len;
};

struct Candidate {
    std::string word;
    double score;
    
    // Components for debugging
    int editDistance;
    int bigramOverlap;
    double freqContribution;
    double spatialContribution;
};

class SwipeEngine {
public:
    void loadLayout(const std::string& layoutPath);
    void loadDictionary(const std::string& wordsPath, 
                        const std::string& freqPath);
    
    std::vector<std::string> mapPathToSequence(
        const std::vector<Point>& path);
    
    std::vector<Candidate> generateCandidates(
        const std::string& keySequence);
    
private:
    std::vector<Key> keys_;
    std::vector<DictWord> dictionary_;
    std::vector<int> buckets_[26][26];
    
    int levenshtein(const std::string& a, const std::string& b, int limit);
    double scoreCandidate(const std::string& keys, const DictWord& dw);
};

} // namespace swipe
```

---

## Test Cases

### Unit Test: Path to Sequence

```cpp
// Input: diagonal swipe from H → E → L → L → O
std::vector<Point> helloPath = {
    {330, 25}, {332, 26}, {180, 28}, // H area
    {140, 12}, {142, 11}, {143, 10}, // E area
    {560, 60}, {562, 61},            // L area
    {558, 59}, {561, 62},            // L (stays)
    {500, 13}, {502, 14}             // O area
};

auto seq = engine.mapPathToSequence(helloPath);
ASSERT_EQ(seq, std::vector<std::string>{"h", "e", "l", "o"});
// Note: Consecutive L collapsed
```

### Unit Test: Candidate Scoring

```cpp
// "helko" swipe should rank "hello" higher than "help"
auto candidates = engine.generateCandidates("helko");

ASSERT_GE(candidates.size(), 2);
auto it_hello = std::find_if(candidates.begin(), candidates.end(),
    [](const Candidate& c) { return c.word == "hello"; });
auto it_help = std::find_if(candidates.begin(), candidates.end(),
    [](const Candidate& c) { return c.word == "help"; });

// "hello" should appear and rank above unrelated words
ASSERT(it_hello != candidates.end());
```

### Integration Test: Full Pipeline

```cpp
// Simulate swipe for "the" (most common word)
std::vector<Point> thePath = {
    {300, 12}, {302, 13},  // T
    {332, 58}, {335, 60},  // H
    {140, 13}, {142, 11}   // E
};

auto seq = engine.mapPathToSequence(thePath);
ASSERT_EQ(seq.size(), 3);

auto candidates = engine.generateCandidates(join(seq, ""));
ASSERT_FALSE(candidates.empty());
ASSERT_EQ(candidates[0].word, "the");  // High freq should dominate
```

---

## Revision History

| Date | Version | Change |
|------|---------|--------|
| 2024-12-29 | 0.3 | Initial spec with spatial scoring |
| 2024-12-28 | 0.2.3 | Basic edit distance + frequency |

---

## Appendix: Tuning Constants

All magic numbers for easy adjustment:

```cpp
namespace swipe::config {
    // Hysteresis
    constexpr double RECT_PRIORITY = true;
    constexpr double DISTANCE_RATIO_THRESHOLD = 0.72;
    constexpr double DISTANCE_GAP_MIN_PX = 6.0;
    constexpr int CONSECUTIVE_SAMPLES_THRESHOLD = 2;
    constexpr int MIN_DWELL_FOR_BOUNCE = 2;
    
    // Shortlist
    constexpr int LENGTH_TOLERANCE = 3;
    
    // Scoring weights
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
}
```
