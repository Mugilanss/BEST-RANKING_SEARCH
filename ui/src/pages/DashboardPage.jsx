import { useEffect, useState, useCallback } from "react";
import { BarChart, Bar, XAxis, YAxis, Tooltip, ResponsiveContainer, CartesianGrid } from "recharts";
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

  // Stats fields are now flat numeric values from the API
  const statCards = stats ? [
    { label: "Documents",     value: stats.docs },
    { label: "Index Size",    value: `${stats.index_kb} KB` },
    { label: "Avg Doc Len",   value: `${parseFloat(stats.avg_doc_len).toFixed(1)} tokens` },
    { label: "Scoring",       value: stats.scoring },
    { label: "Total Queries", value: stats.queries ?? "—" },
    { label: "Cache Hits",    value: stats.cache_hits ?? "—" },
    { label: "Avg Latency",   value: stats.avg_latency_ms != null ? `${parseFloat(stats.avg_latency_ms).toFixed(1)} ms` : "—" },
    { label: "Cache Hit Rate",value: stats.cache_hit_rate != null ? `${parseFloat(stats.cache_hit_rate).toFixed(1)}%` : "—" },
  ] : [];

  const topTermsData = stats?.top_terms?.slice(0, 15).map(t => ({ term: t.term, df: t.df })) || [];

  return (
    <div className="dash-page">
      <div className="dash-header">
        <h2>Dashboard</h2>
        <button className="dash-refresh" onClick={fetchStats} disabled={loading}>
          {loading ? "Loading…" : "↻ Refresh"}
        </button>
      </div>

      {error && <div className="dash-error">Error: {error}</div>}

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
            <h3>Top 15 Terms by Document Frequency</h3>
            <div className="chart-wrap">
              <ResponsiveContainer width="100%" height={280}>
                <BarChart data={topTermsData} margin={{ top: 8, right: 16, left: 0, bottom: 60 }}>
                  <CartesianGrid strokeDasharray="3 3" stroke="#1e293b" />
                  <XAxis dataKey="term" tick={{ fill: "#94a3b8", fontSize: 11 }} angle={-40} textAnchor="end" interval={0} />
                  <YAxis tick={{ fill: "#94a3b8", fontSize: 11 }} />
                  <Tooltip contentStyle={{ background: "#1e293b", border: "1px solid #334155", color: "#f1f5f9" }} />
                  <Bar dataKey="df" fill="#38bdf8" radius={[4, 4, 0, 0]} />
                </BarChart>
              </ResponsiveContainer>
            </div>
          </div>

          <div className="dash-section">
            <h3>Top Terms Table</h3>
            <table className="terms-table">
              <thead>
                <tr><th>#</th><th>Term</th><th>Doc Frequency</th></tr>
              </thead>
              <tbody>
                {stats.top_terms?.map((t, i) => (
                  <tr key={t.term}>
                    <td>{i + 1}</td>
                    <td><code>{t.term}</code></td>
                    <td>{t.df}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        </>
      )}
    </div>
  );
}
