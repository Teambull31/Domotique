export function formatTimeAgo(isoString) {
  if (!isoString) return 'jamais'
  const seconds = Math.floor((Date.now() - new Date(isoString).getTime()) / 1000)
  if (seconds < 5) return 'à l\'instant'
  if (seconds < 60) return `il y a ${seconds}s`
  if (seconds < 3600) return `il y a ${Math.floor(seconds / 60)}min`
  if (seconds < 86400) return `il y a ${Math.floor(seconds / 3600)}h`
  return `il y a ${Math.floor(seconds / 86400)}j`
}

export function formatTimestamp(isoString) {
  if (!isoString) return '--'
  return new Date(isoString).toLocaleTimeString('fr-FR', { hour: '2-digit', minute: '2-digit', second: '2-digit' })
}
