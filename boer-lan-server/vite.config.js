import { defineConfig } from 'vite'
import { createVuePlugin } from 'vite-plugin-vue2'
import path from 'path'

export default defineConfig(({ mode }) => {
  const isWin7Shell = mode === 'win7'
  return {
    plugins: [createVuePlugin()],
    resolve: {
      alias: {
        '@': path.resolve(__dirname, 'src'),
        'vue': 'vue/dist/vue.esm.js',
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
      port: 45173,
      strictPort: true,
      proxy: {
        '/api': {
          target: 'http://localhost:8088',
          changeOrigin: true
        }
      }
    },
    build: {
      outDir: 'dist',
      assetsDir: 'assets',
      sourcemap: false
    },
    clearScreen: false
  }
})
