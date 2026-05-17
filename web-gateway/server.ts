import express from 'express';
import http from 'http';
import { WebSocketServer, WebSocket } from 'ws';
import cors from 'cors';
import path from 'path';
import { fileURLToPath } from 'url';
import * as grpc from '@grpc/grpc-js';
import * as protoLoader from '@grpc/proto-loader';
import { createClient } from 'redis';                 
import { pipeline } from '@xenova/transformers';
import { Kafka } from 'kafkajs';                      // 📥 NEW: Native Apache Kafka Client Import

const app = express();
app.use(cors());
app.use(express.json());

const server = http.createServer(app);
const wss = new WebSocketServer({ server });

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

// ─── REDIS CACHING INSTANCE INITIALIZATION ───────────────────────────
const redisClient = createClient({ url: 'redis://localhost:6379' }); 
redisClient.on('error', (err) => console.error('❌ Redis Connection Fault:', err));

(async () => {
    try {
        await redisClient.connect();
        console.log('⚡ Redis Cache Server Connected Successfully!');
    } catch (err) {
        console.error('❌ Failed to establish Redis session:', err);
    }
})();

// ─── KAFKA BROKER MESH INITIALIZATION ────────────────────────────────
const kafkaInstance = new Kafka({
    clientId: 'node-vector-gateway-worker',
    brokers: ['localhost:9092']                      // Targets containerized cluster node
});
const kafkaConsumer = kafkaInstance.consumer({ groupId: 'hnsw-node-group' });

// ─── AI EMBEDDING MODEL INITIALIZATION ───────────────────────────────
let embedder: any = null;

pipeline('feature-extraction', 'Xenova/all-MiniLM-L6-v2')
  .then(pipe => {
    embedder = pipe;
    console.log('🤖 Local AI Model Loaded! Translating text directly to 384-dim native space...');
  })
  .catch(err => console.error('❌ Model failed to load:', err));

// ─── GRPC PROTO SETUP ────────────────────────────────────────────────
const PROTO_PATH = path.join(__dirname, '../protos/vector_service.proto');
const packageDefinition = protoLoader.loadSync(PROTO_PATH, {
    keepCase: true, longs: String, enums: String, defaults: true, oneofs: true
});
const vectorProto = grpc.loadPackageDefinition(packageDefinition) as any;
const client = new vectorProto.vector_service.VectorComputeEngine(
    'localhost:50051', 
    grpc.credentials.createInsecure()
);

// ─── WEBSOCKET CLIENT POOL ───────────────────────────────────────────
let activeSockets: WebSocket[] = [];
wss.on('connection', (ws: WebSocket) => {
    activeSockets.push(ws);
    ws.on('close', () => activeSockets = activeSockets.filter(s => s !== ws));
});

const streamTelemetryToUI = (event: string, data: any) => {
    const payload = JSON.stringify({ event, data, timestamp: Date.now() });
    activeSockets.forEach(ws => { if (ws.readyState === WebSocket.OPEN) ws.send(payload); });
};

// ─── ASYNCHRONOUS BACKGROUND KAFKA STREAM WORKER LOOP ───────────────
async function initializeKafkaIngestionWorker() {
    try {
        await kafkaConsumer.connect();
        await kafkaConsumer.subscribe({ topic: 'vector-ingestion-stream', fromBeginning: false });
        console.log('📥 Node.js Gateway Kafka Worker actively polling for incoming stream lines...');

        await kafkaConsumer.run({
            eachMessage: async ({ message }) => {
                if (!embedder) {
                    console.warn('⚠️ Kafka message deferred: AI Transformer model is still loading...');
                    return;
                }

                try {
                    const rawPayload = message.value?.toString();
                    if (!rawPayload) return;

                    const parsedData = JSON.parse(rawPayload);
                    const nodeId = parsedData.id;
                    const rawText = parsedData.text;

                    console.log(`📥 Kafka Dequeued Node #${nodeId} -> Computing 384 native embeddings...`);

                    // Generate clean structural high-dimensional embeddings natively
                    const output = await embedder(rawText, { pooling: 'mean', normalize: true });
                    const values = Array.from(output.data) as number[]; 

                    // Forward vector data directly down gRPC channel to your running C++ server binary
                    client.IngestVectorStream({ id: nodeId, values }, (err: any, response: any) => {
                        if (err) {
                            console.error(`❌ Native gRPC Core Insertion Error for Node #${nodeId}:`, err.message);
                        } else {
                            console.log(`🚀 Success! Node #${nodeId} committed directly into C++ HNSW graph engine.`);
                        }
                    });

                } catch (jsonError) {
                    console.error('❌ Failed to extract properties from streaming record line:', jsonError);
                }
            }
        });
    } catch (kafkaError) {
        console.error('❌ Critical Kafka connection failure inside loop initialization:', kafkaError);
    }
}

