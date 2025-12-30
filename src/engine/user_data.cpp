/**
 * Magic Keyboard - Adaptive Learning Implementation
 *
 * File format: Simple binary format for efficiency
 * Location: $XDG_DATA_HOME/magic-keyboard/learned.dat
 */

#include "user_data.h"
#include "settings.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

namespace magickeyboard {

// ============================================================================
// Singleton Access
// ============================================================================

UserDataManager &UserDataManager::instance() {
  static UserDataManager instance;
  return instance;
}

// ============================================================================
// Path Resolution
// ============================================================================

std::string UserDataManager::getDataPath() const {
  return SettingsManager::instance().getUserDataDir() + "/learned.dat";
}

// ============================================================================
// Load/Save Operations
// ============================================================================

bool UserDataManager::load() {
  std::lock_guard<std::mutex> lock(mutex_);

  std::ifstream file(getDataPath(), std::ios::binary);
  if (!file.is_open()) {
    // No learned data yet - start fresh
    loaded_ = true;
    return true;
  }

  // Read magic header
  char magic[4];
  file.read(magic, 4);
  if (std::strncmp(magic, "MKLD", 4) != 0) {
    // Invalid or corrupt file - start fresh
    loaded_ = true;
    return true;
  }

  // Read version
  uint8_t version;
  file.read(reinterpret_cast<char *>(&version), 1);
  if (version != 1) {
    // Unknown version - start fresh
    loaded_ = true;
    return true;
  }

  // Read unigram count
  uint32_t unigramCount;
  file.read(reinterpret_cast<char *>(&unigramCount), 4);

  // Read unigrams
  unigrams_.clear();
  for (uint32_t i = 0; i < unigramCount && file.good(); ++i) {
    uint16_t len;
    file.read(reinterpret_cast<char *>(&len), 2);
    if (len > 100)
      break; // Sanity check

    std::string word(len, '\0');
    file.read(&word[0], len);

    uint32_t freq;
    file.read(reinterpret_cast<char *>(&freq), 4);

    if (file.good()) {
      unigrams_[word] = freq;
    }
  }

  // Read bigram count
  uint32_t bigramCount;
  file.read(reinterpret_cast<char *>(&bigramCount), 4);

  // Read bigrams
  bigrams_.clear();
  for (uint32_t i = 0; i < bigramCount && file.good(); ++i) {
    uint16_t len;
    file.read(reinterpret_cast<char *>(&len), 2);
    if (len > 200)
      break; // Sanity check

    std::string key(len, '\0');
    file.read(&key[0], len);

    uint32_t freq;
    file.read(reinterpret_cast<char *>(&freq), 4);

    if (file.good()) {
      bigrams_[key] = freq;
    }
  }

  // Apply decay to fade old data
  applyDecay();

  loaded_ = true;
  return true;
}

bool UserDataManager::save() {
  std::lock_guard<std::mutex> lock(mutex_);

  // Ensure directory exists
  SettingsManager::instance().load(); // This ensures dir exists

  std::ofstream file(getDataPath(), std::ios::binary | std::ios::trunc);
  if (!file.is_open()) {
    return false;
  }

  // Write magic header
  file.write("MKLD", 4);

  // Write version
  uint8_t version = 1;
  file.write(reinterpret_cast<char *>(&version), 1);

  // Write unigram count
  uint32_t unigramCount = static_cast<uint32_t>(unigrams_.size());
  file.write(reinterpret_cast<char *>(&unigramCount), 4);

  // Write unigrams
  for (const auto &[word, freq] : unigrams_) {
    uint16_t len = static_cast<uint16_t>(word.length());
    file.write(reinterpret_cast<char *>(&len), 2);
    file.write(word.data(), len);
    uint32_t f = freq;
    file.write(reinterpret_cast<char *>(&f), 4);
  }

  // Write bigram count
  uint32_t bigramCount = static_cast<uint32_t>(bigrams_.size());
  file.write(reinterpret_cast<char *>(&bigramCount), 4);

  // Write bigrams
  for (const auto &[key, freq] : bigrams_) {
    uint16_t len = static_cast<uint16_t>(key.length());
    file.write(reinterpret_cast<char *>(&len), 2);
    file.write(key.data(), len);
    uint32_t f = freq;
    file.write(reinterpret_cast<char *>(&f), 4);
  }

  commitsSinceLastSave_ = 0;
  return file.good();
}

// ============================================================================
// Learning Operations
// ============================================================================

void UserDataManager::recordCommit(const std::string &word,
                                   const std::string &previousWord) {
  if (word.empty())
    return;

  // Normalize to lowercase
  std::string normalizedWord = word;
  for (char &c : normalizedWord) {
    c = std::tolower(static_cast<unsigned char>(c));
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);

    // Record unigram
    unigrams_[normalizedWord]++;

    // Record bigram if we have context
    std::string prev = previousWord;
    if (prev.empty() && !lastWord_.empty()) {
      prev = lastWord_;
    }

    if (!prev.empty()) {
      std::string normalizedPrev = prev;
      for (char &c : normalizedPrev) {
        c = std::tolower(static_cast<unsigned char>(c));
      }
      std::string bigramKey = normalizedPrev + "|" + normalizedWord;
      bigrams_[bigramKey]++;
    }

    lastWord_ = normalizedWord;
    commitsSinceLastSave_++;
  }

