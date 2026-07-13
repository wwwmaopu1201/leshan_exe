import serverPackage from '../../package.json'

export async function getVersion() {
  return serverPackage.version || ''
}
