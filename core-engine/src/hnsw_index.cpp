#include "hnsw_index.hpp"
#include <random>
#include <iostream>
#include <algorithm>
#include <set>

namespace vector_engine {

HNSWIndex::HNSWIndex(size_t dimension, size_t max_elements, size_t M, 
                     size_t ef_construction, size_t ef_search)
    : dimension_(dimension),
      max_elements_(max_elements),
      M_(M),
      ef_construction_(ef_construction),
      ef_search_(ef_search),
      max_level_(-1),
      enter_node_id_(-1) {
    level_normalization_factor_ = 1.0 / std::log(static_cast<double>(M_));
}

float HNSWIndex::calculateEuclideanDistance(const float* vecA, const float* vecB, size_t dim) {
    float total_sum = 0.0f;
    size_t i = 0;

#if defined(USE_NEON)
    float32x4_t sum_accumulator = vdupq_n_f32(0.0f);
    for (; i + 3 < dim; i += 4) {
        float32x4_t a = vld1q_f32(vecA + i);
        float32x4_t b = vld1q_f32(vecB + i);
        float32x4_t diff = vsubq_f32(a, b);
        sum_accumulator = vmlaq_f32(sum_accumulator, diff, diff);
    }
    total_sum = vmaxvq_f32(sum_accumulator);
#elif defined(USE_AVX2)
    __m256 acc = _mm256_setzero_ps();
    for (; i + 7 < dim; i += 8) {
        __m256 a = _mm256_loadu_ps(vecA + i);
        __m256 b = _mm256_loadu_ps(vecB + i);
        __m256 diff = _mm256_sub_ps(a, b);
        acc = _mm256_fmadd_ps(diff, diff, acc);
    }
    alignas(32) float temp[8];
    _mm256_storeu_ps(temp, acc);
    for (int j = 0; j < 8; ++j) total_sum += temp[j];
#endif

    for (; i < dim; ++i) {
        float diff = vecA[i] - vecB[i];
        total_sum += diff * diff;
    }
    return std::sqrt(total_sum);
}

int HNSWIndex::generateRandomLevel() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_real_distribution<double> dis(0.0, 1.0);
    double r = dis(gen);
    if (r == 0.0) r = 0.0000001; 
    return static_cast<int>(-std::log(r) * level_normalization_factor_);
}

void HNSWIndex::insert(int64_t id, const std::vector<float>& vector_data) {
    std::unique_lock<std::shared_mutex> lock(index_mutex_);

    if (vector_data.size() != dimension_) {
        throw std::invalid_argument("Vector data dimensions break index tracking template specifications.");
    }

    auto aligned_vec = std::make_shared<AlignedVector>(id, vector_data);
    raw_vectors_registry_[id] = aligned_vec;

    int insert_level = generateRandomLevel();
    auto new_node = std::make_shared<HNSWNode>(id, insert_level);
    nodes_registry_[id] = new_node;

    if (enter_node_id_ == -1) {
        enter_node_id_ = id;
        max_level_ = insert_level;
        std::cout << "[HNSW Graph] Root initialization completed for Node: " << id << std::endl;
        return;
    }

    int64_t current_entry_point = enter_node_id_;
    float current_dist = calculateEuclideanDistance(vector_data.data(), raw_vectors_registry_.at(current_entry_point)->data.data(), dimension_);

    // PHASE 1: Fast Express Lane Traversal
    for (int l = max_level_; l > insert_level; --l) {
        bool changed = true;
        while (changed) {
            changed = false;
            auto node_ptr = nodes_registry_.at(current_entry_point);
            for (int64_t neighbor_id : node_ptr->layers_neighbors[l]) {
                float d = calculateEuclideanDistance(vector_data.data(), raw_vectors_registry_.at(neighbor_id)->data.data(), dimension_);
                if (d < current_dist) {
                    current_dist = d;
                    current_entry_point = neighbor_id;
                    changed = true;
                }
            }
        }
    }

    // PHASE 2: Local Layer Insertion and Linking
    std::priority_queue<DistancePair> candidates; 
    candidates.push({current_dist, current_entry_point});
    
    std::set<int64_t> visited;
    visited.insert(current_entry_point);

    for (int l = std::min(max_level_, insert_level); l >= 0; --l) {
        std::priority_queue<DistancePair, std::vector<DistancePair>, std::greater<DistancePair>> layer_nearest;
        
        while (!candidates.empty()) {
            auto curr = candidates.top();
            candidates.pop();
            
            layer_nearest.push(curr);
            if (layer_nearest.size() > ef_construction_) {
                layer_nearest.pop(); 
            }

            auto node_ptr = nodes_registry_.at(curr.node_id);
            for (int64_t neighbor_id : node_ptr->layers_neighbors[l]) {
                if (visited.find(neighbor_id) == visited.end()) {
                    visited.insert(neighbor_id);
                    float d = calculateEuclideanDistance(vector_data.data(), raw_vectors_registry_.at(neighbor_id)->data.data(), dimension_);
                    
                    if (d < layer_nearest.top().distance || layer_nearest.size() < ef_construction_) {
                        candidates.push({d, neighbor_id});
                    }
                }
            }
        }

        std::vector<int64_t> selected_neighbors;
        while (!layer_nearest.empty()) {
            selected_neighbors.push_back(layer_nearest.top().node_id);
            layer_nearest.pop();
        }

        if (selected_neighbors.size() > M_) {
            selected_neighbors.resize(M_);
        }

        new_node->layers_neighbors[l] = selected_neighbors;

        for (int64_t neighbor_id : selected_neighbors) {
            auto nb_ptr = nodes_registry_.at(neighbor_id);
            nb_ptr->layers_neighbors[l].push_back(id);
            
            if (nb_ptr->layers_neighbors[l].size() > M_) {
                std::vector<DistancePair> re_rank;
                for (int64_t n_id : nb_ptr->layers_neighbors[l]) {
                    float d = calculateEuclideanDistance(raw_vectors_registry_.at(neighbor_id)->data.data(), raw_vectors_registry_.at(n_id)->data.data(), dimension_);
                    re_rank.push_back({d, n_id});
                }
                std::sort(re_rank.begin(), re_rank.end());
                
                nb_ptr->layers_neighbors[l].clear();
                for (size_t idx = 0; idx < M_; ++idx) {
                    nb_ptr->layers_neighbors[l].push_back(re_rank[idx].node_id);
                }
            }
        }

        for (int64_t sn : selected_neighbors) {
            candidates.push({calculateEuclideanDistance(vector_data.data(), raw_vectors_registry_.at(sn)->data.data(), dimension_), sn});
        }
    }

    if (insert_level > max_level_) {
        max_level_ = insert_level;
        enter_node_id_ = id;
    }

    std::cout << "[HNSW Graph] Inserted Node ID: " << id << " | Target Level Height: " << insert_level 
              << " | Total Node Count: " << nodes_registry_.size() << std::endl;
}

