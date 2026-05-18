cmake_minimum_required(VERSION 3.15)
project(hnsw_core_engine CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 🚀 Core Hardware Architecture Optimization Flags
if(APPLE AND CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64")
    add_compile_options(-O3 -march=armv8-a+simd -mcpu=apple-m1)
    message(STATUS "Configuring Apple Silicon ARM NEON Compilation optimizations.")
else()
    add_compile_options(-O3 -mavx2 -mfma)
    message(STATUS "Configuring x86 AVX2/FMA Optimization flags.")
endif()

# ─── ⚡ STEP 4 SWITCH: SIMD CONDITIONAL COMPILATION TOGGLE ──────────────────
# This exposes the ENABLE_SIMD option to CMake so we can profile Scalar vs. SIMD
# ─── ⚡ STEP 4 SWITCH: SIMD CONDITIONAL COMPILATION TOGGLE ──────────────────
option(ENABLE_SIMD "Enable hardware-level SIMD vectorization routines" ON)

if(ENABLE_SIMD)
    add_compile_definitions(ENABLE_SIMD)
    message(STATUS "🔥 SIMD Vectorization Layer: ACTIVATED (Native Registers Engaged)")
else()
    # 🛑 Disables Clang's automatic loop unrolling/vectorization to expose the baseline math performance gap
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "AppleClang")
        add_compile_options(-fno-vectorize -fno-slp-vectorize)
    endif()
    message(STATUS "🐌 SIMD Vectorization Layer: DEACTIVATED (Forced Pure Serial Scalar Baseline)")
endif()
# ─────────────────────────────────────────────────────────────────────────────
# ─────────────────────────────────────────────────────────────────────────────

# ✅ Find gRPC first — it brings in protobuf transitively via its own config
find_package(gRPC CONFIG REQUIRED)

# ✅ Use gRPC's bundled protobuf instead of FindProtobuf.cmake
find_package(Protobuf CONFIG REQUIRED)

find_program(PROTOC_PATH protoc REQUIRED)
find_program(GRPC_CPP_PLUGIN_PATH grpc_cpp_plugin REQUIRED)

set(GEN_PROTO_SRC "${CMAKE_CURRENT_BINARY_DIR}/vector_service.pb.cc")
set(GEN_PROTO_HDR "${CMAKE_CURRENT_BINARY_DIR}/vector_service.pb.h")
set(GEN_GRPC_SRC  "${CMAKE_CURRENT_BINARY_DIR}/vector_service.grpc.pb.cc")
set(GEN_GRPC_HDR  "${CMAKE_CURRENT_BINARY_DIR}/vector_service.grpc.pb.h")

add_custom_command(
    OUTPUT "${GEN_PROTO_SRC}" "${GEN_PROTO_HDR}" "${GEN_GRPC_SRC}" "${GEN_GRPC_HDR}"
    COMMAND "${PROTOC_PATH}"
    ARGS --cpp_out="${CMAKE_CURRENT_BINARY_DIR}"
         --grpc_out="${CMAKE_CURRENT_BINARY_DIR}"
         --plugin=protoc-gen-grpc="${GRPC_CPP_PLUGIN_PATH}"
         -I .
         vector_service.proto
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../protos"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../protos/vector_service.proto"
    COMMENT "Compiling Protocol Buffer and gRPC Service Interfaces relatively..."
)

include_directories(
    include
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
)

add_executable(engine_server
    src/main.cpp
    src/hnsw_index.cpp
    ${GEN_PROTO_SRC}
    ${GEN_GRPC_SRC}
)

target_link_libraries(engine_server
    gRPC::grpc++
    protobuf::libprotobuf
)