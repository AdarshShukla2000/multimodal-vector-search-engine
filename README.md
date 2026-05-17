# 🔍 Multimodal Vector Search Engine

> **Sub-millisecond semantic similarity search over high-dimensional embeddings — built on a polyglot, distributed microservice architecture.**

[![Java](https://img.shields.io/badge/Java-17-ED8B00?style=flat-square&logo=openjdk&logoColor=white)](https://openjdk.org/)
[![Spring Boot](https://img.shields.io/badge/Spring_Boot-3.x-6DB33F?style=flat-square&logo=springboot&logoColor=white)](https://spring.io/projects/spring-boot)
[![Node.js](https://img.shields.io/badge/Node.js-TypeScript-3178C6?style=flat-square&logo=typescript&logoColor=white)](https://www.typescriptlang.org/)
[![C++](https://img.shields.io/badge/C++-HNSW_Engine-00599C?style=flat-square&logo=cplusplus&logoColor=white)](https://isocpp.org/)
[![Kafka](https://img.shields.io/badge/Apache_Kafka-Event_Streaming-231F20?style=flat-square&logo=apachekafka&logoColor=white)](https://kafka.apache.org/)
[![Redis](https://img.shields.io/badge/Redis-Caching-DC382D?style=flat-square&logo=redis&logoColor=white)](https://redis.io/)
[![gRPC](https://img.shields.io/badge/gRPC-Protobuf_v3-244C5A?style=flat-square&logo=grpc&logoColor=white)](https://grpc.io/)
[![Docker](https://img.shields.io/badge/Docker-Compose-2496ED?style=flat-square&logo=docker&logoColor=white)](https://docs.docker.com/compose/)

---

## Table of Contents

- [Overview](#overview)
- [Architecture](#architecture)
- [Tech Stack](#tech-stack)
- [Getting Started](#getting-started)
- [Pipeline Verification](#pipeline-verification)
- [Performance Characteristics](#performance-characteristics)
- [Project Structure](#project-structure)

---

## Overview

This project is a **production-grade distributed vector search infrastructure** capable of serving semantic similarity queries at sub-millisecond latency. It encodes arbitrary text input into 384-dimensional dense embeddings using a local `all-MiniLM-L6-v2` model and indexes them into a custom **C++ HNSW (Hierarchical Navigable Small World)** graph engine — a data structure purpose-built for approximate nearest neighbour search in high-dimensional spaces.

The architecture is deliberately polyglot and microservice-oriented. Each language and runtime was chosen to play to its strengths:

| Concern | Runtime | Rationale |
|---|---|---|
| High-concurrency HTTP ingestion | Java / Spring Boot | Battle-tested thread management; JVM throughput |
| Async message buffering | Apache Kafka | Zero-drop event streaming; absorbs traffic spikes |
| ML model execution | Node.js / TypeScript | Native HuggingFace Transformers ONNX runtime |
| Query caching | Redis | In-memory O(1) lookup; eliminates redundant inference |
| KNN graph traversal | Native C++ | Raw memory control; SIMD-friendly data alignment |
| Inter-service transport | gRPC / Protobuf v3 | Binary framing; ultra-low loopback latency |

---

## Architecture

```
                          ┌─────────────────────────────────────────────────┐
                          │                   CLIENT                        │
                          │         Next.js UI  ·  REST  ·  WebSocket       │
                          └─────────────────────┬───────────────────────────┘
                                                │
                                    ┌───────────▼────────────┐
                                    │   INGRESS LAYER        │
                                    │   Spring Boot :8081    │
                                    │   (< 2ms Kafka offload)│
                                    └───────────┬────────────┘
                                                │  Kafka Produce
                                    ┌───────────▼────────────┐
                                    │   STREAMING BACKBONE   │
                                    │   Apache Kafka :9092   │
                                    │   (Zero-drop buffer)   │
                                    └───────────┬────────────┘
                                                │  Consumer Group
                                    ┌───────────▼────────────┐       ┌───────────────────┐
                                    │  ORCHESTRATION & AI    │──────▶│   CACHE LAYER     │
                                    │  Node.js :4000         │       │   Redis :6379     │
                                    │  MiniLM-L6 · 384-dim   │◀──────│   (sub-1ms hit)   │
                                    └───────────┬────────────┘       └───────────────────┘
                                                │  gRPC
                                    ┌───────────▼────────────┐
                                    │   COMPUTE CORE         │
                                    │   C++ HNSW :50051      │
                                    │   (KNN · gRPC loopback)│
                                    └────────────────────────┘
```

### Service Breakdown

**Ingress Layer — Java Spring Boot (`:8081`)**
The perimeter gate. Accepts high-concurrency REST payloads and immediately serializes them onto a Kafka topic in under **2ms** — fully asynchronous, fire-and-forget. The ingestion path never blocks on model inference or graph writes.

**Streaming Backbone — Apache Kafka (`:9092`)**
Decouples producers from consumers. Acts as a durable, replayable buffer that absorbs traffic spikes without backpressure propagating upstream. Guarantees zero message drop across the pipeline.

**Orchestration & AI Layer — Node.js (`:4000`)**
Hosts the Kafka consumer group and the local HuggingFace feature-extraction pipeline (`Xenova/all-MiniLM-L6-v2`). Converts raw text into 384-dimensional float32 tensors and forwards them to the C++ engine over gRPC. Also owns Redis cache read-through logic on the search path.

**Cache Layer — Redis (`:6379`)**
Intercepts repeated search queries before they reach the neural model. On a cache hit, the entire inference + graph traversal is bypassed, returning results in **sub-1ms**.

**Compute Core — Native C++ (`:50051`)**
The performance-critical heart of the system. Maintains memory-aligned vector arrays, constructs the HNSW proximity graph index, and executes K-Nearest Neighbour traversals over local gRPC loopback sockets — fully bypassing kernel networking overhead.

---

## Demo

> **HNSW Vector Compute Dash** — SIMD Accelerated Real-Time Node Analytics Terminal

<img width="3438" height="1312" alt="Screenshot 2026-05-17 at 11 56 03 PM" src="https://github.com/user-attachments/assets/aa91615a-690c-4779-84fd-cbd6a5fbc0ff" />


*Live query: `"distributed microservices and kafka brokers"` — returning Top 4 KNN results with cosine distance values, alongside a 2D spine projection of the local base-layer graph connections. Avg query latency: **0.525ms** · P95 tail: **7.93ms** · Throughput: **1904 QPS**.*

---

## Tech Stack

| Layer | Technology |
|---|---|
| Frontend | Next.js (React), TailwindCSS, WebSockets |
| Ingestion Server | Java 17, Spring Boot, Spring Kafka |
| AI & Gateway | Node.js, TypeScript, `@xenova/transformers`, `kafkajs` |
| Cache | Redis |
| Message Broker | Apache Kafka, Zookeeper |
| Core Search Engine | Native C++, gRPC, Protocol Buffers v3 |
| Infrastructure | Docker, Docker Compose |

---

## Getting Started

### Prerequisites

- **Docker & Docker Compose** — for Kafka, Zookeeper, and Redis
- **Java 17+** with Maven
- **Node.js 18+** with npm
- **C++ toolchain** (g++ / clang++) with gRPC and Protobuf installed

### 1. Spin Up Infrastructure

Start the decoupled infrastructure mesh — Kafka, Zookeeper, and Redis — using Docker Compose:

```bash
docker-compose up -d
```

Wait for Kafka to be fully healthy before proceeding. You can verify with:

```bash
docker-compose ps
```

### 2. Start the Ingestion Service (Java)

Compile the Protobuf definitions and launch the Spring Boot server:

```bash
cd ingestion-service
mvn clean package -DskipTests
mvn spring-boot:run
```

The embedded Tomcat server will initialize and begin accepting requests on **`:8081`**.

### 3. Start the AI Web Gateway (Node.js)

Install dependencies and launch the background Kafka consumer and gRPC forwarder:

```bash
cd web-gateway
npm install
npx ts-node --esm server.ts
```

Once the model weights have loaded, the consumer will bind to the Kafka topic and begin polling. First startup may take a moment as the ONNX runtime initializes.

### 4. Launch the UI Dashboard (Next.js)

```bash
cd web-gateway/visual-ui
npm run dev
```

Open [http://localhost:3000](http://localhost:3000) to access the real-time telemetry dashboard and vector search interface.

---

## Pipeline Verification

Push a raw text payload directly through the Spring Boot ingestion boundary to trace data flow across the full multi-language pipeline:

```bash
curl -X POST http://localhost:8081/api/v1/vectors/ingest \
  -H "Content-Type: application/json" \
  -d '{
    "id": 500,
    "text": "Deploying containerized microservices into distributed Apache Kafka broker topologies for extreme ingestion scale"
  }'
```

**Expected log telemetry across services:**

```
# Spring Boot
INFO  VectorController  - [Node #500] Received. Offloading to Kafka broker... [1.8ms]

# Node.js Gateway
INFO  KafkaConsumer     - Kafka Dequeued Node #500 -> Computing 384 native embeddings...
INFO  GrpcClient        - Success! Node #500 committed directly into C++ HNSW graph engine.

# C++ Engine (stderr)
[HNSW] INSERT id=500 | dim=384 | graph_size=501 | insert_time=0.31ms
```

Submitting the same text as a search query through the UI will trigger an **immediate Redis cache hit**, dropping end-to-end latency to **< 1ms** — bypassing both model inference and graph traversal entirely.

---

## Performance Characteristics

| Operation | Latency | Notes |
|---|---|---|
| Kafka offload (ingestion) | < 2ms | Spring Boot → Kafka, async |
| Embedding generation | ~30–80ms | MiniLM-L6-v2, ONNX runtime, CPU |
| HNSW KNN traversal | < 1ms | Native C++, memory-aligned arrays |
| Cache hit (Redis) | < 1ms | Bypasses inference + graph entirely |
| Cache miss (full pipeline) | ~35–90ms | Inference + gRPC + HNSW |

> ⚠️ Embedding latency is hardware-dependent. GPU-accelerated runtimes will reduce inference time significantly.

---

## Project Structure

```
MULTIMODAL-SEARCH-ENGINE/
│
├── core-engine/                          # Native C++ — HNSW graph engine & gRPC server
│   ├── build/                            # CMake build artifacts
│   ├── include/                          # Header files
│   ├── src/
│   │   ├── hnsw_index.cpp                # HNSW graph construction & KNN traversal
│   │   └── main.cpp                      # gRPC server entrypoint (:50051)
│   └── CMakeLists.txt
│
├── ingestion-service/                    # Java 17 / Spring Boot — REST ingestion gate
│   ├── src/main/
│   │   ├── java/com/search/engine/
│   │   │   ├── config/
│   │   │   │   └── KafkaConsumerConfig.java
│   │   │   ├── pipeline/
│   │   │   │   ├── VectorIngestionConsumer.java
│   │   │   │   └── VectorIngestionController.java
│   │   │   └── IngestionServiceApplication.java
│   │   └── resources/
│   │       └── application.yml
│   └── pom.xml
│
├── protos/
│   └── vector_service.proto              # Shared Protobuf v3 contract (C++ ↔ Node.js)
│
├── web-gateway/                          # Node.js / TypeScript — AI pipeline & API gateway
│   ├── visual-ui/                        # Next.js frontend — search UI & telemetry dashboard
│   │   ├── app/
│   │   │   ├── page.tsx                  # Main search interface
│   │   │   ├── layout.tsx
│   │   │   └── globals.css
│   │   ├── public/
│   │   ├── AGENTS.md
│   │   ├── CLAUDE.md
│   │   ├── eslint.config.mjs
│   │   └── next-env.d.ts
│   ├── server.ts                         # Kafka consumer + MiniLM embeddings + gRPC client
│   ├── package.json
│   ├── package-lock.json
│   └── tsconfig.json
│
├── benchmark.py                          # End-to-end latency & throughput benchmarks
├── vector_service_pb2_grpc.py            # Python gRPC generated stubs
├── vector_service_pb2.py                 # Python Protobuf generated stubs
├── docker-compose.yml                    # Kafka + Zookeeper + Redis
├── .gitignore
└── README.md
```

---
