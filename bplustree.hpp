#pragma once

#include <iostream>
#include <algorithm>
#include <immintrin.h>
#include <stdexcept>
#include <cstring>
#include <cstdint>
#include <type_traits>

// --- ARENA ---
class Arena
{
private:
    char *memory;
    size_t offset;
    size_t capacity;

public:
    Arena(size_t size) : offset(0), capacity(size)
    {
        memory = (char *)malloc(size);
        if (!memory)
        {
            throw std::runtime_error("Failed to allocate arena memory");
        }
        std::cout << "Arena initialized with " << (size / (1024.0 * 1024 * 1024)) << " GB" << std::endl;
    }

    ~Arena()
    {
        free(memory);
    }

    void *allocate(size_t bytes)
    {
        // 64 bytes for cache line alignment (AVX friendly)
        size_t aligned_bytes = (bytes + 63) & ~63;
        
        if (offset + aligned_bytes > capacity)
        {
            throw std::runtime_error("Arena out of memory");
        }

        void *ptr = memory + offset;
        offset += aligned_bytes;
        return ptr;
    }

    size_t get_used_memory() const
    {
        return offset;
    }

    size_t get_capacity() const
    {
        return capacity;
    }

    void reset()
    {
        offset = 0;
    }
};

extern Arena *global_arena;

template <typename KeyType, typename ValueType, int M = 256>
class BPlusTree
{
private:
    struct alignas(64) Node
    {
        bool is_leaf;
        uint16_t num_keys; // Changed to uint16_t for packing, though alignment padding might negate this
        KeyType keys[M];
        
        // Union for memory efficiency - Leaf uses values, Internal uses children
        union {
            ValueType values[M];      //  used if is_leaf = true
            Node *children[M + 1]; //  used if is_leaf = false
        };

        Node(bool leaf) : is_leaf(leaf), num_keys(0)
        {
            // arrays are uninitialized for performance
        }

        static void *operator new(size_t size)
        {
            if (!global_arena)
            {
                throw std::runtime_error("Arena not initialized");
            }
            return global_arena->allocate(size);
        }
    };

    Node *root;

    // core of insertion algorithm
    void insert_recursive(Node *node, KeyType key, ValueType value, Node *&new_sibling, KeyType &median)
    {
        // 1. find index to insert   --> though this is linear only but we can improve on this to use SIMD/ Binary search
        int i = 0;
        while (i < node->num_keys && key > node->keys[i])
        {
            i++;
        }

        // 2. leaf logic
        if (node->is_leaf)
        {
            // update existing value
            if (i > 0 && node->keys[i - 1] == key)
            {
                node->values[i - 1] = value;
                return;
            }

            // insert into arrays
            // shift elements to right
            for (int k = node->num_keys; k > i; k--) {
                node->keys[k] = node->keys[k-1];
                node->values[k] = node->values[k-1];
            }
            node->keys[i] = key;
            node->values[i] = value;
            node->num_keys++;

            // check Split
            if (node->num_keys >= M)
            {
                split_leaf(node, new_sibling, median);
            }
            return;
        }

        // 3. rebalancing internal nodes
        Node *child_sibling = nullptr;
        KeyType child_median = KeyType();

        insert_recursive(node->children[i], key, value, child_sibling, child_median);

        if (child_sibling != nullptr)
        {
            // child split! ==> insert median and pointer into THIS node
            // shift half keys to the right
            for (int k = node->num_keys; k > i; k--) {
                node->keys[k] = node->keys[k-1];
            }

            for (int k = node->num_keys + 1; k > i + 1; k--) {
                node->children[k] = node->children[k-1];
            }

            node->keys[i] = child_median;
            node->children[i + 1] = child_sibling;
            node->num_keys++;

            if (node->num_keys >= M)
            {
                split_internal(node, new_sibling, median);
            }
        }
    }