  // Prune and auto-save if needed
  pruneIfNeeded();

  if (commitsSinceLastSave_ >= learn_config::AUTO_SAVE_INTERVAL) {
    save();
  }
}

double UserDataManager::getUnigramBoost(const std::string &word) const {
  if (word.empty())
    return 0.0;

  std::string normalized = word;
  for (char &c : normalized) {
    c = std::tolower(static_cast<unsigned char>(c));
  }

  std::lock_guard<std::mutex> lock(mutex_);

  auto it = unigrams_.find(normalized);
  if (it == unigrams_.end()) {
    return 0.0;
  }

  // Log-scaled boost to prevent extreme values
  return std::log1p(static_cast<double>(it->second)) *
         learn_config::UNIGRAM_WEIGHT;
}

double UserDataManager::getBigramBoost(const std::string &word,
                                       const std::string &previousWord) const {
  if (word.empty() || previousWord.empty())
    return 0.0;

  std::string normWord = word;
  std::string normPrev = previousWord;
  for (char &c : normWord) {
    c = std::tolower(static_cast<unsigned char>(c));
  }
  for (char &c : normPrev) {
    c = std::tolower(static_cast<unsigned char>(c));
  }

  std::string bigramKey = normPrev + "|" + normWord;

  std::lock_guard<std::mutex> lock(mutex_);

  auto it = bigrams_.find(bigramKey);
  if (it == bigrams_.end()) {
    return 0.0;
  }

  return std::log1p(static_cast<double>(it->second)) *
         learn_config::BIGRAM_WEIGHT;
}

double UserDataManager::getLearningBoost(const std::string &word,
                                         const std::string &previousWord) const {
  double unigram = getUnigramBoost(word);
  double bigram = 0.0;

  std::string context = previousWord;
  if (context.empty()) {
    std::lock_guard<std::mutex> lock(mutex_);
    context = lastWord_;
  }

  if (!context.empty()) {
    bigram = getBigramBoost(word, context);
  }

  return unigram + bigram;
}

std::string UserDataManager::getLastWord() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return lastWord_;
}

void UserDataManager::reset() {
  std::lock_guard<std::mutex> lock(mutex_);
  unigrams_.clear();
  bigrams_.clear();
  lastWord_.clear();
  commitsSinceLastSave_ = 0;

  // Delete the file
  std::remove(getDataPath().c_str());
}

size_t UserDataManager::getUnigramCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return unigrams_.size();
}

size_t UserDataManager::getBigramCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return bigrams_.size();
}

// ============================================================================
// Internal Operations
// ============================================================================

void UserDataManager::pruneIfNeeded() {
  std::lock_guard<std::mutex> lock(mutex_);

  // Prune unigrams
  if (unigrams_.size() > learn_config::MAX_UNIGRAMS) {
    // Sort by frequency and remove lowest
    std::vector<std::pair<std::string, uint32_t>> sorted(unigrams_.begin(),
                                                          unigrams_.end());
    std::sort(sorted.begin(), sorted.end(),
              [](const auto &a, const auto &b) { return a.second > b.second; });

    unigrams_.clear();
    for (size_t i = 0;
         i < sorted.size() && i < learn_config::MAX_UNIGRAMS * 9 / 10; ++i) {
      unigrams_[sorted[i].first] = sorted[i].second;
    }
  }

  // Prune bigrams
  if (bigrams_.size() > learn_config::MAX_BIGRAMS) {
    std::vector<std::pair<std::string, uint32_t>> sorted(bigrams_.begin(),
                                                          bigrams_.end());
    std::sort(sorted.begin(), sorted.end(),
              [](const auto &a, const auto &b) { return a.second > b.second; });

    bigrams_.clear();
    for (size_t i = 0;
         i < sorted.size() && i < learn_config::MAX_BIGRAMS * 9 / 10; ++i) {
      bigrams_[sorted[i].first] = sorted[i].second;
    }
  }
}

void UserDataManager::applyDecay() {
  // Note: mutex already held by caller (load)

  for (auto &[word, freq] : unigrams_) {
    freq = static_cast<uint32_t>(freq * learn_config::DECAY_FACTOR);
    if (freq == 0)
      freq = 1; // Keep entry but minimal
  }

  for (auto &[key, freq] : bigrams_) {
    freq = static_cast<uint32_t>(freq * learn_config::DECAY_FACTOR);
    if (freq == 0)
      freq = 1;
  }

  // Remove entries that decayed to minimal
  for (auto it = unigrams_.begin(); it != unigrams_.end();) {
    if (it->second <= 1) {
      it = unigrams_.erase(it);
    } else {
      ++it;
    }
  }

  for (auto it = bigrams_.begin(); it != bigrams_.end();) {
    if (it->second <= 1) {
      it = bigrams_.erase(it);
    } else {
      ++it;
    }
  }
}

} // namespace magickeyboard
