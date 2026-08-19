#include "kv_engine.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <vector>

using namespace kv_engine;

static std::string TempDir(const char* name) {
    const auto dir = std::filesystem::temp_directory_path() / name;
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    return dir.string();
}

static KVEngineOptions MakeOptions(const std::string& dir) {
    KVEngineOptions o;
    o.enable_wal = true;
    o.wal_options.wal_dir = dir;
    o.wal_options.buffer_size = 1024;
    o.wal_options.max_file_size = 16 * 1024;
    o.wal_options.sync_on_write = false;
    o.btree_options.max_keys_per_node = 16;
    return o;
}

void TestPersistenceRecovery() {
    const std::string dir = TempDir("kv_engine_recovery_test");
    {
        KVEngine engine(MakeOptions(dir));
        assert(engine.Put("user:1", "Alice").ok());
        assert(engine.Put("user:2", "Bob").ok());
        assert(engine.Put("user:3", "Charlie").ok());
        assert(engine.Delete("user:2").ok());
        assert(engine.Sync().ok());
    }
    {
        KVEngine engine(MakeOptions(dir));
        std::string value;
        assert(engine.Get("user:1", &value).ok() && value == "Alice");
        assert(engine.Get("user:3", &value).ok() && value == "Charlie");
        assert(engine.Get("user:2", &value).code() == ErrorCode::NOT_FOUND);
        assert(engine.Verify());
    }
    std::filesystem::remove_all(dir);
}

void TestRangeAndIntegrity() {
    const std::string dir = TempDir("kv_engine_range_test");
    KVEngine engine(MakeOptions(dir));
    for (int i = 0; i < 200; ++i) {
        char key[32];
        std::snprintf(key, sizeof(key), "key%04d", i);
        assert(engine.Put(key, "value").ok());
    }
    assert(engine.Verify());
    auto* iter = engine.NewIterator("key0050", "key0100");
    size_t count = 0;
    for (iter->SeekToFirst(); iter->Valid(); iter->Next()) ++count;
    delete iter;
    assert(count == 50);
    std::filesystem::remove_all(dir);
}

void TestConcurrentAccess() {
    const std::string dir = TempDir("kv_engine_concurrency_test");
    KVEngine engine(MakeOptions(dir));
    for (int i = 0; i < 100; ++i) {
        assert(engine.Put("key" + std::to_string(i), "initial").ok());
    }

    std::vector<std::thread> readers;
    for (int t = 0; t < 4; ++t) {
        readers.emplace_back([&engine, t] {
            for (int i = 0; i < 2000; ++i) {
                std::string value;
                engine.Get("key" + std::to_string((i + t) % 100), &value);
            }
        });
    }
    for (auto& thread : readers) thread.join();

    std::vector<std::thread> writers;
    for (int t = 0; t < 2; ++t) {
        writers.emplace_back([&engine, t] {
            for (int i = 0; i < 500; ++i) {
                engine.Put("writer:" + std::to_string(t) + ":" + std::to_string(i), "v");
            }
        });
    }
    for (auto& thread : writers) thread.join();
    assert(engine.Verify());
    std::filesystem::remove_all(dir);
}

int main() {
    std::cout << "Running KV Engine integration tests...\n";
    TestPersistenceRecovery();
    std::cout << "  Persistence recovery: PASS\n";
    TestRangeAndIntegrity();
    std::cout << "  Range + integrity: PASS\n";
    TestConcurrentAccess();
    std::cout << "  Concurrent access: PASS\n";
    std::cout << "ALL KV ENGINE TESTS PASSED\n";
    return 0;
}
