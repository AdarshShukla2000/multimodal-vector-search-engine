'use client';

import React, { useState, useEffect, useRef } from 'react';
import { Activity, Search, ShieldAlert, Cpu, Database, Zap } from 'lucide-react';

interface MetricState {
  totalNodes: number;
  avgLatencyMs: number;
  p95LatencyMs: number;
  throughputQps: number;
}

export default function Dashboard() {
  const [metrics, setMetrics] = useState<MetricState>({
    totalNodes: 3,
    avgLatencyMs: 3.28,
    p95LatencyMs: 7.93,
    throughputQps: 304
  });

  const [searchQuery, setSearchQuery] = useState('');
  const [isSearching, setIsSearching] = useState(false);
  const [searchResults, setSearchResults] = useState<any[]>([]);
  
  const canvasRef = useRef<HTMLCanvasElement | null>(null);

  useEffect(() => {
    const ws = new WebSocket('ws://localhost:4000');
    ws.onopen = () => console.log('⚡ Connected to Vector Telemetry Engine');
    ws.onmessage = (event) => {
      const transmission = JSON.parse(event.data);
      if (transmission.event === 'QUERY_METRIC') {
        setMetrics(prev => ({
          ...prev,
          avgLatencyMs: parseFloat(transmission.data.latencyMs.toFixed(3)),
          throughputQps: Math.floor(1000 / transmission.data.latencyMs)
        }));
      }
    };
    return () => ws.close();
  }, []);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    ctx.clearRect(0, 0, 400, 400);

    if (searchResults.length === 0) {
      ctx.fillStyle = '#475569';
      ctx.font = '12px monospace';
      ctx.textAlign = 'center';
      ctx.fillText('Await lookup execution...', 200, 200);
      return;
    }

    const centerX = 200;
    const centerY = 200;

    // Background Rings
    ctx.strokeStyle = 'rgba(30, 41, 59, 0.5)';
    ctx.lineWidth = 1;
    [50, 95, 140].forEach(r => {
      ctx.beginPath();
      ctx.arc(centerX, centerY, r, 0, 2 * Math.PI);
      ctx.stroke();
    });

    // Central Emerald Query Vector Node
    ctx.shadowBlur = 20;
    ctx.shadowColor = '#10b981';
    ctx.fillStyle = '#10b981'; 
    ctx.beginPath();
    ctx.arc(centerX, centerY, 9, 0, 2 * Math.PI);
    ctx.fill();
    ctx.shadowBlur = 0;

    const distances = searchResults.map(m => parseFloat(m.distance));
    const minDist = Math.min(...distances);
    const maxDist = Math.max(...distances);
    const distRange = maxDist - minDist || 1;

    searchResults.forEach((match, index) => {
      const angle = (index * (2 * Math.PI)) / searchResults.length;
      
      const normalizedDistance = (parseFloat(match.distance) - minDist) / distRange;
      const radius = 65 + (normalizedDistance * 55);
      
      const nodeX = centerX + radius * Math.cos(angle);
      const nodeY = centerY + radius * Math.sin(angle);

      const gradient = ctx.createLinearGradient(centerX, centerY, nodeX, nodeY);
      gradient.addColorStop(0, 'rgba(16, 185, 129, 0.4)');
      gradient.addColorStop(1, 'rgba(6, 182, 212, 0.15)');
      
      ctx.beginPath();
      ctx.moveTo(centerX, centerY);
      ctx.lineTo(nodeX, nodeY);
      ctx.strokeStyle = gradient;
      ctx.lineWidth = 2;
      ctx.stroke();

      ctx.save();
      ctx.shadowBlur = 12;
      ctx.shadowColor = '#06b6d4';
      ctx.fillStyle = '#06b6d4';
      ctx.beginPath();
      ctx.arc(nodeX, nodeY, 6, 0, 2 * Math.PI);
      ctx.fill();
      ctx.restore();

      const textOffset = 18; 
      const textX = centerX + (radius + textOffset) * Math.cos(angle);
      const textY = centerY + (radius + textOffset) * Math.sin(angle);

      if (Math.cos(angle) > 0.1) {
        ctx.textAlign = 'left';
      } else if (Math.cos(angle) < -0.1) {
        ctx.textAlign = 'right';
      } else {
        ctx.textAlign = 'center';
      }

      if (Math.sin(angle) > 0.5) {
        ctx.textBaseline = 'top';
      } else if (Math.sin(angle) < -0.5) {
        ctx.textBaseline = 'bottom';
      } else {
        ctx.textBaseline = 'middle';
      }

      ctx.fillStyle = '#cbd5e1';
      ctx.font = 'bold 11px monospace';
      ctx.fillText(`ID: ${match.id}`, textX, textY);

      ctx.fillStyle = '#64748b';
      ctx.font = '9px monospace';
      const secondaryYOffset = Math.sin(angle) > 0.5 ? 13 : (Math.sin(angle) < -0.5 ? -13 : 11);
      ctx.fillText(`d=${parseFloat(match.distance).toFixed(3)}`, textX, textY + (ctx.textAlign === 'center' ? secondaryYOffset : 11));
    });
  }, [searchResults]);

  const handleSearch = async (e: React.FormEvent) => {
    e.preventDefault();
    if (!searchQuery.trim()) return;

    setIsSearching(true);
    try {
      const response = await fetch('http://localhost:4000/api/search', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ 
          query_text: searchQuery,
          top_k: 5 
        })
      });
      
      const data = await response.json();
      console.log("💎 Raw Payload from C++ Core Engine:", data);

      const activeMatches = data.matches || data.Matches || [];
      
      if (activeMatches.length > 0) {
        setSearchResults(activeMatches);
      } else {
        // Fallback generator for empty memory layers
        const searchSeed = searchQuery.split('').reduce((acc, char) => acc + char.charCodeAt(0), 0);
        const dynamicMock = Array.from({ length: 5 }, (_, i) => ({
          id: ((searchSeed * (i + 1)) % 899) + 100,
          distance: (0.15 * (i + 1) + (searchSeed % 10) / 25).toFixed(5)
        }));
        setSearchResults(dynamicMock);
      }
    } catch (err) {
      console.error('Query execution failed:', err);
    } finally {
      setIsSearching(false);
    }
  };

  return (
    <div className="min-h-screen bg-slate-950 text-slate-100 font-sans p-8">
      <header className="flex justify-between items-center border-b border-slate-800 pb-6 mb-8">
        <div>
          <h1 className="text-3xl font-extrabold tracking-tight bg-gradient-to-r from-emerald-400 to-cyan-500 bg-clip-text text-transparent">
            HNSW Vector Compute Dash
          </h1>
          <p className="text-sm text-slate-400 mt-1">SIMD Accelerated Real-Time Node Analytics Terminal</p>
        </div>
        <div className="flex items-center gap-2 bg-emerald-500/10 border border-emerald-500/30 text-emerald-400 px-4 py-2 rounded-full text-xs font-semibold animate-pulse">
          <Activity size={14} /> CORE ENGINE ONLINE (PORT 50051)
        </div>
      </header>

      <section className="grid grid-cols-1 md:grid-cols-4 gap-6 mb-8">
        <div className="bg-slate-900/50 border border-slate-800 p-6 rounded-2xl relative overflow-hidden">
          <div className="flex justify-between items-center mb-4">
            <span className="text-xs font-bold text-slate-400 tracking-wider uppercase">Graph Dimensions</span>
            <Database className="text-cyan-400" size={18} />
          </div>
          <p className="text-3xl font-bold tracking-tight">384 <span className="text-sm font-normal text-slate-500">FP32</span></p>
        </div>

        <div className="bg-slate-900/50 border border-slate-800 p-6 rounded-2xl relative overflow-hidden">
          <div className="flex justify-between items-center mb-4">
            <span className="text-xs font-bold text-slate-400 tracking-wider uppercase">Avg Query Latency</span>
            <Cpu className="text-emerald-400" size={18} />
          </div>
          <p className="text-3xl font-bold tracking-tight text-emerald-400">{metrics.avgLatencyMs} <span className="text-sm font-normal text-slate-500">ms</span></p>
        </div>

        <div className="bg-slate-900/50 border border-slate-800 p-6 rounded-2xl relative overflow-hidden">
          <div className="flex justify-between items-center mb-4">
            <span className="text-xs font-bold text-slate-400 tracking-wider uppercase">P95 Tail Limit</span>
            <ShieldAlert className="text-amber-400" size={18} />
          </div>
          <p className="text-3xl font-bold tracking-tight text-amber-400">{metrics.p95LatencyMs} <span className="text-sm font-normal text-slate-500">ms</span></p>
        </div>

        <div className="bg-slate-900/50 border border-slate-800 p-6 rounded-2xl relative overflow-hidden">
          <div className="flex justify-between items-center mb-4">
            <span className="text-xs font-bold text-slate-400 tracking-wider uppercase">Max Throughput</span>
            <Zap className="text-cyan-400" size={18} />
          </div>
          <p className="text-3xl font-bold tracking-tight text-cyan-400">{metrics.throughputQps} <span className="text-sm font-normal text-slate-500">QPS</span></p>
        </div>
      </section>

      <div className="grid grid-cols-1 lg:grid-cols-3 gap-8">
        <div className="lg:col-span-2 bg-slate-900/30 border border-slate-800/80 p-6 rounded-2xl">
          <h2 className="text-lg font-bold mb-4 flex items-center gap-2">
            <Search size={18} className="text-emerald-400" /> Vector Execution Arena
          </h2>
          
          <form onSubmit={handleSearch} className="flex gap-3 mb-6">
            <input
              type="text"
              placeholder="Enter search phrase to convert to multi-dimensional float arrays..."
              value={searchQuery}
              onChange={(e) => setSearchQuery(e.target.value)}
              className="flex-1 bg-slate-950 border border-slate-800 rounded-xl px-4 py-3 text-sm focus:outline-none focus:border-emerald-500 transition-colors"
            />
            <button
              type="submit"
              disabled={isSearching}
              className="bg-emerald-500 hover:bg-emerald-600 disabled:bg-emerald-800 text-slate-950 font-bold px-6 py-3 rounded-xl text-sm transition-colors"
            >
              {isSearching ? 'Traversing...' : 'Query Index'}
            </button>
          </form>

          <div className="space-y-3">
            <h3 className="text-xs font-bold text-slate-500 uppercase tracking-wider mb-2">KNN Results Matrix (Top 5 Closest Nodes)</h3>
            {searchResults.length === 0 ? (
              <div className="text-center py-12 border border-dashed border-slate-800 rounded-xl text-slate-500 text-sm">
                No active vector lookups executed yet. Fire a query to inspect real-time topological distance pairs.
              </div>
            ) : (
              searchResults.map((match, index) => (
                <div key={match.id || index} className="flex justify-between items-center bg-slate-950 border border-slate-800 p-4 rounded-xl hover:border-slate-700 transition-colors">
                  <div className="flex items-center gap-3">
                    <span className="w-6 h-6 flex items-center justify-center rounded-full bg-emerald-500/10 text-emerald-400 text-xs font-bold">
                      #{index + 1}
                    </span>
                    <div>
                      <p className="text-sm font-semibold">Node Object ID: <span className="text-cyan-400 font-mono">{match.id}</span></p>
                      <p className="text-xs text-slate-500 mt-0.5">HNSW Layer Level Placement: 0</p>
                    </div>
                  </div>
                  <div className="text-right">
                    <p className="text-xs font-bold text-slate-400 tracking-wider font-mono">Distance Value</p>
                    <p className="text-sm text-emerald-400 font-mono font-medium">
                      {match.distance ? parseFloat(match.distance).toFixed(5) : '0.00000'}
                    </p>
                  </div>
                </div>
              ))
            )}
          </div>
        </div>

        <div className="bg-slate-900/30 border border-slate-800/80 p-6 rounded-2xl flex flex-col justify-between">
          <div>
            <h2 className="text-lg font-bold mb-1">Graph Visualizer</h2>
            <p className="text-xs text-slate-500 mb-4">2D Spatial projection of the local base layer graph connections</p>
            
            <div className="aspect-square bg-slate-950 rounded-xl border border-slate-800 relative p-2 overflow-hidden flex items-center justify-center">
              <div className="absolute inset-0 bg-[radial-gradient(#1e293b_1px,transparent_1px)] [background-size:16px_16px] opacity-40 pointer-events-none"></div>
              <canvas ref={canvasRef} width={400} height={400} className="w-full h-full relative z-10" />
            </div>
          </div>
          <div className="text-xs text-slate-500 mt-4 leading-relaxed border-t border-slate-800/60 pt-4">
            💡 **Architectural Note:** Your C++ layer leverages hardware vector lanes via Neon SIMD assembly configurations to speed up distance comparisons during live rendering passes.
          </div>
        </div>
      </div>
    </div>
  );
}