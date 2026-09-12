#pragma once

#include "btree/btree.h"
#include <string>
#include <vector>
#include <cstdint>
#include <functional>

namespace kv_engine {

// WAL record types
enum class WalRecordType : uint8_t {
    kPut = 1,
    kDelete = 2,
    kBeginTransaction = 3,
    kCommitTransaction = 4,
    kRollbackTransaction = 5,
};

// WAL record header
struct WalRecordHeader {
    WalRecordType type;
    uint64_t sequence_number;
    uint64_t timestamp;
    uint32_t key_size;
    uint32_t value_size;
    uint32_t crc32;
    
    static constexpr size_t kHeaderSize = 1 + 8 + 8 + 4 + 4 + 4; // 29 bytes
};

// WAL options
struct WalOptions {
    std::string wal_dir = "./wal";
    size_t max_file_size = 64 * 1024 * 1024; // 64MB
    size_t buffer_size = 4 * 1024 * 1024;    // 4MB
    bool sync_on_write = true;
    bool enable_compression = false;
    uint32_t compression_threshold = 1024;
};

// WAL reader callback
using WalRecordCallback = std::function<void(const WalRecordHeader&, const Slice&, const Slice&)>;

// Write-Ahead Log class
class WriteAheadLog {
public:
    explicit WriteAheadLog(const WalOptions& options);
    ~WriteAheadLog();
    
    // Non-copyable, movable
    WriteAheadLog(const WriteAheadLog&) = delete;
    WriteAheadLog& operator=(const WriteAheadLog&) = delete;
    WriteAheadLog(WriteAheadLog&&) noexcept;
    WriteAheadLog& operator=(WriteAheadLog&&) noexcept;
    
    // Write operations
    Status Put(const Slice& key, const Slice& value, uint64_t sequence_number);
    Status Delete(const Slice& key, uint64_t sequence_number);
    Status BeginTransaction(uint64_t sequence_number);
    Status CommitTransaction(uint64_t sequence_number);
    Status RollbackTransaction(uint64_t sequence_number);
    
    // Flush and sync
    Status Flush();
    Status Sync();
    
    // Recovery
    Status Recover(WalRecordCallback callback, uint64_t* max_sequence);
    
    // File management
    Status RotateLog();
    uint64_t CurrentFileNumber() const;
    size_t CurrentFileSize() const;
    
    // Statistics
    struct Stats {
        uint64_t total_writes = 0;
        uint64_t total_bytes_written = 0;
        uint64_t total_syncs = 0;
        uint64_t current_file_number = 0;
        size_t current_file_size = 0;
    };
    Stats GetStats() const;
    
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace kv_engine