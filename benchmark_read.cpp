#include "bplustree.hpp"
#include <iostream>
#include <vector>
#include <algorithm>
#include <chrono>
#include <random>
#include <map>
#include <unordered_map>
#include <iomanip>
#include <string>

using namespace std;

//  global arena
Arena* global_arena = nullptr;
const size_t ARENA_SIZE = 1ULL * 1024 * 1024 * 1024; // 1 GB

struct BenchmarkResult {
    string name;
    string strategy;
    long long avg_ns;
    long long max_ns;
    long long p50_ns;
    long long p90_ns;
    long long p99_ns;
    long long p999_ns;
    long long total_ms;
};

// Heavy work to wake up CPU cores and caches
void pre_warm_cpu() {
    cout << "----------------------------------------" << endl;
    cout << "Pre-warming CPU to force max frequency..." << endl;
    
    volatile long long counter = 0;
    vector<int> data(10000, 1);
    
    // Run for approx 2 seconds
    auto start = chrono::high_resolution_clock::now();
    while (true) {
        auto now = chrono::high_resolution_clock::now();
        if (chrono::duration_cast<chrono::milliseconds>(now - start).count() > 2000)
            break;

        // Simulate some DP-like dependency chain
        for (size_t i = 2; i < data.size(); i++) {
            data[i] = (data[i-1] + data[i-2]) % 123456;
            counter += data[i];
        }
    }
    cout << "Pre-warming complete. Result: " << counter << endl;
    cout << "----------------------------------------\n" << endl;
}

template<typename Func>
BenchmarkResult run_benchmark(string name, string strategy, int N, const vector<int>& keys, Func lookup_func) {
    long long total_time = 0;
    long long max_time = 0;
    long long found_count = 0; // Changed to long long to prevent overflow although int is enough for N=1M
    vector<long long> latencies;
    latencies.reserve(N);

    auto benchmark_start = chrono::high_resolution_clock::now();

    for (int i = 0; i < N; i++) {
        auto start = chrono::high_resolution_clock::now();
        bool found = lookup_func(keys[i]);
        auto end = chrono::high_resolution_clock::now();

        if (found) found_count++;

        long long latency = chrono::duration_cast<chrono::nanoseconds>(end - start).count();
        latencies.push_back(latency);
        total_time += latency;
        if (latency > max_time) {
            max_time = latency;
        }
        
        // Prevent dead code elimination of the 'found' bool
        // This is a minimal side effect (asm volatile) to force compiler to value 'found'
        // But cleaner is to just use found_count at the end.
    }

    auto benchmark_end = chrono::high_resolution_clock::now();
    
    // Use found_count to prevent optimization
    // We can just print it or add it to result. 
    // Let's print it to stderr so it doesn't mess up table but ensures usage.
    // Or simpler: put it in volatile
    volatile long long keep_alive = found_count; 
    (void)keep_alive;

    sort(latencies.begin(), latencies.end());
    
    return {
        name,
        strategy,
        total_time / N,
        max_time,
        latencies[(size_t)(N * 0.50)], // p50
        latencies[(size_t)(N * 0.90)], // p90
        latencies[(size_t)(N * 0.99)], // p99
        latencies[(size_t)(N * 0.999)], // p99.9
        chrono::duration_cast<chrono::milliseconds>(benchmark_end - benchmark_start).count()
    };
}

void print_table(const vector<BenchmarkResult>& results) {
    // 20 + 20 + 6*12 + 10 = ~122
    cout << "\n" << string(130, '=') << endl;
    cout << left << setw(20) << "Container" 
         << setw(20) << "Strategy" 
         << setw(12) << "Avg(ns)" 
         << setw(12) << "P50(ns)" 
         << setw(12) << "P90(ns)" 
         << setw(12) << "P99(ns)" 
         << setw(12) << "P99.9(ns)" 
         << setw(12) << "Max(ns)" 
         << setw(12) << "Total(ms)" << endl;
    cout << string(130, '-') << endl;

    for (const auto& res : results) {
        cout << left << setw(20) << res.name 
             << setw(20) << res.strategy 
             << setw(12) << res.avg_ns
             << setw(12) << res.p50_ns
             << setw(12) << res.p90_ns
             << setw(12) << res.p99_ns
             << setw(12) << res.p999_ns
             << setw(12) << res.max_ns
             << setw(12) << res.total_ms << endl;
    }
    cout << string(130, '=') << endl;
}

