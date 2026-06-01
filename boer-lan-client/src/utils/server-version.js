export function normalizeServerVersion(value) {
  return String(value || '').trim()
}

export function extractServerVersion(payload) {
  const candidates = [
    payload?.serverVersion,
    payload?.server_version,
    payload?.version,
    payload?.server?.version,
    payload?.user?.serverVersion,
    payload?.user?.server_version,
    payload?.user?.version
  ]

  for (const candidate of candidates) {
    const version = normalizeServerVersion(candidate)
    if (version) {
      return version
    }
  }

  return ''
}
