import { useState, useCallback } from "react";
import SearchBar from "../components/SearchBar";
import ResultCard from "../components/ResultCard";
import { API_BASE } from "../api";
import "./SearchPage.css";

const PAGE_SIZE = 10;

function SkeletonCards() {
  return (
    <div className="skeleton-list">
      {[1,2,3].map(i => (
        <div key={i} className="skeleton-card">
          <div className="skeleton-line wide" />
          <div className="skeleton-line short" />
          <div className="skeleton-line full" />
          <div className="skeleton-line mid" />
        </div>
      ))}
    </div>
  );
}

export default function SearchPage() {
  const [results, setResults]       = useState([]);
  const [query, setQuery]           = useState("");
  const [queryTerms, setQueryTerms] = useState([]);
  const [sort, setSort]             = useState("score");
  const [page, setPage]             = useState(1);
  const [total, setTotal]           = useState(0);
  const [latency, setLatency]       = useState(null);
  const [cacheHit, setCacheHit]     = useState(false);
  const [loading, setLoading]       = useState(false);
  const [error, setError]           = useState("");

  const doSearch = useCallback(async (q, sortBy = sort, pg = 1) => {
    setLoading(true);
    setError("");
    setQuery(q);
    setQueryTerms(q.toLowerCase().split(/\s+/).filter(t => !["and","or","not"].includes(t)));
    try {
      const k = PAGE_SIZE * 10;
      const res = await fetch(`${API_BASE}/search?q=${encodeURIComponent(q)}&k=${k}&sort=${sortBy}`);
      if (!res.ok) throw new Error(`HTTP ${res.status}`);
      const data = await res.json();
      setResults(data.results || []);
      setTotal(data.results?.length || 0);
      setLatency(data.latency_ms);
      setCacheHit(data.cache_hit);
      setPage(pg);
    } catch (e) {
      setError(e.message);
    } finally {
      setLoading(false);
    }
  }, [sort]);

  function handleSort(s) {
    setSort(s);
    if (query) doSearch(query, s, 1);
  }

  const totalPages  = Math.ceil(total / PAGE_SIZE);
  const pageResults = results.slice((page - 1) * PAGE_SIZE, page * PAGE_SIZE);
  const hasResults  = total > 0;
  const hasSearched = !!query;

  return (
    <div className="search-page">
      {/* ── Hero ── */}
      <div className="search-hero">
        <div className="search-hero-content">
          <div className="search-hero-badge">⚡ Powered by C++ · BM25</div>
          <h1 className="search-title">CppSearch</h1>
          <p className="search-sub">
            Full-text search with BM25 ranking, Boolean logic &amp; phrase queries
          </p>
          <SearchBar onSearch={q => doSearch(q, sort, 1)} loading={loading} />
          <div className="search-features">
            {["AND / OR / NOT", "Phrase queries", "Autocomplete", "Ranked results", "Cached"].map(f => (
              <span key={f} className="search-feature-tag">✓ {f}</span>
            ))}
          </div>
        </div>
      </div>

      {/* ── Results area ── */}
      <div className="search-results-area">
        {error && (
          <div className="search-error">
            ⚠️ {error}
          </div>
        )}

        {loading && <SkeletonCards />}

        {!loading && hasResults && (
          <>
            {/* Meta bar */}
            <div className="search-meta-bar">
              <div className="search-meta-info">
                <span className="search-meta-count">{total} results</span>
                {latency !== null && (
                  <>
                    <span className="search-meta-sep">·</span>
                    <span className="search-meta-latency">{latency} ms</span>
                  </>
                )}
                {cacheHit && <span className="cache-badge">⚡ Cached</span>}
              </div>
              <div className="sort-controls">
                <span className="sort-label">Sort:</span>
                {["score", "date", "size"].map(s => (
                  <button
                    key={s}
                    className={`sort-btn ${sort === s ? "active" : ""}`}
                    onClick={() => handleSort(s)}
                  >
                    {s}
                  </button>
                ))}
              </div>
            </div>

            {/* Results */}
            <div className="results-list">
              {pageResults.map((r, i) => (
                <ResultCard
                  key={r.docID}
                  result={r}
                  queryTerms={queryTerms}
                  rank={(page - 1) * PAGE_SIZE + i + 1}
                />
              ))}
            </div>

            {/* Pagination */}
            {totalPages > 1 && (
              <div className="pagination">
                <button
                  className="pagination-btn"
                  disabled={page === 1}
                  onClick={() => setPage(p => p - 1)}
                >
                  ← Prev
                </button>
                <span className="pagination-info">
                  {page} / {totalPages}
                </span>
                <button
                  className="pagination-btn"
                  disabled={page === totalPages}
                  onClick={() => setPage(p => p + 1)}
                >
                  Next →
                </button>
              </div>
            )}
          </>
        )}

        {!loading && hasSearched && total === 0 && !error && (
          <div className="no-results">
            <div className="no-results-icon">🔍</div>
            <div className="no-results-title">No results for "{query}"</div>
            <div className="no-results-sub">
              Try different keywords, or check for typos
            </div>
          </div>
        )}
      </div>
    </div>
  );
}
