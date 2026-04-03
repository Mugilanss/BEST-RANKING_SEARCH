import { useEffect, useState } from "react";
import { useParams, useNavigate } from "react-router-dom";
import { API_BASE } from "../api";
import "./DocumentPage.css";

export default function DocumentPage() {
  const { id } = useParams();
  const navigate = useNavigate();
  const [doc, setDoc]     = useState(null);
  const [error, setError] = useState("");

  useEffect(() => {
    fetch(`${API_BASE}/document/${id}`)
      .then(r => { if (!r.ok) throw new Error(`HTTP ${r.status}`); return r.json(); })
      .then(setDoc)
      .catch(e => setError(e.message));
  }, [id]);

  if (error) return <div className="doc-page"><div className="doc-error">Error: {error}</div></div>;
  if (!doc)  return <div className="doc-page"><div className="doc-loading">Loading…</div></div>;

  const filename = doc.path.split(/[\\/]/).pop();
  const date = doc.mtime ? new Date(doc.mtime * 1000).toLocaleString() : "—";
  const sizeKB = (doc.size / 1024).toFixed(2);

  return (
    <div className="doc-page">
      <button className="doc-back" onClick={() => navigate(-1)}>← Back</button>
      <div className="doc-header">
        <h2 className="doc-title">{filename}</h2>
        <div className="doc-meta">
          <span>ID: {doc.docID}</span>
          <span>{sizeKB} KB</span>
          <span>{date}</span>
        </div>
        <div className="doc-path">{doc.path}</div>
      </div>
      <pre className="doc-content">{doc.content || "(content not loaded)"}</pre>
    </div>
  );
}
