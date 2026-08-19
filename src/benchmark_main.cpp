#include "benchmark/benchmark.h"
#include "kv_engine.h"
#include <filesystem>
#include <iostream>
#include <string>

static kv_engine::KVEngineOptions MakeOptions(const std::string& dir) {
    kv_engine::KVEngineOptions options;
    options.enable_wal = false; // Benchmark the in-memory engine without disk I/O.
    options.btree_options.max_keys_per_node = 64;
    options.wal_options.wal_dir = dir;
    return options;
}

int main() {
    const auto base = std::filesystem::temp_directory_path() / "kv_engine_benchmark";
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
    std::filesystem::create_directories(base, ec);

    kv_engine::BenchmarkConfig config;
    config.num_operations = 100000;
    config.key_size = 16;
    config.value_size = 100;
    config.seed = 42;

    std::cout << "=== KV Engine Benchmark ===\n";
    std::cout << "Operations: " << config.num_operations << "\n\n";

    {
        kv_engine::KVEngine engine(MakeOptions((base / "bplustree").string()));
        kv_engine::Benchmark benchmark(engine);
        auto result = benchmark.RunInsertBenchmark(config);
        kv_engine::Benchmark::PrintResult(result);
    }
    {
        kv_engine::KVEngine engine(MakeOptions((base / "lookup").string()));
        kv_engine::Benchmark benchmark(engine);
        auto result = benchmark.RunLookupBenchmark(config);
        kv_engine::Benchmark::PrintResult(result);
    }
    {
        kv_engine::KVEngine engine(MakeOptions((base / "scan").string()));
        kv_engine::Benchmark benchmark(engine);
        auto result = benchmark.RunRangeScanBenchmark(config);
        kv_engine::Benchmark::PrintResult(result);
    }
    {
        kv_engine::KVEngine engine(MakeOptions((base / "delete").string()));
        kv_engine::Benchmark benchmark(engine);
        auto result = benchmark.RunDeleteBenchmark(config);
        kv_engine::Benchmark::PrintResult(result);
    }
    {
        kv_engine::KVEngine engine(MakeOptions((base / "map").string()));
        kv_engine::Benchmark benchmark(engine);
        auto result = benchmark.RunStdMapComparison(config);
        kv_engine::Benchmark::PrintResult(result);
        auto unordered = benchmark.RunStdUnorderedMapComparison(config);
        kv_engine::Benchmark::PrintResult(unordered);
    }

    std::filesystem::remove_all(base, ec);
    return 0;
}
