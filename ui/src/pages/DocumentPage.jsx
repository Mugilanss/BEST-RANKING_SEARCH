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
      .then(r => { if (!r.ok) throw new Error(`HTTP Status ${r.status}`); return r.json(); })
      .then(setDoc)
      .catch(e => setError(e.message));
  }, [id]);

  if (error) return (
    <div className="doc-page">
      <div className="doc-error">
        <h3>⚠️ Document Error</h3>
        <p>{error}</p>
        <button className="doc-back" style={{marginTop:"2rem"}} onClick={() => navigate(-1)}>Go Back</button>
      </div>
    </div>
  );

  if (!doc)  return (
    <div className="doc-page">
      <div className="doc-loading">
        <span className="searchbar-spinner" style={{width:"40px", height:"40px", marginBottom:"1rem"}} />
        <p>Retrieving source contents...</p>
      </div>
    </div>
  );

  const filename = doc.path.split(/[\\\/]/).pop();
  const date = doc.mtime ? new Date(doc.mtime * 1000).toLocaleString("en-US", {
    weekday: 'short', year: 'numeric', month: 'short', day: 'numeric', hour: '2-digit', minute: '2-digit'
  }) : "—";
  const sizeKB = (doc.size / 1024).toFixed(2);

  return (
    <div className="doc-page">
      <button className="doc-back" onClick={() => navigate(-1)}>
        <span>←</span> Back to Search
      </button>
      
      <div className="doc-header">
        <h2 className="doc-title">{filename}</h2>
        <div className="doc-meta">
          <div className="doc-meta-item">ID <b>{doc.docID}</b></div>
          <div className="doc-meta-item">SIZE <b>{sizeKB} KB</b></div>
          <div className="doc-meta-item">MODIFIED <b>{date}</b></div>
        </div>
        <div className="doc-path-box">
          <span>📁</span> {doc.path}
        </div>
      </div>

      <div className="doc-content-container">
        <div className="doc-content-header">
          <span className="doc-content-title">File content reader</span>
          <div style={{display:"flex", gap:"8px"}}>
            <span style={{width:"12px", height:"12px", borderRadius:"50%", background:"#f87171"}}></span>
            <span style={{width:"12px", height:"12px", borderRadius:"50%", background:"#fbbf24"}}></span>
            <span style={{width:"12px", height:"12px", borderRadius:"50%", background:"#34d399"}}></span>
          </div>
        </div>
        <pre className="doc-content">
          {doc.content || "// No content available for this document."}
        </pre>
      </div>
    </div>
  );
}