    // --- SPLITTING LOGIC ---
    void split_leaf(Node *node, Node *&new_leaf, KeyType &median)
    {
        int mid = M / 2;
        new_leaf = new Node(true);

        // move right half
        int num_moving = node->num_keys - mid;
        for(int i=0; i<num_moving; i++) {
            new_leaf->keys[i] = node->keys[mid + i];
            new_leaf->values[i] = node->values[mid + i];
        }
        new_leaf->num_keys = num_moving;

        // update the number of entries in old node
        node->num_keys = mid;

        // leaf split copies up
        median = new_leaf->keys[0];
    }

    void split_internal(Node *node, Node *&new_node, KeyType &median)
    {
        int mid = M / 2;
        new_node = new Node(false);

        // the key at mid moves UP
        median = node->keys[mid];

        // move right half keys (excluding mid)
        int num_keys_moving = node->num_keys - (mid + 1);
        for(int i=0; i<num_keys_moving; i++) {
            new_node->keys[i] = node->keys[mid + 1 + i];
        }
        new_node->num_keys = num_keys_moving;

        // move right half children
        int num_children_moving = num_keys_moving + 1;
        for(int i=0; i<num_children_moving; i++) {
            new_node->children[i] = node->children[mid + 1 + i];
        }

        // update the entries in old node
        node->num_keys = mid;
    }

    void remove_recursive(Node *node, KeyType key)
    {
        int i = 0;
        while (i < node->num_keys && key >= node->keys[i])
        {
            i++;
        }

        if (node->is_leaf)
        {
            // check exact match
            for (int k = 0; k < node->num_keys; k++)
            {
                if (node->keys[k] == key)
                {
                    // shift to the left
                    for(int j=k; j < node->num_keys - 1; j++) {
                        node->keys[j] = node->keys[j+1];
                        node->values[j] = node->values[j+1];
                    }
                    node->num_keys--;
                    return;
                }
            }
            return;
        }

        // internal balancing
        remove_recursive(node->children[i], key);
    }

public:
    BPlusTree()
    {
        root = new Node(true);
    }

    // --- SEARCH (Linear Scan) ---
    bool findLinear(KeyType key, ValueType &val_out)
    {
        Node *curr = root;
        while (!curr->is_leaf)
        {
            int i = 0;
            // linear Scan: find first key > input
            while (i < curr->num_keys && key >= curr->keys[i])
            {
                i++;
            }
            curr = curr->children[i];
        }

        // search in leaf
        for (int i = 0; i < curr->num_keys; i++)
        {
            if (curr->keys[i] == key)
            {
                val_out = curr->values[i];
                return true;
            }
        }
        return false;
    }

    bool findBinary(KeyType key, ValueType &val_out)
    {
        Node *curr = root;
        while (!curr->is_leaf)
        {
            // binary search
            int hi = curr->num_keys - 1;
            int lo = 0;
            int i = hi;
            int mid = lo + (hi - lo) / 2;

            while (lo <= hi)
            {
                mid = lo + (hi - lo) / 2;

                if (curr->keys[mid] <= key)
                {
                    lo = mid + 1;
                }
                else
                {
                    i = mid;
                    hi = mid - 1;
                }
            }

            if (i == hi)
            {
                if (curr->keys[i] > key)
                { // means left child contains the leaf
                    curr = curr->children[i];
                }
                else
                {
                    // or it's rght will
                    curr = curr->children[i + 1];
                }
            }
            else
            {
                curr = curr->children[i];
            }
        }

        // search in leaf
        int hi = curr->num_keys - 1;
        int lo = 0;
        int i = hi;
        int mid = lo + (hi - lo) / 2;

        while (lo <= hi)
        {
            mid = lo + (hi - lo) / 2;

            if (curr->keys[mid] < key)
            {
                lo = mid + 1;
            }
            else
            {
                i = mid;
                hi = mid - 1;
            }
        }

        if (i >= 0 && i < curr->num_keys && curr->keys[i] == key)
        {
            val_out = curr->values[i];
            return true;
        }
        return false;
    }

    // --- INSERTION ---

