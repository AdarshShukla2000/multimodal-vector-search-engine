package com.search.engine.config;

import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;
import org.springframework.kafka.annotation.EnableKafka;
import org.springframework.kafka.config.ConcurrentKafkaListenerContainerFactory;
import org.springframework.kafka.core.ConsumerFactory;

@EnableKafka
@Configuration
public class KafkaConsumerConfig {

    @Bean
    public ConcurrentKafkaListenerContainerFactory<String, String> kafkaListenerContainerFactory(
            ConsumerFactory<String, String> consumerFactory) {
        
        ConcurrentKafkaListenerContainerFactory<String, String> factory = 
                new ConcurrentKafkaListenerContainerFactory<>();
        
        // Injecting the auto-configured consumer factory built from your application.yml
        factory.setConsumerFactory(consumerFactory);
        
        // CRITICAL FIX: Enable multi-record batch processing mode.
        // This stops Spring from parsing a single string payload into a list of comma-separated chunks.
        factory.setBatchListener(true); 
        
        // Multi-threaded consumption processing
        // Spawns concurrent worker threads to pull from multiple topic partitions parallelly
        factory.setConcurrency(3); 
        
        return factory;
    }
}