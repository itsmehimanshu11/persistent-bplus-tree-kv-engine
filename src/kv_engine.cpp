#include "kv_engine.h"
#include <iostream>
#include <mutex>

namespace kv_engine {

KVEngine::KVEngine(const KVEngineOptions& options)
    : options_(options), tree_(options.btree_options) {
    
    if (options_.enable_wal) {
        wal_ = std::make_unique<WriteAheadLog>(options_.wal_options);
        // Recover from WAL
        Status s = Recover();
        if (!s.ok()) {
            std::cerr << "Warning: WAL recovery failed: " << s.ToString() << std::endl;
        }
    }
}

KVEngine::~KVEngine() = default;

KVEngine::KVEngine(KVEngine&& other) noexcept
    : options_(std::move(other.options_)), tree_(std::move(other.tree_)),
      wal_(std::move(other.wal_)), sequence_number_(other.sequence_number_.load()) {
    other.sequence_number_.store(1);
}

KVEngine& KVEngine::operator=(KVEngine&& other) noexcept {
    if (this != &other) {
        options_ = std::move(other.options_);
        tree_ = std::move(other.tree_);
        wal_ = std::move(other.wal_);
        sequence_number_.store(other.sequence_number_.load());
        other.sequence_number_.store(1);
    }
    return *this;
}

Status KVEngine::Put(const Slice& key, const Slice& value) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    
    uint64_t seq = sequence_number_.fetch_add(1);
    
    // Write to WAL first
    if (wal_) {
        Status s = wal_->Put(key, value, seq);
        if (!s.ok()) return s;
        
        if (options_.sync_on_write) {
            s = wal_->Sync();
            if (!s.ok()) return s;
        }
    }
    
    // Write to B+ Tree (B+ Tree has its own locking)
    return tree_.Put(key, value);
}

Status KVEngine::Get(const Slice& key, std::string* value) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return tree_.Get(key, value);
}

Status KVEngine::Delete(const Slice& key) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    
    uint64_t seq = sequence_number_.fetch_add(1);
    
    // Write to WAL first
    if (wal_) {
        Status s = wal_->Delete(key, seq);
        if (!s.ok()) return s;
        
        if (options_.sync_on_write) {
            s = wal_->Sync();
            if (!s.ok()) return s;
        }
    }
    
    // Delete from B+ Tree (B+ Tree has its own locking)
    return tree_.Delete(key);
}

BPlusTree::Iterator* KVEngine::NewIterator() {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return tree_.NewIterator();
}

BPlusTree::Iterator* KVEngine::NewIterator(const Slice& start) {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return tree_.NewIterator(start);
}

BPlusTree::Iterator* KVEngine::NewIterator(const Slice& start, const Slice& limit) {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return tree_.NewIterator(start, limit);
}

Status KVEngine::Recover() {
    if (!wal_) return Status::OK();
    
    uint64_t max_sequence = 0;
    Status s = wal_->Recover(
        [this](const WalRecordHeader& header, const Slice& key, const Slice& value) {
            RecoveryCallback(header, key, value, this);
        },
        &max_sequence
    );
    
    if (s.ok() && max_sequence > 0) {
        sequence_number_.store(max_sequence + 1);
    }
    
    return s;
}

void KVEngine::RecoveryCallback(const WalRecordHeader& header, 
                                const Slice& key, const Slice& value, 
                                KVEngine* engine) {
    switch (header.type) {
        case WalRecordType::kPut:
            engine->tree_.Put(key, value);
            break;
        case WalRecordType::kDelete:
            engine->tree_.Delete(key);
            break;
        case WalRecordType::kBeginTransaction:
        case WalRecordType::kCommitTransaction:
        case WalRecordType::kRollbackTransaction:
            // Transaction markers - handled by sequence number
            break;
    }
}

Status KVEngine::Flush() {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    if (wal_) {
        return wal_->Flush();
    }
    return Status::OK();
}

Status KVEngine::Sync() {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    if (wal_) {
        return wal_->Sync();
    }
    return Status::OK();
}

WriteAheadLog::Stats KVEngine::GetWalStats() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    if (wal_) {
        return wal_->GetStats();
    }
    return WriteAheadLog::Stats{};
}

void KVEngine::Print() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::cout << "=== KV Engine ===" << std::endl;
    std::cout << "Size: " << tree_.Size() << std::endl;
    std::cout << "Nodes: " << tree_.NodeCount() << std::endl;
    std::cout << "Height: " << tree_.Height() << std::endl;
    std::cout << "Memory: " << tree_.MemoryUsage() << " bytes" << std::endl;
    std::cout << "Sequence: " << sequence_number_.load() << std::endl;
    
    if (wal_) {
        auto stats = wal_->GetStats();
        std::cout << "WAL: files=" << stats.current_file_number 
                  << ", size=" << stats.current_file_size 
                  << ", writes=" << stats.total_writes << std::endl;
    }
    
    tree_.Print();
}

bool KVEngine::Verify() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return tree_.Verify();
}

} // namespace kv_engine
