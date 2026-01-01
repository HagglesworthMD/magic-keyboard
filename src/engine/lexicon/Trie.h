#pragma once

#include <string>
#include <vector>
#include <memory>
#include <cstdint>

namespace magickeyboard::lexicon {

struct TrieNode {
    // Linked list or fixed array? Fixed array 27 is requested (a-z + ')
    // Using index into nodes vector
    int children[27]; 
    bool isTerminal = false;
    uint32_t frequency = 0;

    TrieNode();
};

class Trie {
public:
    Trie();
    
    // Insert a word with frequency
    void insert(const std::string& word, uint32_t freq);
    
    // Check if word exists (exact match)
    bool contains(const std::string& word) const;

    // Helper to map char to index (0-26)
    static int charToIndex(char c);

    const std::vector<TrieNode>& nodes() const { return nodes_; }

private:
    std::vector<TrieNode> nodes_;
};

} // namespace magickeyboard::lexicon
