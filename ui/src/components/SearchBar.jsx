import { useState, useEffect, useRef } from "react";
import { useDebounce } from "../hooks/useDebounce";
import { API_BASE } from "../api";
import "./SearchBar.css";

export default function SearchBar({ onSearch, loading }) {
  const [query, setQuery] = useState("");
  const [suggestions, setSuggestions] = useState([]);
  const [showSug, setShowSug] = useState(false);
  const debouncedQuery = useDebounce(query, 250);
  const inputRef = useRef(null);

  // Autocomplete fetch
  useEffect(() => {
    if (debouncedQuery.trim().length < 2) { setSuggestions([]); return; }
    const lastWord = debouncedQuery.trim().split(/\s+/).pop();
    if (!lastWord) return;
    fetch(`${API_BASE}/autocomplete?q=${encodeURIComponent(lastWord)}&k=6`)
      .then(r => r.json())
      .then(d => setSuggestions(d.suggestions || []))
      .catch(() => setSuggestions([]));
  }, [debouncedQuery]);

  function submit(q) {
    const val = (q ?? query).trim();
    if (!val) return;
    setShowSug(false);
    setSuggestions([]);
    onSearch(val);
  }

  function pickSuggestion(term) {
    const words = query.trim().split(/\s+/);
    words[words.length - 1] = term;
    const newQ = words.join(" ");
    setQuery(newQ);
    setShowSug(false);
    onSearch(newQ);
  }

  return (
    <div className="searchbar-wrap">
      <div className="searchbar-row">
        <input
          ref={inputRef}
          className="searchbar-input"
          type="text"
          placeholder='Search… (AND / OR / NOT / "phrase")'
          value={query}
          onChange={e => { setQuery(e.target.value); setShowSug(true); }}
          onKeyDown={e => e.key === "Enter" && submit()}
          onFocus={() => setShowSug(true)}
          onBlur={() => setTimeout(() => setShowSug(false), 150)}
          autoComplete="off"
        />
        <button className="searchbar-btn" onClick={() => submit()} disabled={loading}>
          {loading ? "…" : "Search"}
        </button>
      </div>
      {showSug && suggestions.length > 0 && (
        <ul className="suggestions">
          {suggestions.map((s, i) => (
            <li key={i} onMouseDown={() => pickSuggestion(s.term)}>
              {s.term} <span className="sug-freq">({s.freq})</span>
            </li>
          ))}
        </ul>
      )}
    </div>
  );
}
