#include "flush/flush.h"
#include <fstream>
#include <algorithm>
#include <chrono>
#include <random>

namespace kv_engine {

// ============================================================
// FlushManager Implementation
// ============================================================

struct FlushManager::Impl {
    Options options;
    std::vector<std::thread> flush_threads;
    std::queue<FlushJob> flush_queue;
    std::mutex queue_mutex;
    std::condition_variable queue_cv;
    std::atomic<uint64_t> next_job_id{1};
    std::atomic<int> active_flushes{0};
    std::atomic<bool> stopping{false};
    std::mutex wait_mutex;
    std::condition_variable wait_cv;
    
    Impl(const Options& opts) : options(opts) {}
    
    void FlushThreadMain() {
        while (true) {
            FlushJob job;
            {
                std::unique_lock<std::mutex> lock(queue_mutex);
                queue_cv.wait(lock, [this] { 
                    return !flush_queue.empty() || stopping.load(); 
                });
                
                if (stopping.load() && flush_queue.empty()) {
                    return;
                }
                
                if (flush_queue.empty()) continue;
                
                job = std::move(flush_queue.front());
                flush_queue.pop();
            }
            
            active_flushes++;
            
            // Perform the flush
            FlushResult result;
            Status status = PerformFlush(std::move(job.memtable), result);
            
            // Call callback if provided
            if (job.callback) {
                job.callback(result, status);
            }
            
            active_flushes--;
            
            // Notify waiters
            {
                std::lock_guard<std::mutex> lock(wait_mutex);
                wait_cv.notify_all();
            }
        }
    }
    
    Status PerformFlush(std::unique_ptr<MemTable> memtable, FlushResult& result) {
        if (!memtable || memtable->Size() == 0) {
            return Status::OK();
        }
        
        // Generate unique filename
        auto now = std::chrono::system_clock::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();
        
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(1000, 9999);
        
        std::string filename = options.db_path + "/sst_" + 
                              std::to_string(timestamp) + "_" + 
                              std::to_string(dis(gen)) + ".sst";
        
        // Create SSTable builder
        SSTableBuilder builder(options.sstable_options);
        
        // Iterate through memtable and add to builder
        auto iter = memtable->NewIterator();
        for (iter->SeekToFirst(); iter->Valid(); iter->Next()) {
            Status s = builder.Add(iter->key(), iter->value(), 
                                  iter->sequence_number(), iter->is_deleted());
            if (!s.ok()) {
                delete iter;
                return s;
            }
        }
        Status iter_status = iter->status();
        delete iter;
        
        if (!iter_status.ok()) {
            return iter_status;
        }
        
        // Finish building SSTable
        Status s = builder.Finish(filename);
        if (!s.ok()) {
            return s;
        }
        
        // Fill result
        result.sstable_file = filename;
        result.min_sequence = builder.min_sequence();
        result.max_sequence = builder.max_sequence();
        result.num_entries = builder.num_entries();
        result.file_size = builder.file_size();
        result.smallest_key = Slice(builder.smallest_key());
        result.largest_key = Slice(builder.largest_key());
        
        return Status::OK();
    }
};

FlushManager::FlushManager(const Options& options)
    : impl_(std::make_unique<Impl>(options)), options_(options) {}

FlushManager::~FlushManager() {
    Stop();
}

FlushManager::FlushManager(FlushManager&& other) noexcept
    : impl_(std::move(other.impl_)), options_(std::move(other.options_)),
      memory_usage_(other.memory_usage_.load()), running_(other.running_.load()) {}

FlushManager& FlushManager::operator=(FlushManager&& other) noexcept {
    if (this != &other) {
        Stop();
        impl_ = std::move(other.impl_);
        options_ = std::move(other.options_);
        memory_usage_ = other.memory_usage_.load();
        running_ = other.running_.load();
    }
    return *this;
}

void FlushManager::Start() {
    if (running_.load()) return;
    
    running_.store(true);
    impl_->stopping.store(false);
    
    for (int i = 0; i < options_.max_background_flushes; ++i) {
        impl_->flush_threads.emplace_back(&Impl::FlushThreadMain, impl_.get());
    }
}

void FlushManager::Stop() {
    if (!running_.load()) return;
    
    impl_->stopping.store(true);
    
    {
        std::lock_guard<std::mutex> lock(impl_->queue_mutex);
        impl_->queue_cv.notify_all();
    }
    
    for (auto& thread : impl_->flush_threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    
    impl_->flush_threads.clear();
    running_.store(false);
}

Status FlushManager::ScheduleFlush(std::unique_ptr<MemTable> memtable, FlushCallback callback) {
    if (!running_.load()) {
        return Status::InvalidArgument("FlushManager not started");
    }
    
    if (!memtable || memtable->Size() == 0) {
        if (callback) {
            FlushResult result;
            callback(result, Status::OK());
        }
        return Status::OK();
    }
    
    FlushJob job;
    job.memtable = std::move(memtable);
    job.callback = std::move(callback);
    job.job_id = impl_->next_job_id++;
    
    {
        std::lock_guard<std::mutex> lock(impl_->queue_mutex);
        impl_->flush_queue.push(std::move(job));
    }
    
    impl_->queue_cv.notify_one();
    return Status::OK();
}

void FlushManager::WaitForFlushes() {
    std::unique_lock<std::mutex> lock(impl_->wait_mutex);
    impl_->wait_cv.wait(lock, [this] { 
        return impl_->flush_queue.empty() && impl_->active_flushes.load() == 0; 
    });
}

bool FlushManager::NeedsFlush() const {
    return memory_usage_.load() >= options_.max_write_buffer_size * 0.8;
}

size_t FlushManager::PendingFlushes() const {
    std::lock_guard<std::mutex> lock(impl_->queue_mutex);
    return impl_->flush_queue.size() + impl_->active_flushes.load();
}

Status FlushManager::FlushAll() {
    // This would require access to all active memtables
    // For now, just wait for pending flushes
    WaitForFlushes();
    return Status::OK();
}

// ============================================================
// FlushScheduler Implementation
// ============================================================

FlushScheduler::FlushScheduler(const Options& options) : options_(options) {}

FlushScheduler::~FlushScheduler() = default;

void FlushScheduler::OnMemTableSizeChange(size_t new_size) {
    current_memtable_size_.store(new_size);
    total_write_buffer_size_.fetch_add(new_size);
}

void FlushScheduler::OnNewMemTable() {
    active_memtables_.fetch_add(1);
}

bool FlushScheduler::ShouldFlush() const {
    size_t total = total_write_buffer_size_.load();
    size_t threshold = static_cast<size_t>(options_.max_write_buffer_size * options_.flush_trigger_factor);
    return total >= threshold;
}

int FlushScheduler::GetFlushPriority() const {
    size_t total = total_write_buffer_size_.load();
    size_t max_size = options_.max_write_buffer_size;
    
    if (max_size == 0) return 0;
    
    double ratio = static_cast<double>(total) / max_size;
    int priority = static_cast<int>(ratio * 100);
    return std::min(100, std::max(0, priority));
}

void FlushScheduler::OnFlushComplete(size_t flushed_size) {
    total_write_buffer_size_.fetch_sub(flushed_size);
    pending_flushes_.fetch_sub(1);
}

} // namespace kv_engine