#include "wal/wal.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

using namespace kv_engine;

static std::string TempDir() {
    auto p = std::filesystem::temp_directory_path() / "kv_engine_wal_test";
    std::error_code ec;
    std::filesystem::remove_all(p, ec);
    std::filesystem::create_directories(p, ec);
    return p.string();
}

int main() {
    const std::string dir = TempDir();
    WalOptions options;
    options.wal_dir = dir;
    options.max_file_size = 120;
    options.buffer_size = 64;
    options.sync_on_write = false;

    {
        WriteAheadLog wal(options);
        for (uint64_t i = 1; i <= 20; ++i) {
            assert(wal.Put("key" + std::to_string(i), "value" + std::to_string(i), i).ok());
        }
        assert(wal.Sync().ok());
    }

    size_t file_count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.path().extension() == ".log") ++file_count;
    }
    assert(file_count >= 2); // Rotation is exercised.

    uint64_t max_seq = 0;
    size_t recovered = 0;
    {
        WriteAheadLog wal(options);
        Status s = wal.Recover([&](const WalRecordHeader& header, const Slice&, const Slice&) {
            (void)header;
            assert(header.sequence_number >= 1 && header.sequence_number <= 20);
            ++recovered;
        }, &max_seq);
        assert(s.ok());
        assert(recovered == 20);
        assert(max_seq == 20);
    }

    // Corrupt the first complete WAL record and ensure CRC validation catches it.
    const auto first = std::filesystem::path(dir) / "wal_0.log";
    {
        std::fstream file(first, std::ios::in | std::ios::out | std::ios::binary);
        assert(file.is_open());
        file.seekp(40); // Inside key/value bytes of the first record.
        char byte = 0;
        file.write(&byte, 1);
        file.flush();
    }

    {
        WriteAheadLog wal(options);
        Status s = wal.Recover([](const WalRecordHeader&, const Slice&, const Slice&) {}, nullptr);
        assert(s.code() == ErrorCode::CORRUPTION);
    }

    std::filesystem::remove_all(dir);
    std::cout << "WAL tests passed\n";
    return 0;
}
