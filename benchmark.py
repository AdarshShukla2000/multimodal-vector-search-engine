import grpc
import time
import random
import concurrent.futures
import numpy as np

# Import the stubs generated at your root directory
import vector_service_pb2
import vector_service_pb2_grpc

GRPC_SERVER_ADDRESS = "localhost:50051"
DIMENSION = 384
NUM_BENCHMARK_QUERIES = 10000  # Scaled up to cleanly measure sustained throughput
CONCURRENCY_WORKERS = 32       # Generates enough pressure to saturate the 10 C++ core threads

def generate_random_vector(dim):
    return [random.uniform(-1.0, 1.0) for _ in range(dim)]

def send_query_request(stub, query_vec, k=10):
    request = vector_service_pb2.QueryRequest(
        query_vector=query_vec,
        top_k=k
    )
    start_time = time.perf_counter()
    try:
        response = stub.QueryNearestNeighbors(request)
        latency = (time.perf_counter() - start_time) * 1000.0  # Calculate in milliseconds
        return latency, True
    except grpc.RpcError as e:
        return 0.0, False

def run_performance_suite():
    print("======================================================================")
    print("🏋️‍♂️  INITIATING END-TO-END HNSW ENGINE CONCURRENCY STRESS TEST")
    print("======================================================================")
    print(f"Target Endpoint : {GRPC_SERVER_ADDRESS}")
    print(f"Load Parameters : {NUM_BENCHMARK_QUERIES} queries | {CONCURRENCY_WORKERS} parallel client channels\n")
    
    # Pre-generate vector matrices to keep Python generation out of the timing loop
    print("📊 Pre-generating test datasets...")
    query_vectors = [generate_random_vector(DIMENSION) for _ in range(NUM_BENCHMARK_QUERIES)]
    
    # Establish connection channel
    channel = grpc.insecure_channel(GRPC_SERVER_ADDRESS)
    stub = vector_service_pb2_grpc.VectorComputeEngineStub(channel)
    
    latencies = []
    success_count = 0
    
    print("🔥 Launching concurrent worker threads. Bombarding C++ compute engine...")
    global_start = time.perf_counter()
    
    # Utilize Python's thread pool to simulate multi-client web-gateway traffic
    with concurrent.futures.ThreadPoolExecutor(max_workers=CONCURRENCY_WORKERS) as executor:
        futures = [executor.submit(send_query_request, stub, vec) for vec in query_vectors]
        
        for future in concurrent.futures.as_completed(futures):
            latency, success = future.result()
            if success:
                latencies.append(latency)
                success_count += 1

    global_duration = time.perf_counter() - global_start
    throughput = success_count / global_duration

    # Extract statistical percentiles
    p50 = np.percentile(latencies, 50) if latencies else 0
    p95 = np.percentile(latencies, 95) if latencies else 0
    p99 = np.percentile(latencies, 99) if latencies else 0
    avg_latency = np.mean(latencies) if latencies else 0

    print("\n📊 ─── SYSTEM BENCHMARK SUITE PERFORMANCE METRICS ───────────────────")
    print(f"✅ Successful Requests   : {success_count}/{NUM_BENCHMARK_QUERIES}")
    print(f"🚀 Measured Throughput   : {throughput:.2f} vectors/second")
    print(f"⏱️  Average Latency       : {avg_latency:.2f} ms")
    print(f"📉 p50 Percentile (Med)  : {p50:.2f} ms")
    print(f"📉 p95 Percentile        : {p95:.2f} ms")
    print(f"🔥 p99 Percentile (Max)  : {p99:.2f} ms")
    print("──────────────────────────────────────────────────────────────────────")
    
    if throughput >= 10000 and p99 < 10.0:
        print("🏆 VERIFICATION SUCCESS: Metrics validate resume performance claims!")
    else:
        print("⚠️  VERIFICATION NOTICE: Performance bounds out of target spectrum. Optimize graph parameters.")
    print("======================================================================\n")

if __name__ == "__main__":
    run_performance_suite()