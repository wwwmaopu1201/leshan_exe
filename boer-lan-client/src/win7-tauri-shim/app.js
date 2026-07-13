import clientPackage from '../../package.json'

export async function getVersion() {
  return clientPackage.version || ''
}
