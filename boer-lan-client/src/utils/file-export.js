import { invoke } from '@tauri-apps/api/core'

function isTauriRuntime() {
  return typeof window !== 'undefined' && Boolean(window.__TAURI_INTERNALS__)
}

function buildPickerType(description, mimeType, extensions = []) {
  if (!mimeType || !extensions.length) {
    return undefined
  }
  return [{
    description,
    accept: {
      [mimeType]: extensions.map(ext => ext.startsWith('.') ? ext : `.${ext}`)
    }
  }]
}

function fallbackDownloadBlob(blob, filename) {
  const url = window.URL.createObjectURL(blob)
  const link = document.createElement('a')
  link.href = url
  link.download = filename
  document.body.appendChild(link)
  link.click()
  document.body.removeChild(link)
  window.URL.revokeObjectURL(url)
}

async function saveWithBrowserPicker(blob, filename, options = {}) {
  if (typeof window === 'undefined' || typeof window.showSaveFilePicker !== 'function') {
    return false
  }

  try {
    const handle = await window.showSaveFilePicker({
      suggestedName: filename,
      types: buildPickerType(options.description, options.mimeType, options.extensions)
    })
    const writable = await handle.createWritable()
    await writable.write(blob)
    await writable.close()
    return true
  } catch (error) {
    if (error?.name !== 'AbortError') {
      console.error('Browser save picker failed:', error)
    }
    return false
  }
}

async function saveWithTauri(blob, filename) {
  if (!isTauriRuntime()) {
    return false
  }

  try {
    const bytes = Array.from(new Uint8Array(await blob.arrayBuffer()))
    const savedPath = await invoke('save_export_file', {
      suggestedName: filename,
      bytes
    })
    return Boolean(savedPath)
  } catch (error) {
    console.error('Tauri save dialog failed:', error)
    return false
  }
}

export function parseContentDispositionFilename(contentDisposition, fallbackName) {
  if (!contentDisposition) return fallbackName
  const utf8Match = contentDisposition.match(/filename\*=UTF-8''([^;]+)/i)
  if (utf8Match && utf8Match[1]) {
    return decodeURIComponent(utf8Match[1])
  }
  const normalMatch = contentDisposition.match(/filename="?([^";]+)"?/i)
  return normalMatch?.[1] || fallbackName
}

export async function saveBlobWithDialog(blob, filename, options = {}) {
  const saved = await saveWithTauri(blob, filename) || await saveWithBrowserPicker(blob, filename, options)
  if (!saved) {
    fallbackDownloadBlob(blob, filename)
  }
  return saved
}

export async function saveTextWithDialog(content, filename, options = {}) {
  const blob = new Blob([content], {
    type: options.mimeType || 'text/plain;charset=utf-8;'
  })
  return saveBlobWithDialog(blob, filename, options)
}

export async function saveResponseWithDialog(response, fallbackName, options = {}) {
  const blob = response?.data instanceof Blob
    ? response.data
    : new Blob([response?.data], { type: options.mimeType || 'application/octet-stream' })
  const filename = parseContentDispositionFilename(response?.headers?.['content-disposition'], fallbackName)
  const saved = await saveBlobWithDialog(blob, filename, options)
  return {
    filename,
    saved
  }
}
