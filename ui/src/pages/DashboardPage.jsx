import { useEffect, useState, useCallback } from "react";
import { BarChart, Bar, XAxis, YAxis, Tooltip, ResponsiveContainer, CartesianGrid, Cell } from "recharts";
import { API_BASE } from "../api";
import "./DashboardPage.css";

export default function DashboardPage() {
  const [stats, setStats]     = useState(null);
  const [error, setError]     = useState("");
  const [loading, setLoading] = useState(false);

  const fetchStats = useCallback(async () => {
    setLoading(true);
    setError("");
    try {
      const res = await fetch(`${API_BASE}/stats`);
      if (!res.ok) throw new Error(`HTTP ${res.status}`);
      setStats(await res.json());
    } catch (e) {
      setError(e.message);
    } finally {
      setLoading(false);
    }
  }, []);

  useEffect(() => { fetchStats(); }, [fetchStats]);

  const statCards = stats ? [
    { label: "Total Documents", value: stats.docs, icon: "📄" },
    { label: "Index Size",      value: `${stats.index_kb} KB`, icon: "💾" },
    { label: "Avg Doc Length",  value: `${parseFloat(stats.avg_doc_len).toFixed(1)} tkn`, icon: "📏" },
    { label: "Scoring Model",   value: stats.scoring, icon: "🎯" },
    { label: "Query Volume",    value: stats.queries ?? "0", icon: "📈" },
    { label: "Cache Hits",      value: stats.cache_hits ?? "0", icon: "⚡" },
    { label: "Avg Latency",     value: stats.avg_latency_ms != null ? `${parseFloat(stats.avg_latency_ms).toFixed(1)} ms` : "0 ms", icon: "⏱️" },
    { label: "Cache Ratio",     value: stats.cache_hit_rate != null ? `${parseFloat(stats.cache_hit_rate).toFixed(1)}%` : "0%", icon: "🧩" },
  ] : [];

  const topTermsData = stats?.top_terms?.slice(0, 15).map(t => ({ term: t.term, df: t.df })) || [];

  return (
    <div className="dash-page">
      <div className="dash-header">
        <div className="dash-header-left">
          <h2>Analytics & Metrics</h2>
          <p className="dash-header-subtitle">Real-time performance and indexing data</p>
        </div>
        <button className="dash-refresh" onClick={fetchStats} disabled={loading}>
          {loading ? <span className="searchbar-spinner" /> : "↻"} 
          {loading ? "Refreshing..." : "Refresh"}
        </button>
      </div>

      {error && <div className="dash-error">⚠️ {error}</div>}

      {stats && (
        <>
          <div className="stat-grid">
            {statCards.map(c => (
              <div key={c.label} className="stat-card">
                <div className="stat-value">{c.value}</div>
                <div className="stat-label">{c.label}</div>
              </div>
            ))}
          </div>

          <div className="dash-section">
            <div className="dash-section-header">
              <span className="dash-section-icon">📊</span>
              <h3>Term Distribution (Top 15)</h3>
            </div>
            <div className="chart-container">
              <ResponsiveContainer width="100%" height={320}>
                <BarChart data={topTermsData} margin={{ top: 10, right: 10, left: 0, bottom: 60 }}>
                  <defs>
                    <linearGradient id="barGradient" x1="0" y1="0" x2="0" y2="1">
                      <stop offset="0%" stopColor="#63b3ed" />
                      <stop offset="100%" stopColor="#a78bfa" />
                    </linearGradient>
                  </defs>
                  <CartesianGrid strokeDasharray="3 3" vertical={false} stroke="rgba(255,255,255,0.05)" />
                  <XAxis 
                    dataKey="term" 
                    tick={{ fill: "#94a3b8", fontSize: 11, fontWeight: 500 }} 
                    angle={-45} 
                    textAnchor="end" 
                    interval={0}
                    height={60}
                    axisLine={{ stroke: "rgba(255,255,255,0.1)" }}
                    tickLine={false}
                  />
                  <YAxis 
                    tick={{ fill: "#94a3b8", fontSize: 11, fontWeight: 500 }}
                    axisLine={{ stroke: "rgba(255,255,255,0.1)" }}
                    tickLine={false}
                  />
                  <Tooltip 
                    cursor={{ fill: "rgba(255,255,255,0.02)" }}
                    contentStyle={{ 
                      background: "#111827", 
                      border: "1px solid rgba(255,255,255,0.1)", 
                      borderRadius: "10px",
                      color: "#f0f4ff",
                      fontSize: "12px",
                      boxShadow: "0 10px 15px -3px rgba(0, 0, 0, 0.4)"
                    }} 
                  />
                  <Bar dataKey="df" fill="url(#barGradient)" radius={[4, 4, 0, 0]} barSize={24} />
                </BarChart>
              </ResponsiveContainer>
            </div>
          </div>

          <div className="dash-section">
            <div className="dash-section-header">
              <span className="dash-section-icon">📑</span>
              <h3>Dictionary Statistics</h3>
            </div>
            <div className="table-wrap">
              <table className="terms-table">
                <thead>
                  <tr>
                    <th>Rank</th>
                    <th>Term</th>
                    <th>Document Frequency</th>
                  </tr>
                </thead>
                <tbody>
                  {stats.top_terms?.map((t, i) => (
                    <tr key={t.term}>
                      <td className="term-rank">#{i + 1}</td>
                      <td><code className="term-code">{t.term}</code></td>
                      <td className="term-freq">{t.df} docs</td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          </div>
        </>
      )}
    </div>
  );
}
