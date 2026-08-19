#include "sstable/sstable.h"
#include <fstream>
#include <vector>
#include <algorithm>
#include <cstring>
#include <cstdint>

namespace kv_engine {

// Simple CRC32 implementation (if crc32c not available)
static uint32_t ComputeCRC32(const char* data, size_t length) {
    uint32_t crc = 0xFFFFFFFF;
    static const uint32_t table[256] = {
        0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA, 0x076DC419, 0x706AF48F,
        0xE963A535, 0x9E6495A3, 0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988,
        0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91, 0x1DB71064, 0x6AB020F2,
        0xF3B97148, 0x84BE41DE, 0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
        0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC, 0x14015C4F, 0x63066CD9,
        0xFA0F3D63, 0x8D080DF5, 0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172,
        0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B, 0x35B5A8FA, 0x42B2986C,
        0xDBBBC9D6, 0xACBCF940, 0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
        0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116, 0x21B4F4B5, 0x56B3C423,
        0xCFBA9599, 0xB8BDA50F, 0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924,
        0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D, 0x76DC4190, 0x01DB7106,
        0x98D220BC, 0xEFD5102A, 0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
        0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818, 0x7F6A0DBB, 0x086D3D2D,
        0x91646C97, 0xE6635C01, 0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E,
        0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457, 0x65B0D9C6, 0x12B7E950,
        0x8BBEB8EA, 0xFCB9887C, 0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
        0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2, 0x4ADFA541, 0x3DD895D7,
        0xA4D1C46D, 0xD3D6F4FB, 0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0,
        0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9, 0x5005713C, 0x270241AA,
        0xBE0B1010, 0xC90C2086, 0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
        0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4, 0x59B33D17, 0x2EB40D81,
        0xB7BD5C3B, 0xC0BA6CAD, 0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A,
        0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683, 0xE3630B12, 0x94643B84,
        0x0D6D6A3E, 0x7A6A5AA8, 0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
        0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE, 0xF762575D, 0x806567CB,
        0x196C3671, 0x6E6B06E7, 0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC,
        0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5, 0xD6D6A3E8, 0xA1D1937E,
        0x38D8C2C4, 0x4FDFF252, 0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
        0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60, 0xDF60EFC3, 0xA867DF55,
        0x316E8EEF, 0x4669BE79, 0xCB61F38E, 0xBC66C318, 0x256FD2A0, 0x5268E236,
        0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F, 0xC5BA3BBE, 0xB2BD0B28,
        0x2BB45A92, 0x5CB36A04, 0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
        0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A, 0x9C0906A9, 0xEB0E363F,
        0x72076785, 0x05005713, 0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38,
        0x92D28E90, 0xE5D5BE02, 0x7CDCEFB7, 0x0BDBDF21, 0x86D3D2D4, 0xF1D4E242,
        0x68DDB3F8, 0x1FDA836E, 0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
        0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C, 0x8F659EFF, 0xF862AE69,
        0x616BFFD3, 0x166CCF45, 0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2,
        0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB, 0xAED16A4A, 0xD9D65AD8,
        0x40DF0B66, 0x37D83BF0, 0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
        0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6, 0xBAD03605, 0xCDD70693,
        0x54DE5729, 0x23D967BF, 0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94,
        0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D
    };
    
    for (size_t i = 0; i < length; ++i) {
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return ~crc;
}

// BlockHandle implementation
std::string BlockHandle::Encode() const {
    std::string result;
    result.resize(16);
    char* ptr = &result[0];
    std::memcpy(ptr, &offset, 8);
    std::memcpy(ptr + 8, &size, 8);
    return result;
}

Status BlockHandle::Decode(const Slice& input, BlockHandle* handle) {
    if (input.size() < 16) {
        return Status::Corruption("BlockHandle too small");
    }
    const char* ptr = input.data();
    std::memcpy(&handle->offset, ptr, 8);
    std::memcpy(&handle->size, ptr + 8, 8);
    return Status::OK();
}

// SSTableFooter implementation
std::string SSTableFooter::Encode() const {
    std::string result;
    result.resize(kEncodedSize);
    char* ptr = &result[0];
    std::memcpy(ptr, &index_block.offset, 8);
    std::memcpy(ptr + 8, &index_block.size, 8);
    std::memcpy(ptr + 16, &filter_block.offset, 8);
    std::memcpy(ptr + 24, &filter_block.size, 8);
    std::memcpy(ptr + 32, &meta_block.offset, 8);
    std::memcpy(ptr + 40, &meta_block.size, 8);
    std::memcpy(ptr + 48, &magic_number, 8);
    std::memcpy(ptr + 56, &version, 4);
    std::memcpy(ptr + 60, &crc32, 4);
    return result;
}

Status SSTableFooter::Decode(const Slice& input, SSTableFooter* footer) {
    if (input.size() < kEncodedSize) {
        return Status::Corruption("Footer too small");
    }
    const char* ptr = input.data();
    std::memcpy(&footer->index_block.offset, ptr, 8);
    std::memcpy(&footer->index_block.size, ptr + 8, 8);
    std::memcpy(&footer->filter_block.offset, ptr + 16, 8);
    std::memcpy(&footer->filter_block.size, ptr + 24, 8);
    std::memcpy(&footer->meta_block.offset, ptr + 32, 8);
    std::memcpy(&footer->meta_block.size, ptr + 40, 8);
    std::memcpy(&footer->magic_number, ptr + 48, 8);
    std::memcpy(&footer->version, ptr + 56, 4);
    std::memcpy(&footer->crc32, ptr + 60, 4);
    
    if (footer->magic_number != 0xDBDBDBDBDBDBDBDB) {
        return Status::Corruption("Invalid magic number");
    }
    return Status::OK();
}

// ============================================================
// SSTableBuilder Implementation
// ============================================================

struct SSTableBuilder::Impl {
    SSTableOptions options;
    std::ofstream file;
    std::string filename;
    std::vector<char> buffer;
    std::vector<char> index_buffer;
    std::vector<char> filter_buffer;
    std::vector<uint64_t> filter_keys_hash;
    
    // Block building state
    std::vector<char> current_block;
    std::vector<uint32_t> restart_points;
    std::string last_key;
    size_t entries_in_block = 0;
    size_t block_offset = 0;
    
    // Index block state
    std::vector<std::pair<std::string, BlockHandle>> index_entries;
    
    // Meta
    uint64_t min_seq = UINT64_MAX;
    uint64_t max_seq = 0;
    std::string smallest_key_;
    std::string largest_key_;
    size_t num_entries_ = 0;
    
    Impl(const SSTableOptions& opts) : options(opts) {
        buffer.reserve(options.block_size);
        current_block.reserve(options.block_size);
    }
    
    // Accessors for builder
    uint64_t min_sequence() const { return min_seq; }
    uint64_t max_sequence() const { return max_seq; }
    size_t num_entries() const { return num_entries_; }
    const std::string& smallest_key() const { return smallest_key_; }
    const std::string& largest_key() const { return largest_key_; }
    
    void ResetBlock() {
        current_block.clear();
        restart_points.clear();
        restart_points.push_back(0);
        last_key.clear();
        entries_in_block = 0;
    }
    
    void AddToBlock(const Slice& key, const Slice& value, uint64_t seq, bool deleted) {
        // Compute shared prefix with last key
        size_t shared = 0;
        if (!last_key.empty()) {
            size_t min_len = std::min(key.size(), last_key.size());
            while (shared < min_len && key[shared] == last_key[shared]) {
                shared++;
            }
        }
        
        size_t unshared = key.size() - shared;
        
        // Write entry: shared_bytes (varint), unshared_bytes (varint), value_length (varint)
        // Then unshared key bytes, then value bytes
        // For simplicity, use fixed 4-byte encoding
        uint32_t shared_u32 = static_cast<uint32_t>(shared);
        uint32_t unshared_u32 = static_cast<uint32_t>(unshared);
        uint32_t value_len = static_cast<uint32_t>(value.size());
        uint32_t seq_u32 = static_cast<uint32_t>(seq);
        uint8_t deleted_flag = deleted ? 1 : 0;
        
        // Reserve space
        size_t needed = 4 + 4 + 4 + 4 + 1 + unshared + value.size();
        if (current_block.size() + needed > options.block_size && entries_in_block > 0) {
            // Block full, flush it
            FlushBlock();
        }
        
        // Write entry header
        char* ptr = &current_block[0] + current_block.size();
        current_block.resize(current_block.size() + 17);
        std::memcpy(ptr, &shared_u32, 4);
        std::memcpy(ptr + 4, &unshared_u32, 4);
        std::memcpy(ptr + 8, &value_len, 4);
        std::memcpy(ptr + 12, &seq_u32, 4);
        ptr[16] = deleted_flag;
        
        // Write unshared key
        current_block.insert(current_block.end(), key.data() + shared, key.data() + key.size());
        
        // Write value
        current_block.insert(current_block.end(), value.data(), value.data() + value.size());
        
        entries_in_block++;
        
        // Add restart point periodically
        if (entries_in_block % options.block_restart_interval == 0) {
            restart_points.push_back(current_block.size() - 17 - unshared - value.size());
        }
        
        last_key = key.ToString();
    }
    
    void FlushBlock() {
        if (entries_in_block == 0) return;
        
        // Write restart points at end of block
        for (uint32_t rp : restart_points) {
            char buf[4];
            std::memcpy(buf, &rp, 4);
            current_block.insert(current_block.end(), buf, buf + 4);
        }
        // Write number of restart points
        uint32_t num_restarts = static_cast<uint32_t>(restart_points.size());
        char buf[4];
        std::memcpy(buf, &num_restarts, 4);
        current_block.insert(current_block.end(), buf, buf + 4);
        
        // Write block to file
        BlockHandle handle(block_offset, current_block.size());
        file.write(current_block.data(), current_block.size());
        block_offset += current_block.size();
        
        // Add to index
        index_entries.emplace_back(last_key, handle);
        
        // Add to bloom filter
        if (options.enable_bloom_filter) {
            for (const auto& entry : index_entries) {
                // Simple hash for bloom filter
                uint64_t hash = 0;
                for (size_t i = 0; i < entry.first.size(); ++i) {
                    hash = hash * 31 + static_cast<unsigned char>(entry.first[i]);
                }
                filter_keys_hash.push_back(hash);
            }
        }
        
        ResetBlock();
    }
    
    void WriteIndexBlock() {
        // Build index block (similar format to data block)
        std::vector<char> index_data;
        std::string last_idx_key;
        std::vector<uint32_t> idx_restarts = {0};
        size_t idx_entries = 0;
        
        for (const auto& entry : index_entries) {
            const std::string& key = entry.first;
            const BlockHandle& handle = entry.second;
            
            size_t shared = 0;
            if (!last_idx_key.empty()) {
                size_t min_len = std::min(key.size(), last_idx_key.size());
                while (shared < min_len && key[shared] == last_idx_key[shared]) {
                    shared++;
                }
            }
            size_t unshared = key.size() - shared;
            
            uint32_t shared_u32 = static_cast<uint32_t>(shared);
            uint32_t unshared_u32 = static_cast<uint32_t>(unshared);
            uint32_t handle_size = static_cast<uint32_t>(handle.Encode().size());
            
            char* ptr = &index_data[0] + index_data.size();
            index_data.resize(index_data.size() + 12);
            std::memcpy(ptr, &shared_u32, 4);
            std::memcpy(ptr + 4, &unshared_u32, 4);
            std::memcpy(ptr + 8, &handle_size, 4);
            
            index_data.insert(index_data.end(), key.data() + shared, key.data() + key.size());
            index_data.insert(index_data.end(), handle.Encode().data(), handle.Encode().data() + handle.Encode().size());
            
            idx_entries++;
            if (idx_entries % options.block_restart_interval == 0) {
                idx_restarts.push_back(index_data.size() - 12 - unshared - handle_size);
            }
            
            last_idx_key = key;
        }
        
        // Write restart points
        for (uint32_t rp : idx_restarts) {
            char buf[4];
            std::memcpy(buf, &rp, 4);
            index_data.insert(index_data.end(), buf, buf + 4);
        }
        uint32_t num_restarts = static_cast<uint32_t>(idx_restarts.size());
        char buf[4];
        std::memcpy(buf, &num_restarts, 4);
        index_data.insert(index_data.end(), buf, buf + 4);
        
        // Write index block
        BlockHandle handle(block_offset, index_data.size());
        file.write(index_data.data(), index_data.size());
        block_offset += index_data.size();
        
        // Store for footer
        index_buffer = std::move(index_data);
    }
    
    void WriteFilterBlock() {
        if (!options.enable_bloom_filter || filter_keys_hash.empty()) {
            return;
        }
        
        // Simple bloom filter implementation
        size_t bits_per_key = static_cast<size_t>(options.bloom_filter_bits_per_key);
        size_t n = filter_keys_hash.size();
        size_t bits = n * bits_per_key;
        size_t bytes = (bits + 7) / 8;
        size_t k = static_cast<size_t>(bits_per_key * 0.69); // ln(2) ≈ 0.69
        
        std::vector<uint8_t> filter(bytes, 0);
        
        for (uint64_t hash : filter_keys_hash) {
            for (size_t i = 0; i < k; ++i) {
                uint64_t bit = (hash + i * 0x9e3779b97f4a7c15ULL) % bits;
                filter[bit / 8] |= (1 << (bit % 8));
            }
        }
        
        // Write filter data
        filter_buffer.assign(reinterpret_cast<char*>(filter.data()), 
                            reinterpret_cast<char*>(filter.data()) + filter.size());
        
        // Write k (number of hash functions) at end
        char k_buf[4];
        std::memcpy(k_buf, &k, 4);
        filter_buffer.insert(filter_buffer.end(), k_buf, k_buf + 4);
        
        // Write to file
        BlockHandle handle(block_offset, filter_buffer.size());
        file.write(filter_buffer.data(), filter_buffer.size());
        block_offset += filter_buffer.size();
    }
    
    void WriteMetaBlock() {
        // Write meta information (min/max sequence, key range, etc.)
        std::vector<char> meta;
        
        // Write min/max sequence
        char buf[8];
        std::memcpy(buf, &min_seq, 8);
        meta.insert(meta.end(), buf, buf + 8);
        std::memcpy(buf, &max_seq, 8);
        meta.insert(meta.end(), buf, buf + 8);
        
        // Write smallest/largest key lengths and keys
        uint32_t smallest_len = static_cast<uint32_t>(smallest_key_.size());
        uint32_t largest_len = static_cast<uint32_t>(largest_key_.size());
        std::memcpy(buf, &smallest_len, 4);
        meta.insert(meta.end(), buf, buf + 4);
        meta.insert(meta.end(), smallest_key_.data(), smallest_key_.data() + smallest_key_.size());
        std::memcpy(buf, &largest_len, 4);
        meta.insert(meta.end(), buf, buf + 4);
        meta.insert(meta.end(), largest_key_.data(), largest_key_.data() + largest_key_.size());
        
        // Write to file
        BlockHandle handle(block_offset, meta.size());
        file.write(meta.data(), meta.size());
        block_offset += meta.size();
    }
};

SSTableBuilder::SSTableBuilder(const SSTableOptions& options)
    : impl_(std::make_unique<Impl>(options)), options_(options) {}

SSTableBuilder::~SSTableBuilder() = default;

SSTableBuilder::SSTableBuilder(SSTableBuilder&& other) noexcept = default;
SSTableBuilder& SSTableBuilder::operator=(SSTableBuilder&& other) noexcept = default;

uint64_t SSTableBuilder::min_sequence() const {
    return impl_ ? impl_->min_sequence() : UINT64_MAX;
}

uint64_t SSTableBuilder::max_sequence() const {
    return impl_ ? impl_->max_sequence() : 0;
}

size_t SSTableBuilder::num_entries() const {
    return impl_ ? impl_->num_entries() : 0;
}

const std::string& SSTableBuilder::smallest_key() const {
    static const std::string empty;
    return impl_ ? impl_->smallest_key() : empty;
}

const std::string& SSTableBuilder::largest_key() const {
    static const std::string empty;
    return impl_ ? impl_->largest_key() : empty;
}

Status SSTableBuilder::Add(const Slice& key, const Slice& value, uint64_t sequence_number, bool is_deleted) {
    if (finished_ || abandoned_) {
        return Status::InvalidArgument("Builder already finished or abandoned");
    }
    
    if (impl_->num_entries() > 0) {
        // Check ordering
        int cmp = options_.comparator ? options_.comparator->Compare(key, Slice(impl_->last_key)) 
                                      : key.compare(Slice(impl_->last_key));
        if (cmp <= 0) {
            return Status::InvalidArgument("Keys must be added in strictly increasing order");
        }
    }
    
    // Update meta
    impl_->min_seq = std::min(impl_->min_seq, sequence_number);
    impl_->max_seq = std::max(impl_->max_seq, sequence_number);
    
    if (impl_->smallest_key().empty()) {
        // First entry - set smallest and largest
        // We need to access the member directly, so let's add a non-const accessor
        // For now, we'll track this in the builder class
    }
    // We'll track smallest/largest in the builder class instead
    
    // Add to current block
    impl_->AddToBlock(key, value, sequence_number, is_deleted);
    
    impl_->num_entries_++;
    return Status::OK();
}

Status SSTableBuilder::Finish(const std::string& filename) {
    if (finished_ || abandoned_) {
        return Status::InvalidArgument("Builder already finished or abandoned");
    }
    
    impl_->filename = filename;
    impl_->file.open(filename, std::ios::binary | std::ios::trunc);
    if (!impl_->file.is_open()) {
        return Status::IOError("Failed to open file: " + filename);
    }
    
    // Flush any remaining block
    impl_->FlushBlock();
    
    // Write index block
    impl_->WriteIndexBlock();
    BlockHandle index_handle(impl_->block_offset - impl_->index_buffer.size(), impl_->index_buffer.size());
    
    // Write filter block
    BlockHandle filter_handle(0, 0);
    if (options_.enable_bloom_filter) {
        size_t filter_start = impl_->block_offset;
        impl_->WriteFilterBlock();
        filter_handle = BlockHandle(filter_start, impl_->filter_buffer.size());
    }
    
    // Write meta block
    size_t meta_start = impl_->block_offset;
    impl_->WriteMetaBlock();
    BlockHandle meta_handle(meta_start, impl_->block_offset - meta_start);
    
    // Write footer
    SSTableFooter footer;
    footer.index_block = index_handle;
    footer.filter_block = filter_handle;
    footer.meta_block = meta_handle;
    footer.version = 1;
    
    // Compute CRC of footer (excluding CRC field itself)
    std::string footer_data = footer.Encode();
    footer.crc32 = ComputeCRC32(footer_data.data(), footer_data.size() - 4);
    
    impl_->file.write(footer_data.data(), footer_data.size());
    impl_->file.close();
    
    if (!impl_->file.good()) {
        return Status::IOError("Error writing SSTable file");
    }
    
    file_size_ = impl_->block_offset + footer_data.size();
    num_entries_ = impl_->num_entries();
    finished_ = true;
    
    return Status::OK();
}

void SSTableBuilder::Abandon() {
    abandoned_ = true;
    if (impl_->file.is_open()) {
        impl_->file.close();
    }
    if (!impl_->filename.empty()) {
        std::remove(impl_->filename.c_str());
    }
}

// ============================================================
// SSTableReader Implementation
// ============================================================

struct SSTableReader::Impl {
    Options options;
    std::string filename;
    std::vector<char> file_data; // Memory-mapped or loaded
    SSTableFooter footer;
    std::vector<char> index_block_data;
    std::vector<char> filter_block_data;
    std::vector<char> meta_block_data;
    size_t num_entries = 0;
    uint64_t min_seq = 0;
    uint64_t max_seq = 0;
    std::string smallest_key;
    std::string largest_key;
    
    // Parsed index entries
    struct IndexEntry {
        std::string key;
        BlockHandle handle;
    };
    std::vector<IndexEntry> index_entries;
    
    // Bloom filter
    std::vector<uint8_t> bloom_filter;
    size_t bloom_k = 0;
    size_t bloom_bits = 0;
    
    Impl(const Options& opts) : options(opts) {}
    
    Status LoadFile(const std::string& fname) {
        filename = fname;
        std::ifstream file(fname, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            return Status::IOError("Failed to open file: " + fname);
        }
        
        size_t file_size = file.tellg();
        if (file_size < SSTableFooter::kEncodedSize) {
            return Status::Corruption("File too small");
        }
        
        // Read footer
        file.seekg(file_size - SSTableFooter::kEncodedSize);
        std::vector<char> footer_data(SSTableFooter::kEncodedSize);
        file.read(footer_data.data(), footer_data.size());
        
        Status s = SSTableFooter::Decode(Slice(footer_data.data(), footer_data.size()), &footer);
        if (!s.ok()) return s;
        
        // Verify CRC
        uint32_t computed_crc = ComputeCRC32(footer_data.data(), footer_data.size() - 4);
        if (computed_crc != footer.crc32) {
            return Status::Corruption("Footer CRC mismatch");
        }
        
        // Read index block
        if (footer.index_block.size > 0) {
            file.seekg(footer.index_block.offset);
            index_block_data.resize(footer.index_block.size);
            file.read(index_block_data.data(), index_block_data.size());
            ParseIndexBlock();
        }
        
        // Read filter block
        if (footer.filter_block.size > 0) {
            file.seekg(footer.filter_block.offset);
            filter_block_data.resize(footer.filter_block.size);
            file.read(filter_block_data.data(), filter_block_data.size());
            ParseFilterBlock();
        }
        
        // Read meta block
        if (footer.meta_block.size > 0) {
            file.seekg(footer.meta_block.offset);
            meta_block_data.resize(footer.meta_block.size);
            file.read(meta_block_data.data(), meta_block_data.size());
            ParseMetaBlock();
        }
        
        file.close();
        return Status::OK();
    }
    
    void ParseIndexBlock() {
        // Parse index block (same format as data block)
        const char* data = index_block_data.data();
        size_t size = index_block_data.size();
        
        if (size < 4) return;
        
        // Read number of restart points from end
        uint32_t num_restarts;
        std::memcpy(&num_restarts, data + size - 4, 4);
        
        if (num_restarts == 0 || num_restarts > size / 4) return;
        
        // Read restart points
        std::vector<uint32_t> restarts(num_restarts);
        std::memcpy(restarts.data(), data + size - 4 - num_restarts * 4, num_restarts * 4);
        
        // Parse entries
        size_t pos = 0;
        for (size_t r = 0; r < num_restarts; ++r) {
            size_t restart_pos = restarts[r];
            size_t next_restart = (r + 1 < num_restarts) ? restarts[r + 1] : size - 4 - num_restarts * 4;
            
            pos = restart_pos;
            std::string last_key;
            
            while (pos < next_restart && pos + 12 <= size) {
                uint32_t shared, unshared, handle_size;
                std::memcpy(&shared, data + pos, 4);
                std::memcpy(&unshared, data + pos + 4, 4);
                std::memcpy(&handle_size, data + pos + 8, 4);
                pos += 12;
                
                if (pos + shared + unshared + handle_size > size) break;
                
                std::string key = last_key.substr(0, shared);
                key.append(data + pos, unshared);
                pos += unshared;
                
                BlockHandle handle;
                Status s = BlockHandle::Decode(Slice(data + pos, handle_size), &handle);
                if (!s.ok()) break;
                pos += handle_size;
                
                index_entries.push_back({key, handle});
                last_key = key;
            }
        }
        
        num_entries = index_entries.size();
        if (!index_entries.empty()) {
            smallest_key = index_entries.front().key;
            largest_key = index_entries.back().key;
        }
    }
    
    void ParseFilterBlock() {
        if (filter_block_data.size() < 4) return;
        
        // Last 4 bytes is k
        std::memcpy(&bloom_k, filter_block_data.data() + filter_block_data.size() - 4, 4);
        bloom_bits = (filter_block_data.size() - 4) * 8;
        bloom_filter.assign(filter_block_data.begin(), filter_block_data.end() - 4);
    }
    
    void ParseMetaBlock() {
        if (meta_block_data.size() < 24) return; // 8+8+4+key+4+key
        
        const char* data = meta_block_data.data();
        std::memcpy(&min_seq, data, 8);
        std::memcpy(&max_seq, data + 8, 8);
        
        size_t pos = 16;
        uint32_t smallest_len;
        std::memcpy(&smallest_len, data + pos, 4);
        pos += 4;
        if (pos + smallest_len <= meta_block_data.size()) {
            smallest_key.assign(data + pos, smallest_len);
            pos += smallest_len;
        }
        
        uint32_t largest_len;
        std::memcpy(&largest_len, data + pos, 4);
        pos += 4;
        if (pos + largest_len <= meta_block_data.size()) {
            largest_key.assign(data + pos, largest_len);
        }
    }
    
    bool BloomFilterCheck(const Slice& key) const {
        if (bloom_filter.empty() || bloom_k == 0) return true;
        
        // Compute hash
        uint64_t hash = 0;
        for (size_t i = 0; i < key.size(); ++i) {
            hash = hash * 31 + static_cast<unsigned char>(key[i]);
        }
        
        for (size_t i = 0; i < bloom_k; ++i) {
            uint64_t bit = (hash + i * 0x9e3779b97f4a7c15ULL) % bloom_bits;
            if ((bloom_filter[bit / 8] & (1 << (bit % 8))) == 0) {
                return false;
            }
        }
        return true;
    }
    
    Status GetFromBlock(const BlockHandle& handle, const Slice& key, 
                        std::string* value, uint64_t* seq, bool* deleted) const {
        // Read block data
        std::ifstream file(filename, std::ios::binary);
        if (!file.is_open()) {
            return Status::IOError("Failed to open file");
        }
        
        file.seekg(handle.offset);
        std::vector<char> block_data(handle.size);
        file.read(block_data.data(), handle.size);
        file.close();
        
        // Parse block (similar to index block parsing)
        const char* data = block_data.data();
        size_t size = block_data.size();
        
        if (size < 4) return Status::NotFound("Empty block");
        
        uint32_t num_restarts;
        std::memcpy(&num_restarts, data + size - 4, 4);
        
        if (num_restarts == 0 || num_restarts > size / 4) {
            return Status::Corruption("Invalid restart count");
        }
        
        std::vector<uint32_t> restarts(num_restarts);
        std::memcpy(restarts.data(), data + size - 4 - num_restarts * 4, num_restarts * 4);
        
        // Binary search in restart points
        size_t left = 0, right = num_restarts;
        
        while (left < right) {
            size_t mid = (left + right) / 2;
            // Need to get key at restart point - simplified: linear search for now
            // In production, would store restart keys separately
            left = mid + 1;
        }
        
        // Linear search from appropriate restart point
        size_t search_start = (left > 0) ? restarts[left - 1] : 0;
        size_t search_end = (left < num_restarts) ? restarts[left] : size - 4 - num_restarts * 4;
        
        std::string last_key;
        size_t pos = search_start;
        
        while (pos < search_end && pos + 12 <= size) {
            uint32_t shared, unshared, value_len, seq_u32;
            std::memcpy(&shared, data + pos, 4);
            std::memcpy(&unshared, data + pos + 4, 4);
            std::memcpy(&value_len, data + pos + 8, 4);
            std::memcpy(&seq_u32, data + pos + 12, 4);
            uint8_t deleted_flag = data[pos + 16];
            pos += 17;
            
            if (pos + shared + unshared + value_len > size) break;
            
            std::string cur_key = last_key.substr(0, shared);
            cur_key.append(data + pos, unshared);
            pos += unshared;
            
            int key_cmp = options.comparator ? options.comparator->Compare(Slice(cur_key), key) 
                                             : Slice(cur_key).compare(key);
            
            if (key_cmp == 0) {
                // Found!
                *value = std::string(data + pos, value_len);
                pos += value_len;
                if (seq) *seq = seq_u32;
                if (deleted) *deleted = deleted_flag;
                return Status::OK();
            } else if (key_cmp > 0) {
                // Key not in this block
                break;
            }
            
            pos += value_len;
            last_key = cur_key;
        }
        
        return Status::NotFound("Key not found in block");
    }
};

SSTableReader::SSTableReader(const Options& options)
    : impl_(std::make_unique<Impl>(options)), options_(options) {}

SSTableReader::~SSTableReader() = default;

SSTableReader::SSTableReader(SSTableReader&& other) noexcept = default;
SSTableReader& SSTableReader::operator=(SSTableReader&& other) noexcept = default;

Status SSTableReader::Open(const std::string& filename) {
    Status s = impl_->LoadFile(filename);
    if (!s.ok()) return s;
    
    file_number_ = 0; // Extract from filename if needed
    file_size_ = impl_->index_block_data.size() + impl_->filter_block_data.size() + 
                 impl_->meta_block_data.size() + SSTableFooter::kEncodedSize;
    num_entries_ = impl_->num_entries;
    min_sequence_ = impl_->min_seq;
    max_sequence_ = impl_->max_seq;
    smallest_key_ = impl_->smallest_key;
    largest_key_ = impl_->largest_key;
    
    return Status::OK();
}

Status SSTableReader::Get(const Slice& key, std::string* value, 
                          uint64_t* sequence_number, bool* is_deleted) const {
    // Check bloom filter first
    if (!impl_->BloomFilterCheck(key)) {
        return Status::NotFound("Key not found (bloom filter)");
    }
    
    // Binary search in index to find block
    int left = 0, right = static_cast<int>(impl_->index_entries.size()) - 1;
    int found_block = -1;
    
    while (left <= right) {
        int mid = (left + right) / 2;
        int cmp = options_.comparator ? options_.comparator->Compare(key, Slice(impl_->index_entries[mid].key))
                                      : key.compare(Slice(impl_->index_entries[mid].key));
        if (cmp <= 0) {
            found_block = mid;
            right = mid - 1;
        } else {
            left = mid + 1;
        }
    }
    
    if (found_block == -1) {
        // Key larger than all index entries, check last block
        found_block = impl_->index_entries.size() - 1;
    }
    
    if (found_block >= 0 && found_block < static_cast<int>(impl_->index_entries.size())) {
        return impl_->GetFromBlock(impl_->index_entries[found_block].handle, key, 
                                   value, sequence_number, is_deleted);
    }
    
    return Status::NotFound("Key not found");
}

bool SSTableReader::MightContain(const Slice& key) const {
    return impl_->BloomFilterCheck(key);
}

// Iterator implementation
struct SSTableReader::Iterator::Impl {
    const SSTableReader* reader = nullptr;
    const SSTableReader::Impl* impl = nullptr;
    size_t current_block_idx = 0;
    size_t current_entry_idx = 0;
    std::vector<char> current_block_data;
    std::string current_key;
    std::string current_value;
    uint64_t current_seq = 0;
    bool current_deleted = false;
    Status status_;
    bool valid_ = false;
    
    explicit Impl(const SSTableReader* r) : reader(r), impl(r->impl_.get()) {}
    
    void LoadBlock(size_t block_idx) {
        if (block_idx >= impl->index_entries.size()) {
            valid_ = false;
            return;
        }
        
        const auto& handle = impl->index_entries[block_idx].handle;
        std::ifstream file(impl->filename, std::ios::binary);
        if (!file.is_open()) {
            status_ = Status::IOError("Failed to open file");
            valid_ = false;
            return;
        }
        
        file.seekg(handle.offset);
        current_block_data.resize(handle.size);
        file.read(current_block_data.data(), handle.size);
        file.close();
        
        current_block_idx = block_idx;
        current_entry_idx = 0;
        ParseFirstEntry();
    }
    
    void ParseFirstEntry() {
        // Parse first entry in block
        const char* data = current_block_data.data();
        size_t size = current_block_data.size();
        
        if (size < 4) {
            valid_ = false;
            return;
        }
        
        uint32_t num_restarts;
        std::memcpy(&num_restarts, data + size - 4, 4);
        
        if (num_restarts == 0) {
            valid_ = false;
            return;
        }
        
        // Start at first restart point
        std::vector<uint32_t> restarts(num_restarts);
        std::memcpy(restarts.data(), data + size - 4 - num_restarts * 4, num_restarts * 4);
        
        size_t pos = restarts[0];
        std::string last_key;
        
        if (pos + 17 <= size) {
            uint32_t shared, unshared, value_len, seq_u32;
            std::memcpy(&shared, data + pos, 4);
            std::memcpy(&unshared, data + pos + 4, 4);
            std::memcpy(&value_len, data + pos + 8, 4);
            std::memcpy(&seq_u32, data + pos + 12, 4);
            uint8_t deleted_flag = data[pos + 16];
            pos += 17;
            
            if (pos + shared + unshared + value_len <= size) {
                current_key = last_key.substr(0, shared);
                current_key.append(data + pos, unshared);
                pos += unshared;
                current_value.assign(data + pos, value_len);
                current_seq = seq_u32;
                current_deleted = deleted_flag;
                valid_ = true;
                return;
            }
        }
        valid_ = false;
    }
    
    void NextEntry() {
        const char* data = current_block_data.data();
        size_t size = current_block_data.size();
        
        if (size < 4) {
            valid_ = false;
            return;
        }
        
        uint32_t num_restarts;
        std::memcpy(&num_restarts, data + size - 4, 4);
        std::vector<uint32_t> restarts(num_restarts);
        std::memcpy(restarts.data(), data + size - 4 - num_restarts * 4, num_restarts * 4);
        
        // Find current position and advance
        // Simplified: just invalidate for now
        valid_ = false;
    }
};

SSTableReader::Iterator::Iterator(const SSTableReader* reader)
    : impl_(std::make_unique<Impl>(reader)) {}

SSTableReader::Iterator::~Iterator() = default;

SSTableReader::Iterator::Iterator(Iterator&& other) noexcept = default;
SSTableReader::Iterator& SSTableReader::Iterator::operator=(Iterator&& other) noexcept = default;

bool SSTableReader::Iterator::Valid() const {
    return impl_->valid_ && impl_->status_.ok();
}

void SSTableReader::Iterator::SeekToFirst() {
    impl_->LoadBlock(0);
}

void SSTableReader::Iterator::SeekToLast() {
    if (impl_->impl->index_entries.empty()) {
        impl_->valid_ = false;
        return;
    }
    impl_->LoadBlock(impl_->impl->index_entries.size() - 1);
    // Would need to seek to last entry in block
    impl_->valid_ = false; // Simplified
}

void SSTableReader::Iterator::Seek(const Slice& target) {
    // Binary search in index
    int left = 0, right = static_cast<int>(impl_->impl->index_entries.size()) - 1;
    int found = 0;
    
    while (left <= right) {
        int mid = (left + right) / 2;
        int cmp = impl_->reader->options_.comparator ? 
            impl_->reader->options_.comparator->Compare(target, Slice(impl_->impl->index_entries[mid].key)) :
            target.compare(Slice(impl_->impl->index_entries[mid].key));
        if (cmp <= 0) {
            found = mid;
            right = mid - 1;
        } else {
            left = mid + 1;
        }
    }
    
    impl_->LoadBlock(found);
    // Would need to seek within block to target
}

void SSTableReader::Iterator::Next() {
    impl_->NextEntry();
    if (!impl_->valid_) {
        // Try next block
        if (impl_->current_block_idx + 1 < impl_->impl->index_entries.size()) {
            impl_->LoadBlock(impl_->current_block_idx + 1);
        }
    }
}

void SSTableReader::Iterator::Prev() {
    // Not implemented for simplicity
    impl_->status_ = Status::NotSupported("Prev not implemented");
}

Slice SSTableReader::Iterator::key() const {
    return Slice(impl_->current_key);
}

Slice SSTableReader::Iterator::value() const {
    return Slice(impl_->current_value);
}

uint64_t SSTableReader::Iterator::sequence_number() const {
    return impl_->current_seq;
}

bool SSTableReader::Iterator::is_deleted() const {
    return impl_->current_deleted;
}

Status SSTableReader::Iterator::status() const {
    return impl_->status_;
}

SSTableReader::Iterator* SSTableReader::NewIterator() const {
    return new Iterator(this);
}

SSTableReader::Stats SSTableReader::GetStats() const {
    Stats stats;
    stats.num_entries = impl_->num_entries;
    stats.index_size = impl_->index_block_data.size();
    stats.filter_size = impl_->filter_block_data.size();
    stats.data_size = file_size_ - stats.index_size - stats.filter_size - 
                      impl_->meta_block_data.size() - SSTableFooter::kEncodedSize;
    stats.num_data_blocks = impl_->index_entries.size();
    return stats;
}

} // namespace kv_engine