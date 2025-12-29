/**
 * Swipe Engine Test Utility
 *
 * Validates the deterministic swipe typing engine with known test cases.
 * Run: g++ -std=c++17 -I.. swipe_engine_test.cpp swipe_engine.cpp -o swipe_test
 * && ./swipe_test
 */

#include "swipe_engine.h"
#include <cassert>
#include <iostream>
#include <string>

using namespace swipe;

// Test helper: join key sequence to string
std::string joinKeys(const std::vector<std::string> &keys) {
  std::string result;
  for (const auto &k : keys) {
    if (k.length() == 1 && std::isalpha(k[0])) {
      result += k;
    }
  }
  return result;
}

// ANSI colors for output
#define GREEN "\033[32m"
#define RED "\033[31m"
#define YELLOW "\033[33m"
#define RESET "\033[0m"

int testsRun = 0;
int testsPassed = 0;

#define TEST(name)                                                             \
  void test_##name();                                                          \
  struct Register_##name {                                                     \
    Register_##name() { tests.push_back({#name, test_##name}); }               \
  } reg_##name;                                                                \
  void test_##name()

std::vector<std::pair<std::string, void (*)()>> tests;

void runTest(const std::string &name, void (*fn)()) {
  testsRun++;
  try {
    fn();
    testsPassed++;
    std::cout << GREEN << "✓ " << RESET << name << std::endl;
  } catch (const std::exception &e) {
    std::cout << RED << "✗ " << RESET << name << ": " << e.what() << std::endl;
  }
}

