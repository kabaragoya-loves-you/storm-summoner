/* Shared pedal manifest catalog (Pedals tab + scene editor picker) */

window.PedalCatalog = (function () {
  function formatVendorName (vendor) {
    return String(vendor || 'Unknown').split('_')
      .map(w => w.charAt(0).toUpperCase() + w.slice(1))
      .join(' ')
  }

  function getDeviceDisplayName (device) {
    if (device.product) return device.product
    if (device.name) return device.name
    return device.slug || 'Unknown'
  }

  function isUserBucketVendor (vendor) {
    return vendor && (vendor.toLowerCase() === 'user')
  }

  function formatTrsLabel (trsType) {
    if (typeof trsType === 'number') {
      switch (trsType) {
        case 1: return 'Type A'
        case 2: return 'Type B'
        case 3: return 'TS'
        case 4: return 'Both'
        default: return 'Global'
      }
    }
    switch (String(trsType || '').toUpperCase()) {
      case 'TYPE_A': return 'Type A'
      case 'TYPE_B': return 'Type B'
      case 'TYPE_TS': return 'TS'
      case 'BOTH': return 'Both'
      default: return 'Unknown'
    }
  }

  function formatPedalMenuLabel (slug, displayName, isUser) {
    const name = displayName || 'Unknown'
    if (isUser) return `User: ${name}`
    return name
  }

  function formatPedalDisplayName (deviceId, catalog, globalPedal) {
    if (!deviceId) {
      const name = globalPedal?.name || 'Default'
      return `${name} (Inherited)`
    }
    const info = catalog?.deviceBySlug?.get(deviceId)
    if (info) {
      return formatPedalMenuLabel(deviceId, getDeviceDisplayName(info.entry), info.isUser)
    }
    return deviceId
  }

  function buildCatalog (sharedManifest, userManifest) {
    const deviceBySlug = new Map()
    const userSlugs = new Set()
    const userDevices = (userManifest?.devices || []).slice()
      .sort((a, b) => getDeviceDisplayName(a).toLowerCase()
        .localeCompare(getDeviceDisplayName(b).toLowerCase()))
    for (const d of userDevices) {
      userSlugs.add(d.slug)
      deviceBySlug.set(d.slug, { entry: d, isUser: true })
    }

    const vendors = {}
    for (const device of (sharedManifest?.devices || [])) {
      if (userSlugs.has(device.slug)) continue
      const vendor = device.vendor || 'Unknown'
      if (isUserBucketVendor(vendor)) continue
      if (!vendors[vendor]) vendors[vendor] = []
      vendors[vendor].push(device)
      deviceBySlug.set(device.slug, { entry: device, isUser: false })
    }

    const sortedVendors = Object.keys(vendors).sort((a, b) =>
      a.toLowerCase().localeCompare(b.toLowerCase()))
    const vendorTree = sortedVendors.map(vendor => ({
      name: vendor,
      displayName: formatVendorName(vendor),
      devices: vendors[vendor].sort((a, b) =>
        getDeviceDisplayName(a).toLowerCase()
          .localeCompare(getDeviceDisplayName(b).toLowerCase()))
    }))

    return { deviceBySlug, userDevices, vendorTree, sharedManifest, userManifest }
  }

  async function ensureAssetsReady (connection) {
    connection.clearPendingRx?.()
    if (typeof connection._ensureAssetsReadyDedicated === 'function') {
      return connection._ensureAssetsReadyDedicated()
    }
    await connection.sendRaw('ASSETS\n')
    const response = await connection._readLineBody(3000)
    if (!response?.includes('ASSETS_STARTED')) {
      throw new Error(`Assets not ready (got: ${response || 'nothing'})`)
    }
    // Device ignores commands while ASSETS_SENDING; allow prior transfer to finish.
    await new Promise(r => setTimeout(r, 80))
  }

  async function fetchManifestByCommand (connection, type) {
    const maxAttempts = 2
    for (let attempt = 1; attempt <= maxAttempts; attempt++) {
      try {
        await ensureAssetsReady(connection)
        const { data } = await connection._fetchSizedTransferImpl(`MANIFEST ${type}`)
        const manifest = JSON.parse(new TextDecoder().decode(data))
        return Array.isArray(manifest?.devices) ? manifest : { devices: [] }
      } catch (err) {
        const msg = err?.message || String(err)
        const retryable = /Incomplete download|No response|Unexpected response|Unknown assets command|Assets not ready/i.test(msg)
        const missing = /Manifest not found/i.test(msg)
        if (missing) return { devices: [] }
        if (retryable && attempt < maxAttempts) {
          await new Promise(r => setTimeout(r, 300))
          continue
        }
        console.warn(`Manifest fetch failed (${type}):`, err)
        return { devices: [] }
      }
    }
    return { devices: [] }
  }

  async function ensureAssetsModeBody (connection) {
    const modeGranted = await connection._requestModeImpl('ASSETS')
    if (!modeGranted) throw new Error('Could not enter ASSETS mode')
    await ensureAssetsReady(connection)
  }

  async function fetchManifestsInAssets (connection) {
    const shared = await fetchManifestByCommand(connection, 'shared_devices')
    const user = await fetchManifestByCommand(connection, 'user_devices')
    return buildCatalog(shared, user)
  }

  async function fetchCatalog (connection) {
    return connection.runSerialTask(async () => {
      if (connection.currentMode) {
        await connection._exitModeImpl()
        await new Promise(r => setTimeout(r, 200))
      }
      await ensureAssetsModeBody(connection)
      const catalog = await fetchManifestsInAssets(connection)
      await connection.sendRaw('EXIT\n')
      await new Promise(r => setTimeout(r, 200))
      await connection.drainInput?.()
      return catalog
    })
  }

  const PEDALS_USER_DIR = '/userdata/devices/user'

  function deviceJsonPaths (entry, isUser) {
    const rel = entry.path || entry.file
    if (!rel) return []
    const root = isUser ? '/userdata' : '/assets'
    const fname = rel.split('/').pop()
    const paths = []
    if (isUser) {
      paths.push(`${PEDALS_USER_DIR}/${fname}`)
      paths.push(`${root}/${rel}`)
      if (!rel.startsWith('devices/user/')) {
        paths.push(`${root}/devices/user/${fname}`)
      }
    } else {
      paths.push(`${root}/devices/${rel}`)
    }
    return [...new Set(paths)]
  }

  async function fetchDeviceJson (connection, catalog, slug) {
    const info = catalog?.deviceBySlug?.get(slug)
    if (!info) return null
    const paths = deviceJsonPaths(info.entry, info.isUser)
    let lastErr = null
    for (const path of paths) {
      try {
        await ensureAssetsReady(connection)
        const { data } = await connection._fetchSizedTransferImpl(`GET ${path}`)
        return JSON.parse(new TextDecoder().decode(data))
      } catch (err) {
        lastErr = err
        const retryable = /Incomplete download|No response|Unexpected response|Unknown assets command|Assets not ready/i.test(err.message)
        if (retryable) {
          await new Promise(r => setTimeout(r, 300))
          continue
        }
      }
    }
    if (lastErr) throw lastErr
    return null
  }

  function findVendorForSlug (catalog, slug) {
    if (!slug || !catalog) return null
    const info = catalog.deviceBySlug.get(slug)
    if (!info) return null
    if (info.isUser) return '__user__'
    return info.entry.vendor || null
  }

  return {
    formatVendorName,
    getDeviceDisplayName,
    formatTrsLabel,
    formatPedalMenuLabel,
    formatPedalDisplayName,
    buildCatalog,
    ensureAssetsReady,
    fetchManifestByCommand,
    ensureAssetsModeBody,
    fetchManifestsInAssets,
    fetchCatalog,
    fetchDeviceJson,
    deviceJsonPaths,
    findVendorForSlug,
    isUserBucketVendor
  }
})()
