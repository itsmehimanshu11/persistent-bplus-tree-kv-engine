#include "benchmark/benchmark.h"
#include <iostream>
#include <random>
#include <algorithm>
#include <map>
#include <unordered_map>
#include <functional>
#include <numeric>

namespace kv_engine {

Benchmark::Benchmark(KVEngine& engine) : engine_(engine) {}

std::vector<std::string> Benchmark::GenerateKeys(size_t count, size_t key_size, int seed) {
    std::vector<std::string> keys;
    keys.reserve(count);

    std::mt19937 rng(seed);
    std::uniform_int_distribution<char> dist('a', 'z');
    const std::string prefix = "key_";
    const size_t digits = std::max<size_t>(1, key_size > prefix.size() ? key_size - prefix.size() : 1);

    for (size_t i = 0; i < count; ++i) {
        std::string suffix = std::to_string(i);
        if (suffix.size() < digits) suffix.insert(suffix.begin(), digits - suffix.size(), '0');
        std::string key = prefix + suffix;
        // Keep keys at least the requested size without changing their ordering.
        while (key.size() < key_size) key.insert(prefix.size(), 1, dist(rng));
        keys.push_back(std::move(key));
    }
    return keys;
}

std::vector<std::string> Benchmark::GenerateValues(size_t count, size_t value_size) {
    std::vector<std::string> values;
    values.reserve(count);
    
    std::mt19937 rng(12345);
    std::uniform_int_distribution<char> dist('a', 'z');
    
    for (size_t i = 0; i < count; ++i) {
        std::string value;
        value.reserve(value_size);
        for (size_t j = 0; j < value_size; ++j) {
            value += dist(rng);
        }
        values.push_back(std::move(value));
    }
    
    return values;
}

std::vector<double> Benchmark::MeasureLatencies(const std::function<void()>& operation, size_t count) {
    std::vector<double> latencies;
    latencies.reserve(count);
    
    for (size_t i = 0; i < count; ++i) {
        auto start = std::chrono::high_resolution_clock::now();
        operation();
        auto end = std::chrono::high_resolution_clock::now();
        
        double latency_us = std::chrono::duration<double, std::micro>(end - start).count();
        latencies.push_back(latency_us);
    }
    
    return latencies;
}

BenchmarkResult Benchmark::CalculateStats(const std::string& name, size_t operations, 
                                          const std::vector<double>& latencies_us, 
                                          size_t memory_bytes) {
    BenchmarkResult result;
    result.name = name;
    result.operations = operations;
    
    if (latencies_us.empty() || operations == 0) return result;
    operations = std::min(operations, latencies_us.size());
    result.operations = operations;
    
    // Sort for percentiles
    std::vector<double> sorted = latencies_us;
    std::sort(sorted.begin(), sorted.end());
    
    double total_us = std::accumulate(sorted.begin(), sorted.end(), 0.0);
    result.duration_ms = total_us / 1000.0;
    result.ops_per_sec = operations / (result.duration_ms / 1000.0);
    result.avg_latency_us = total_us / operations;
    result.p50_latency_us = sorted[std::min(sorted.size() - 1, operations * 50 / 100)];
    result.p95_latency_us = sorted[std::min(sorted.size() - 1, operations * 95 / 100)];
    result.p99_latency_us = sorted[std::min(sorted.size() - 1, operations * 99 / 100)];
    result.memory_bytes = memory_bytes;
    
    return result;
}

BenchmarkResult Benchmark::RunInsertBenchmark(const BenchmarkConfig& config) {
    auto keys = GenerateKeys(config.num_operations, config.key_size, config.seed);
    auto values = GenerateValues(config.num_operations, config.value_size);
    
    std::vector<double> latencies;
    latencies.reserve(config.num_operations);
    
    for (size_t i = 0; i < config.num_operations; ++i) {
        auto start = std::chrono::high_resolution_clock::now();
        engine_.Put(keys[i], values[i]);
        auto end = std::chrono::high_resolution_clock::now();
        
        double latency_us = std::chrono::duration<double, std::micro>(end - start).count();
        latencies.push_back(latency_us);
    }
    
    return CalculateStats("Insert", config.num_operations, latencies, engine_.MemoryUsage());
}

BenchmarkResult Benchmark::RunLookupBenchmark(const BenchmarkConfig& config) {
    auto keys = GenerateKeys(config.num_operations, config.key_size, config.seed);
    auto values = GenerateValues(config.num_operations, config.value_size);
    
    // First insert all keys
    for (size_t i = 0; i < config.num_operations; ++i) {
        engine_.Put(keys[i], values[i]);
    }
    
    // Shuffle for random lookups
    std::mt19937 rng(config.seed + 1);
    std::shuffle(keys.begin(), keys.end(), rng);
    
    std::vector<double> latencies;
    latencies.reserve(config.num_operations);
    std::string value;
    
    for (size_t i = 0; i < config.num_operations; ++i) {
        auto start = std::chrono::high_resolution_clock::now();
        engine_.Get(keys[i], &value);
        auto end = std::chrono::high_resolution_clock::now();
        
        double latency_us = std::chrono::duration<double, std::micro>(end - start).count();
        latencies.push_back(latency_us);
    }
    
    return CalculateStats("Lookup", config.num_operations, latencies, engine_.MemoryUsage());
}

BenchmarkResult Benchmark::RunRangeScanBenchmark(const BenchmarkConfig& config) {
    auto keys = GenerateKeys(config.num_operations, config.key_size, config.seed);
    auto values = GenerateValues(config.num_operations, config.value_size);
    
    // Insert all keys
    for (size_t i = 0; i < config.num_operations; ++i) {
        engine_.Put(keys[i], values[i]);
    }
    
    // Perform range scans
    size_t num_scans = config.num_operations / 100; // 1% of operations as scans
    if (num_scans == 0) num_scans = 1;
    
    std::vector<double> latencies;
    latencies.reserve(num_scans);
    
    std::mt19937 rng(config.seed + 2);
    std::uniform_int_distribution<size_t> dist(0, keys.size() - 1);
    
    for (size_t i = 0; i < num_scans; ++i) {
        size_t start_idx = dist(rng);
        size_t end_idx = std::min(start_idx + 100, keys.size() - 1);
        
        auto start = std::chrono::high_resolution_clock::now();
        auto* iter = engine_.NewIterator(keys[start_idx], keys[end_idx]);
        size_t count = 0;
        iter->SeekToFirst();
        while (iter->Valid()) {
            iter->Next();
            count++;
        }
        delete iter;
        auto end = std::chrono::high_resolution_clock::now();
        
        double latency_us = std::chrono::duration<double, std::micro>(end - start).count();
        latencies.push_back(latency_us);
    }
    
    return CalculateStats("RangeScan", num_scans, latencies, engine_.MemoryUsage());
}

BenchmarkResult Benchmark::RunDeleteBenchmark(const BenchmarkConfig& config) {
    auto keys = GenerateKeys(config.num_operations, config.key_size, config.seed);
    auto values = GenerateValues(config.num_operations, config.value_size);
    
    // First insert all keys
    for (size_t i = 0; i < config.num_operations; ++i) {
        engine_.Put(keys[i], values[i]);
    }
    
    // Shuffle for random deletes
    std::mt19937 rng(config.seed + 3);
    std::shuffle(keys.begin(), keys.end(), rng);
    
    std::vector<double> latencies;
    latencies.reserve(config.num_operations);
    
    for (size_t i = 0; i < config.num_operations; ++i) {
        auto start = std::chrono::high_resolution_clock::now();
        engine_.Delete(keys[i]);
        auto end = std::chrono::high_resolution_clock::now();
        
        double latency_us = std::chrono::duration<double, std::micro>(end - start).count();
        latencies.push_back(latency_us);
    }
    
    return CalculateStats("Delete", config.num_operations, latencies, engine_.MemoryUsage());
}

BenchmarkResult Benchmark::RunMixedWorkloadBenchmark(const BenchmarkConfig& config) {
    auto keys = GenerateKeys(config.num_operations, config.key_size, config.seed);
    auto values = GenerateValues(config.num_operations, config.value_size);
    
    // Pre-populate with half the keys
    size_t pre_populate = config.num_operations / 2;
    for (size_t i = 0; i < pre_populate; ++i) {
        engine_.Put(keys[i], values[i]);
    }
    
    std::vector<double> latencies;
    latencies.reserve(config.num_operations);
    std::string value;
    
    std::mt19937 rng(config.seed + 4);
    std::uniform_real_distribution<double> op_dist(0.0, 1.0);
    std::uniform_int_distribution<size_t> key_dist(0, keys.size() - 1);
    
    for (size_t i = 0; i < config.num_operations; ++i) {
        double op = op_dist(rng);
        size_t key_idx = key_dist(rng);
        
        auto start = std::chrono::high_resolution_clock::now();
        
        if (op < config.read_ratio) {
            // Read operation
            engine_.Get(keys[key_idx], &value);
        } else {
            // Write operation (insert or update)
            engine_.Put(keys[key_idx], values[key_idx]);
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        double latency_us = std::chrono::duration<double, std::micro>(end - start).count();
        latencies.push_back(latency_us);
    }
    
    return CalculateStats("MixedWorkload", config.num_operations, latencies, engine_.MemoryUsage());
}

std::vector<BenchmarkResult> Benchmark::RunAllBenchmarks(const BenchmarkConfig& config) {
    std::vector<BenchmarkResult> results;
    
    std::cout << "Running benchmarks with " << config.num_operations << " operations...\n\n";
    
    // Create fresh engine for each benchmark to avoid interference
    // Note: In practice, you'd want to reset the engine between benchmarks
    
    std::cout << "[1/5] Insert benchmark...\n";
    results.push_back(RunInsertBenchmark(config));
    PrintResult(results.back());
    
    std::cout << "\n[2/5] Lookup benchmark...\n";
    results.push_back(RunLookupBenchmark(config));
    PrintResult(results.back());
    
    std::cout << "\n[3/5] Range Scan benchmark...\n";
    results.push_back(RunRangeScanBenchmark(config));
    PrintResult(results.back());
    
    std::cout << "\n[4/5] Delete benchmark...\n";
    results.push_back(RunDeleteBenchmark(config));
    PrintResult(results.back());
    
    std::cout << "\n[5/5] Mixed Workload benchmark...\n";
    results.push_back(RunMixedWorkloadBenchmark(config));
    PrintResult(results.back());
    
    return results;
}

BenchmarkResult Benchmark::RunStdMapComparison(const BenchmarkConfig& config) {
    auto keys = GenerateKeys(config.num_operations, config.key_size, config.seed);
    auto values = GenerateValues(config.num_operations, config.value_size);
    
    std::map<std::string, std::string> std_map;
    
    // Insert benchmark
    std::vector<double> latencies;
    latencies.reserve(config.num_operations);
    
    for (size_t i = 0; i < config.num_operations; ++i) {
        auto start = std::chrono::high_resolution_clock::now();
        std_map[keys[i]] = values[i];
        auto end = std::chrono::high_resolution_clock::now();
        
        double latency_us = std::chrono::duration<double, std::micro>(end - start).count();
        latencies.push_back(latency_us);
    }
    
    return CalculateStats("std::map Insert", config.num_operations, latencies, 0);
}

BenchmarkResult Benchmark::RunStdUnorderedMapComparison(const BenchmarkConfig& config) {
    auto keys = GenerateKeys(config.num_operations, config.key_size, config.seed);
    auto values = GenerateValues(config.num_operations, config.value_size);
    
    std::unordered_map<std::string, std::string> std_map;
    std_map.reserve(config.num_operations * 2);
    
    // Insert benchmark
    std::vector<double> latencies;
    latencies.reserve(config.num_operations);
    
    for (size_t i = 0; i < config.num_operations; ++i) {
        auto start = std::chrono::high_resolution_clock::now();
        std_map[keys[i]] = values[i];
        auto end = std::chrono::high_resolution_clock::now();
        
        double latency_us = std::chrono::duration<double, std::micro>(end - start).count();
        latencies.push_back(latency_us);
    }
    
    return CalculateStats("std::unordered_map Insert", config.num_operations, latencies, 0);
}

void Benchmark::PrintResult(const BenchmarkResult& result) {
    std::cout << "  " << result.name << ":\n";
    std::cout << "    Operations:     " << result.operations << "\n";
    std::cout << "    Duration:       " << result.duration_ms << " ms\n";
    std::cout << "    Throughput:     " << static_cast<size_t>(result.ops_per_sec) << " ops/sec\n";
    std::cout << "    Avg Latency:    " << result.avg_latency_us << " us\n";
    std::cout << "    P50 Latency:    " << result.p50_latency_us << " us\n";
    std::cout << "    P95 Latency:    " << result.p95_latency_us << " us\n";
    std::cout << "    P99 Latency:    " << result.p99_latency_us << " us\n";
    if (result.memory_bytes > 0) {
        std::cout << "    Memory:         " << result.memory_bytes << " bytes\n";
    }
}

void Benchmark::PrintComparison(const std::vector<BenchmarkResult>& results) {
    std::cout << "\n=== Benchmark Comparison ===\n\n";
    
    for (const auto& result : results) {
        PrintResult(result);
        std::cout << "\n";
    }
}

} // namespace kv_engine