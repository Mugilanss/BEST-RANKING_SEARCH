import { useNavigate } from "react-router-dom";
import Highlight from "./Highlight";
import "./ResultCard.css";

function getFileIcon(filename) {
  const ext = filename.split(".").pop()?.toLowerCase();
  const map = {
    cpp: "⚙️", cc: "⚙️", cxx: "⚙️", c: "⚙️",
    h: "📋", hpp: "📋",
    txt: "📄", md: "📝", pdf: "📕",
    js: "🟨", ts: "🔷", jsx: "⚛️", tsx: "⚛️",
    json: "📦", xml: "🗂️", html: "🌐", css: "🎨",
    py: "🐍", java: "☕", rs: "🦀", go: "🐹",
  };
  return map[ext] || "📄";
}

export default function ResultCard({ result, queryTerms, rank }) {
  const navigate = useNavigate();
  const filename = result.path.split(/[\\\/]/).pop();
  const score = parseFloat(result.score);
  const scoreDisplay = score.toFixed(4);
  const scorePct = Math.min(100, (score / 20) * 100); // normalize out of 20
  const sizeKB = (result.size / 1024).toFixed(1);
  const date = result.mtime ? new Date(result.mtime * 1000).toLocaleDateString("en-US", {
    month: "short", day: "numeric", year: "numeric"
  }) : "—";

  return (
    <div
      className="result-card"
      onClick={() => navigate(`/document/${result.docID}`)}
      role="article"
      tabIndex={0}
      onKeyDown={e => e.key === "Enter" && navigate(`/document/${result.docID}`)}
    >
      {/* ── Rank Badge ── */}
      {rank && <span className="result-rank">#{rank}</span>}

      {/* ── Header: icon + filename + doc ID ── */}
      <div className="result-header">
        <span className="result-file-icon">{getFileIcon(filename)}</span>
        <div className="result-title-group">
          <span className="result-filename">{filename}</span>
          <span className="result-doc-id">DOC·{result.docID}</span>
        </div>
      </div>

      {/* ── Score bar ── */}
      <div className="result-score-bar">
        <span className="result-score-label">Relevance</span>
        <div className="result-score-track">
          <div className="result-score-fill" style={{ width: `${scorePct}%` }} />
        </div>
        <span className="result-score-value">{scoreDisplay}</span>
      </div>

      {/* ── Full path ── */}
      <div className="result-path">
        <span className="result-path-icon">📁</span>
        {result.path}
      </div>

      {/* ── Matching passage ── */}
      {result.snippet && (
        <div className="result-passage">
          <span className="result-passage-label">
            ✦ Matching passage
          </span>
          <div className="result-snippet">
            <Highlight text={result.snippet} terms={queryTerms} />
          </div>
        </div>
      )}

      {/* ── Footer meta ── */}
      <div className="result-meta">
        <span className="result-meta-item">
          💾 {sizeKB} KB
        </span>
        <span className="result-meta-item">
          📅 {date}
        </span>
        <span className="result-view-link">
          Open document →
        </span>
      </div>
    </div>
  );
}