    // SIMD Search - enabled only for int keys
    template <typename K = KeyType>
    typename std::enable_if<std::is_same<K, int>::value, bool>::type
    findSIMD(KeyType key, ValueType &val_out)
    {
        Node *curr = root;

        while (!curr->is_leaf)
        {
            int i = 0;
            int result_index = curr->num_keys; // default to "Rightmost Child" if no key is bigger

            // 1. fill  the Search Key into all 8 lanes
            __m256i target_key_vec = _mm256_set1_epi32(key);

            // loop in chunks of 8
            // Unrolling slightly? 
            for (; i < curr->num_keys; i += 8)
            {
                // Prefetch next chunk of keys
                _mm_prefetch((const char*)&curr->keys[i + 8], _MM_HINT_T0);

                // 2. load 8 keys from the node (unaligned load is safe ==> tells the cpu that the adrees might not be multiple of 32 so handle this)
                // With alignas(64) on Node and Arena, keys should be aligned, we can try aligned load later or rely on HW
                __m256i chunk_key_vec = _mm256_loadu_si256((__m256i *)&curr->keys[i]);

                // 3. compare node vector with search vector : is NodeKey > SearchKey?
                __m256i cmp_vec = _mm256_cmpgt_epi32(chunk_key_vec, target_key_vec);

                // 4. create result Bitmask
                int mask = _mm256_movemask_ps(_mm256_castsi256_ps(cmp_vec));

                // 5. check the mask
                if (__builtin_expect(mask != 0, 0))
                {
                    int bit_pos = __builtin_ctz(mask);
                    int found_idx = i + bit_pos;
                    
                    // ensure we don't pick a garbage key beyond num_keys
                    if (found_idx < curr->num_keys) {
                        result_index = found_idx;
                        break; 
                    }
                }
            }
            
            // Prefetch the next node
            // Note: curr->children is in the same union location.
            // When we go deeper, we access children. 
            // We should prefetch the 'keys' of the next child, but we don't know the exact address offset without dereferencing logic.
            // Just prefetching the pointer target:
            Node* next_node = curr->children[result_index];
            _mm_prefetch((const char*)next_node, _MM_HINT_T0);
            _mm_prefetch((const char*)((char*)next_node + 64), _MM_HINT_T0); // prefetch 2 cache lines of the next node

            curr = next_node;
        }

        // --- Search in Leaf (SIMD) ---
        __m256i target_vec = _mm256_set1_epi32(key);

        for (int i = 0; i < curr->num_keys; i += 8)
        {
            // Prefetch next chunk
             _mm_prefetch((const char*)&curr->keys[i + 8], _MM_HINT_T0);

            __m256i chunk_vec = _mm256_loadu_si256((__m256i *)&curr->keys[i]);
            __m256i eq_vec = _mm256_cmpeq_epi32(chunk_vec, target_vec);
            int mask = _mm256_movemask_ps(_mm256_castsi256_ps(eq_vec));

            if (__builtin_expect(mask != 0, 0))
            {
                int bit_pos = __builtin_ctz(mask);
                int idx = i + bit_pos;

                if (idx < curr->num_keys)
                {
                    val_out = curr->values[idx];
                    return true;
                }
            }
        }

        return false;
    }


    template <typename K = KeyType>
    typename std::enable_if<!std::is_same<K, int>::value, bool>::type
    findSIMD(KeyType key, ValueType &val_out)
    {
        return findBinary(key, val_out);
    }

    void insert(KeyType key, ValueType value)
    {
        Node *new_child = nullptr;
        KeyType median = KeyType();

        insert_recursive(root, key, value, new_child, median);

        if (new_child != nullptr)
        {
            Node *new_root = new Node(false);
            new_root->keys[0] = median;
            new_root->children[0] = root;
            new_root->children[1] = new_child;
            new_root->num_keys = 1;
            root = new_root;
        }
    }

    void remove(KeyType key)
    {
        remove_recursive(root, key);
    }
};
