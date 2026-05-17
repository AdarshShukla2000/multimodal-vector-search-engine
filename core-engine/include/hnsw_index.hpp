#pragma once

#include <vector>
#include <unordered_map>
#include <queue>
#include <shared_mutex>
#include <memory>
#include <cmath>

// Architecture-specific vector optimization detection
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    #include <arm_neon.h>
    #define USE_NEON
#elif defined(__AVX2__)
    #include <immintrin.h>
    #define USE_AVX2
#endif

namespace vector_engine {

/**
 * @brief Cache-aligned continuous structure to store raw vector data.
 * Crucial for avoiding split-cache-line performance degradation during SIMD loops.
 */
struct alignas(64) AlignedVector {
    std::vector<float> data;
    int64_t id;

    AlignedVector() : id(-1) {}
    AlignedVector(int64_t node_id, const std::vector<float>& vec_data) 
        : data(vec_data), id(node_id) {}
};

/**
 * @brief Represents a single element in our HNSW graph.
 */
struct HNSWNode {
    int64_t id;
    int level; // The maximum layer this node reaches
    
    // Graph links layout: layers_neighbors[level_idx] = list of neighbor node IDs
    std::vector<std::vector<int64_t>> layers_neighbors;

    HNSWNode(int64_t node_id, int max_level) 
        : id(node_id), level(max_level), layers_neighbors(max_level + 1) {}
};

/**
 * @brief Helper structure used to rank search results inside priority queues.
 */
struct DistancePair {
    float distance;
    int64_t node_id;

    bool operator>(const DistancePair& other) const {
        return distance > other.distance; // Min-heap behavior
    }
    bool operator<(const DistancePair& other) const {
        return distance < other.distance; // Max-heap behavior
    }
};

class HNSWIndex {
private:
    // Core parameters configured at index creation
    size_t dimension_;        // e.g., 128
    size_t max_elements_;     // Upper limit of total indexed items
    size_t M_;                // Max number of outgoing connections per node per layer
    size_t ef_construction_;  // Size of dynamic candidate list during build phase
    size_t ef_search_;        // Size of dynamic candidate list during query phase
    
    int max_level_;
    int64_t enter_node_id_;   // The entry point node for graph traversal
    double level_normalization_factor_;

    // Thread-safe storage structures
    std::shared_mutex index_mutex_;
    std::unordered_map<int64_t, std::shared_ptr<HNSWNode>> nodes_registry_;
    std::unordered_map<int64_t, std::shared_ptr<AlignedVector>> raw_vectors_registry_;

    // Internal Random Level Generator (similar to Skip-List design)
    int generateRandomLevel();

public:
    HNSWIndex(size_t dimension, size_t max_elements, size_t M = 16, 
              size_t ef_construction = 200, size_t ef_search = 50);
    
    ~HNSWIndex() = default;

    // Core SIMD Accelerated Vector Math
    static float calculateEuclideanDistance(const float* vecA, const float* vecB, size_t dim);

    // Primary Public API Operations
    void insert(int64_t id, const std::vector<float>& vector_data);
    std::vector<DistancePair> searchKnn(const std::vector<float>& query_vector, size_t k);

    // Read-only inspection metrics for our analytical system dashboard
    size_t getIndexSize() { 
        std::lock_guard<std::shared_mutex> lock(index_mutex_);
        return nodes_registry_.size(); 
    }
    
    int getMaxLevel() { return max_level_; }
};

} // namespace vector_engine