export default function Highlight({ text, terms }) {
  if (!terms || terms.length === 0) return <span>{text}</span>;
  const pattern = new RegExp(`(${terms.map(t => t.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")).join("|")})`, "gi");
  const parts = text.split(pattern);
  return (
    <span>
      {parts.map((part, i) =>
        pattern.test(part)
          ? <mark key={i} style={{ background: "#fef08a", borderRadius: "2px", padding: "0 1px" }}>{part}</mark>
          : <span key={i}>{part}</span>
      )}
    </span>
  );
}
