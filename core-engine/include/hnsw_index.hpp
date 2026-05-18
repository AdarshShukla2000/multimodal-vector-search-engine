#pragma once

#include <vector>
#include <unordered_map>
#include <queue>
#include <shared_mutex>
#include <memory>
#include <cmath>
#include "VectorStorage.hpp" // Link to your aligned memory storage structure

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
 * @brief Represents a single structural node in our HNSW graph layout.
 */
struct HNSWNode {
    int64_t id;
    int level; 
    size_t storage_index; // Maps this node to its data offset inside CacheAlignedVectorStorage
    
    // layers_neighbors[level_idx] = list of neighbor node IDs
    std::vector<std::vector<int64_t>> layers_neighbors;

    HNSWNode(int64_t node_id, int max_level, size_t internal_idx) 
        : id(node_id), level(max_level), storage_index(internal_idx), layers_neighbors(max_level + 1) {}
};

/**
 * @brief Helper structure used to rank search results inside priority queues.
 */
struct DistancePair {
    float distance;
    int64_t node_id;

    bool operator>(const DistancePair& other) const { return distance > other.distance; }
    bool operator<(const DistancePair& other) const { return distance < other.distance; }
};

class HNSWIndex {
private:
    size_t dimension_;        
    size_t max_elements_;     
    size_t M_;                
    size_t ef_construction_;  
    size_t ef_search_;        
    
    int max_level_;
    int64_t enter_node_id_;   
    double level_normalization_factor_;

    std::shared_mutex index_mutex_;
    std::unordered_map<int64_t, std::shared_ptr<HNSWNode>> nodes_registry_;
    
    // Hardware-optimized continuous memory block
    std::unique_ptr<CacheAlignedVectorStorage> vector_storage_;

    int generateRandomLevel();

public:
    HNSWIndex(size_t dimension, size_t max_elements, size_t M = 16, 
              size_t ef_construction = 200, size_t ef_search = 50);
    
    ~HNSWIndex() = default;

    // Core SIMD Accelerated Vector Math targeting cache-aligned strides
    static inline float calculateEuclideanDistance(const float* vecA, const float* vecB, size_t dim);

    void insert(int64_t id, const std::vector<float>& vector_data);
    std::vector<DistancePair> searchKnn(const std::vector<float>& query_vector, size_t k);

    size_t getIndexSize() { 
        std::lock_guard<std::shared_mutex> lock(index_mutex_);
        return nodes_registry_.size(); 
    }
    
    int getMaxLevel() { return max_level_; }
};

} // namespace vector_engine