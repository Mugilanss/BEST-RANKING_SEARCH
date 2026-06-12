import { useState } from "react";
import { API_BASE } from "../api";
import "./AdminPage.css";

function LoginForm({ onLogin }) {
  const [username, setUsername] = useState("");
  const [password, setPassword] = useState("");
  const [error, setError]       = useState("");
  const [loading, setLoading]   = useState(false);

  async function handle() {
    if (!username || !password) { setError("Enter username and password"); return; }
    setLoading(true); setError("");
    try {
      const res = await fetch(
        `${API_BASE}/auth/login?username=${encodeURIComponent(username)}&password=${encodeURIComponent(password)}`,
        { method: "POST" }
      );
      const d = await res.json();
      if (!res.ok) throw new Error(d.error || "Login failed");
      localStorage.setItem("adminToken", d.token);
      onLogin(d.token);
    } catch (e) {
      setError(e.message);
    } finally {
      setLoading(false);
    }
  }

  return (
    <div className="login-wrap">
      <div className="login-card">
        <div className="login-title">🔐 Admin Login</div>
        <div className="login-subtitle">Sign in to access the control deck</div>
        <input
          className="login-input"
          type="text"
          placeholder="Username"
          value={username}
          onChange={e => setUsername(e.target.value)}
          onKeyDown={e => e.key === "Enter" && handle()}
        />
        <input
          className="login-input"
          type="password"
          placeholder="Password"
          value={password}
          onChange={e => setPassword(e.target.value)}
          onKeyDown={e => e.key === "Enter" && handle()}
        />
        <button className="admin-btn" onClick={handle} disabled={loading}>
          {loading ? <span className="searchbar-spinner" /> : null}
          {loading ? "Signing in..." : "Sign In"}
        </button>
        {error && <div className="admin-status err">✗ {error}</div>}
      </div>
    </div>
  );
}

function ActionCard({ title, description, buttonLabel, onAction, danger, icon }) {
  const [status, setStatus]   = useState("");
  const [loading, setLoading] = useState(false);

  async function handle() {
    setLoading(true); setStatus("");
    try {
      const res = await onAction();
      setStatus(`✓ Success: ${JSON.stringify(res)}`);
    } catch (e) { setStatus(`✗ Error: ${e.message}`); }
    finally { setLoading(false); }
  }

  return (
    <div className={`admin-card ${danger ? "danger" : ""}`}>
      <div className="admin-card-title"><span>{icon}</span> {title}</div>
      <div className="admin-card-desc">{description}</div>
      <button className={`admin-btn ${danger ? "danger" : ""}`} onClick={handle} disabled={loading}>
        {loading ? <span className="searchbar-spinner" /> : null}
        {loading ? "Processing..." : buttonLabel}
      </button>
      {status && <div className={`admin-status ${status.startsWith("✓") ? "ok" : "err"}`}>{status}</div>}
    </div>
  );
}

function CrawlCard({ token }) {
  const [url, setUrl]         = useState("");
  const [depth, setDepth]     = useState(2);
  const [pages, setPages]     = useState(20);
  const [status, setStatus]   = useState("");
  const [loading, setLoading] = useState(false);

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
      <div className="admin-card-desc">Expand index by crawling remote sites.</div>
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
  const [token, setToken] = useState(localStorage.getItem("adminToken") || "");
  const [logs, setLogs]   = useState("");

  function handleLogin(t) { setToken(t); }
  function handleLogout() {
    localStorage.removeItem("adminToken");
    setToken("");
  }

  if (!token) return <LoginForm onLogin={handleLogin} />;

  async function postAction(path) {
    const res = await fetch(`${API_BASE}${path}`, {
      method: "POST",
      headers: { Authorization: `Bearer ${token}` }
    });
    if (!res.ok) {
      if (res.status === 403) { handleLogout(); throw new Error("Session expired, please login again"); }
      throw new Error(`HTTP ${res.status}`);
    }
    return res.json();
  }

  return (
    <div className="admin-page">
      <div style={{display:"flex", justifyContent:"space-between", alignItems:"center"}}>
        <h2 className="admin-title">Control Deck</h2>
        <button className="admin-btn" onClick={handleLogout} style={{width:"auto", padding:"0.4rem 1rem"}}>
          Sign Out
        </button>
      </div>

      <div className="admin-grid">
        <ActionCard
          icon="⚡"
          title="Full Index Rebuild"
          description="Drops the existing index and parses all documents in the corpus."
          buttonLabel="Trigger Cold Rebuild"
          onAction={() => postAction("/index/rebuild")}
          danger
        />
        <ActionCard
          icon="🔄"
          title="Incremental Sync"
          description="Fast scan of the documents folder for modified or new files."
          buttonLabel="Run Hot Update"
          onAction={() => postAction("/index/update")}
        />
        <CrawlCard token={token} />
        <ActionCard
          icon="📊"
          title="Engine Observability"
          description="Retrieve low-level metrics including document counts and scoring metadata."
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
          <div className="admin-section-header"><h3>Metrics Explorer</h3></div>
          <pre className="admin-logs-pre">{logs}</pre>
        </div>
      )}

      <div className="admin-section">
        <div className="admin-section-header"><h3>Top Query Intent</h3></div>
        <PopularQueries />
      </div>

      <div className="admin-section">
        <div className="admin-section-header"><h3>System Endpoints</h3></div>
        <div className="admin-table-wrap">
          <table className="admin-table">
            <thead><tr><th>Verb</th><th>Route</th><th>Operation</th></tr></thead>
            <tbody>
              {[
                ["GET",  "/search",        "Execute full-text query"],
                ["GET",  "/document",      "Retrieve raw source by ID"],
                ["GET",  "/stats",         "System metrics and term frequency"],
                ["GET",  "/autocomplete",  "Prefix-based term discovery"],
                ["GET",  "/popular",       "Historical query throughput"],
                ["POST", "/auth/login",    "Authenticate and get JWT"],
                ["POST", "/auth/register", "Create new user (admin only)"],
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