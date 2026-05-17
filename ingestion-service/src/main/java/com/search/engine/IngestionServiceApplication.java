package com.search.engine;

import org.springframework.boot.SpringApplication;
import org.springframework.boot.autoconfigure.SpringBootApplication;

/**
 * @brief Main Entry Point for the High-Throughput Vector Ingestion Service.
 */
@SpringBootApplication(scanBasePackages = "com.search.engine")
public class IngestionServiceApplication {

    public static void main(String[] args) {
        // Spin up the Spring Application Context framework loop
        SpringApplication.run(IngestionServiceApplication.class, args);
    }
}