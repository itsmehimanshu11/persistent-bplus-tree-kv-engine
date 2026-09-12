#include "wal/wal.h"
#include <fstream>
#include <filesystem>
#include <chrono>
#include <cstring>
#include <zlib.h>
#include <algorithm>
#include <mutex>
#include <limits>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace kv_engine {

// CRC32 calculation
static uint32_t CalculateCRC32(const void* data, size_t length) {
    return crc32(0, static_cast<const Bytef*>(data), static_cast<uInt>(length));
}

// Encode fixed 32-bit integer (little-endian)
static void EncodeFixed32(char* buf, uint32_t value) {
    buf[0] = static_cast<char>(value & 0xff);
    buf[1] = static_cast<char>((value >> 8) & 0xff);
    buf[2] = static_cast<char>((value >> 16) & 0xff);
    buf[3] = static_cast<char>((value >> 24) & 0xff);
}

// Encode fixed 64-bit integer (little-endian)
static void EncodeFixed64(char* buf, uint64_t value) {
    EncodeFixed32(buf, static_cast<uint32_t>(value & 0xffffffff));
    EncodeFixed32(buf + 4, static_cast<uint32_t>(value >> 32));
}

// Decode fixed 32-bit integer (little-endian)
static uint32_t DecodeFixed32(const char* buf) {
    return static_cast<uint32_t>(static_cast<unsigned char>(buf[0]))
         | (static_cast<uint32_t>(static_cast<unsigned char>(buf[1])) << 8)
         | (static_cast<uint32_t>(static_cast<unsigned char>(buf[2])) << 16)
         | (static_cast<uint32_t>(static_cast<unsigned char>(buf[3])) << 24);
}

// Decode fixed 64-bit integer (little-endian)
static uint64_t DecodeFixed64(const char* buf) {
    return static_cast<uint64_t>(DecodeFixed32(buf))
         | (static_cast<uint64_t>(DecodeFixed32(buf + 4)) << 32);
}

struct WriteAheadLog::Impl {
    WalOptions options;
    std::ofstream current_file;
    uint64_t current_file_number = 0;
    size_t current_file_size = 0;
    std::vector<char> write_buffer;
    size_t buffer_offset = 0;
    std::string current_filename;
    uint64_t sequence_number = 0;
    uint64_t total_writes = 0;
    uint64_t total_bytes_written = 0;
    uint64_t total_syncs = 0;
    std::mutex mutex;
    
    Impl(const WalOptions& opts) : options(opts), write_buffer(std::max<size_t>(1, opts.buffer_size)) {
        std::filesystem::create_directories(options.wal_dir);
        // Reopen the highest-numbered WAL file so a restart continues the log.
        uint64_t highest = 0;
        bool found = false;
        for (const auto& entry : std::filesystem::directory_iterator(options.wal_dir)) {
            if (!entry.is_regular_file()) continue;
            const std::string name = entry.path().filename().string();
            if (name.rfind("wal_", 0) != 0 || name.size() <= 8 || name.substr(name.size() - 4) != ".log") continue;
            const std::string number = name.substr(4, name.size() - 8);
            try {
                size_t used = 0;
                uint64_t n = std::stoull(number, &used);
                if (used == number.size() && (!found || n > highest)) { highest = n; found = true; }
            } catch (...) {
                // Ignore files that do not follow the WAL naming convention.
            }
        }
        current_file_number = found ? highest : 0;
        OpenNewLogFile();
    }
    
    ~Impl() {
        if (current_file.is_open()) {
            FlushBuffer();
            current_file.close();
        }
    }
    
    void OpenNewLogFile() {
        if (current_file.is_open()) {
            current_file.close();
        }
        
        current_filename = options.wal_dir + "/wal_" +
            std::to_string(current_file_number) + ".log";
        current_file.open(current_filename, std::ios::binary | std::ios::app);
        current_file_size = 0;
        
        if (current_file.is_open()) {
            current_file.seekp(0, std::ios::end);
            current_file_size = current_file.tellp();
        }
    }
    
