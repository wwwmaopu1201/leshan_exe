// CSV records can span lines. Pasted spreadsheet cells normally use tabs.
export function parseEmployeeRows(text) {
  const input = String(text || '').replace(/^\uFEFF/, '')
  const firstLine = input.split(/\r?\n/, 1)[0]
  const delimiter = firstLine.includes('\t') ? '\t' : (firstLine.includes(',') ? ',' : ';')
  const rows = []
  let row = [], field = '', quoted = false
  for (let i = 0; i < input.length; i++) {
    const char = input[i]
    if (char === '"') {
      if (quoted && input[i + 1] === '"') { field += '"'; i++ } else { quoted = !quoted }
    } else if (!quoted && char === delimiter) { row.push(field.trim()); field = '' }
    else if (!quoted && (char === '\n' || char === '\r')) {
      if (char === '\r' && input[i + 1] === '\n') i++
      row.push(field.trim()); field = ''
      if (row.some(Boolean)) rows.push(row)
      row = []
    } else { field += char }
  }
  if (quoted) throw new Error('CSV 引号未闭合，请检查文件格式')
  row.push(field.trim())
  if (row.some(Boolean)) rows.push(row)
  return rows
}

export function decodeEmployeeFile(buffer) {
  const bytes = new Uint8Array(buffer)
  if (bytes[0] === 0xff && bytes[1] === 0xfe) return new TextDecoder('utf-16le').decode(bytes)
  if (bytes[0] === 0xfe && bytes[1] === 0xff) return new TextDecoder('utf-16be').decode(bytes)
  try { return new TextDecoder('utf-8', { fatal: true }).decode(bytes) }
  catch (_) { return new TextDecoder('gb18030', { fatal: true }).decode(bytes) }
}
