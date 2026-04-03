// In dev: Vite proxy forwards to http://localhost:8080 (see vite.config.js)
// In production: set VITE_API_BASE env var or update this to your server URL
export const API_BASE = import.meta.env.VITE_API_BASE ?? "";
