#pragma once

/**
 * Magic Keyboard - Adaptive Learning System
 *
 * Tracks word usage frequencies for personalized candidate ranking.
 * - Unigram: Boost words the user commits often
 * - Bigram: Boost words that follow previously committed words
 *
 * Design Principles:
 * - Learn only on explicit commit
 * - Bounded memory usage
 * - Persisted to small local state file
 * - Safe fallback if data is missing or corrupt
 * - No neural networks, no background training
 */

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace magickeyboard {

// ============================================================================
// Configuration
// ============================================================================

namespace learn_config {
// Maximum number of unigrams to track (oldest entries pruned)
constexpr size_t MAX_UNIGRAMS = 10000;
// Maximum number of bigrams to track
constexpr size_t MAX_BIGRAMS = 5000;
// Weight applied to learned frequency in scoring
constexpr double UNIGRAM_WEIGHT = 2.5;
// Weight for bigram context boost
constexpr double BIGRAM_WEIGHT = 1.8;
// Auto-save interval (number of commits between saves)
constexpr int AUTO_SAVE_INTERVAL = 10;
// Decay factor for old entries (applied on load to fade stale data)
constexpr double DECAY_FACTOR = 0.95;
} // namespace learn_config

// ============================================================================
// User Data Manager
// ============================================================================

class UserDataManager {
public:
  // Get singleton instance
  static UserDataManager &instance();

  // Load user data from disk
  bool load();

  // Save user data to disk
  bool save();

  // Called when a word is explicitly committed by the user
  // previousWord: the word committed just before this one (for bigram)
  void recordCommit(const std::string &word,
                    const std::string &previousWord = "");

  // Get unigram boost score for a word (0.0 if unknown)
  double getUnigramBoost(const std::string &word) const;

  // Get bigram boost score for word given previous context
  double getBigramBoost(const std::string &word,
                        const std::string &previousWord) const;

  // Get combined learning boost for candidate ranking
  double getLearningBoost(const std::string &word,
                          const std::string &previousWord = "") const;

  // Get the last committed word (for bigram context)
  std::string getLastWord() const;

  // Reset all learned data
  void reset();

  // Get stats for debugging
  size_t getUnigramCount() const;
  size_t getBigramCount() const;

private:
  UserDataManager() = default;
  UserDataManager(const UserDataManager &) = delete;
  UserDataManager &operator=(const UserDataManager &) = delete;

  // Get user data file path
  std::string getDataPath() const;

  // Prune old entries if over limit
  void pruneIfNeeded();

  // Apply decay to all entries
  void applyDecay();

  mutable std::mutex mutex_;

  // word -> frequency count
  std::unordered_map<std::string, uint32_t> unigrams_;
  // "prev|curr" -> frequency count
  std::unordered_map<std::string, uint32_t> bigrams_;

  // Last committed word for context
  std::string lastWord_;

  // Dirty flag and commit counter for auto-save
  int commitsSinceLastSave_ = 0;
  bool loaded_ = false;
};

} // namespace magickeyboard
