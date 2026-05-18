#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <queue>
#include <condition_variable>
#include <mutex>
#include <shared_mutex> // For high-concurrency read-write separation
#include <functional>
#include <grpcpp/grpcpp.h>
#include <future>

#include "hnsw_index.hpp"
#include "vector_service.grpc.pb.h"
#include "vector_service.pb.h"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;

using vector_service::VectorComputeEngine;
using vector_service::VectorMessage;
using vector_service::IngestResponse;
using vector_service::QueryRequest;
using vector_service::QueryResponse;

// ─── HIGH PERFORMANCE C++ THREAD POOL WORKER INFRASTRUCTURE ─────────────────
class WorkerThreadPool {
public:
    explicit WorkerThreadPool(size_t threads) : stop(false) {
        for (size_t i = 0; i < threads; ++i) {
            workers.emplace_back([this] {
                while (true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(this->queue_mutex);
                        this->condition.wait(lock, [this] { return this->stop || !this->tasks.empty(); });
                        if (this->stop && this->tasks.empty()) return;
                        task = std::move(this->tasks.front());
                        this->tasks.pop();
                    }
                    task(); // Execute HNSW query or insertion tasks concurrently
                }
            });
        }
    }
    
    template<class F>
    void enqueue(F&& f) {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            tasks.emplace(std::forward<F>(f));
        }
        condition.notify_one();
    }
    
    ~WorkerThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            stop = true;
        }
        condition.notify_all();
        for (std::thread &worker : workers) {
            if (worker.joinable()) worker.join();
        }
    }

private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop;
};

// ─── CONCURRENT VECTOR COMPUTE SERVICE ───────────────────────────────────────
class VectorComputeEngineServiceImpl final : public VectorComputeEngine::Service {
public:
    VectorComputeEngineServiceImpl() 
        : index_(std::make_unique<vector_engine::HNSWIndex>(384, 100000, 16, 200, 50)),
          pool_(std::thread::hardware_concurrency()) {
        std::cout << "⚡ HNSW Core initialized with " << std::thread::hardware_concurrency() 
          << " concurrent hardware worker threads." << std::endl;
    }

    // Write Path: Guarded by exclusive write locks to protect structural graph updates
    Status IngestVectorStream(ServerContext* context, 
                             const VectorMessage* request, 
                             IngestResponse* response) override {
        try {
            int64_t vector_id = request->id();
            std::vector<float> incoming_values(request->values().begin(), request->values().end());

            // Exclusive Lock: Ensures no queries or mutations happen simultaneously during structural graph shift
            std::unique_lock<std::shared_mutex> write_lock(index_mutex_);
            
            index_->insert(vector_id, incoming_values);

            response->set_status("SUCCESS");
            response->set_message("Vector compiled and added to graph index nodes.");
            return Status::OK;
        } catch (const std::exception& e) {
            std::cerr << "[C++ Ingestion Error] Node write crashed: " << e.what() << std::endl;
            response->set_status("ERROR");
            return Status(grpc::StatusCode::INTERNAL, e.what());
        }
    }

    // Read Path: Fully parallelized via WorkerThreadPool and shared lock references
    Status QueryNearestNeighbors(ServerContext* context,
                                 const QueryRequest* request,
                                 QueryResponse* response) override {
        // ⚡ FIX 1: Renamed variable to 'status_promise' and explicitly qualified 'grpc::Status' 
        // to stop the compiler from mistaking the variable name for the template type structure.
        auto status_promise = std::make_shared<std::promise<grpc::Status>>();
        auto future = status_promise->get_future();

        // Offload execution block into our hardware thread pool worker layer
        pool_.enqueue([this, request, response, status_promise]() {
            try {
                std::vector<float> query_vec(request->query_vector().begin(), request->query_vector().end());
                size_t k = request->top_k();

                // ⚡ FIX 2: Correctly maps to your custom core C++ implementation struct
                std::vector<vector_engine::DistancePair> knn_matches;
                {
                    // Shared Lock: Allows unlimited concurrent read threads as long as write_lock is unheld
                    std::shared_lock<std::shared_mutex> read_lock(index_mutex_);
                    knn_matches = index_->searchKnn(query_vec, k);
                }

                for (const auto& match : knn_matches) {
                    auto* proto_match = response->add_matches();
                    proto_match->set_id(match.node_id);
                    proto_match->set_distance(match.distance);
                }

                response->set_status("SUCCESS");
                status_promise->set_value(grpc::Status::OK);
            } catch (const std::exception& e) {
                std::cerr << "[C++ Core Error] Parallel query search dropped: " << e.what() << std::endl;
                response->set_status("ERROR");
                status_promise->set_value(grpc::Status(grpc::StatusCode::INTERNAL, e.what()));
            }
        });

        // Block the current connection context thread until the assigned pool worker resolves the future task
        return future.get();
    }

private:
    std::unique_ptr<vector_engine::HNSWIndex> index_;
    WorkerThreadPool pool_;
    std::shared_mutex index_mutex_; // Prevents race conditions across high-throughput pipeline worker channels
};

void RunServer() {
    std::string server_address("0.0.0.0:50051");
    VectorComputeEngineServiceImpl service;

    ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    
    std::unique_ptr<Server> server(builder.BuildAndStart());
    std::cout << "🚀 Async Parallel C++ HNSW Engine online on " << server_address << std::endl;
    server->Wait();
}

int main(int argc, char** argv) {
    RunServer();
    return 0;
}