#pragma once

#include "../kv_types.h"
#include "../error/status.h"
#include <vector>
#include <memory>
#include <cstdint>
#include <string>

namespace kv_engine {

// SSTable block types
enum class BlockType : uint8_t {
    kDataBlock = 1,
    kIndexBlock = 2,
    kFilterBlock = 3,
    kMetaBlock = 4,
};

// Block handle for locating blocks in file
struct BlockHandle {
    uint64_t offset = 0;
    uint64_t size = 0;
    
    BlockHandle() = default;
    BlockHandle(uint64_t off, uint64_t sz) : offset(off), size(sz) {}
    
    std::string Encode() const;
    static Status Decode(const Slice& input, BlockHandle* handle);
};

// SSTable footer (fixed size, at end of file)
struct SSTableFooter {
    BlockHandle index_block;
    BlockHandle filter_block;
    BlockHandle meta_block;
    uint64_t magic_number = 0xDBDBDBDBDBDBDBDB; // "DBDBDBDB"
    uint32_t version = 1;
    uint32_t crc32 = 0;
    
    static constexpr size_t kEncodedSize = 8 + 8 + 8 + 8 + 8 + 8 + 8 + 8 + 4 + 4; // 68 bytes
    
    std::string Encode() const;
    static Status Decode(const Slice& input, SSTableFooter* footer);
};

// SSTable options
struct SSTableOptions {
    size_t block_size = 4096;           // Target block size
    size_t block_restart_interval = 16; // Keys per restart point
    bool enable_compression = false;    // Snappy compression
    uint32_t compression_threshold = 1024;
    const Comparator* comparator = nullptr;
    bool enable_bloom_filter = true;    // Bloom filter for point lookups
    double bloom_filter_bits_per_key = 10.0;
};

// SSTable builder - constructs SSTable from sorted key-value pairs
class SSTableBuilder {
public:
    explicit SSTableBuilder(const SSTableOptions& options);
    ~SSTableBuilder();

    // Non-copyable, movable
    SSTableBuilder(const SSTableBuilder&) = delete;
    SSTableBuilder& operator=(const SSTableBuilder&) = delete;
    SSTableBuilder(SSTableBuilder&&) noexcept;
    SSTableBuilder& operator=(SSTableBuilder&&) noexcept;

    // Add key-value pair (keys must be added in sorted order)
    Status Add(const Slice& key, const Slice& value, uint64_t sequence_number, bool is_deleted = false);
    
    // Finish building and write to file
    Status Finish(const std::string& filename);
    
    // Abort building
    void Abandon();
    
    // Statistics
    size_t NumEntries() const { return num_entries_; }
    size_t FileSize() const { return file_size_; }
    bool IsEmpty() const { return num_entries_ == 0; }
    
    // Accessors for flush result (delegate to impl)
    uint64_t min_sequence() const;
    uint64_t max_sequence() const;
    size_t num_entries() const;
    size_t file_size() const { return file_size_; }
    const std::string& smallest_key() const;
    const std::string& largest_key() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    SSTableOptions options_;
    size_t num_entries_ = 0;
    size_t file_size_ = 0;
    bool finished_ = false;
    bool abandoned_ = false;
    static const std::string empty_string_;
};

// SSTable reader options
struct SSTableReaderOptions {
    const Comparator* comparator = nullptr;
    bool verify_checksums = true;
    bool prefetch_index_and_filter = true;
    
    SSTableReaderOptions() = default;
};

// SSTable reader - reads SSTable files
class SSTableReader {
public:
    using Options = SSTableReaderOptions;

    explicit SSTableReader(const Options& options = Options());
    ~SSTableReader();

    // Non-copyable, movable
    SSTableReader(const SSTableReader&) = delete;
    SSTableReader& operator=(const SSTableReader&) = delete;
    SSTableReader(SSTableReader&&) noexcept;
    SSTableReader& operator=(SSTableReader&&) noexcept;

    // Open SSTable file
    Status Open(const std::string& filename);
    
    // Point lookup
    Status Get(const Slice& key, std::string* value, uint64_t* sequence_number = nullptr, bool* is_deleted = nullptr) const;
    
    // Check if key might exist (uses bloom filter)
    bool MightContain(const Slice& key) const;
    
    // Iterator for range scans
    class Iterator;
    Iterator* NewIterator() const;
    
    // File info
    uint64_t FileNumber() const { return file_number_; }
    size_t FileSize() const { return file_size_; }
    size_t NumEntries() const { return num_entries_; }
    uint64_t MinSequenceNumber() const { return min_sequence_; }
    uint64_t MaxSequenceNumber() const { return max_sequence_; }
    Slice SmallestKey() const { return smallest_key_; }
    Slice LargestKey() const { return largest_key_; }
    
    // Statistics
    struct Stats {
        size_t data_size = 0;
        size_t index_size = 0;
        size_t filter_size = 0;
        size_t num_data_blocks = 0;
        size_t num_entries = 0;
    };
    Stats GetStats() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    Options options_;
    uint64_t file_number_ = 0;
    size_t file_size_ = 0;
    size_t num_entries_ = 0;
    uint64_t min_sequence_ = 0;
    uint64_t max_sequence_ = 0;
    std::string smallest_key_;
    std::string largest_key_;
};

// Iterator for SSTable range scans
class SSTableReader::Iterator {
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
    uint64_t sequence_number() const;
    bool is_deleted() const;
    Status status() const;

private:
    friend class SSTableReader;
    struct Impl;
    std::unique_ptr<Impl> impl_;

    explicit Iterator(const SSTableReader* reader);
};

} // namespace kv_engine