int main()
{
    // Pre-warm the CPU
    pre_warm_cpu();

    // initialize Arena allocator
    cout << "========================================" << endl;
    cout << "INITIALIZING ARENA ALLOCATOR" << endl;
    cout << "========================================" << endl;
    global_arena = new Arena(ARENA_SIZE);
    cout << "Arena capacity: " << (global_arena->get_capacity() / (1024.0 * 1024 * 1024)) << " GB" << endl;
    cout << "========================================\n" << endl;

    const int N = 1000000;
    vector<BenchmarkResult> results;

    // Generate random keys
    vector<int> random_keys(N);
    random_device rd;
    mt19937 gen(rd());
    uniform_int_distribution<int> dist(1, N * 10);
    
    cout << "Generating " << N << " random keys..." << endl;
    for (int i = 0; i < N; i++) {
        random_keys[i] = dist(gen);
    }

    // --- SETUP CONTAINERS ---
    cout << "Populating containers..." << endl;

    // 1. B+ Tree
    cout << "  - Inserting into B+ Tree (Arena)..." << endl;
    BPlusTree<int, int> tree;
    for (int i = 0; i < N; i++) {
        tree.insert(random_keys[i], random_keys[i] * 10);
    }

    // 2. std::map
    cout << "  - Inserting into std::map..." << endl;
    map<int, int> stl_map;
    for (int i = 0; i < N; i++) {
        stl_map[random_keys[i]] = random_keys[i] * 10;
    }

    // 3. std::unordered_map
    cout << "  - Inserting into std::unordered_map..." << endl;
    unordered_map<int, int> stl_unordered_map;
    stl_unordered_map.reserve(N); // Give it a fair chance
    for (int i = 0; i < N; i++) {
        stl_unordered_map[random_keys[i]] = random_keys[i] * 10;
    }

    // --- STRATEGY 1: RANDOM READ ---
    cout << "\nRunning Random Read Benchmark..." << endl;
    vector<int> query_keys = random_keys;
    shuffle(query_keys.begin(), query_keys.end(), gen);

    // B+ Tree
    results.push_back(run_benchmark("B+ Tree (SIMD)", "Random Read", N, query_keys, [&](int key) {
        int val;
        return tree.findSIMD(key, val);
    }));

    // std::map
    results.push_back(run_benchmark("std::map", "Random Read", N, query_keys, [&](int key) {
        auto it = stl_map.find(key);
        return it != stl_map.end();
    }));

    // std::unordered_map
    results.push_back(run_benchmark("std::unordered_map", "Random Read", N, query_keys, [&](int key) {
        auto it = stl_unordered_map.find(key);
        return it != stl_unordered_map.end();
    }));


    // --- STRATEGY 2: SEQUENTIAL READ (Sorted) ---
    cout << "Running Sequential Read Benchmark..." << endl;
    sort(query_keys.begin(), query_keys.end());

    // B+ Tree
    results.push_back(run_benchmark("B+ Tree (SIMD)", "Sequential Read", N, query_keys, [&](int key) {
        int val;
        return tree.findSIMD(key, val);
    }));

    // std::map
    results.push_back(run_benchmark("std::map", "Sequential Read", N, query_keys, [&](int key) {
        auto it = stl_map.find(key);
        return it != stl_map.end();
    }));

    // std::unordered_map
    results.push_back(run_benchmark("std::unordered_map", "Sequential Read", N, query_keys, [&](int key) {
        auto it = stl_unordered_map.find(key);
        return it != stl_unordered_map.end();
    }));

    // --- PRINT RESULTS ---
    print_table(results);

    // Cleanup arena
    delete global_arena;
    global_arena = nullptr;

    return 0;
}
