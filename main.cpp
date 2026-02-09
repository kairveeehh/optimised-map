#include "bplustree.hpp"
#include <iostream>
#include <vector>
#include <algorithm>
#include <chrono>
#include <random>

using namespace std;

// define global arena
Arena* global_arena = nullptr;
const size_t ARENA_SIZE = 1ULL * 1024 * 1024 * 1024; // 1 GB

int main()
{
    // intialize Arena 
    cout << "========================================" << endl;
    cout << "INITIALIZING ARENA ALLOCATOR" << endl;
    cout << "========================================" << endl;
    global_arena = new Arena(ARENA_SIZE);
    cout << "Arena capacity: " << (global_arena->get_capacity() / (1024.0 * 1024 * 1024)) << " GB" << endl;
    cout << "========================================\n" << endl;

    BPlusTree<int, int> tree;
    const int N = 1000000;

    // ==================== INSERTION LATENCY BENCHMARK ====================
    cout << "========================================" << endl;
    cout << "INSERTION LATENCY BENCHMARK (WITH ARENA)" << endl;
    cout << "========================================" << endl;
    
    // generate random keys
    vector<int> random_keys(N);
    random_device rd;
    mt19937 gen(rd());
    uniform_int_distribution<int> dist(1, N * 10);
    
    for (int i = 0; i < N; i++) {
        random_keys[i] = dist(gen);
    }
    
    cout << "Generated " << N << " random keys for insertion..." << endl;
    
    // track insertion latencies
    vector<long long> insertion_times(N);
    long long total_insertion_time = 0;
    long long max_insertion_time = 0;
    
    auto benchmark_start = chrono::high_resolution_clock::now();
    
    for (int i = 0; i < N; i++) {
        auto start = chrono::high_resolution_clock::now();
        tree.insert(random_keys[i], random_keys[i] * 10);
        auto end = chrono::high_resolution_clock::now();
        
        long long latency = chrono::duration_cast<chrono::nanoseconds>(end - start).count();
        insertion_times[i] = latency;
        total_insertion_time += latency;
        
        if (latency > max_insertion_time) {
            max_insertion_time = latency;
        }
    }
    
    auto benchmark_end = chrono::high_resolution_clock::now();
    
    // evaluating benchmarking parameters
    long long avg_insertion_time = total_insertion_time / N;
    
    // median latency
    sort(insertion_times.begin(), insertion_times.end());
    long long median_insertion_time = insertion_times[N / 2];
    
    // p95 and p99 
    long long p95_insertion_time = insertion_times[(int)(N * 0.95)];
    long long p99_insertion_time = insertion_times[(int)(N * 0.99)];
    
    // min insertion time
    long long min_insertion_time = insertion_times[0];
    
    cout << "\n--- INSERTION LATENCY RESULTS ---" << endl;
    cout << "Total entries inserted: " << N << endl;
    cout << "Total time: " 
         << chrono::duration_cast<chrono::milliseconds>(benchmark_end - benchmark_start).count()
         << " ms" << endl;
    cout << "\nLatency Statistics (nanoseconds):" << endl;
    cout << "  Average:     " << avg_insertion_time << " ns" << endl;
    cout << "  Median:      " << median_insertion_time << " ns" << endl;
    cout << "  Minimum:     " << min_insertion_time << " ns" << endl;
    cout << "  Maximum:     " << max_insertion_time << " ns" << endl;
    cout << "  95th %ile:   " << p95_insertion_time << " ns" << endl;
    cout << "  99th %ile:   " << p99_insertion_time << " ns" << endl;
    
    cout << "\n--- ARENA MEMORY USAGE ---" << endl;
    double used_mb = global_arena->get_used_memory() / (1024.0 * 1024.0);
    double capacity_mb = global_arena->get_capacity() / (1024.0 * 1024.0);
    double usage_percent = (global_arena->get_used_memory() * 100.0) / global_arena->get_capacity();
    cout << "  Used:        " << used_mb << " MB" << endl;
    cout << "  Capacity:    " << capacity_mb << " MB" << endl;
    cout << "  Usage:       " << usage_percent << "%" << endl;
    cout << "  Bytes/Node:  ~" << (global_arena->get_used_memory() / N) << " bytes (average)" << endl;
    cout << "========================================\n" << endl;

    // clean up arena
    delete global_arena;
    global_arena = nullptr;

    return 0;
}
