import time
import random
import concurrent.futures
import numpy as np
import grpc

# Automatically compile or import your generated proto structures
# Ensure your python path can see the compiled proto specs
import vector_service_pb2 as pb2
import vector_service_pb2_grpc as pb2_grpc

SERVER_ADDRESS = "localhost:50051"
DIMENSION = 128
NUM_VECTORS = 1000
NUM_QUERIES = 100
K_NEIGHBORS = 5

def generate_vector(dim):
    # Create normalized float arrays to simulate true embedding spaces
    vec = np.random.randn(dim).astype(np.float32)
    norm = np.linalg.norm(vec)
    return (vec / (norm if norm > 0 else 1.0)).tolist()

def run_ingestion_benchmark(stub):
    print(f"📦 Starting Ingestion Benchmark of {NUM_VECTORS} vectors...")
    vectors_pool = {i: generate_vector(DIMENSION) for i in range(1, NUM_VECTORS + 1)}
    
    start_time = time.perf_counter()
    
    # Use thread pool to simulate concurrent upstream microservice traffic
    with concurrent.futures.ThreadPoolExecutor(max_workers=10) as executor:
        futures = []
        for vec_id, vec_data in vectors_pool.items():
            req = pb2.VectorMessage(id=vec_id, values=vec_data)
            futures.append(executor.submit(stub.IngestVectorStream, req))
        
        # Wait for all streams to clear out
        concurrent.futures.wait(futures)
            
    end_time = time.perf_counter()
    total_time = (end_time - start_time) * 1000
    print(f"✅ Ingestion complete in {total_time:.2f} ms ({total_time/NUM_VECTORS:.2f} ms/vector)\n")
    return vectors_pool

def run_query_benchmark(stub, vectors_pool):
    print(f"🔍 Running {NUM_QUERIES} Parallel KNN Queries (K={K_NEIGHBORS})...")
    latencies = []

    with concurrent.futures.ThreadPoolExecutor(max_workers=5) as executor:
        futures = []
        for _ in range(NUM_QUERIES):
            query_vec = generate_vector(DIMENSION)
            req = pb2.QueryRequest(query_vector=query_vec, top_k=K_NEIGHBORS)
            
            # Time the individual execution block with high-precision counters
            start_query = time.perf_counter()
            future = executor.submit(stub.QueryNearestNeighbors, req)
            futures.append((future, start_query))

        for future, start_query in futures:
            try:
                response = future.result()
                end_query = time.perf_counter()
                if response.status == "SUCCESS":
                    latencies.append((end_query - start_query) * 1000)
            except Exception as e:
                print(f"Query failure: {e}")

    avg_latency = np.mean(latencies)
    p95_latency = np.percentile(latencies, 95)
    p99_latency = np.percentile(latencies, 99)

    print(f"📊 --- SEARCH PERFORMANCE METRICS ---")
    print(f"⏱️ Average Latency: {avg_latency:.3f} ms")
    print(f"⚡ P95 Latency:     {p95_latency:.3f} ms")
    print(f"🔥 P99 Latency:     {p99_latency:.3f} ms")
    print(f"🚀 Max Throughput:  {int(1000 / avg_latency)} queries/sec")

if __name__ == "__main__":
    # Ensure your C++ engine binary is actively running on port 50051 before executing
    with grpc.insecure_channel(SERVER_ADDRESS) as channel:
        stub = pb2_grpc.VectorComputeEngineStub(channel)
        
        # Compile python proto stubs dynamically if not pre-built
        try:
            pool = run_ingestion_benchmark(stub)
            run_query_benchmark(stub, pool)
        except grpc.RpcError as e:
            print(f"\n❌ gRPC Communication breakdown. Is your C++ Engine running? Details: {e.details()}")