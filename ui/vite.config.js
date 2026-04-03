import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'

export default defineConfig({
  plugins: [react()],
  server: {
    port: 5173,
    proxy: {
      '/search':        'http://localhost:8080',
      '/document':      'http://localhost:8080',
      '/stats':         'http://localhost:8080',
      '/autocomplete':  'http://localhost:8080',
      '/index':         'http://localhost:8080',
    }
  }
})
