import { useState } from "react";
import { API_BASE } from "../api";
import "./AdminPage.css";

function ActionCard({ title, description, buttonLabel, onAction, danger, icon }) {
  const [status, setStatus]   = useState("");
  const [loading, setLoading] = useState(false);

  async function handle() {
    setLoading(true); setStatus("");
    try { 
      const res = await onAction(); 
      setStatus(`✓ Success: ${JSON.stringify(res)}`); 
    }
    catch (e) { setStatus(`✗ Error: ${e.message}`); }
    finally { setLoading(false); }
  }

  return (
    <div className={`admin-card ${danger ? "danger" : ""}`}>
      <div className="admin-card-title">
        <span>{icon}</span> {title}
      </div>
      <div className="admin-card-desc">{description}</div>
      <button className={`admin-btn ${danger ? "danger" : ""}`} onClick={handle} disabled={loading}>
        {loading ? <span className="searchbar-spinner" /> : null}
        {loading ? "Processing..." : buttonLabel}
      </button>
      {status && <div className={`admin-status ${status.startsWith("✓") ? "ok" : "err"}`}>{status}</div>}
    </div>
  );
}

function CrawlCard() {
  const [url, setUrl]       = useState("");
  const [depth, setDepth]   = useState(2);
  const [pages, setPages]   = useState(20);
  const [status, setStatus] = useState("");
  const [loading, setLoading] = useState(false);
  const token = localStorage.getItem("adminToken") || "admin";

  async function handle() {
    if (!url.trim()) { setStatus("✗ Error: Enter valid URL"); return; }
    setLoading(true); setStatus("");
    try {
      const res = await fetch(
        `${API_BASE}/crawl?url=${encodeURIComponent(url)}&depth=${depth}&pages=${pages}`,
        { method: "POST", headers: { Authorization: `Bearer ${token}` } }
      );
      const d = await res.json();
      if (!res.ok) throw new Error(d.error || res.status);
      setStatus(`✓ Started: up to ${d.max_pages} pages from ${d.seed}`);
    } catch(e) { setStatus(`✗ Error: ${e.message}`); }
    finally { setLoading(false); }
  }

  return (
    <div className="admin-card">
      <div className="admin-card-title"><span>🕸️</span> Web Crawler</div>
      <div className="admin-card-desc">Expand index by crawling remote sites. Files are saved to docs/crawled and indexed incrementally.</div>
      <div className="crawl-form">
        <div className="crawl-inputs">
          <div className="crawl-field">
            <label>Target URL</label>
            <input className="crawl-input" placeholder="https://example.com" value={url} onChange={e=>setUrl(e.target.value)} />
          </div>
          <div className="crawl-field">
            <label>Depth</label>
            <input type="number" min={1} max={5} value={depth} onChange={e=>setDepth(+e.target.value)} className="crawl-num" />
          </div>
          <div className="crawl-field">
            <label>Limit (Pages)</label>
            <input type="number" min={1} max={100} value={pages} onChange={e=>setPages(+e.target.value)} className="crawl-num" />
          </div>
        </div>
        <button className="admin-btn" onClick={handle} disabled={loading}>
          {loading ? <span className="searchbar-spinner" /> : "🚀"}
          {loading ? "Starting..." : "Begin Crawl"}
        </button>
      </div>
      {status && <div className={`admin-status ${status.startsWith("✓") ? "ok" : "err"}`}>{status}</div>}
    </div>
  );
}