#define ASSERT_TRUE(cond)                                                      \
  if (!(cond))                                                                 \
  throw std::runtime_error("Assertion failed: " #cond)
#define ASSERT_EQ(a, b)                                                        \
  if ((a) != (b))                                                              \
  throw std::runtime_error("Assertion failed: " #a " == " #b)
#define ASSERT_GE(a, b)                                                        \
  if ((a) < (b))                                                               \
  throw std::runtime_error("Assertion failed: " #a " >= " #b)
#define ASSERT_LE(a, b)                                                        \
  if ((a) > (b))                                                               \
  throw std::runtime_error("Assertion failed: " #a " <= " #b)

// ============================================================================
// Tests
// ============================================================================

void test_loadLayout() {
  SwipeEngine engine;
  bool ok = engine.loadLayout("../../data/layouts/qwerty.json");
  ASSERT_TRUE(ok);
  ASSERT_GE(engine.getKeyCount(), 26); // At least alphabet
}

void test_loadDictionary() {
  SwipeEngine engine;
  bool ok = engine.loadDictionary("../../data/dict/words.txt",
                                  "../../data/dict/freq.tsv");
  ASSERT_TRUE(ok);
  ASSERT_GE(engine.getDictionarySize(), 100); // MVP dictionary has ~1000 words
}

void test_pathToSequence_simpleSwipe() {
  SwipeEngine engine;
  engine.loadLayout("../../data/layouts/qwerty.json");

  // Simulate a diagonal swipe (doesn't matter exact coords for this test)
  std::vector<Point> path = {
      {60, 25},  // Near 'q' area
      {120, 25}, // Near 'w' area
      {180, 25}, // Near 'e' area
  };

  auto seq = engine.mapPathToSequence(path);
  ASSERT_GE(seq.size(), 1); // Should produce at least one key
}

void test_pathToSequence_singlePoint() {
  SwipeEngine engine;
  engine.loadLayout("../../data/layouts/qwerty.json");

  std::vector<Point> path = {{60, 25}};
  auto seq = engine.mapPathToSequence(path);

  ASSERT_EQ(seq.size(), 1); // Single point = single key
}

void test_pathToSequence_duplicatesCollapsed() {
  SwipeEngine engine;
  engine.loadLayout("../../data/layouts/qwerty.json");

  // Multiple points in same key area should collapse
  std::vector<Point> path = {
      {60, 25},
      {61, 26},
      {62, 27},
      {63, 28}, // All near 'q'
  };

  auto seq = engine.mapPathToSequence(path);
  ASSERT_EQ(seq.size(), 1); // Should collapse to single 'q'
}

void test_pathToSequence_emptyPath() {
  SwipeEngine engine;
  engine.loadLayout("../../data/layouts/qwerty.json");

  std::vector<Point> empty;
  auto seq = engine.mapPathToSequence(empty);

  ASSERT_TRUE(seq.empty());
}

void test_generateCandidates_returnsResults() {
  SwipeEngine engine;
  engine.loadLayout("../../data/layouts/qwerty.json");
  engine.loadDictionary("../../data/dict/words.txt",
                        "../../data/dict/freq.tsv");

  // "the" is in the dictionary
  auto candidates = engine.generateCandidates("the");

  ASSERT_GE(candidates.size(), 1);
  ASSERT_EQ(candidates[0].word, "the"); // Exact match should be top
}

void test_generateCandidates_tooShort() {
  SwipeEngine engine;
  engine.loadLayout("../../data/layouts/qwerty.json");
  engine.loadDictionary("../../data/dict/words.txt",
                        "../../data/dict/freq.tsv");

  // Single char should be filtered (MIN_KEY_SEQUENCE_LENGTH = 2)
  auto candidates = engine.generateCandidates("a");
  ASSERT_TRUE(candidates.empty());
}

void test_generateCandidates_sortedByScore() {
  SwipeEngine engine;
  engine.loadLayout("../../data/layouts/qwerty.json");
  engine.loadDictionary("../../data/dict/words.txt",
                        "../../data/dict/freq.tsv");

  auto candidates = engine.generateCandidates("helko");

  if (candidates.size() >= 2) {
    ASSERT_GE(candidates[0].score, candidates[1].score);
  }
}

void test_generateCandidates_maxEight() {
  SwipeEngine engine;
  engine.loadLayout("../../data/layouts/qwerty.json");
  engine.loadDictionary("../../data/dict/words.txt",
                        "../../data/dict/freq.tsv");

  auto candidates = engine.generateCandidates("help");
  ASSERT_LE(candidates.size(), 8);
}

void test_determinism() {
  SwipeEngine engine1, engine2;
  engine1.loadLayout("../../data/layouts/qwerty.json");
  engine1.loadDictionary("../../data/dict/words.txt",
                         "../../data/dict/freq.tsv");
  engine2.loadLayout("../../data/layouts/qwerty.json");
  engine2.loadDictionary("../../data/dict/words.txt",
                         "../../data/dict/freq.tsv");

  // Same path should give same results
  std::vector<Point> path = {
      {60, 25},
      {120, 25},
      {180, 25},
      {240, 25},
  };

  auto seq1 = engine1.mapPathToSequence(path);
  auto seq2 = engine2.mapPathToSequence(path);

  ASSERT_EQ(seq1.size(), seq2.size());
  for (size_t i = 0; i < seq1.size(); ++i) {
    ASSERT_EQ(seq1[i], seq2[i]);
  }

  auto keys1 = joinKeys(seq1);
  auto keys2 = joinKeys(seq2);

  auto cand1 = engine1.generateCandidates(keys1);
  auto cand2 = engine2.generateCandidates(keys2);

  ASSERT_EQ(cand1.size(), cand2.size());
}

void test_confidence() {
  std::vector<Candidate> empty;
  ASSERT_EQ(static_cast<int>(getConfidence(empty)),
            static_cast<int>(Confidence::LOW));

  std::vector<Candidate> single = {{"the", 10.0}};
  auto conf = getConfidence(single);
  ASSERT_TRUE(conf == Confidence::HIGH || conf == Confidence::MEDIUM);
}

// ============================================================================
// Main
// ============================================================================

int main() {
  std::cout << YELLOW << "\n=== Swipe Engine Tests ===" << RESET << "\n\n";

  runTest("loadLayout", test_loadLayout);
  runTest("loadDictionary", test_loadDictionary);
  runTest("pathToSequence_simpleSwipe", test_pathToSequence_simpleSwipe);
  runTest("pathToSequence_singlePoint", test_pathToSequence_singlePoint);
  runTest("pathToSequence_duplicatesCollapsed",
          test_pathToSequence_duplicatesCollapsed);
  runTest("pathToSequence_emptyPath", test_pathToSequence_emptyPath);
  runTest("generateCandidates_returnsResults",
          test_generateCandidates_returnsResults);
  runTest("generateCandidates_tooShort", test_generateCandidates_tooShort);
  runTest("generateCandidates_sortedByScore",
          test_generateCandidates_sortedByScore);
  runTest("generateCandidates_maxEight", test_generateCandidates_maxEight);
  runTest("determinism", test_determinism);
  runTest("confidence", test_confidence);

  std::cout << "\n"
            << YELLOW << "Results: " << RESET << testsPassed << "/" << testsRun
            << " passed\n\n";

  return (testsPassed == testsRun) ? 0 : 1;
}
