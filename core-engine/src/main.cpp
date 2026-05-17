#include <iostream>
#include <memory>
#include <string>
#include <grpcpp/grpcpp.h>

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

class VectorComputeEngineServiceImpl final : public VectorComputeEngine::Service {
public:
    VectorComputeEngineServiceImpl() 
        : index_(std::make_unique<vector_engine::HNSWIndex>(384, 100000, 16, 200, 50)) {}

    Status IngestVectorStream(ServerContext* context, 
                             const VectorMessage* request, 
                             IngestResponse* response) override {
        try {
            int64_t vector_id = request->id();
            std::vector<float> incoming_values(request->values().begin(), request->values().end());
            index_->insert(vector_id, incoming_values);

            response->set_status("SUCCESS");
            response->set_message("Vector compiled and added to graph index nodes.");
            return Status::OK;
        } catch (const std::exception& e) {
            response->set_status("ERROR");
            return Status(grpc::StatusCode::INTERNAL, e.what());
        }
    }

    Status QueryNearestNeighbors(ServerContext* context,
                                 const QueryRequest* request,
                                 QueryResponse* response) override {
        try {
            std::vector<float> query_vec(request->query_vector().begin(), request->query_vector().end());
            size_t k = request->top_k();

            auto knn_matches = index_->searchKnn(query_vec, k);

            for (const auto& match : knn_matches) {
                auto* proto_match = response->add_matches();
                proto_match->set_id(match.node_id);
                proto_match->set_distance(match.distance);
            }

            response->set_status("SUCCESS");
            return Status::OK;
        } catch (const std::exception& e) {
            std::cerr << "[C++ Core Error] Query search dropped: " << e.what() << std::endl;
            response->set_status("ERROR");
            return Status(grpc::StatusCode::INTERNAL, e.what());
        }
    }

private:
    std::unique_ptr<vector_engine::HNSWIndex> index_;
};

void RunServer() {
    std::string server_address("0.0.0.0:50051");
    VectorComputeEngineServiceImpl service;

    ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    
    std::unique_ptr<Server> server(builder.BuildAndStart());
    std::cout << "🚀 Native C++ HNSW Engine online on " << server_address << std::endl;
    server->Wait();
}

int main(int argc, char** argv) {
    RunServer();
    return 0;
}