/* Shared release-list ordering for Updater / Info / System Update.

   releases.json is rebuilt by promote_release.cmake; historically that used
   a lexicographic sort on unpadded version keys, so 0.10 landed between 0.1
   and 0.2. Controllers sort at load time so the UI always shows newest first
   regardless of manifest order. */

window.Releases = (function () {
  function parseVersion (str) {
    const parts = String(str || '').replace(/^v/i, '').split('.')
      .map(p => Number(p))
    return {
      major: Number.isFinite(parts[0]) ? parts[0] : 0,
      minor: Number.isFinite(parts[1]) ? parts[1] : 0,
      patch: Number.isFinite(parts[2]) ? parts[2] : 0
    }
  }

  function compareVersionDesc (a, b) {
    const va = parseVersion(a)
    const vb = parseVersion(b)
    if (va.major !== vb.major) return vb.major - va.major
    if (va.minor !== vb.minor) return vb.minor - va.minor
    return vb.patch - va.patch
  }

  // Newest date first; equal dates break ties by semver (also newest first).
  function compareChronologicalDesc (aDate, aVersion, bDate, bVersion) {
    const da = String(aDate || '')
    const db = String(bDate || '')
    if (da !== db) return db.localeCompare(da)
    return compareVersionDesc(aVersion, bVersion)
  }

  function sortFirmware (list) {
    if (!Array.isArray(list)) return []
    return list.slice().sort((a, b) =>
      compareChronologicalDesc(a.date, a.version, b.date, b.version))
  }

  function sortAssets (list) {
    if (!Array.isArray(list)) return []
    return list.slice().sort((a, b) =>
      compareChronologicalDesc(a.date, a.checksum, b.date, b.checksum))
  }

  function sortSystemUpdate (list) {
    if (!Array.isArray(list)) return []
    return list.slice().sort((a, b) =>
      compareChronologicalDesc(a.date, a.firmware_version,
        b.date, b.firmware_version))
  }

  // Normalize a parsed releases.json so every consumer sees the same order.
  function normalize (manifest) {
    if (!manifest || typeof manifest !== 'object') return manifest
    if (manifest.firmware) manifest.firmware = sortFirmware(manifest.firmware)
    if (manifest.assets) manifest.assets = sortAssets(manifest.assets)
    if (manifest.system_update) {
      manifest.system_update = sortSystemUpdate(manifest.system_update)
    }
    return manifest
  }

  return {
    parseVersion,
    compareVersionDesc,
    sortFirmware,
    sortAssets,
    sortSystemUpdate,
    normalize
  }
})()
