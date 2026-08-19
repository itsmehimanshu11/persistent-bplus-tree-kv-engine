#pragma once

#include <vector>
#include <memory>
#include <algorithm>
#include <cstdint>
#include <shared_mutex>
#include "../kv_types.h"
#include "../error/status.h"

namespace kv_engine {

// B+ Tree Node base class
class BTreeNode {
public:
    virtual ~BTreeNode() = default;
    virtual bool IsLeaf() const = 0;
    virtual size_t Size() const = 0;
    virtual size_t Capacity() const = 0;
    virtual bool IsFull() const = 0;
    virtual bool IsUnderflow() const = 0;
};

// Internal node (non-leaf)
class InternalNode : public BTreeNode {
public:
    std::vector<std::string> keys;      // Separator keys (owned)
    std::vector<uint64_t> children; // Child page IDs (or pointers in memory)
    size_t max_keys = 128;
    
    InternalNode() = default;
    explicit InternalNode(size_t capacity) : max_keys(capacity) {
        keys.reserve(capacity);
        children.reserve(capacity + 1);
    }
    
    bool IsLeaf() const override { return false; }
    size_t Size() const override { return keys.size(); }
    size_t Capacity() const override { return max_keys; }
    bool IsFull() const override { return keys.size() >= max_keys; }
    bool IsUnderflow() const override { return keys.size() < (max_keys + 1) / 2; }
};

// Leaf node - stores actual key-value pairs
class LeafNode : public BTreeNode {
public:
    std::vector<std::string> keys; // Owned data
    std::vector<std::string> values; // Owned data
    uint64_t next_leaf = 0; // For range scans
    size_t max_keys = 128;
    
    LeafNode() = default;
    explicit LeafNode(size_t capacity) : max_keys(capacity) {
        keys.reserve(capacity);
        values.reserve(capacity);
    }
    
    bool IsLeaf() const override { return true; }
    size_t Size() const override { return keys.size(); }
    size_t Capacity() const override { return max_keys; }
    bool IsFull() const override { return keys.size() >= max_keys; }
    bool IsUnderflow() const override { return keys.size() < (max_keys + 1) / 2; }
};

// B+ Tree configuration
struct BTreeOptions {
    size_t page_size = 4096; // Node size in bytes
    size_t max_keys_per_node = 128; // Approximate
    const Comparator* comparator = nullptr;
};

// In-memory B+ Tree implementation
class BPlusTree {
public:
    BPlusTree(const BTreeOptions& options = BTreeOptions());
    ~BPlusTree();
    
    // Non-copyable, movable
    BPlusTree(const BPlusTree&) = delete;
    BPlusTree& operator=(const BPlusTree&) = delete;
    BPlusTree(BPlusTree&&) noexcept;
    BPlusTree& operator=(BPlusTree&&) noexcept;
    
    // Core operations
    Status Put(const Slice& key, const Slice& value);
    Status Get(const Slice& key, std::string* value) const;
    Status Delete(const Slice& key);
    
    // Range operations
    class Iterator;
    Iterator* NewIterator();
    Iterator* NewIterator(const Slice& start);
    Iterator* NewIterator(const Slice& start, const Slice& limit);
    
    // Statistics
    size_t Size() const { return size_; }
    size_t NodeCount() const { return node_count_; }
    size_t Height() const { return height_; }
    size_t MemoryUsage() const;
    
    // Debug
    void Print() const;
    bool Verify() const;

private:
    struct Impl;
    
    // Members in initialization order (matching constructor)
    std::unique_ptr<Impl> impl_;
    uint64_t root_id_ = 0;
    bool root_is_leaf_ = true;
    BTreeOptions options_;
    size_t size_ = 0;
    size_t node_count_ = 0;
    size_t height_ = 1;
    
    // Concurrency
    mutable std::shared_mutex mutex_;
    
    // Node management
    uint64_t AllocateNode(bool is_leaf);
    void FreeNode(uint64_t node_id);
    BTreeNode* GetNode(uint64_t node_id);
    const BTreeNode* GetNode(uint64_t node_id) const;
    
    // Core operations
    Status InsertIntoLeaf(LeafNode* leaf, const Slice& key, const Slice& value);
    Status InsertIntoInternal(InternalNode* internal, const Slice& key, uint64_t child);
    void SplitLeaf(LeafNode* leaf, LeafNode* new_leaf, std::string* separator);
    void SplitInternal(InternalNode* internal, InternalNode* new_internal, Slice* separator, uint64_t* new_child);
    Status DeleteFromLeaf(LeafNode* leaf, const Slice& key);
    Status DeleteFromInternal(InternalNode* internal, const Slice& key);
    void RebalanceOrMerge(InternalNode* parent, size_t child_idx);
    
    // Split and insert operations
    Status SplitLeafAndInsert(LeafNode* leaf, uint64_t leaf_id);
    Status InsertIntoParent(uint64_t parent_id, const Slice& separator, uint64_t new_child_id);
    Status SplitInternalAndInsert(InternalNode* internal, uint64_t internal_id);
    
    // Deletion with rebalancing
    Status DeleteFromLeafWithRebalance(uint64_t leaf_id, const Slice& key);
    Status DeleteFromInternalWithRebalance(uint64_t internal_id, const Slice& key);
    void RebalanceLeaf(uint64_t leaf_id, InternalNode* parent, size_t child_idx);
    void RebalanceInternal(uint64_t internal_id, InternalNode* parent, size_t child_idx);
    void MergeLeaves(LeafNode* left, LeafNode* right, InternalNode* parent, size_t child_idx);
    void MergeInternals(InternalNode* left, InternalNode* right, InternalNode* parent, size_t child_idx);
    void RedistributeLeaves(LeafNode* left, LeafNode* right, InternalNode* parent, size_t child_idx, bool left_is_target);
    void RedistributeInternals(InternalNode* left, InternalNode* right, InternalNode* parent, size_t child_idx, bool left_is_target);
    void UpdateParentSeparator(InternalNode* parent, size_t child_idx, const Slice& new_separator);
    
    // Search
    uint64_t FindLeaf(const Slice& key) const;
    int BinarySearchKeys(const std::vector<std::string>& keys, const Slice& key) const;
    int BinarySearchKeys(const std::vector<Slice>& keys, const Slice& key) const;
    
    // Debug
    void PrintNode(uint64_t node_id, int depth) const;
};

// Iterator for range scans
class BPlusTree::Iterator {
public:
    Iterator() = default;
    ~Iterator();
    
    Iterator(const Iterator&) = delete;
    Iterator& operator=(const Iterator&) = delete;
    Iterator(Iterator&&) noexcept;
    Iterator& operator=(Iterator&&) noexcept;
    
    bool Valid() const;
    void SeekToFirst();
    void SeekToLast();
    void Seek(const Slice& target);
    void Next();
    void Prev();
    
    Slice key() const;
    Slice value() const;
    Status status() const;

private:
    friend class BPlusTree;
    struct Impl;
    std::unique_ptr<Impl> impl_;
    
    Iterator(BPlusTree* tree, const Slice* start, const Slice* limit);
};

} // namespace kv_engine