    Status WriteRecord(WalRecordType type, const Slice& key, const Slice& value, uint64_t seq) {
        std::lock_guard<std::mutex> lock(mutex);
        
        // Calculate record size
        size_t key_size = key.size();
        size_t value_size = value.size();
        if (key_size > std::numeric_limits<uint32_t>::max() || value_size > std::numeric_limits<uint32_t>::max()) {
            return Status::InvalidArgument("WAL key/value is too large");
        }
        size_t record_size = WalRecordHeader::kHeaderSize + key_size + value_size;
        
        // Check if we need to rotate
        if (current_file_size + record_size > options.max_file_size) {
            Status s = RotateLog();
            if (!s.ok()) return s;
        }
        
        // Prepare header
        WalRecordHeader header;
        header.type = type;
        header.sequence_number = seq;
        header.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        header.key_size = static_cast<uint32_t>(key_size);
        header.value_size = static_cast<uint32_t>(value_size);
        
        // Write to buffer
        char header_buf[WalRecordHeader::kHeaderSize];
        char* ptr = header_buf;
        *ptr++ = static_cast<char>(header.type);
        EncodeFixed64(ptr, header.sequence_number); ptr += 8;
        EncodeFixed64(ptr, header.timestamp); ptr += 8;
        EncodeFixed32(ptr, header.key_size); ptr += 4;
        EncodeFixed32(ptr, header.value_size); ptr += 4;
        
        // Calculate CRC32 of header + key + value
        std::string crc_data;
        crc_data.reserve(WalRecordHeader::kHeaderSize - 4 + key_size + value_size);
        crc_data.append(header_buf, WalRecordHeader::kHeaderSize - 4);
        crc_data.append(key.data(), key_size);
        crc_data.append(value.data(), value_size);
        header.crc32 = CalculateCRC32(crc_data.data(), crc_data.size());
        EncodeFixed32(ptr, header.crc32);
        
        // Write header
        if (buffer_offset + WalRecordHeader::kHeaderSize > write_buffer.size()) {
            FlushBuffer();
        }
        std::memcpy(write_buffer.data() + buffer_offset, header_buf, WalRecordHeader::kHeaderSize);
        buffer_offset += WalRecordHeader::kHeaderSize;
        
        // Write key
        if (buffer_offset + key_size > write_buffer.size()) {
            FlushBuffer();
        }
        if (key_size > 0) {
            std::memcpy(write_buffer.data() + buffer_offset, key.data(), key_size);
            buffer_offset += key_size;
        }
        
        // Write value
        if (buffer_offset + value_size > write_buffer.size()) {
            FlushBuffer();
        }
        if (value_size > 0) {
            std::memcpy(write_buffer.data() + buffer_offset, value.data(), value_size);
            buffer_offset += value_size;
        }
        
        current_file_size += record_size;
        total_writes++;
        total_bytes_written += record_size;
        
        if (options.sync_on_write) {
            FlushBuffer();
            Sync();
        }
        
        return Status::OK();
    }
    
    void FlushBuffer() {
        if (buffer_offset > 0 && current_file.is_open()) {
            current_file.write(write_buffer.data(), buffer_offset);
            buffer_offset = 0;
        }
    }
    
    Status Sync() {
        if (!current_file.is_open()) return Status::IOError("WAL file is not open");
        FlushBuffer();
        current_file.flush();
        if (!current_file.good()) return Status::IOError("Failed to flush WAL");

        // Best-effort OS-level durability after the C++ stream is flushed.
#ifdef _WIN32
        int fd = _open(current_filename.c_str(), _O_RDWR | _O_BINARY);
        if (fd < 0) return Status::IOError("Failed to open WAL for durable sync");
        const int rc = _commit(fd);
        _close(fd);
        if (rc != 0) return Status::IOError("Failed to commit WAL to disk");
#else
        int fd = ::open(current_filename.c_str(), O_RDWR);
        if (fd < 0) return Status::IOError("Failed to open WAL for durable sync");
        const int rc = ::fsync(fd);
        ::close(fd);
        if (rc != 0) return Status::IOError("Failed to sync WAL to disk");
#endif
        total_syncs++;
        return Status::OK();
    }
    
    Status RotateLog() {
        FlushBuffer();
        if (current_file.is_open()) {
            current_file.close();
        }
        current_file_number++;
        OpenNewLogFile();
        return Status::OK();
    }
    
