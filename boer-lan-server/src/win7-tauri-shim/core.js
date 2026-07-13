function trialStatus() {
  return {
    valid: true,
    message: 'Win7 shell trial check bypassed',
    expires_at: null,
    remaining_seconds: Number.MAX_SAFE_INTEGER
  }
}

async function getBackendPort() {
  const response = await fetch('/__boerlan_win7/backend-port', {
    cache: 'no-store'
  })
  if (!response.ok) {
    throw new Error(`failed to read backend port: ${response.status}`)
  }
  const payload = await response.json()
  return payload.port
}

export function isTauri() {
  return false
}

export async function invoke(command) {
  switch (command) {
    case 'get_trial_status':
      return trialStatus()
    case 'get_backend_port':
      return getBackendPort()
    default:
      throw new Error(`Unsupported Win7 shell command: ${command}`)
  }
}
