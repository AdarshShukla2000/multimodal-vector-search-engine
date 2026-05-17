package com.search.engine.pipeline;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import io.grpc.ManagedChannel;
import io.grpc.ManagedChannelBuilder;
import com.search.engine.grpc.VectorComputeEngineGrpc;
import com.search.engine.grpc.VectorMessage;
import com.search.engine.grpc.IngestResponse;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.kafka.annotation.KafkaListener;
import org.springframework.stereotype.Service;

import jakarta.annotation.PostConstruct;
import jakarta.annotation.PreDestroy;
import java.util.ArrayList;
import java.util.List;

// 🛑 COMMENTED OUT: Tells Spring Framework to ignore this class completely during the container scan phase
// @Service
public class VectorIngestionConsumer {

    private static final Logger log = LoggerFactory.getLogger(VectorIngestionConsumer.class);
    private static final ObjectMapper objectMapper = new ObjectMapper();
    
    private ManagedChannel channel;
    private VectorComputeEngineGrpc.VectorComputeEngineBlockingStub blockingStub;

    /**
     * @brief Establishes a persistent RPC connection loopback channel to our native C++ server.
     */
    @PostConstruct
    public void initGrpcClient() {
        log.info("Initializing Low-Latency IPC loopback socket to C++ Compute Engine...");
        this.channel = ManagedChannelBuilder.forAddress("localhost", 50051)
                .usePlaintext() // Local network channel loopback optimization
                .build();
        this.blockingStub = VectorComputeEngineGrpc.newBlockingStub(channel);
    }

    /**
     * @brief Concurrently consumes high-throughput batch vectors out of Apache Kafka.
     * 🛑 COMMENTED OUT: Stops this worker loop thread pool from contending with the Node gateway for the Kafka stream.
     */
    // @KafkaListener(
    //     topics = "vector-ingestion-stream", 
    //     containerFactory = "kafkaListenerContainerFactory"
    // )
    public void consumeVectorBatch(List<String> messageBatch) {
        long startTime = System.currentTimeMillis();
        log.info("Received execution batch chunk from Kafka. Size: {} records.", messageBatch.size());

        for (String record : messageBatch) {
            try {
                // 1. Safe parsing encapsulation
                JsonNode jsonNode = objectMapper.readTree(record);
                
                // 2. Structural Defensive Validation: Ensure root node and mandatory fields exist
                if (jsonNode == null || !jsonNode.has("id") || !jsonNode.has("vector")) {
                    log.warn("Skipping structurally incomplete or corrupt message packet. Value: {}", record);
                    continue; 
                }

                long vectorId = jsonNode.get("id").asLong();
                JsonNode vectorArray = jsonNode.get("vector");
                
                // 3. Type Validation: Ensure 'vector' field is actually an accessible JSON array
                if (vectorArray == null || !vectorArray.isArray()) {
                    log.warn("Skipping record ID {}: 'vector' field is missing or not a valid JSON array.", vectorId);
                    continue;
                }

                List<Float> floatValues = new ArrayList<>(vectorArray.size());
                for (int i = 0; i < vectorArray.size(); i++) {
                    JsonNode element = vectorArray.get(i);
                    if (element != null && element.isNumber()) {
                        floatValues.add((float) element.asDouble());
                    } else {
                        log.warn("Non-numeric value encountered at index {} for vector ID {}. Defaulting to 0.0f.", i, vectorId);
                        floatValues.add(0.0f);
                    }
                }

                // --- PIPELINE STEP: Native Handshake Execution Boundary ---
                VectorMessage vectorMessage = VectorMessage.newBuilder()
                        .setId(vectorId)
                        .addAllValues(floatValues)
                        .build();

                // Dispatch over our ultra-low latency local gRPC socket channel
                IngestResponse response = blockingStub.ingestVectorStream(vectorMessage);
                
                log.debug("Native core response status: {} for node ID: {}", response.getStatus(), vectorId);

            } catch (com.fasterxml.jackson.core.JsonProcessingException e) {
                log.error("Poison pill ignored. Failed to parse string payload into valid JSON tree structure. Error: {}", e.getMessage());
            } catch (Exception e) {
                log.error("Pipeline degradation: Failed to forward vector data to C++ compute layer", e);
            }
        }

        long processingDuration = System.currentTimeMillis() - startTime;
        log.info("Batch computation ingestion loop finished in {} ms.", processingDuration);
    }

    @PreDestroy
    public void shutdownChannel() {
        if (channel != null && !channel.isShutdown()) {
            log.info("Closing IPC gRPC channel safely...");
            channel.shutdown();
        }
    }
}