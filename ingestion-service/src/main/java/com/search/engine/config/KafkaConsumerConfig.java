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
        
        factory.setConsumerFactory(consumerFactory);
        
        // ✅ FIXED: Record mode required for @RetryableTopic compatibility.
        // Your consumer method signature (String record) is already record-mode —
        // batch mode was never needed here and directly caused the startup crash.
        factory.setBatchListener(false);
        
        // Multi-threaded partition consumption
        factory.setConcurrency(3); 
        
        return factory;
    }
}