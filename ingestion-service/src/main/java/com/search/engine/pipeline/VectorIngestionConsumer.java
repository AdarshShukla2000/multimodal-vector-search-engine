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
import org.springframework.kafka.annotation.RetryableTopic;
import org.springframework.retry.annotation.Backoff;
import org.springframework.stereotype.Service;

import jakarta.annotation.PostConstruct;
import jakarta.annotation.PreDestroy;
import java.util.ArrayList;
import java.util.List;

@Service // 🚀 Activated back into the Spring Context
public class VectorIngestionConsumer {

    private static final Logger log = LoggerFactory.getLogger(VectorIngestionConsumer.class);
    private static final ObjectMapper objectMapper = new ObjectMapper();
    
    private ManagedChannel channel;
    private VectorComputeEngineGrpc.VectorComputeEngineBlockingStub blockingStub;

    @PostConstruct
    public void initGrpcClient() {
        log.info("Initializing Enterprise IPC loopback socket to C++ Compute Core...");
        this.channel = ManagedChannelBuilder.forAddress("localhost", 50051)
                .usePlaintext() 
                .build();
        this.blockingStub = VectorComputeEngineGrpc.newBlockingStub(channel);
    }

    /**
     * @brief Concurrently consumes single streaming records from Kafka.
     * Aligns perfectly with Spring Retryable Topics for automated exponential backoff and DLT routing.
     */
    @RetryableTopic(
        attempts = "3",
        backoff = @Backoff(delay = 1000, multiplier = 2.0),
        dltTopicSuffix = "-dlt"
    )
    @KafkaListener(
        topics = "vector-ingestion-stream", 
        containerFactory = "kafkaListenerContainerFactory"
    )
    public void consumeVectorRecord(String record) {
        long startTime = System.currentTimeMillis();
        log.debug("📊 Processing incoming streaming message payload.");

        try {
            JsonNode jsonNode = objectMapper.readTree(record);
            
            if (jsonNode == null || !jsonNode.has("id") || !jsonNode.has("vector")) {
                log.warn("Malformed packet encountered. Routing out of core loop.");
                return; 
            }

            long vectorId = jsonNode.get("id").asLong();
            JsonNode vectorArray = jsonNode.get("vector");
            
            if (vectorArray == null || !vectorArray.isArray()) {
                return;
            }

            List<Float> floatValues = new ArrayList<>(vectorArray.size());
            for (int i = 0; i < vectorArray.size(); i++) {
                JsonNode element = vectorArray.get(i);
                floatValues.add(element != null && element.isNumber() ? (float) element.asDouble() : 0.0f);
            }

            VectorMessage vectorMessage = VectorMessage.newBuilder()
                    .setId(vectorId)
                    .addAllValues(floatValues)
                    .build();

            // Direct transaction into C++ compute substrate via gRPC
            IngestResponse response = blockingStub.ingestVectorStream(vectorMessage);
            log.info("🚀 Core commit status: {} for node: {} [Cleared in {} ms]", 
                    response.getStatus(), vectorId, (System.currentTimeMillis() - startTime));

        } catch (Exception e) {
            log.error("Pipeline processing degradation on record string segment. Exception context: ", e);
            throw new RuntimeException("Triggering Kafka transaction retry block...", e); 
        }
    }

    @PreDestroy
    public void shutdownChannel() {
        if (channel != null && !channel.isShutdown()) {
            log.info("Gracefully severing C++ IPC channel linkage...");
            channel.shutdown();
        }
    }
}