#include "btree/btree.h"
#include <iostream>
#include <vector>
#include <string>
#include <random>
#include <chrono>
#include <cassert>
#include <cstdio>

using namespace kv_engine;

void TestBasicOperations() {
    std::cout << "=== TestBasicOperations ===\n";
    BPlusTree tree;
    
    // Put some values
    assert(tree.Put("key1", "value1").ok());
    assert(tree.Put("key2", "value2").ok());
    assert(tree.Put("key3", "value3").ok());
    
    // Get values
    std::string value;
    assert(tree.Get("key1", &value).ok());
    assert(value == "value1");
    
    assert(tree.Get("key2", &value).ok());
    assert(value == "value2");
    
    assert(tree.Get("key3", &value).ok());
    assert(value == "value3");
    
    // Non-existent key
    assert(tree.Get("key4", &value).code() == ErrorCode::NOT_FOUND);
    
    // Update existing
    assert(tree.Put("key2", "new_value2").ok());
    assert(tree.Get("key2", &value).ok());
    assert(value == "new_value2");
    
    // Delete
    assert(tree.Delete("key2").ok());
    assert(tree.Get("key2", &value).code() == ErrorCode::NOT_FOUND);
    
    // Size
    assert(tree.Size() == 2);
    
    std::cout << "PASSED\n\n";
}

void TestRangeScan() {
    std::cout << "=== TestRangeScan ===\n";
    BPlusTree tree;
    
    // Insert ordered keys
    for (int i = 0; i < 100; ++i) {
        char key[16];
        char val[16];
        snprintf(key, sizeof(key), "key%04d", i);
        snprintf(val, sizeof(val), "val%04d", i);
        assert(tree.Put(key, val).ok());
    }
    
    // Full scan
    int count = 0;
    auto* iter = tree.NewIterator();
    for (iter->SeekToFirst(); iter->Valid(); iter->Next()) {
        count++;
    }
    assert(count == 100);
    delete iter;
    
    // Range scan
    count = 0;
    iter = tree.NewIterator("key0010", "key0020");
    for (iter->SeekToFirst(); iter->Valid(); iter->Next()) {
        count++;
    }
    assert(count == 10); // key0010 to key0019
    delete iter;
    
    std::cout << "PASSED\n\n";
}

