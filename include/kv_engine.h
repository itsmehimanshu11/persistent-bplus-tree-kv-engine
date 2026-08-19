#pragma once

#include "btree/btree.h"
#include "wal/wal.h"
#include "kv_types.h"
#include "error/status.h"
#include <string>
#include <atomic>
#include <shared_mutex>

namespace kv_engine {

// KV Engine options
struct KVEngineOptions {
    BTreeOptions btree_options;
    WalOptions wal_options;
    bool enable_wal = true;
    bool sync_on_write = false;
    
    KVEngineOptions() = default;
};

// Main KV Engine class combining B+ Tree with WAL for persistence
class KVEngine {
public:
    explicit KVEngine(const KVEngineOptions& options = KVEngineOptions());
    ~KVEngine();
    
    // Non-copyable, movable
    KVEngine(const KVEngine&) = delete;
    KVEngine& operator=(const KVEngine&) = delete;
    KVEngine(KVEngine&&) noexcept;
    KVEngine& operator=(KVEngine&&) noexcept;
    
    // Core operations
    Status Put(const Slice& key, const Slice& value);
    Status Get(const Slice& key, std::string* value) const;
    Status Delete(const Slice& key);
    
    // Range operations
    BPlusTree::Iterator* NewIterator();
    BPlusTree::Iterator* NewIterator(const Slice& start);
    BPlusTree::Iterator* NewIterator(const Slice& start, const Slice& limit);
    
    // Recovery
    Status Recover();
    
    // Flush and sync
    Status Flush();
    Status Sync();
    
    // Statistics
    size_t Size() const { return tree_.Size(); }
    size_t NodeCount() const { return tree_.NodeCount(); }
    size_t Height() const { return tree_.Height(); }
    size_t MemoryUsage() const { return tree_.MemoryUsage(); }
    
    WriteAheadLog::Stats GetWalStats() const;
    
    // Debug
    void Print() const;
    bool Verify() const;
    
private:
    KVEngineOptions options_;
    BPlusTree tree_;
    std::unique_ptr<WriteAheadLog> wal_;
    std::atomic<uint64_t> sequence_number_{1};
    mutable std::shared_mutex mutex_; // For WAL operations
    
    // Recovery callback
    static void RecoveryCallback(const WalRecordHeader& header, 
                                 const Slice& key, const Slice& value, 
                                 KVEngine* engine);
};

} // namespace kv_engine
