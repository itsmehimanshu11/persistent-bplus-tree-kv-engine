#pragma once

#include "../kv_types.h"
#include "../error/status.h"
#include "../memtable/memtable.h"
#include "../sstable/sstable.h"
#include <vector>
#include <memory>
#include <string>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <queue>
#include <functional>

namespace kv_engine {

// Flush manager - handles MemTable to SSTable flushing
class FlushManager {
public:
    struct Options {
        size_t max_memtable_size = 64 * 1024 * 1024;  // 64MB
        size_t max_write_buffer_size = 128 * 1024 * 1024; // 128MB total
        int max_background_flushes = 4;
        std::string db_path;
        SSTableOptions sstable_options;
        const Comparator* comparator = nullptr;
    };

    struct FlushResult {
        std::string sstable_file;
        uint64_t min_sequence;
        uint64_t max_sequence;
        size_t num_entries;
        size_t file_size;
        Slice smallest_key;
        Slice largest_key;
    };

    using FlushCallback = std::function<void(const FlushResult&, const Status&)>;

    explicit FlushManager(const Options& options);
    ~FlushManager();

    // Non-copyable, movable
    FlushManager(const FlushManager&) = delete;
    FlushManager& operator=(const FlushManager&) = delete;
    FlushManager(FlushManager&&) noexcept;
    FlushManager& operator=(FlushManager&&) noexcept;

    // Start background flush threads
    void Start();
    
    // Stop background flush threads
    void Stop();
    
    // Schedule a memtable for flushing
    Status ScheduleFlush(std::unique_ptr<MemTable> memtable, FlushCallback callback = nullptr);
    
    // Wait for all pending flushes to complete
    void WaitForFlushes();
    
    // Check if flush is needed
    bool NeedsFlush() const;
    
    // Get current memory usage
    size_t MemoryUsage() const { return memory_usage_.load(); }
    
    // Get pending flush count
    size_t PendingFlushes() const;
    
    // Force flush all memtables
    Status FlushAll();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    Options options_;
    std::atomic<size_t> memory_usage_{0};
    std::atomic<bool> running_{false};
};

// Flush job for background processing
struct FlushJob {
    std::unique_ptr<MemTable> memtable;
    FlushManager::FlushCallback callback;
    uint64_t job_id;
};

// Compaction-aware flush scheduler
class FlushScheduler {
public:
    struct Options {
        size_t target_file_size = 64 * 1024 * 1024; // 64MB
        size_t max_write_buffer_size = 128 * 1024 * 1024;
        int max_background_flushes = 4;
        double flush_trigger_factor = 0.8; // Trigger at 80% capacity
    };

    explicit FlushScheduler(const Options& options);
    ~FlushScheduler();

    // Called when memtable size changes
    void OnMemTableSizeChange(size_t new_size);
    
    // Called when new memtable is created
    void OnNewMemTable();
    
    // Check if flush should be triggered
    bool ShouldFlush() const;
    
    // Get recommended flush priority (0-100)
    int GetFlushPriority() const;
    
    // Record flush completion
    void OnFlushComplete(size_t flushed_size);

private:
    Options options_;
    std::atomic<size_t> current_memtable_size_{0};
    std::atomic<size_t> total_write_buffer_size_{0};
    std::atomic<int> active_memtables_{0};
    std::atomic<int> pending_flushes_{0};
    mutable std::mutex mutex_;
};

} // namespace kv_engine