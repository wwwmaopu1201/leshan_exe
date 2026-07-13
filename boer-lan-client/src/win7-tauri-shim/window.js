export function getCurrentWindow() {
  return {
    async close() {
      window.close()
    }
  }
}