function PopularQueries() {
  const [queries, setQueries] = useState([]);
  const [loaded, setLoaded]   = useState(false);

  async function load() {
    try {
      const res = await fetch(`${API_BASE}/popular?k=15`);
      const d = await res.json();
      setQueries(d.popular || []);
      setLoaded(true);
    } catch(e) { setLoaded(true); }
  }

  if (!loaded) return <button className="admin-btn" onClick={load}>📊 Load Popular Queries</button>;
  if (queries.length === 0) return <p style={{color:"var(--text-muted)", fontSize:"0.9rem"}}>No query logs found.</p>;
  
  return (
    <div className="admin-table-wrap">
      <table className="admin-table">
        <thead><tr><th>Rank</th><th>Search Query</th><th>Frequency</th></tr></thead>
        <tbody>
          {queries.map((q,i) => (
            <tr key={q.query}>
              <td style={{fontWeight:600, color:"var(--text-muted)"}}>#{i+1}</td>
              <td><code>{q.query}</code></td>
              <td style={{color:"var(--accent-primary)", fontWeight:600}}>{q.count} times</td>
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  );
}

export default function AdminPage() {
  const [logs, setLogs] = useState("");
  const token = localStorage.getItem("adminToken") || "admin";

  async function postAction(path) {
    const res = await fetch(`${API_BASE}${path}`, {
      method: "POST",
      headers: { Authorization: `Bearer ${token}` }
    });
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    return res.json();
  }

  return (
    <div className="admin-page">
      <h2 className="admin-title">Control Deck</h2>

      <div className="admin-grid">
        <ActionCard
          icon="⚡"
          title="Full Index Rebuild"
          description="Drops the existing index and parses all documents in the corpus. Use this after configuration changes or massive file updates."
          buttonLabel="Trigger Cold Rebuild"
          onAction={() => postAction("/index/rebuild")}
          danger
        />
        <ActionCard
          icon="🔄"
          title="Incremental Sync"
          description="Fast scan of the documents folder. Checks for modified or new files since the last indexing session."
          buttonLabel="Run Hot Update"
          onAction={() => postAction("/index/update")}
        />
        <CrawlCard />
        <ActionCard
          icon="📊"
          title="Engine Observability"
          description="Retrieve low-level metrics including document counts, average token length, and scoring metadata."
          buttonLabel="Fetch Live Stats"
          onAction={async () => {
            const res = await fetch(`${API_BASE}/stats`);
            if (!res.ok) throw new Error(`HTTP ${res.status}`);
            const d = await res.json();
            setLogs(JSON.stringify({ 
              status: "online",
              docs: d.docs, 
              index_kb: d.index_kb, 
              avg_doc_len: d.avg_doc_len, 
              scoring: d.scoring 
            }, null, 2));
            return { docs: d.docs, size: d.index_kb };
          }}
        />
      </div>

      {logs && (
        <div className="admin-section">
          <div className="admin-section-header">
            <h3>Metrics Explorer</h3>
          </div>
          <pre className="admin-logs-pre">{logs}</pre>
        </div>
      )}

      <div className="admin-section">
        <div className="admin-section-header">
          <h3>Top Query Intent</h3>
        </div>
        <PopularQueries />
      </div>

      <div className="admin-section">
        <div className="admin-section-header">
          <h3>System Endpoints</h3>
        </div>
        <div className="admin-table-wrap">
          <table className="admin-table">
            <thead><tr><th>Verb</th><th>Route</th><th>Operation</th></tr></thead>
            <tbody>
              {[
                ["GET",  "/search",     "Execute full-text query"],
                ["GET",  "/document",   "Retrieve raw source by ID"],
                ["GET",  "/stats",      "System metrics and term frequency"],
                ["GET",  "/autocomplete","Prefix-based term discovery"],
                ["GET",  "/popular",    "Historical query throughput"],
                ["POST", "/index/rebuild", "Purge and reconstruct index"],
                ["POST", "/index/update",  "WAL-style incremental delta"],
                ["POST", "/crawl",         "Remote resource ingestion"],
              ].map(([m, p, d]) => (
                <tr key={p}>
                  <td><span className={`method-badge ${m.toLowerCase()}`}>{m}</span></td>
                  <td><code>{p}</code></td>
                  <td>{d}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      </div>
    </div>
  );
}
