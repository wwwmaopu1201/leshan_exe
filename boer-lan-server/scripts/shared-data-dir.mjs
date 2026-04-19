import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const __dirname = path.dirname(fileURLToPath(import.meta.url))
const tauriConfigPath = path.resolve(__dirname, '../src-tauri/tauri.conf.json')

function resolveAppIdentifier() {
  try {
    const raw = fs.readFileSync(tauriConfigPath, 'utf8')
    const parsed = JSON.parse(raw)
    return String(parsed.identifier || '').trim() || 'com.boer.lan-server'
  } catch {
    return 'com.boer.lan-server'
  }
}

export function resolveSharedDataDir() {
  const envOverride = String(process.env.BOERLAN_DATA_DIR || '').trim()
  if (envOverride) {
    return envOverride
  }

  const identifier = resolveAppIdentifier()
  const homeDir = os.homedir()
  switch (process.platform) {
    case 'darwin':
      return path.join(homeDir, 'Library', 'Application Support', identifier)
    case 'win32':
      return path.join(process.env.APPDATA || path.join(homeDir, 'AppData', 'Roaming'), identifier)
    default:
      return path.join(process.env.XDG_DATA_HOME || path.join(homeDir, '.local', 'share'), identifier)
  }
}

export function resolveSharedDatabasePath() {
  return path.join(resolveSharedDataDir(), 'boer-lan.db')
}

export function resolveSharedPortFile() {
  return path.join(resolveSharedDataDir(), 'backend-port.txt')
}
