import { BrowserRouter, Routes, Route } from "react-router-dom";
import Navbar from "./components/Navbar";
import SearchPage   from "./pages/SearchPage";
import DocumentPage from "./pages/DocumentPage";
import DashboardPage from "./pages/DashboardPage";
import AdminPage    from "./pages/AdminPage";

export default function App() {
  return (
    <BrowserRouter>
      <Navbar />
      <Routes>
        <Route path="/"              element={<SearchPage />} />
        <Route path="/document/:id"  element={<DocumentPage />} />
        <Route path="/dashboard"     element={<DashboardPage />} />
        <Route path="/admin"         element={<AdminPage />} />
      </Routes>
    </BrowserRouter>
  );
}
