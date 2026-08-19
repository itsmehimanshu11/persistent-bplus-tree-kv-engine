#pragma once

#include <string>
#include <cstdint>
#include <functional>
#include <memory>
#include <cstring>
#include "error/status.h"

namespace kv_engine {

// Slice - lightweight reference to a byte array (like LevelDB/RocksDB)
class Slice {
public:
    Slice() : data_(nullptr), size_(0) {}
    Slice(const char* data, size_t size) : data_(data), size_(size) {}
    Slice(const std::string& str) : data_(str.data()), size_(str.size()) {}
    Slice(const char* str) : data_(str), size_(strlen(str)) {}

    const char* data() const { return data_; }
    size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }

    char operator[](size_t n) const { return data_[n]; }

    void clear() { data_ = nullptr; size_ = 0; }
    void remove_prefix(size_t n) {
        if (n > size_) n = size_;
        data_ += n;
        size_ -= n;
    }

    std::string ToString() const { return std::string(data_, size_); }
    int compare(const Slice& other) const;

    bool operator==(const Slice& other) const {
        return size_ == other.size_ && 
               (data_ == other.data_ || memcmp(data_, other.data_, size_) == 0);
    }
    bool operator!=(const Slice& other) const { return !(*this == other); }
    bool operator<(const Slice& other) const { return compare(other) < 0; }

private:
    const char* data_;
    size_t size_;
};

inline int Slice::compare(const Slice& other) const {
    size_t min_size = (size_ < other.size_) ? size_ : other.size_;
    int r = memcmp(data_, other.data_, min_size);
    if (r == 0) {
        if (size_ < other.size_) r = -1;
        else if (size_ > other.size_) r = 1;
    }
    return r;
}

// Comparator interface for custom key ordering
class Comparator {
public:
    virtual ~Comparator() = default;
    virtual int Compare(const Slice& a, const Slice& b) const = 0;
    virtual const char* Name() const = 0;
    virtual void FindShortestSeparator(std::string* /*start*/, const Slice& /*limit*/) const {}
    virtual void FindShortSuccessor(std::string* /*key*/) const {}
};

// Default byte-wise comparator
class BytewiseComparator : public Comparator {
public:
    int Compare(const Slice& a, const Slice& b) const override {
        return a.compare(b);
    }
    const char* Name() const override { return "leveldb.BytewiseComparator"; }
};

// Options for database configuration
struct Options {
    // Comparator for key ordering
    const Comparator* comparator = nullptr;
    
    // Create database if missing
    bool create_if_missing = true;
    
    // Error if database exists
    bool error_if_exists = false;
    
    // Maximum size of memtable before flush (default: 4MB)
    size_t write_buffer_size = 4 * 1024 * 1024;
    
    // Maximum number of open files
    int max_open_files = 1000;
    
    // Block size for SSTable (default: 4KB)
    size_t block_size = 4096;
    
    // Block cache size (default: 8MB)
    size_t block_cache_size = 8 * 1024 * 1024;
    
    // Bloom filter bits per key (0 = disabled)
    int bloom_bits_per_key = 10;
    
    // Compaction style
    enum class CompactionStyle { LEVELED, TIERED, UNIVERSAL };
    CompactionStyle compaction_style = CompactionStyle::LEVELED;
    
    // Target file size for compaction (default: 2MB)
    size_t target_file_size = 2 * 1024 * 1024;
    
    // Max bytes for level base (default: 10MB)
    size_t max_bytes_for_level_base = 10 * 1024 * 1024;
    
    // Max bytes for level multiplier (default: 10)
    int max_bytes_for_level_multiplier = 10;
    
    // Number of levels (default: 7)
    int num_levels = 7;
    
    // Enable WAL
    bool enable_wal = true;
    
    // Sync WAL on every write
    bool wal_sync = false;
    
    // Number of background compaction threads
    int max_background_compactions = 1;
    
    // Number of background flush threads
    int max_background_flushes = 1;
};

// Read options
struct ReadOptions {
    // Verify checksums
    bool verify_checksums = false;
    
    // Fill block cache
    bool fill_cache = true;
    
    // Snapshot for consistent reads
    class Snapshot* snapshot = nullptr;
};

// Write options
struct WriteOptions {
    // Sync to disk before returning
    bool sync = false;
    
    // Disable WAL for this write
    bool disable_wal = false;
};

// Range for iteration
struct Range {
    Slice start;
    Slice limit;
    bool start_inclusive = true;
    bool limit_inclusive = false;
};

} // namespace kv_engine