void TestLargeDataset() {
    std::cout << "=== TestLargeDataset ===\n";
    BPlusTree tree;
    
    const int N = 10000;
    std::vector<std::string> keys;
    keys.reserve(N);
    
    // Generate random keys
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> dist(0, 1000000);
    
    for (int i = 0; i < N; ++i) {
        char key[32];
        snprintf(key, sizeof(key), "key%08d", dist(rng));
        keys.push_back(key);
    }
    
    // Insert all
    auto start = std::chrono::high_resolution_clock::now();
    for (const auto& k : keys) {
        (void)k; // Explicitly mark as used
        assert(tree.Put(k, "value").ok());
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto insert_time = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    
    // Verify all
    start = std::chrono::high_resolution_clock::now();
    for (const auto& k : keys) {
        (void)k; // Explicitly mark as used
        std::string value;
        assert(tree.Get(k, &value).ok());
        assert(value == "value");
    }
    end = std::chrono::high_resolution_clock::now();
    auto get_time = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    
    std::cout << "Inserted " << N << " keys in " << insert_time << "ms\n";
    std::cout << "Retrieved " << N << " keys in " << get_time << "ms\n";
    std::cout << "Tree height: " << tree.Height() << ", nodes: " << tree.NodeCount() << "\n";
    std::cout << "Memory usage: " << tree.MemoryUsage() / 1024 << " KB\n";
    
    std::cout << "PASSED\n\n";
}

void TestSplitAndHeight() {
    std::cout << "=== TestSplitAndHeight ===\n";
    BPlusTree tree;
    
    // Insert enough to cause splits and height increase
    for (int i = 0; i < 1000; ++i) {
        char key[16];
        snprintf(key, sizeof(key), "key%05d", i);
        auto s = tree.Put(key, "value");
        if (!s.ok()) {
            std::cout << "Put failed at i=" << i << ": " << s.ToString() << "\n";
        }
        assert(s.ok());
        if (i < 10 || i % 200 == 0) {
            std::cout << "  After " << i+1 << " inserts: height=" << tree.Height() 
                      << ", nodes=" << tree.NodeCount() << ", size=" << tree.Size() << "\n";
        }
    }
    
    std::cout << "Height after 1000 inserts: " << tree.Height() << "\n";
    std::cout << "Nodes: " << tree.NodeCount() << ", Size: " << tree.Size() << "\n";
    tree.Print();
    assert(tree.Height() >= 2); // Should have split at least once
    
    // Verify all still accessible
    for (int i = 0; i < 1000; ++i) {
        char key[16];
        snprintf(key, sizeof(key), "key%05d", i);
        std::string value;
        assert(tree.Get(key, &value).ok());
    }
    
    std::cout << "PASSED\n\n";
}

void TestDelete() {
    std::cout << "=== TestDelete ===\n";
    BPlusTree tree;
    
    for (int i = 0; i < 100; ++i) {
        char key[16];
        snprintf(key, sizeof(key), "key%04d", i);
        assert(tree.Put(key, "value").ok());
    }
    
    // Delete every other key
    for (int i = 0; i < 100; i += 2) {
        char key[16];
        snprintf(key, sizeof(key), "key%04d", i);
        assert(tree.Delete(key).ok());
    }
    
    // Verify remaining
    for (int i = 1; i < 100; i += 2) {
        char key[16];
        snprintf(key, sizeof(key), "key%04d", i);
        std::string value;
        assert(tree.Get(key, &value).ok());
    }
    
    // Verify deleted
    for (int i = 0; i < 100; i += 2) {
        char key[16];
        snprintf(key, sizeof(key), "key%04d", i);
        std::string value;
        assert(tree.Get(key, &value).code() == ErrorCode::NOT_FOUND);
    }
    
    std::cout << "PASSED\n\n";
}

void TestStressDeleteAndRebalance() {
    std::cout << "=== TestStressDeleteAndRebalance ===\n";
    BTreeOptions options;
    options.max_keys_per_node = 8;
    BPlusTree tree(options);

    const int N = 1000;
    for (int i = 0; i < N; ++i) {
        char key[32];
        snprintf(key, sizeof(key), "key%06d", i);
        assert(tree.Put(key, "value").ok());
    }
    assert(tree.Verify());

    std::vector<int> order;
    order.reserve(N);
    for (int i = 0; i < N; ++i) order.push_back(i);
    std::mt19937 rng(123);
    std::shuffle(order.begin(), order.end(), rng);

    for (int i : order) {
        char key[32];
        snprintf(key, sizeof(key), "key%06d", i);
        assert(tree.Delete(key).ok());
        assert(tree.Verify());
    }
    assert(tree.Size() == 0);
    assert(tree.Height() == 1);
    assert(tree.NodeCount() == 1);
    std::cout << "PASSED\n\n";
}

int main() {
    std::cout << "Running B+ Tree Tests...\n\n";
    
    // Quick debug test
    {
        std::cout << "=== Debug Test ===\n";
        BPlusTree tree;
        for (int i = 0; i < 10; ++i) {
            char key[16];
            snprintf(key, sizeof(key), "key%05d", i);
            tree.Put(key, "value");
        }
        std::cout << "Height: " << tree.Height() << ", Nodes: " << tree.NodeCount() << "\n";
        tree.Print();
    }
    
    TestBasicOperations();
    TestRangeScan();
    TestSplitAndHeight();
    TestDelete();
    TestStressDeleteAndRebalance();
    TestLargeDataset();
    
    std::cout << "=== ALL TESTS PASSED ===\n";
    return 0;
}