    Status Recover(WalRecordCallback callback, uint64_t* max_sequence) {
        std::lock_guard<std::mutex> lock(mutex);
        
        // Find all WAL files
        std::vector<std::string> wal_files;
        for (const auto& entry : std::filesystem::directory_iterator(options.wal_dir)) {
            if (entry.is_regular_file()) {
                std::string filename = entry.path().filename().string();
                if (filename.rfind("wal_", 0) == 0 && filename.size() > 4 && 
                    filename.substr(filename.size() - 4) == ".log") {
                    wal_files.push_back(entry.path().string());
                }
            }
        }
        
        // Sort WAL files numerically, not lexicographically (wal_10 must follow wal_9).
        auto file_number = [](const std::string& path) -> uint64_t {
            const std::string name = std::filesystem::path(path).filename().string();
            const std::string number = name.substr(4, name.size() - 8);
            try { return std::stoull(number); } catch (...) { return 0; }
        };
        std::sort(wal_files.begin(), wal_files.end(), [&](const std::string& a, const std::string& b) {
            return file_number(a) < file_number(b);
        });
        
        uint64_t max_seq = 0;
        
        for (const auto& file_path : wal_files) {
            std::ifstream file(file_path, std::ios::binary);
            if (!file.is_open()) continue;
            
            while (file.good()) {
                // Read header
                char header_buf[WalRecordHeader::kHeaderSize];
                file.read(header_buf, WalRecordHeader::kHeaderSize);
                if (file.gcount() != static_cast<std::streamsize>(WalRecordHeader::kHeaderSize)) {
                    break; // End of file or partial record
                }
                
                // Parse header
                const char* ptr = header_buf;
                WalRecordHeader header;
                header.type = static_cast<WalRecordType>(static_cast<unsigned char>(*ptr++));
                header.sequence_number = DecodeFixed64(ptr); ptr += 8;
                header.timestamp = DecodeFixed64(ptr); ptr += 8;
                header.key_size = DecodeFixed32(ptr); ptr += 4;
                header.value_size = DecodeFixed32(ptr); ptr += 4;
                header.crc32 = DecodeFixed32(ptr);

                // Reject impossible record sizes before allocating memory.
                constexpr uint32_t kMaxFieldSize = 256U * 1024U * 1024U;
                if (header.key_size > kMaxFieldSize || header.value_size > kMaxFieldSize) {
                    return Status::Corruption("WAL record contains an invalid field size");
                }
                
                // Read key and value
                std::string key_data(header.key_size, '\0');
                std::string value_data(header.value_size, '\0');
                
                bool partial_record = false;
                if (header.key_size > 0) {
                    file.read(&key_data[0], header.key_size);
                    if (file.gcount() != static_cast<std::streamsize>(header.key_size)) partial_record = true;
                }
                if (!partial_record && header.value_size > 0) {
                    file.read(&value_data[0], header.value_size);
                    if (file.gcount() != static_cast<std::streamsize>(header.value_size)) partial_record = true;
                }
                if (partial_record) break; // tolerate a trailing partial record after a crash
                
                // Verify CRC32
                std::string crc_data;
                crc_data.reserve(WalRecordHeader::kHeaderSize - 4 + header.key_size + header.value_size);
                crc_data.append(header_buf, WalRecordHeader::kHeaderSize - 4);
                crc_data.append(key_data);
                crc_data.append(value_data);
                
                uint32_t calculated_crc = CalculateCRC32(crc_data.data(), crc_data.size());
                if (calculated_crc != header.crc32) {
                    return Status::Corruption("WAL CRC32 mismatch in " + file_path);
                }
                
                // Call callback
                Slice key_slice(key_data);
                Slice value_slice(value_data);
                callback(header, key_slice, value_slice);
                
                if (header.sequence_number > max_seq) {
                    max_seq = header.sequence_number;
                }
            }
        }
        
        if (max_sequence) {
            *max_sequence = max_seq;
        }
        
        return Status::OK();
    }
};

WriteAheadLog::WriteAheadLog(const WalOptions& options) 
    : impl_(std::make_unique<Impl>(options)) {}

WriteAheadLog::~WriteAheadLog() = default;

WriteAheadLog::WriteAheadLog(WriteAheadLog&& other) noexcept 
    : impl_(std::move(other.impl_)) {}

WriteAheadLog& WriteAheadLog::operator=(WriteAheadLog&& other) noexcept {
    if (this != &other) {
        impl_ = std::move(other.impl_);
    }
    return *this;
}

Status WriteAheadLog::Put(const Slice& key, const Slice& value, uint64_t sequence_number) {
    return impl_->WriteRecord(WalRecordType::kPut, key, value, sequence_number);
}

Status WriteAheadLog::Delete(const Slice& key, uint64_t sequence_number) {
    return impl_->WriteRecord(WalRecordType::kDelete, key, Slice(), sequence_number);
}

Status WriteAheadLog::BeginTransaction(uint64_t sequence_number) {
    return impl_->WriteRecord(WalRecordType::kBeginTransaction, Slice(), Slice(), sequence_number);
}

Status WriteAheadLog::CommitTransaction(uint64_t sequence_number) {
    return impl_->WriteRecord(WalRecordType::kCommitTransaction, Slice(), Slice(), sequence_number);
}

Status WriteAheadLog::RollbackTransaction(uint64_t sequence_number) {
    return impl_->WriteRecord(WalRecordType::kRollbackTransaction, Slice(), Slice(), sequence_number);
}

Status WriteAheadLog::Flush() {
    impl_->FlushBuffer();
    return Status::OK();
}

Status WriteAheadLog::Sync() {
    return impl_->Sync();
}

Status WriteAheadLog::Recover(WalRecordCallback callback, uint64_t* max_sequence) {
    return impl_->Recover(callback, max_sequence);
}

Status WriteAheadLog::RotateLog() {
    return impl_->RotateLog();
}

uint64_t WriteAheadLog::CurrentFileNumber() const {
    return impl_->current_file_number;
}

size_t WriteAheadLog::CurrentFileSize() const {
    return impl_->current_file_size;
}

WriteAheadLog::Stats WriteAheadLog::GetStats() const {
    Stats stats;
    stats.total_writes = impl_->total_writes;
    stats.total_bytes_written = impl_->total_bytes_written;
    stats.total_syncs = impl_->total_syncs;
    stats.current_file_number = impl_->current_file_number;
    stats.current_file_size = impl_->current_file_size;
    return stats;
}

} // namespace kv_engine