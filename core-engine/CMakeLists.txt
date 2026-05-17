cmake_minimum_required(VERSION 3.15)
project(CoreVectorEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 1. Resolve Homebrew Target Conflict by calling gRPC first
find_package(gRPC REQUIRED)
find_package(Protobuf REQUIRED)

# 2. Configure Generation Paths
set(PROTO_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../protos")
set(GEN_SRC_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY ${GEN_SRC_DIR})

# Map our expected output targets explicitly
set(PROTO_SRCS "${GEN_SRC_DIR}/vector_service.pb.cc" "${GEN_SRC_DIR}/vector_service.grpc.pb.cc")
set(PROTO_HDRS "${GEN_SRC_DIR}/vector_service.pb.h" "${GEN_SRC_DIR}/vector_service.grpc.pb.h")

# 3. Use add_custom_command to tie protoc generation strictly to the Build Lifecycle
add_custom_command(
    OUTPUT ${PROTO_SRCS} ${PROTO_HDRS}
    COMMAND ${PROTOBUF_PROTOC_EXECUTABLE}
            --proto_path=${PROTO_SRC_DIR}
            --cpp_out=${GEN_SRC_DIR}
            --grpc_out=${GEN_SRC_DIR}
            --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
            "${PROTO_SRC_DIR}/vector_service.proto"
    DEPENDS "${PROTO_SRC_DIR}/vector_service.proto"
    COMMENT "Compiling Protocol Buffers contract via protoc..."
    VERBATIM
)

# 4. Separate Generated Proto Files into their own isolated static library target
add_library(proto_generated_lib STATIC ${PROTO_SRCS})
target_include_directories(proto_generated_lib PUBLIC ${GEN_SRC_DIR} ${PROTOBUF_INCLUDE_DIRS})
target_link_libraries(proto_generated_lib PUBLIC gRPC::grpc++ ${Protobuf_LIBRARIES})

# 5. Gather core application source files safely (excluding generated build loops)
file(GLOB_RECURSE SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

# Link explicit system includes 
include_directories(include)

# 6. Define Executable Target referencing ONLY your core source files
add_executable(core_engine ${SOURCES})

# Target-specific optimizations based on host architecture
if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64" OR CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
    message(STATUS "Detected Apple Silicon / ARM64 architecture. Enabling NEON extensions.")
else()
    message(STATUS "Detected x86_64 architecture. Enabling AVX2 SIMD acceleration.")
    target_compile_options(core_engine PRIVATE -mavx2)
endif()

target_compile_options(core_engine PRIVATE -O3 -Wall -Wextra)

# 7. Link your clean executable against our isolated proto library
target_link_libraries(core_engine PRIVATE proto_generated_lib)