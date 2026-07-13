function trialStatus() {
  return {
    valid: true,
    message: 'Win7 shell trial check bypassed',
    expires_at: null,
    remaining_seconds: Number.MAX_SAFE_INTEGER
  }
}

export function isTauri() {
  return false
}

export async function invoke(command) {
  switch (command) {
    case 'get_trial_status':
      return trialStatus()
    case 'save_export_file':
      return null
    default:
      throw new Error(`Unsupported Win7 shell command: ${command}`)
  }
}
