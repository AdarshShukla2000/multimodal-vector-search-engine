#ifndef VECTOR_STORAGE_HPP
#define VECTOR_STORAGE_HPP

#include <vector>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

class CacheAlignedVectorStorage {
public:
    // Automatically detect architecture alignment
    #if defined(__ARM_NEON) || defined(__aarch64__)
        static constexpr size_t ALIGNMENT_BYTES = 16; // 128-bit NEON registers
    #else
        static constexpr size_t ALIGNMENT_BYTES = 32; // 256-bit AVX2 registers
    #endif

    CacheAlignedVectorStorage(size_t dimensions, size_t initial_capacity)
        : dimensions_(dimensions), capacity_(initial_capacity), size_(0) {
        
        size_t floats_per_alignment = ALIGNMENT_BYTES / sizeof(float); 
        padded_dimensions_ = ((dimensions + floats_per_alignment - 1) / floats_per_alignment) * floats_per_alignment;

        size_t total_bytes = capacity_ * padded_dimensions_ * sizeof(float);
        
        #if defined(_MSC_VER)
            data_ = static_cast<float*>(_aligned_malloc(total_bytes, ALIGNMENT_BYTES));
        #else
            // Standard POSIX aligned allocation for macOS and Linux
            if (posix_memalign(reinterpret_cast<void**>(&data_), ALIGNMENT_BYTES, total_bytes) != 0) {
                throw std::bad_alloc();
            }
        #endif

        if (!data_) throw std::bad_alloc();
    }

    ~CacheAlignedVectorStorage() {
        #if defined(_MSC_VER)
            _aligned_free(data_);
        #else
            std::free(data_);
        #endif
    }

    void AppendVector(long long id, const std::vector<float>& values) {
        if (values.size() != dimensions_) {
            throw std::invalid_argument("Dimension mismatch against initialized schema.");
        }
        if (size_ >= capacity_) {
            throw std::runtime_error("Vector storage capacity reached target limit.");
        }

        float* target_ptr = data_ + (size_ * padded_dimensions_);
        std::memcpy(target_ptr, values.data(), dimensions_ * sizeof(float));

        // Pad trailing space with zeros for safe SIMD hardware processing
        if (padded_dimensions_ > dimensions_) {
            std::memset(target_ptr + dimensions_, 0, (padded_dimensions_ - dimensions_) * sizeof(float));
        }

        vector_ids_.push_back(id);
        size_++;
    }

    const float* GetVectorRow(size_t index) const {
        return data_ + (index * padded_dimensions_);
    }

    size_t GetPaddedDimensions() const { return padded_dimensions_; }
    size_t GetSize() const { return size_; }

private:
    size_t dimensions_;
    size_t padded_dimensions_;
    size_t capacity_;
    size_t size_;
    float* data_ = nullptr;
    std::vector<long long> vector_ids_;
};

#endif // VECTOR_STORAGE_HPP