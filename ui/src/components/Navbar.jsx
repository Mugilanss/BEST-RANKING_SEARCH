import { NavLink } from "react-router-dom";
import "./Navbar.css";

export default function Navbar() {
  return (
    <nav className="navbar">
      <span className="navbar-brand">
        <span className="navbar-brand-icon">⚡</span>
        CppSearch
      </span>
      <div className="navbar-links">
        <NavLink to="/" end className={({ isActive }) => isActive ? "active" : ""}>
          🔍 Search
        </NavLink>
        <NavLink to="/dashboard" className={({ isActive }) => isActive ? "active" : ""}>
          📊 Dashboard
        </NavLink>
        <NavLink to="/admin" className={({ isActive }) => isActive ? "active" : ""}>
          ⚙️ Admin
        </NavLink>
      </div>
      <div className="navbar-status">
        <span className="navbar-status-dot" />
        Live
      </div>
    </nav>
  );
}
