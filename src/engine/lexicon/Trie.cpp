#include "Trie.h"
#include <algorithm>

namespace magickeyboard::lexicon {

TrieNode::TrieNode() {
  std::fill(std::begin(children), std::end(children), -1);
}

Trie::Trie() {
  // Initialize root node
  nodes_.emplace_back();
}

int Trie::charToIndex(char c) {
  if (c >= 'a' && c <= 'z')
    return c - 'a';
  if (c == '\'')
    return 26;
  return -1;
}

void Trie::insert(const std::string &word, uint32_t freq) {
  int curr = 0; // Root index
  for (char c : word) {
    int idx = charToIndex(c);
    if (idx == -1)
      continue; // Skip invalid chars

    if (nodes_[curr].children[idx] == -1) {
      nodes_[curr].children[idx] = static_cast<int>(nodes_.size());
      nodes_.emplace_back();
    }
    curr = nodes_[curr].children[idx];
  }
  nodes_[curr].isTerminal = true;
  nodes_[curr].frequency = freq;
}

bool Trie::contains(const std::string &word) const {
  int curr = 0;
  for (char c : word) {
    int idx = charToIndex(c);
    if (idx == -1)
      return false;

    if (nodes_[curr].children[idx] == -1) {
      return false;
    }
    curr = nodes_[curr].children[idx];
  }
  return nodes_[curr].isTerminal;
}

} // namespace magickeyboard::lexicon
