#pragma once

#include "kv_engine.h"
#include <string>
#include <vector>
#include <chrono>
#include <map>

namespace kv_engine {

struct BenchmarkResult {
    std::string name;
    size_t operations;
    double duration_ms;
    double ops_per_sec;
    double avg_latency_us;
    double p50_latency_us;
    double p95_latency_us;
    double p99_latency_us;
    size_t memory_bytes;
};

struct BenchmarkConfig {
    size_t num_operations = 100000;
    size_t key_size = 16;
    size_t value_size = 100;
    int seed = 42;
    bool use_random_keys = true;
    double read_ratio = 0.5; // For mixed workload
};

class Benchmark {
public:
    explicit Benchmark(KVEngine& engine);
    ~Benchmark() = default;

    // Run individual benchmarks
    BenchmarkResult RunInsertBenchmark(const BenchmarkConfig& config);
    BenchmarkResult RunLookupBenchmark(const BenchmarkConfig& config);
    BenchmarkResult RunRangeScanBenchmark(const BenchmarkConfig& config);
    BenchmarkResult RunDeleteBenchmark(const BenchmarkConfig& config);
    BenchmarkResult RunMixedWorkloadBenchmark(const BenchmarkConfig& config);
    
    // Run all benchmarks
    std::vector<BenchmarkResult> RunAllBenchmarks(const BenchmarkConfig& config = BenchmarkConfig());

    // Compare with std containers
    BenchmarkResult RunStdMapComparison(const BenchmarkConfig& config);
    BenchmarkResult RunStdUnorderedMapComparison(const BenchmarkConfig& config);

    // Print results
    static void PrintResult(const BenchmarkResult& result);
    static void PrintComparison(const std::vector<BenchmarkResult>& results);

private:
    KVEngine& engine_;

    // Helper functions
    std::vector<std::string> GenerateKeys(size_t count, size_t key_size, int seed);
    std::vector<std::string> GenerateValues(size_t count, size_t value_size);
    std::vector<double> MeasureLatencies(const std::function<void()>& operation, size_t count);
    BenchmarkResult CalculateStats(const std::string& name, size_t operations, 
                                   const std::vector<double>& latencies_us, 
                                   size_t memory_bytes);
};

} // namespace kv_engine