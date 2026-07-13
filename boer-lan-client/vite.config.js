import { defineConfig } from 'vite'
import vue2 from '@vitejs/plugin-vue2'
import path from 'path'

export default defineConfig(({ mode }) => {
  const isWin7Shell = mode === 'win7'
  return {
    plugins: [vue2()],
    resolve: {
      alias: {
        '@': path.resolve(__dirname, 'src'),
        ...(isWin7Shell
          ? {
              '@tauri-apps/api/core': path.resolve(__dirname, 'src/win7-tauri-shim/core.js'),
              '@tauri-apps/api/window': path.resolve(__dirname, 'src/win7-tauri-shim/window.js'),
              '@tauri-apps/api/app': path.resolve(__dirname, 'src/win7-tauri-shim/app.js')
            }
          : {})
      }
    },
    server: {
      port: 43117,
      strictPort: true
    },
    build: {
      outDir: 'dist',
      assetsDir: 'assets',
      sourcemap: false
    },
    // Tauri expects a fixed port
    clearScreen: false
  }
})
