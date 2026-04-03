import { useNavigate } from "react-router-dom";
import Highlight from "./Highlight";
import "./ResultCard.css";

export default function ResultCard({ result, queryTerms }) {
  const navigate = useNavigate();
  const filename = result.path.split(/[\\/]/).pop();
  const score = parseFloat(result.score).toFixed(4);
  const sizeKB = (result.size / 1024).toFixed(1);
  const date = result.mtime ? new Date(result.mtime * 1000).toLocaleDateString() : "—";

  return (
    <div className="result-card" onClick={() => navigate(`/document/${result.docID}`)}>
      <div className="result-header">
        <span className="result-filename">{filename}</span>
        <span className="result-score">score {score}</span>
      </div>
      <div className="result-path">{result.path}</div>
      <div className="result-snippet">
        <Highlight text={result.snippet} terms={queryTerms} />
      </div>
      <div className="result-meta">
        <span>{sizeKB} KB</span>
        <span>{date}</span>
      </div>
    </div>
  );
}