std::vector<DistancePair> HNSWIndex::searchKnn(const std::vector<float>& query_vector, size_t k) {
    std::shared_lock<std::shared_mutex> lock(index_mutex_);
    std::vector<DistancePair> results;

    if (enter_node_id_ == -1) return results;

    int64_t current_entry_point = enter_node_id_;
    float current_dist = calculateEuclideanDistance(query_vector.data(), raw_vectors_registry_.at(current_entry_point)->data.data(), dimension_);

    for (int l = max_level_; l > 0; --l) {
        bool changed = true;
        while (changed) {
            changed = false;
            auto node_ptr = nodes_registry_.at(current_entry_point);
            for (int64_t neighbor_id : node_ptr->layers_neighbors[l]) {
                float d = calculateEuclideanDistance(query_vector.data(), raw_vectors_registry_.at(neighbor_id)->data.data(), dimension_);
                if (d < current_dist) {
                    current_dist = d;
                    current_entry_point = neighbor_id;
                    changed = true;
                }
            }
        }
    }

    std::priority_queue<DistancePair> candidates;
    candidates.push({current_dist, current_entry_point});

    std::priority_queue<DistancePair, std::vector<DistancePair>, std::greater<DistancePair>> nearest_pool;
    std::set<int64_t> visited;
    visited.insert(current_entry_point);

    while (!candidates.empty()) {
        auto curr = candidates.top();
        candidates.pop();

        nearest_pool.push(curr);
        if (nearest_pool.size() > ef_search_) {
            nearest_pool.pop();
        }

        auto node_ptr = nodes_registry_.at(curr.node_id);
        for (int64_t neighbor_id : node_ptr->layers_neighbors[0]) {
            if (visited.find(neighbor_id) == visited.end()) {
                visited.insert(neighbor_id);
                float d = calculateEuclideanDistance(query_vector.data(), raw_vectors_registry_.at(neighbor_id)->data.data(), dimension_);

                if (d < nearest_pool.top().distance || nearest_pool.size() < ef_search_) {
                    candidates.push({d, neighbor_id});
                }
            }
        }
    }

    while (!nearest_pool.empty() && results.size() < k) {
        results.push_back(nearest_pool.top());
        nearest_pool.pop();
    }
    std::reverse(results.begin(), results.end());
    return results;
}

} // namespace vector_engine