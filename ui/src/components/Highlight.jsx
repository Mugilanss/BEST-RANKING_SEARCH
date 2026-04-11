export default function Highlight({ text, terms }) {
  if (!text) return null;
  if (!terms || terms.length === 0) return <span>{text}</span>;

  // Build a regex that matches query terms OR the ellipsis separator
  const escaped = terms.map(t => t.replace(/[.*+?^${}()|[\]\\]/g, "\\$&"));
  const pattern = new RegExp(
    `(${escaped.join("|")}|\u2026)`,
    "gi"
  );
  const parts = text.split(pattern);

  return (
    <span>
      {parts.map((part, i) => {
        if (part === "\u2026")
          return (
            <span key={i} style={{
              color: "var(--text-muted)",
              fontStyle: "italic",
              margin: "0 4px",
              userSelect: "none",
              fontSize: "1.1em"
            }}>…</span>
          );
        if (pattern.test(part) && part !== "\u2026")
          return (
            <mark key={i} style={{
              background: "rgba(251, 191, 36, 0.15)",
              color: "#fbbf24",
              borderRadius: "4px",
              padding: "1px 3px",
              fontWeight: 600,
              boxShadow: "0 0 10px rgba(251, 191, 36, 0.05)"
            }}>{part}</mark>
          );
        return <span key={i}>{part}</span>;
      })}
    </span>
  );
}
