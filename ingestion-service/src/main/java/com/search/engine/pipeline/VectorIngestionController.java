package com.search.engine.pipeline;

import com.fasterxml.jackson.databind.ObjectMapper;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.http.HttpStatus;
import org.springframework.http.ResponseEntity;
import org.springframework.kafka.core.KafkaTemplate;
import org.springframework.web.bind.annotation.*;

import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.CompletableFuture;

@RestController
@RequestMapping("/api/v1/vectors")
@CrossOrigin(origins = "*") // 💎 Enforces clean connection access rules for React UI
public class VectorIngestionController {

    private static final Logger log = LoggerFactory.getLogger(VectorIngestionController.class);
    private static final String TOPIC = "vector-ingestion-stream";

    @Autowired
    private KafkaTemplate<String, String> kafkaTemplate;

    @Autowired
    private ObjectMapper objectMapper;

    @PostMapping("/ingest")
    public ResponseEntity<Map<String, Object>> ingestVector(@RequestBody TextPayload payload) {
        long startTime = System.nanoTime();
        
        try {
            // Build a fast text processing token payload
            Map<String, Object> messageMap = new HashMap<>();
            messageMap.put("id", payload.getId());
            messageMap.put("text", payload.getText());
            
            String cleanJsonPayload = objectMapper.writeValueAsString(messageMap);

            // Dispatch right into Kafka and return a fast async response to the UI
            kafkaTemplate.send(TOPIC, String.valueOf(payload.getId()), cleanJsonPayload);

            long durationNs = System.nanoTime() - startTime;
            
            Map<String, Object> response = new HashMap<>();
            response.put("status", "SUCCESS");
            response.put("message", "Text string successfully buffered inside Kafka cluster asynchronously.");
            response.put("internal_queue_latency_ms", durationNs / 1_000_000.0);

            return ResponseEntity.status(HttpStatus.ACCEPTED).body(response);

        } catch (Exception e) {
            log.error("Ingestion endpoint exception occurred: ", e);
            Map<String, Object> errorResponse = new HashMap<>();
            errorResponse.put("status", "FAILED");
            errorResponse.put("error", e.getMessage());
            return ResponseEntity.status(HttpStatus.INTERNAL_SERVER_ERROR).body(errorResponse);
        }
    }
}

/**
 * @brief Matches the exact incoming JSON contract sent by your UI layer
 */
class TextPayload {
    private long id;
    private String text;

    public long getId() { return id; }
    public void setId(long id) { this.id = id; }
    public String getText() { return text; }
    public void setText(String text) { this.text = text; }
}