export function isServerHost(value) {
  if (!value || /\s|[/:?#@\\]/.test(value)) return false
  if (/^[\d.]+$/.test(value)) {
    const parts = value.split('.')
    return parts.length === 4 && parts.every(part => /^\d{1,3}$/.test(part) && Number(part) <= 255)
  }
  return value.length <= 253 && value.split('.').every(part => /^[a-z\d](?:[a-z\d-]{0,61}[a-z\d])?$/i.test(part))
}

export function readLoginHistory() {
  try {
    const history = JSON.parse(localStorage.getItem('loginHistory') || '[]')
    return Array.isArray(history) ? history.filter(item => item && typeof item.serverIp === 'string' && typeof item.username === 'string') : []
  } catch (_) {
    return []
  }
}

export function saveLoginHistory(form) {
  const history = readLoginHistory().filter(item => item.serverIp !== form.serverIp || item.username !== form.username)
  // Preserve the existing opt-in remember-password behavior, scoped by server and account.
  history.unshift({ serverIp: form.serverIp, username: form.username, password: form.remember ? form.password : '' })
  const saved = history.slice(0, 30)
  localStorage.setItem('loginHistory', JSON.stringify(saved))
  return saved
}
