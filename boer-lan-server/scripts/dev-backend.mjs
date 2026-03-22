import fs from 'node:fs'
import path from 'node:path'
import { spawn } from 'node:child_process'

const rootDir = process.cwd()
const backendDir = path.join(rootDir, 'backend')
const dataDir = path.join(rootDir, '.dev-data')
const portFile = path.join(dataDir, 'backend-port.txt')

fs.mkdirSync(dataDir, { recursive: true })

const child = spawn('go', ['run', 'cmd/server/main.go'], {
  cwd: backendDir,
  stdio: 'inherit',
  env: {
    ...process.env,
    DATA_DIR: dataDir,
    PORT_FILE: portFile,
    LOG_TO_STDOUT: 'true',
    QUIET_MODE: 'false',
    GORM_LOG_LEVEL: process.env.GORM_LOG_LEVEL || 'error'
  }
})

child.on('exit', (code, signal) => {
  if (signal) {
    process.kill(process.pid, signal)
    return
  }
  process.exit(code ?? 0)
})

child.on('error', (error) => {
  console.error('Failed to start Go backend:', error)
  process.exit(1)
})
