import { useState } from "react";
import { API_BASE } from "../api";
import "./AdminPage.css";

function ActionCard({ title, description, buttonLabel, onAction, danger }) {
  const [status, setStatus]   = useState("");
  const [loading, setLoading] = useState(false);

  async function handle() {
    setLoading(true); setStatus("");
    try { const res = await onAction(); setStatus(`✓ ${JSON.stringify(res)}`); }
    catch (e) { setStatus(`✗ ${e.message}`); }
    finally { setLoading(false); }
  }

  return (
    <div className={`admin-card ${danger ? "danger" : ""}`}>
      <div className="admin-card-title">{title}</div>
      <div className="admin-card-desc">{description}</div>
      <button className={`admin-btn ${danger ? "danger" : ""}`} onClick={handle} disabled={loading}>
        {loading ? "Working…" : buttonLabel}
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
    if (!url.trim()) { setStatus("✗ Enter a URL"); return; }
    setLoading(true); setStatus("");
    try {
      const res = await fetch(
        `${API_BASE}/crawl?url=${encodeURIComponent(url)}&depth=${depth}&pages=${pages}`,
        { method: "POST", headers: { Authorization: `Bearer ${token}` } }
      );
      const d = await res.json();
      if (!res.ok) throw new Error(d.error || res.status);
      setStatus(`✓ ${d.status} — up to ${d.max_pages} pages from ${d.seed}`);
    } catch(e) { setStatus(`✗ ${e.message}`); }
    finally { setLoading(false); }
  }

  return (
    <div className="admin-card">
      <div className="admin-card-title">Crawl a Website</div>
      <div className="admin-card-desc">Fetches pages, respects robots.txt, saves to docs/crawled/ and indexes incrementally.</div>
      <div className="crawl-inputs">
        <input className="crawl-input" placeholder="https://example.com" value={url} onChange={e=>setUrl(e.target.value)} />
        <label>Depth <input type="number" min={1} max={5} value={depth} onChange={e=>setDepth(+e.target.value)} className="crawl-num" /></label>
        <label>Pages <input type="number" min={1} max={100} value={pages} onChange={e=>setPages(+e.target.value)} className="crawl-num" /></label>
      </div>
      <button className="admin-btn" onClick={handle} disabled={loading}>{loading ? "Starting…" : "Start Crawl"}</button>
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

  if (!loaded) return <button className="admin-btn" onClick={load}>Load Popular Queries</button>;
  if (queries.length === 0) return <p style={{color:"#64748b"}}>No queries logged yet.</p>;
  return (
    <table className="admin-table">
      <thead><tr><th>#</th><th>Query</th><th>Count</th></tr></thead>
      <tbody>
        {queries.map((q,i) => (
          <tr key={q.query}><td>{i+1}</td><td><code>{q.query}</code></td><td>{q.count}</td></tr>
        ))}
      </tbody>
    </table>
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
      <h2 className="admin-title">Admin Panel</h2>

      <div className="admin-grid">
        <ActionCard
          title="Rebuild Index"
          description="Drops the current index and rebuilds it from scratch by scanning the docs folder. This may take a while for large corpora."
          buttonLabel="Trigger Full Rebuild"
          onAction={() => postAction("/index/rebuild")}
          danger
        />
        <ActionCard
          title="Incremental Update"
          description="Scans the docs folder for new files not yet in the index and adds them without a full rebuild. Fast for small additions."
          buttonLabel="Run Incremental Update"
          onAction={() => postAction("/index/update")}
        />
      </div>

      <div className="admin-section">
        <h3>Quick Stats</h3>
        <ActionCard
          title="Fetch Stats"
          description="Fetch current engine stats from /stats endpoint."
          buttonLabel="Fetch Stats"
          onAction={async () => {
            const res = await fetch(`${API_BASE}/stats`);
            if (!res.ok) throw new Error(`HTTP ${res.status}`);
            const d = await res.json();
            setLogs(JSON.stringify({ docs: d.docs, index_kb: d.index_kb, avg_doc_len: d.avg_doc_len, scoring: d.scoring }, null, 2));
            return { docs: d.docs, index_kb: d.index_kb };
          }}
        />
        {logs && (
          <pre className="admin-logs">{logs}</pre>
        )}
      </div>

      <div className="admin-section">
        <h3>Web Crawler (6.4)</h3>
        <div className="crawl-form">
          <CrawlCard ctx={API_BASE} />
        </div>
      </div>

      <div className="admin-section">
        <h3>Popular Queries (6.3)</h3>
        <PopularQueries />
      </div>

      <div className="admin-section">
        <h3>API Reference</h3>
        <table className="admin-table">
          <thead><tr><th>Method</th><th>Endpoint</th><th>Description</th></tr></thead>
          <tbody>
            {[
              ["GET",  "/search?q=&k=&sort=",     "Full-text search"],
              ["GET",  "/document/<id>",           "Fetch document by ID"],
              ["GET",  "/stats",                   "Engine metrics & top terms"],
              ["GET",  "/autocomplete?q=&k=",      "Prefix autocomplete + query suggestions"],
              ["GET",  "/popular?k=",              "Top queries from query log"],
              ["POST", "/index/rebuild",           "Full index rebuild (auth required)"],
              ["POST", "/index/update",            "Incremental WAL update (auth required)"],
              ["POST", "/crawl?url=&depth=&pages=","Web crawler (auth required)"],
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
  );
}
