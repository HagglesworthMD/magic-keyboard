#pragma once

/**
 * SHARK2 Swipe Typing Algorithm Implementation
 * Based on: Kristensson & Zhai, UIST 2004
 * "SHARK2: A Large Vocabulary Shorthand Writing System for Pen-Based Computers"
 *
 * Key concepts:
 * - Shape channel: scale/translation invariant gesture matching
 * - Location channel: absolute position matching
 * - Uniform sampling to fixed points
 * - Start/end key pruning
 * - Frequency-weighted scoring
 */

#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace shark2 {

// ============================================================================
// Configuration
// ============================================================================
namespace config {
constexpr int SAMPLE_POINTS = 100;        // Uniform sampling target
constexpr double SHAPE_WEIGHT = 0.5;      // Weight for shape channel
constexpr double LOCATION_WEIGHT = 0.5;   // Weight for location channel
constexpr double FREQUENCY_WEIGHT = 0.3;  // Weight for word frequency
constexpr int MAX_CANDIDATES = 8;         // Maximum results to return
constexpr double PRUNING_RADIUS = 40.0;   // Pixels tolerance for start/end
constexpr int LENGTH_TOLERANCE = 3;       // Word length tolerance
} // namespace config

// ============================================================================
// Point Structure
// ============================================================================
struct Point {
    double x = 0;
    double y = 0;

    Point() = default;
    Point(double x_, double y_) : x(x_), y(y_) {}

    double distance(const Point& other) const {
        double dx = x - other.x;
        double dy = y - other.y;
        return std::sqrt(dx * dx + dy * dy);
    }

    Point operator+(const Point& o) const { return {x + o.x, y + o.y}; }
    Point operator-(const Point& o) const { return {x - o.x, y - o.y}; }
    Point operator*(double s) const { return {x * s, y * s}; }
    Point operator/(double s) const { return {x / s, y / s}; }
};

// ============================================================================
// Gesture Template (precomputed for each word)
// ============================================================================
struct GestureTemplate {
    std::string word;
    uint32_t frequencyRank;  // Lower = more common

    // Raw template points (connecting letter centers)
    std::vector<Point> rawPoints;

    // Uniformly sampled points
    std::vector<Point> sampledPoints;

    // Normalized shape (centroid at origin, unit scale)
    std::vector<Point> normalizedShape;

    // For pruning
    char firstChar;
    char lastChar;
    Point startPoint;
    Point endPoint;
};

// ============================================================================
// Candidate Result
// ============================================================================
struct Candidate {
    std::string word;
    double score;
    double shapeDistance;
    double locationDistance;
    double frequencyScore;

    Candidate() : score(0), shapeDistance(0), locationDistance(0), frequencyScore(0) {}
};

// ============================================================================
// SHARK2 Engine
// ============================================================================
class Shark2Engine {
public:
    Shark2Engine();

    // Initialize with keyboard dimensions
    void setKeyboardSize(int width, int height);

    // Load dictionary (line number = frequency rank)
    bool loadDictionary(const std::string& path);

    // Load dictionary from raw word list with separate frequency
    bool loadDictionaryWithFrequency(const std::vector<std::pair<std::string, uint32_t>>& words);

    // Main recognition function
    std::vector<Candidate> recognize(
        const std::vector<Point>& inputPoints,
        int maxCandidates = config::MAX_CANDIDATES
    );

    // Alternative API matching user request
    std::vector<std::pair<std::string, float>> recognize(
        const std::vector<std::pair<float, float>>& points,
        int maxCandidates = config::MAX_CANDIDATES
    );

    // Get key center for a character
    Point getKeyCenter(char c) const;

    // Accessors
    size_t getTemplateCount() const { return templates_.size(); }

private:
    // Keyboard layout
    int keyboardWidth_ = 580;
    int keyboardHeight_ = 200;
    std::unordered_map<char, Point> keyCenters_;

    // Templates
    std::vector<GestureTemplate> templates_;

    // Pruning buckets [first][last]
    std::vector<size_t> buckets_[26][26];

    // ---- Core SHARK2 Algorithm ----

    // Generate template for a word
    GestureTemplate generateTemplate(const std::string& word, uint32_t freq);

    // Uniform sampling to N points
    std::vector<Point> uniformSample(const std::vector<Point>& points, int n);

    // Normalize gesture (centroid at origin, unit scale)
    std::vector<Point> normalizeShape(const std::vector<Point>& points);

    // Compute path length
    double pathLength(const std::vector<Point>& points);

    // Compute centroid
    Point centroid(const std::vector<Point>& points);

    // ---- Distance Metrics ----

    // Shape channel distance (scale/translation invariant)
    double shapeDistance(const std::vector<Point>& a, const std::vector<Point>& b);

    // Location channel distance (absolute position)
    double locationDistance(const std::vector<Point>& a, const std::vector<Point>& b);

    // ---- Pruning ----

    // Get candidate templates based on start/end points
    std::vector<size_t> pruneByStartEnd(const Point& start, const Point& end, int inputLen);

    // Initialize QWERTY key positions
    void initializeKeyboard();

    // Convert frequency rank to score (log scale)
    double frequencyToScore(uint32_t rank);
};

} // namespace shark2
