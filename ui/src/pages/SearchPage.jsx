import { useState, useCallback } from "react";
import SearchBar from "../components/SearchBar";
import ResultCard from "../components/ResultCard";
import { API_BASE } from "../api";
import "./SearchPage.css";

const PAGE_SIZE = 10;

export default function SearchPage() {
  const [results, setResults]     = useState([]);
  const [query, setQuery]         = useState("");
  const [queryTerms, setQueryTerms] = useState([]);
  const [sort, setSort]           = useState("score");
  const [page, setPage]           = useState(1);
  const [total, setTotal]         = useState(0);
  const [latency, setLatency]     = useState(null);
  const [cacheHit, setCacheHit]   = useState(false);
  const [loading, setLoading]     = useState(false);
  const [error, setError]         = useState("");

  const doSearch = useCallback(async (q, sortBy = sort, pg = 1) => {
    setLoading(true);
    setError("");
    setQuery(q);
    setQueryTerms(q.toLowerCase().split(/\s+/).filter(t => !["and","or","not"].includes(t)));
    try {
      const k = PAGE_SIZE * 10; // fetch up to 100 for client-side pagination
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

  const totalPages = Math.ceil(total / PAGE_SIZE);
  const pageResults = results.slice((page - 1) * PAGE_SIZE, page * PAGE_SIZE);

  return (
    <div className="search-page">
      <div className="search-hero">
        <h1 className="search-title">CppSearch</h1>
        <p className="search-sub">Full-text search engine · BM25 · Boolean · Phrase</p>
        <SearchBar onSearch={q => doSearch(q, sort, 1)} loading={loading} />
      </div>

      {error && <div className="search-error">Error: {error}</div>}

      {total > 0 && (
        <>
          <div className="search-meta-bar">
            <span>{total} results {latency !== null && `· ${latency}ms`} {cacheHit && "· cached"}</span>
            <div className="sort-controls">
              <span>Sort:</span>
              {["score","date","size"].map(s => (
                <button key={s} className={`sort-btn ${sort === s ? "active" : ""}`} onClick={() => handleSort(s)}>
                  {s}
                </button>
              ))}
            </div>
          </div>

          <div className="results-list">
            {pageResults.map(r => (
              <ResultCard key={r.docID} result={r} queryTerms={queryTerms} />
            ))}
          </div>

          {totalPages > 1 && (
            <div className="pagination">
              <button disabled={page === 1} onClick={() => setPage(p => p - 1)}>← Prev</button>
              <span>Page {page} / {totalPages}</span>
              <button disabled={page === totalPages} onClick={() => setPage(p => p + 1)}>Next →</button>
            </div>
          )}
        </>
      )}

      {!loading && query && total === 0 && !error && (
        <div className="no-results">No results for <strong>"{query}"</strong></div>
      )}
    </div>
  );
}
