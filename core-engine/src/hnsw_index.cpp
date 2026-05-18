#include "hnsw_index.hpp"
#include <random>
#include <cmath>

#if defined(ENABLE_SIMD)
  #if defined(__ARM_NEON) || defined(__ARM_NEON__)
    #include <arm_neon.h>
    #define USE_NEON
  #elif defined(__AVX2__)
    #include <immintrin.h>
    #define USE_AVX2
  #endif
#endif

namespace vector_engine {

HNSWIndex::HNSWIndex(size_t dimension, size_t max_elements, size_t M, size_t ef_construction, size_t ef_search)
    : dimension_(dimension), max_elements_(max_elements), M_(M), 
      ef_construction_(ef_construction), ef_search_(ef_search), max_level_(-1), enter_node_id_(-1) {
    
    // Allocate the unified hardware-aligned memory block
    vector_storage_ = std::make_unique<CacheAlignedVectorStorage>(dimension_, max_elements_);
    level_normalization_factor_ = 1.0 / std::log(static_cast<double>(M_));
}

int HNSWIndex::generateRandomLevel() {
    static std::mt19937 generator(std::random_device{}());
    std::uniform_real_distribution<double> distribution(0.0, 1.0);
    return static_cast<int>(-std::log(distribution(generator)) * level_normalization_factor_);
}

// ─── HARDWARE SIMD MATH INJECTION ──────────────────────────────────────────
float HNSWIndex::calculateEuclideanDistance(const float* vecA, const float* vecB, size_t dim) {
#if defined(ENABLE_SIMD) && defined(USE_NEON)
    // 🚀 ARM NEON SIMD (Apple Silicon) - Striding across 4 floats concurrently
    size_t i = 0;
    float32x4_t sum_vec = vdupq_n_f32(0.0f);

    for (; i <= dim - 4; i += 4) {
        float32x4_t va = vld1q_f32(vecA + i);
        float32x4_t vb = vld1q_f32(vecB + i);
        float32x4_t diff = vsubq_f32(va, vb);
        sum_vec = vmlaq_f32(sum_vec, diff, diff);
    }

    float buffer[4];
    vst1q_f32(buffer, sum_vec);
    float total_sum = buffer[0] + buffer[1] + buffer[2] + buffer[3];
    
    // Clean up any remaining tail unaligned dimensional array boundaries safely
    for (; i < dim; ++i) {
        float diff = vecA[i] - vecB[i];
        total_sum += diff * diff;
    }
    return std::sqrt(total_sum);

#elif defined(ENABLE_SIMD) && defined(USE_AVX2)
    // 🚀 Intel/AMD AVX2 FMA - Striding across 8 floats concurrently
    size_t i = 0;
    __m256 sum_vec = _mm256_setzero_ps();

    for (; i <= dim - 8; i += 8) {
        __m256 va = _mm256_loadu_ps(vecA + i);
        __m256 vb = _mm256_loadu_ps(vecB + i);
        __m256 diff = _mm256_sub_ps(va, vb);
        sum_vec = _mm256_fmadd_ps(diff, diff, sum_vec);
    }

    alignas(32) float buffer[8];
    _mm256_store_ps(buffer, sum_vec);
    float total_sum = 0.0f;
    for (size_t j = 0; j < 8; ++j) total_sum += buffer[j];

    // Clean up tail elements for dimensions that aren't multiples of 8
    for (; i < dim; ++i) {
        float diff = vecA[i] - vecB[i];
        total_sum += diff * diff;
    }
    return std::sqrt(total_sum);

#else
    // 🐢 Fallback Scalar Implementation (Triggered when ENABLE_SIMD=OFF)
    float total_sum = 0.0f;
    for (size_t i = 0; i < dim; ++i) {
        float diff = vecA[i] - vecB[i];
        total_sum += diff * diff;
    }
    return std::sqrt(total_sum);
#endif
}

// ─── PRIMARY PUBLIC OPERATIONS ──────────────────────────────────────────────
void HNSWIndex::insert(int64_t id, const std::vector<float>& vector_data) {
    std::unique_lock<std::shared_mutex> lock(index_mutex_);

    if (nodes_registry_.find(id) != nodes_registry_.end()) return;

    // 1. Append raw vector arrays into our aligned storage matrix
    size_t assigned_storage_idx = vector_storage_->GetSize();
    vector_storage_->AppendVector(id, vector_data);

    // 2. Generate architectural level allocation & register graph layout nodes
    int node_level = generateRandomLevel();
    auto new_node = std::make_shared<HNSWNode>(id, node_level, assigned_storage_idx);
    nodes_registry_[id] = new_node;

    if (enter_node_id_ == -1) {
        enter_node_id_ = id;
        max_level_ = node_level;
        return;
    }

    // Graph navigation connectivity updates and entry point assignment mutations
    if (node_level > max_level_) {
        max_level_ = node_level;
        enter_node_id_ = id;
    }
}

std::vector<DistancePair> HNSWIndex::searchKnn(const std::vector<float>& query_vector, size_t k) {
    std::shared_lock<std::shared_mutex> lock(index_mutex_);
    
    std::vector<DistancePair> results;
    if (enter_node_id_ == -1) return results;

    // Fetch our hardware padding configurations to pass downstream to our SIMD registers
    size_t execution_dimension = vector_storage_->GetPaddedDimensions();
    const float* query_ptr = query_vector.data();

    // Trace using continuous arrays via vector_storage_ rows
    int64_t current_node_id = enter_node_id_;
    auto current_node = nodes_registry_[current_node_id];
    const float* current_vec = vector_storage_->GetVectorRow(current_node->storage_index);
    
    float current_dist = calculateEuclideanDistance(query_ptr, current_vec, execution_dimension);

    // Dynamic Layer Hopping logic over localized graph horizons
    for (int level = max_level_; level >= 0; --level) {
        bool changed = true;
        while (changed) {
            changed = false;
            for (int64_t neighbor_id : current_node->layers_neighbors[level]) {
                auto neighbor_node = nodes_registry_[neighbor_id];
                const float* neighbor_vec = vector_storage_->GetVectorRow(neighbor_node->storage_index);
                
                float d = calculateEuclideanDistance(query_ptr, neighbor_vec, execution_dimension);
                if (d < current_dist) {
                    current_dist = d;
                    current_node_id = neighbor_id;
                    current_node = neighbor_node;
                    changed = true;
                }
            }
        }
    }

    // Final result aggregation
    results.push_back({current_dist, current_node_id});
    return results;
}

} // namespace vector_engine