#pragma once

#include "../kv_types.h"
#include "../error/status.h"
#include <vector>
#include <memory>
#include <atomic>
#include <random>
#include <cstdint>

namespace kv_engine {

// Forward declaration
class MemTableIterator;

// Skip List Node
template<typename Key, typename Value>
class SkipListNode {
public:
    Key key;
    Value value;
    std::vector<SkipListNode*> forward;
    uint64_t sequence_number;
    bool is_deleted;

    SkipListNode(const Key& k, const Value& v, int level, uint64_t seq, bool deleted = false)
        : key(k), value(v), forward(level, nullptr), sequence_number(seq), is_deleted(deleted) {}
};

// MemTable options
struct MemTableOptions {
    size_t max_size = 64 * 1024 * 1024; // 64MB
    int max_level = 12;                  // Supports up to 2^12 elements
    double probability = 0.25;           // Skip list promotion probability
    const Comparator* comparator = nullptr;
    
    MemTableOptions() = default;
};

// MemTable using Skip List for O(log n) operations
class MemTable {
public:
    using Options = MemTableOptions;

    // Skip List Node (public for iterator access)
    struct Node {
        std::string key;
        std::string value;
        std::vector<Node*> forward;
        uint64_t sequence_number;
        bool is_deleted;

        Node(const std::string& k, const std::string& v, int level, uint64_t seq, bool deleted = false)
            : key(k), value(v), forward(level, nullptr), sequence_number(seq), is_deleted(deleted) {}
    };

    // Implementation details (accessible to iterator)
    struct Impl {
        using Node = MemTable::Node;
        
        Node* head = nullptr;
        int max_level;
        double probability;
        const Comparator* comparator;
        std::mt19937_64 rng;
        std::uniform_real_distribution<double> dist;
        int current_max_level = 1;
        
        Impl(const Options& options);
        ~Impl();
        
        int RandomLevel();
        int CompareKeys(const Slice& a, const Slice& b) const;
        Node* FindGreaterOrEqual(const Slice& key, std::vector<Node*>* prev) const;
        Node* FindLessThan(const Slice& key) const;
        Node* FindLast() const;
        void InsertNode(Node* node, const std::vector<Node*>& prev);
        bool DeleteNode(const Slice& key, std::vector<Node*>* prev);
    };

    explicit MemTable(const Options& options = Options());
    ~MemTable();

    // Non-copyable, movable
    MemTable(const MemTable&) = delete;
    MemTable& operator=(const MemTable&) = delete;
    MemTable(MemTable&&) noexcept;
    MemTable& operator=(MemTable&&) noexcept;

    // Core operations
    Status Put(const Slice& key, const Slice& value, uint64_t sequence_number);
    Status Get(const Slice& key, std::string* value, uint64_t* sequence_number = nullptr) const;
    Status Delete(const Slice& key, uint64_t sequence_number);
    
    // Check if key exists (including tombstones)
    bool Contains(const Slice& key) const;

    // Iterator support
    MemTableIterator* NewIterator();

    // Statistics
    size_t Size() const { return size_.load(); }
    size_t MemoryUsage() const { return memory_usage_.load(); }
    bool IsFull() const { return memory_usage_.load() >= options_.max_size; }
    uint64_t MaxSequenceNumber() const { return max_sequence_.load(); }

    // Debug
    void Print() const;
    bool Verify() const;

    // Iterator access to internal structure
    Impl* GetImpl() const { return impl_.get(); }

    // Friend for iterator access
    friend class MemTableIterator;

private:
    std::unique_ptr<Impl> impl_;
    Options options_;
    std::atomic<size_t> size_{0};
    std::atomic<size_t> memory_usage_{0};
    std::atomic<uint64_t> max_sequence_{0};
};

// Iterator for MemTable
class MemTableIterator {
public:
    MemTableIterator() = default;
    ~MemTableIterator();

    MemTableIterator(const MemTableIterator&) = delete;
    MemTableIterator& operator=(const MemTableIterator&) = delete;
    MemTableIterator(MemTableIterator&&) noexcept;
    MemTableIterator& operator=(MemTableIterator&&) noexcept;

    bool Valid() const;
    void SeekToFirst();
    void SeekToLast();
    void Seek(const Slice& target);
    void Next();
    void Prev();

    Slice key() const;
    Slice value() const;
    uint64_t sequence_number() const;
    bool is_deleted() const;
    Status status() const;

private:
    friend class MemTable;
    struct Impl;
    std::unique_ptr<Impl> impl_;

    explicit MemTableIterator(MemTable* table);
};

} // namespace kv_engine