// Fire up the listener loop as soon as your server boot sequence clears
setTimeout(() => {
    initializeKafkaIngestionWorker().catch(err => console.error("❌ Kafka consumer worker crashed:", err));
}, 4000);

// ─── API ENDPOINTS ───────────────────────────────────────────────────

// 1. Synchronous Direct Ingest Fallback Route (Old architecture route)
app.post('/api/ingest', async (req, res) => {
    const { id, text } = req.body;
    if (!embedder) return res.status(503).json({ error: 'AI engine warming up...' });

    try {
        const output = await embedder(text, { pooling: 'mean', normalize: true });
        const values = Array.from(output.data) as number[]; 

        client.IngestVectorStream({ id, values }, (err: any, response: any) => {
            if (err) return res.status(500).json({ error: err.message });
            return res.json({ id, status: response.status });
        });
    } catch (err: any) {
        return res.status(500).json({ error: err.message });
    }
});

// 2. Search Route: Intercepts queries via Redis cache for sub-1ms execution
app.post('/api/search', async (req, res) => {
    const { query_text, top_k } = req.body; 
    if (!embedder) return res.status(503).json({ error: 'AI engine warming up...' });

    const cacheKey = `query:${query_text.trim().toLowerCase()}:k:${top_k}`;

    try {
        const startTime = process.hrtime.bigint();

        // REDIS LOOKUP: Check if query has a valid hit in memory cache
        if (redisClient.isOpen) {
            const cachedResults = await redisClient.get(cacheKey);
            if (cachedResults) {
                const endTime = process.hrtime.bigint();
                const latencyMs = Number(endTime - startTime) / 1_000_000;

                const parsedData = JSON.parse(cachedResults);
                console.log(`🎯 Cache Hit! Returning results for "${query_text}" in ${latencyMs}ms`);
                
                streamTelemetryToUI('QUERY_METRIC', { latencyMs, matchCount: parsedData.matches?.length || 0 });
                return res.json({ latencyMs, cached: true, ...parsedData });
            }
        }

        // Cache Miss -> Fall back to Neural Computation + C++ HNSW Graph Traversal
        const output = await embedder(query_text, { pooling: 'mean', normalize: true });
        const query_vector = Array.from(output.data) as number[];

        client.QueryNearestNeighbors({ query_vector, top_k }, async (err: any, response: any) => {
            if (err) return res.status(500).json({ error: err.message });

            const endTime = process.hrtime.bigint();
            const latencyMs = Number(endTime - startTime) / 1_000_000;

            // Save clean response string in Redis with an Expiration TTL of 1 Hour (3600 seconds)
            if (redisClient.isOpen) {
                await redisClient.setEx(cacheKey, 3600, JSON.stringify(response));
            }

            console.log(`📡 Cache Miss. Computed vector embeddings and traversed HNSW graph over gRPC in ${latencyMs}ms`);
            streamTelemetryToUI('QUERY_METRIC', { latencyMs, matchCount: response.matches?.length || 0 });
            return res.json({ latencyMs, cached: false, ...response });
        });
    } catch (err: any) {
        return res.status(500).json({ error: err.message });
    }
});

const PORT = 4000;
server.listen(PORT, () => console.log(`🚀 AI Web Gateway operational on http://localhost:${PORT}`));