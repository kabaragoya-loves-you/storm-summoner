/* Storm Summoner - Pedals Controller (user pedal authoring) */

const PEDALS_USER_DIR = '/userdata/devices/user'
const DEVICE_AUTHORING_URL = '/DEVICE_AUTHORING.md'
// Tabs that keep the device in CDC ASSETS mode (do not EXIT when switching between them).
const ASSETS_MODE_TABS = new Set(['pedals', 'assets'])
const PEDALS_MIDI_TOKENS = [
  'NOTE_NUMBER', 'PROGRAM_CHANGE', 'VELOCITY_NOTE_ON', 'VELOCITY_NOTE_OFF',
  'CHANNEL_PRESSURE', 'POLY_PRESSURE', 'PITCH_BEND', 'CLOCK',
  'TRANSPORT_START', 'TRANSPORT_STOP', 'TRANSPORT_CONTINUE'
]
const PEDALS_TRS_TYPES = ['BOTH', 'TYPE_A', 'TYPE_B', 'TYPE_TS']
const PEDALS_BANK_MODES = ['none', 'CC0', 'CC0_CC32']

const INDEX_BASE_HELP_TOOLTIP =
  'The MIDI program number for the first preset. Some devices use PC 0 as bypass (first preset is PC 1); ' +
  'others use PC 0 as the first preset. Index Base matches your device.'

function renderIndexBaseLabelHtml () {
  return `<span class="field-label-with-hint">Index Base
    <wa-icon id="pedal-index-base-help" name="circle-info" variant="regular"
      class="field-hint-icon" tabindex="0" aria-label="About Index Base"></wa-icon>
    <wa-tooltip for="pedal-index-base-help" placement="top">${INDEX_BASE_HELP_TOOLTIP}</wa-tooltip>
  </span>`
}

application.register(
  'pedals',
  class extends BaseController {
    static targets = [
      'searchInput', 'refreshBtn', 'newPedalBtn', 'llmPromptBtn', 'treeContainer', 'detailContainer',
      'newPedalDialog', 'newPedalInput', 'deleteDialog', 'deletePedalName',
      'messageDialog', 'messageDialogBody', 'jsonTextarea',
    ]

    connect () {
      this.sharedManifest = null
      this.userManifest = null
      this.userDevices = []
      this.vendorTree = null
      this.deviceBySlug = new Map()
      this.currentSlug = null
      this.selectedSlug = null
      this.selectedIsUser = false
      this.selectedEntry = null
      this.currentDevice = null
      this.editModel = null
      this.viewJson = false
      this.validationErrors = []
      this.validationSuccess = null
      this.inAssetsMode = false
      this.isActivating = false
      this._activateQueue = Promise.resolve()
      this._schema = null
      this._schemaLoad = null
      this._authoringText = null
      this._authoringLoad = null
      this._deleteSlug = null
      this._deletePath = null
      this._pendingPedalsActivate = false

      this.connection.on('connection:changed', this.onConnectionChanged.bind(this))
      this.connection.on('mode:changed', ({ mode }) => {
        if (mode !== 'ASSETS') this.inAssetsMode = false
      })

      document.addEventListener('app:tab-activated', (e) => {
        if (e.detail.tab === 'pedals') {
          if (this.connection.isConnected) {
            this._pendingPedalsActivate = false
            this.activate()
          } else {
            this._pendingPedalsActivate = true
          }
        } else if (!ASSETS_MODE_TABS.has(e.detail.tab) &&
            (this.inAssetsMode || this.connection.currentMode === 'ASSETS')) {
          this.leaveAssetsMode()
        }
      })

      document.addEventListener('app:tab-params', (e) => {
        if (e.detail.tab === 'pedals' && e.detail.params?.slug) {
          this.currentSlug = e.detail.params.slug
          if (this.inAssetsMode && this.deviceBySlug.size > 0) {
            this.selectedSlug = this.currentSlug
            const info = this.deviceBySlug.get(this.currentSlug)
            this.selectedIsUser = info?.isUser ?? false
            this.renderTree()
            this.loadPedalDetails(this.currentSlug)
          }
        }
      })
    }

    onConnectionChanged ({ connected }) {
      this.refreshBtnTarget.disabled = !connected
      if (this.hasNewPedalBtnTarget) {
        this.newPedalBtnTarget.disabled = !connected
      }
      if (!connected) {
        this._pendingPedalsActivate = false
        this.inAssetsMode = false
        this.isActivating = false
        this.resetCatalog()
        this.renderEmptyTree()
        this.renderEmptyDetail()
      } else if (this._pendingPedalsActivate) {
        this._pendingPedalsActivate = false
        this.activate()
      }
    }

    resetCatalog () {
      this.sharedManifest = null
      this.userManifest = null
      this.userDevices = []
      this.vendorTree = null
      this.deviceBySlug = new Map()
      this.currentDevice = null
      this.editModel = null
      this.viewJson = false
      this.clearValidationFeedback()
    }

    escapeHtml (str) {
      return String(str ?? '')
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;')
    }

    async activate () {
      if (!this.connection.isConnected) return
      this._activateQueue = this._activateQueue.then(() => this._runActivate())
      return this._activateQueue
    }

    async _runActivate () {
      if (this.isActivating) return
      this.isActivating = true
      this.renderCatalogLoading()
      try {
        await this.connection.runSerialTask(async () => {
          this.connection.beginExclusiveSession()
          try {
            if (!this.currentSlug) await this.fetchCurrentDeviceFirstBody()
            await this.ensureAssetsModeBody()
            await this.fetchManifestsBody()
            await this.loadCurrentPedalIfKnownBody()
          } finally {
            this.connection.endExclusiveSession()
          }
        })
      } catch (err) {
        console.error('Pedals activation error:', err)
      } finally {
        this.isActivating = false
      }
    }

    async loadCurrentPedalIfKnownBody () {
      if (!this.currentSlug || !this.deviceBySlug.has(this.currentSlug)) return
      this.selectedSlug = this.currentSlug
      const info = this.deviceBySlug.get(this.currentSlug)
      this.selectedIsUser = info?.isUser ?? false
      this.renderTree()
      await this.loadPedalDetailsBody(this.currentSlug)
    }

    async fetchCurrentDeviceFirstBody () {
      try {
        if (this.connection.currentMode) {
          await this.connection._exitModeImpl()
          await this.sleep(200)
        }
        const response = await this.connection._sendCommandImpl('INFO', 5000, (data) =>
          typeof data.version === 'string' && typeof data.build === 'number')
        if (response && !response.startsWith('ERROR:')) {
          const info = JSON.parse(response)
          if (info.pedal?.slug) this.currentSlug = info.pedal.slug
        }
      } catch (err) {
        console.warn('Failed to fetch current device:', err)
      }
    }

    async leaveAssetsMode () {
      if (!this.inAssetsMode && this.connection.currentMode !== 'ASSETS') return
      try {
        if (this.connection.currentMode) {
          await this.connection.exitMode()
          await this.sleep(200)
        }
      } catch (err) {
        console.warn('Pedals leave ASSETS mode:', err)
      }
      this.inAssetsMode = false
    }

    async ensureAssetsModeBody () {
      const modeGranted = await this.connection._requestModeImpl('ASSETS')
      if (!modeGranted) throw new Error('Could not enter ASSETS mode')
      if (this.inAssetsMode) return

      await this.sleep(50)
      await this.connection.sendRaw('ASSETS\n')
      const response = await this.readLine(5000)
      if (response?.includes('ASSETS_STARTED')) {
        this.inAssetsMode = true
        return
      }
      throw new Error(`Timeout waiting for ASSETS_STARTED (got: ${response || 'nothing'})`)
    }

    async ensureAssetsMode () {
      return this.connection.runSerialTask(() => this.ensureAssetsModeBody())
    }

    async assetsCommandBody (cmd, timeout = 30000) {
      if (!this.inAssetsMode) await this.ensureAssetsModeBody()
      return this.connection._sendCommandImpl(cmd, timeout)
    }

    async assetsCommand (cmd, timeout = 30000) {
      return this.connection.runSerialTask(() => this.assetsCommandBody(cmd, timeout))
    }

    async fetchManifestsBody () {
      const shared = await this.fetchManifestByCommand('shared_devices')
      const user = await this.fetchManifestByCommand('user_devices')
      this.sharedManifest = shared
      this.userManifest = user
      this.buildCatalog()
      this.renderTree()
    }

    async fetchManifests () {
      return this.connection.runSerialTask(() => this.fetchManifestsBody())
    }

    async fetchManifestByCommand (type) {
      const maxAttempts = 2
      for (let attempt = 1; attempt <= maxAttempts; attempt++) {
        try {
          const { data } = await this.connection._fetchSizedTransferImpl(`MANIFEST ${type}`)
          const manifest = JSON.parse(new TextDecoder().decode(data))
          return Array.isArray(manifest?.devices) ? manifest : { devices: [] }
        } catch (err) {
          const retryable = /Incomplete download|No response|Unexpected response/i.test(err.message)
          if (retryable && attempt < maxAttempts) {
            console.warn(`Manifest fetch retry (${type}):`, err)
            await this.sleep(300)
            continue
          }
          console.warn(`Manifest fetch failed (${type}):`, err)
          return { devices: [] }
        }
      }
      return { devices: [] }
    }

    buildCatalog () {
      this.deviceBySlug = new Map()
      const userSlugs = new Set()
      this.userDevices = (this.userManifest?.devices || []).slice()
        .sort((a, b) => (a.product || a.name || '').toLowerCase()
          .localeCompare((b.product || b.name || '').toLowerCase()))
      for (const d of this.userDevices) {
        userSlugs.add(d.slug)
        this.deviceBySlug.set(d.slug, { entry: d, isUser: true })
      }

      const vendors = {}
      for (const device of (this.sharedManifest?.devices || [])) {
        if (userSlugs.has(device.slug)) continue
        const vendor = device.vendor || 'Unknown'
        if (!vendors[vendor]) vendors[vendor] = []
        vendors[vendor].push(device)
        this.deviceBySlug.set(device.slug, { entry: device, isUser: false })
      }

      const sortedVendors = Object.keys(vendors).sort((a, b) =>
        a.toLowerCase().localeCompare(b.toLowerCase()))
      this.vendorTree = sortedVendors.map(vendor => ({
        name: vendor,
        displayName: this.formatVendorName(vendor),
        devices: vendors[vendor].sort((a, b) =>
          (a.product || a.name || '').toLowerCase()
            .localeCompare((b.product || b.name || '').toLowerCase()))
      }))
    }

    deviceJsonPaths (entry, isUser) {
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

    deviceFilePath (entry, isUser) {
      const paths = this.deviceJsonPaths(entry, isUser)
      return paths[0] || null
    }

    async fetchDeviceJson (slug) {
      const info = this.deviceBySlug.get(slug)
      if (!info) throw new Error('Device not in catalog')
      const paths = this.deviceJsonPaths(info.entry, info.isUser)
      let lastErr = null
      for (const path of paths) {
        try {
          const device = await this.assetsGetFileBody(path)
          return device
        } catch (err) {
          lastErr = err
        }
      }
      throw lastErr || new Error('Failed to load device JSON')
    }

    async assetsGetFileBody (path) {
      if (!this.inAssetsMode) await this.ensureAssetsModeBody()
      const maxAttempts = 2
      let lastErr = null
      for (let attempt = 1; attempt <= maxAttempts; attempt++) {
        try {
          const { data } = await this.connection._fetchSizedTransferImpl(`GET ${path}`)
          return JSON.parse(new TextDecoder().decode(data))
        } catch (err) {
          lastErr = err
          const retryable = /Incomplete download|No response|Unexpected response/i.test(err.message)
          if (retryable && attempt < maxAttempts) {
            console.warn(`GET retry (${path}):`, err)
            await this.sleep(300)
            continue
          }
          throw err
        }
      }
      throw lastErr
    }

    async assetsGetFile (path) {
      return this.connection.runSerialTask(() => this.assetsGetFileBody(path))
    }

    async assetsPutFile (path, jsonObj) {
      return this.connection.runSerialTask(async () => {
        const text = JSON.stringify(jsonObj, null, 2)
        const data = new TextEncoder().encode(text)
        const response = await this.assetsCommandBody(`PUT ${path} ${data.length}`)
        if (response !== 'READY') throw new Error(response || 'PUT not ready')
        await this.connection.sendBinary(data)
        const result = await this.connection.readLine(30000)
        if (result !== 'OK') throw new Error(result || 'PUT failed')
      })
    }

    async assetsRm (path) {
      return this.connection.runSerialTask(async () => {
        const response = await this.assetsCommandBody(`RM ${path}`)
        if (response !== 'OK') throw new Error(response || 'RM failed')
      })
    }

    async assetsMkdir (path) {
      return this.connection.runSerialTask(async () => {
        const response = await this.assetsCommandBody(`MKDIR ${path}`)
        if (response !== 'OK' && !response?.includes('exists')) {
          throw new Error(response || 'MKDIR failed')
        }
      })
    }

    async regenerateUserManifest () {
      // Manifest is regenerated on-device when userdata device files change (PUT/RM).
      await this.sleep(50)
    }

    async deleteDeviceCache (slug) {
      try {
        await this.assetsRm(`/userdata/cache/${slug}.bin`)
      } catch (err) {
        // Cache may not exist
      }
    }

    formatVendorName (vendor) {
      return vendor.split('_')
        .map(w => w.charAt(0).toUpperCase() + w.slice(1))
        .join(' ')
    }

    getDeviceDisplayName (device) {
      if (device.product) return device.product
      if (device.name) return device.name
      return device.slug || 'Unknown'
    }

    pedalHeaderName (device) {
      const model = (device.device?.model || '').trim()
      if (model) return model
      if (device.product) return device.product
      return device.title || device.displayName || 'Unknown Device'
    }

    pedalHeaderManufacturer (device) {
      return (device.device?.manufacturer || '').trim()
    }

    renderEmptyTree () {
      this.treeContainerTarget.innerHTML = `
        <div class="empty-state">
          <wa-icon name="guitar"></wa-icon>
          <p>Connect to browse pedals</p>
        </div>
      `
    }

    renderCatalogLoading () {
      if (!this.hasTreeContainerTarget) return
      this.treeContainerTarget.innerHTML = `
        <div class="loading-state">
          <wa-spinner></wa-spinner>
          <p>Loading pedals…</p>
        </div>
      `
    }

    renderEmptyDetail () {
      this.detailContainerTarget.innerHTML = `
        <div class="empty-state">
          <wa-icon name="circle-info"></wa-icon>
          <p>Select a pedal to view details</p>
        </div>
      `
    }

    renderTree () {
      if (!this.hasTreeContainerTarget) {
        console.error('Pedals: treeContainer target missing')
        return
      }
      const hasUser = this.userDevices.length > 0
      const hasShared = this.vendorTree?.length > 0
      if (!hasUser && !hasShared) {
        this.renderEmptyTree()
        return
      }

      let html = '<div class="pedal-tree">'

      const userOpen = this.userDevices.some(d => d.slug === this.currentSlug)
        || this.userDevices.some(d => d.slug === this.selectedSlug)
      html += `<wa-details summary="User Pedals" class="pedal-tree-user" ${userOpen ? 'open' : ''}>
        <div class="pedal-list">`
      if (hasUser) {
        for (const device of this.userDevices) {
          html += this.renderTreeItem(device, true)
        }
      } else {
        html += `<p class="pedal-tree-empty-hint">No user pedals yet</p>`
      }
      html += `</div></wa-details><wa-divider></wa-divider>`

      for (const vendor of (this.vendorTree || [])) {
        const hasCurrent = vendor.devices.some(d => d.slug === this.currentSlug)
        html += `<wa-details summary="${this.escapeHtml(vendor.displayName)}" ${hasCurrent ? 'open' : ''}>
          <div class="pedal-list">`
        for (const device of vendor.devices) {
          html += this.renderTreeItem(device, false)
        }
        html += `</div></wa-details>`
      }

      html += '</div>'
      this.treeContainerTarget.innerHTML = html
    }

    renderTreeItem (device, isUser) {
      const slug = device.slug
      const isCurrent = slug === this.currentSlug
      const isSelected = slug === this.selectedSlug
      return `
        <div class="pedal-item ${isCurrent ? 'current' : ''} ${isSelected ? 'selected' : ''}"
             data-action="click->pedals#selectPedal"
             data-slug="${this.escapeHtml(slug)}"
             data-user="${isUser ? '1' : '0'}">
          <span class="pedal-name">${this.escapeHtml(this.getDeviceDisplayName(device))}</span>
          ${isCurrent ? '<wa-icon name="check" class="current-icon"></wa-icon>' : ''}
        </div>
      `
    }

    filterTree () {
      const query = this.searchInputTarget.value.toLowerCase().trim()
      if (!query) {
        this.treeContainerTarget.querySelectorAll('wa-details').forEach(el => {
          el.style.display = ''
          el.querySelectorAll('.pedal-item').forEach(item => { item.style.display = '' })
        })
        return
      }

      this.treeContainerTarget.querySelectorAll('wa-details').forEach(vendorEl => {
        const vendorName = (vendorEl.getAttribute('summary') || '').toLowerCase()
        const vendorMatches = vendorName.includes(query)
        let anyVisible = false
        vendorEl.querySelectorAll('.pedal-item').forEach(item => {
          const name = item.querySelector('.pedal-name')?.textContent.toLowerCase() || ''
          if (vendorMatches || name.includes(query)) {
            item.style.display = ''
            anyVisible = true
          } else {
            item.style.display = 'none'
          }
        })
        vendorEl.style.display = anyVisible ? '' : 'none'
        if (anyVisible) vendorEl.open = true
      })
    }

    async selectPedal (event) {
      const slug = event.currentTarget.dataset.slug
      const isUser = event.currentTarget.dataset.user === '1'
      if (!slug || slug === this.selectedSlug) return
      this.selectedSlug = slug
      this.selectedIsUser = isUser
      this.viewJson = false
      this.clearValidationFeedback()
      this.renderTree()
      await this.loadPedalDetails(slug)
    }

    async loadPedalDetailsBody (slug) {
      this.detailContainerTarget.innerHTML = `
        <div class="loading-state">
          <wa-spinner></wa-spinner>
          <p>Loading pedal details...</p>
        </div>
      `
      try {
        const device = await this.fetchDeviceJson(slug)
        this.currentDevice = device
        const info = this.deviceBySlug.get(slug)
        this.selectedEntry = info?.entry
        if (info?.isUser) {
          this.editModel = this.cloneDevice(device)
        } else {
          this.editModel = null
        }
        this.renderPedalDetails(slug, info?.isUser ?? false)
      } catch (err) {
        console.error('Error loading pedal:', err)
        this.detailContainerTarget.innerHTML = `
          <div class="empty-state">
            <wa-icon name="triangle-exclamation"></wa-icon>
            <p>${this.escapeHtml(err.message || 'Error loading pedal')}</p>
          </div>
        `
      }
    }

    async loadPedalDetails (slug) {
      return this.connection.runSerialTask(() => this.loadPedalDetailsBody(slug))
    }

    cloneDevice (device) {
      return JSON.parse(JSON.stringify(device))
    }

    toggleViewJson () {
      this.viewJson = !this.viewJson
      if (this.selectedSlug) {
        this.renderPedalDetails(this.selectedSlug, this.selectedIsUser)
      }
    }

    getJsonForView () {
      const obj = this.selectedIsUser && this.editModel
        ? this.editModel
        : this.currentDevice
      return JSON.stringify(obj, null, 2)
    }

    renderPedalDetails (slug, isUser) {
      const isCurrent = slug === this.currentSlug
      const device = isUser && this.editModel ? this.editModel : this.currentDevice
      if (!device) return

      const pedalName = this.pedalHeaderName(device)
      const manufacturer = this.pedalHeaderManufacturer(device)
      const headerActions = this.renderHeaderActions(slug, isUser, isCurrent)

      let html = `
        <div class="pedal-detail" data-slug="${this.escapeHtml(slug)}">
          <div class="pedal-detail-header">
            <div class="pedal-detail-title">
              <h2>${this.escapeHtml(pedalName)}</h2>
              ${manufacturer ? `<span class="pedal-detail-subtitle">${this.escapeHtml(manufacturer)}</span>` : ''}
            </div>
            <div class="pedal-detail-header-actions">
              ${headerActions}
              ${isCurrent ? '<span class="current-badge">Current</span>' : ''}
            </div>
          </div>
      `

      if (this.viewJson) {
        html += this.renderJsonView(isUser)
      } else if (isUser) {
        html += this.renderUserEditor(slug)
      } else {
        html += this.renderReadonlySections(device)
      }

      html += '</div>'
      this.detailContainerTarget.innerHTML = html
    }

    renderHeaderActions (slug, isUser, isCurrent) {
      const jsonLabel = this.viewJson ? 'Hide JSON' : 'View JSON'
      let html = `
        <wa-button size="small" variant="neutral" appearance="outlined"
                   data-action="click->pedals#toggleViewJson">${jsonLabel}</wa-button>
        <wa-button size="small" variant="brand" appearance="outlined"
                   data-action="click->pedals#duplicatePedal" data-slug="${this.escapeHtml(slug)}">
          Duplicate
        </wa-button>
      `
      if (!isCurrent) {
        html += `
          <wa-button size="small" variant="brand"
                     data-action="click->pedals#usePedal" data-slug="${this.escapeHtml(slug)}">
            Activate
          </wa-button>
        `
      }
      if (isUser) {
        html += `
          <wa-button size="small" variant="danger" appearance="outlined"
                     data-action="click->pedals#showDeleteDialog" data-slug="${this.escapeHtml(slug)}">
            Delete
          </wa-button>
        `
      }
      return html
    }

    renderJsonView (isUser) {
      const json = this.getJsonForView()
      const readonly = isUser ? '' : 'readonly'
      const copyBlock = `
        <div class="pedal-json-view" data-controller="copy">
          <wa-button class="pedal-json-copy" size="small" variant="neutral"
                     data-action="click->copy#copy">Copy</wa-button>
          <textarea class="pedal-json-textarea" data-copy-target="source" ${readonly}
                    ${isUser ? 'data-action="input->pedals#onJsonEdit"' : ''}
                    data-pedals-target="jsonTextarea">${this.escapeHtml(json)}</textarea>
        </div>
      `
      if (isUser) {
        return copyBlock + `
          ${this.renderValidationCallout()}
          <div class="pedal-json-save-row">
            <wa-button variant="brand" data-action="click->pedals#savePedal">Save</wa-button>
          </div>
        `
      }
      return copyBlock
    }

    onJsonEdit (event) {
      // Parsed on save from textarea
      this._jsonDirty = true
      this.clearValidationFeedback()
    }

    renderReadonlySections (device) {
      const receives = device.receives || []
      const transmits = device.transmits || []
      const pcInfo = device.x_pc || {}
      const ccCommands = device.controlChangeCommands || []
      const nrpnCommands = device.nrpnCommands || []

      let html = `
        <div class="pedal-detail-section">
          <h3>MIDI Configuration</h3>
          <div class="detail-grid">
            <div class="detail-item"><span class="detail-label">TRS Type</span>
              <span class="detail-value">${this.escapeHtml(this.formatTrsType(device.x_midiTrs))}</span></div>
            <div class="detail-item"><span class="detail-label">Default MIDI Channel</span>
              <span class="detail-value">${device.x_midiChannel ?? 'Not specified'}</span></div>
            <div class="detail-item"><span class="detail-label">Receives</span>
              <span class="detail-value">${receives.length ? receives.join(', ') : 'None'}</span></div>
            <div class="detail-item"><span class="detail-label">Transmits</span>
              <span class="detail-value">${transmits.length ? transmits.join(', ') : 'None'}</span></div>
          </div>
        </div>
        <div class="pedal-detail-section">
          <h3>Program Change</h3>
          <div class="detail-grid">
            <div class="detail-item"><span class="detail-label">Presets</span>
              <span class="detail-value">${pcInfo.count ?? 128}</span></div>
            <div class="detail-item"><span class="detail-label">${renderIndexBaseLabelHtml()}</span>
              <span class="detail-value">${pcInfo.indexBase ?? 0}</span></div>
            <div class="detail-item"><span class="detail-label">Bank Select</span>
              <span class="detail-value">${this.escapeHtml(this.formatBankMode(pcInfo.bankSelectMode))}</span></div>
          </div>
        </div>
      `
      html += this.renderCcTable(ccCommands)
      html += this.renderNrpnTable(nrpnCommands)
      return html
    }

    renderUserEditor (slug) {
      const m = this.editModel
      if (!m.device) m.device = { displayName: '', manufacturer: '', model: '', version: '' }
      if (!m.x_pc) m.x_pc = { indexBase: 0, count: 128, bankSelectMode: 'none' }
      if (!m.receives) m.receives = []
      if (!m.transmits) m.transmits = []
      if (!m.controlChangeCommands) m.controlChangeCommands = []

      this.syncIdentityFromDevice(m)
      let html = `
        <div class="pedal-detail-section pedal-editor-section">
          <h3>Identity</h3>
          <div class="pedal-editor-grid pedal-identity-grid">
            <label>Manufacturer<wa-input size="small" value="${this.escapeHtml(m.device.manufacturer || '')}"
              data-action="input->pedals#patchField" data-field="device.manufacturer"></wa-input></label>
            <label>Model<wa-input size="small" value="${this.escapeHtml(m.device.model || '')}"
              data-action="input->pedals#patchField" data-field="device.model"></wa-input></label>
          </div>
        </div>
        <div class="pedal-detail-section pedal-editor-section">
          <h3>MIDI Configuration</h3>
          <div class="pedal-editor-grid">
            <label>TRS Type
              <wa-select size="small" value="${m.x_midiTrs || 'BOTH'}"
                data-action="change->pedals#patchField" data-field="x_midiTrs">
                ${PEDALS_TRS_TYPES.map(t =>
                  `<wa-option value="${t}">${this.escapeHtml(this.formatTrsType(t))}</wa-option>`).join('')}
              </wa-select>
            </label>
            <label>Default MIDI Channel
              <wa-input type="number" size="small" min="1" max="16"
                value="${m.x_midiChannel ?? 1}"
                data-action="input->pedals#patchField" data-field="x_midiChannel"></wa-input>
            </label>
          </div>
          <div class="pedal-token-groups">
            <div class="pedal-token-group">
              <span class="detail-label">Receives</span>
              ${this.renderTokenCheckboxes('receives', m.receives)}
            </div>
            <div class="pedal-token-group">
              <span class="detail-label">Transmits</span>
              ${this.renderTokenCheckboxes('transmits', m.transmits)}
            </div>
          </div>
        </div>
        <div class="pedal-detail-section pedal-editor-section">
          <h3>Program Change</h3>
          <div class="pedal-editor-grid">
            <label>${renderIndexBaseLabelHtml()}
              <wa-select size="small" value="${String(m.x_pc.indexBase ?? 0)}"
                data-action="change->pedals#patchField" data-field="x_pc.indexBase">
                <wa-option value="0">0</wa-option>
                <wa-option value="1">1</wa-option>
              </wa-select>
            </label>
            <label>Preset Count
              <wa-input type="number" size="small" min="0" max="16384"
                value="${m.x_pc.count ?? 128}"
                data-action="input->pedals#patchField" data-field="x_pc.count"></wa-input>
            </label>
            <label>Bank Select
              <wa-select size="small" value="${m.x_pc.bankSelectMode || 'none'}"
                data-action="change->pedals#patchField" data-field="x_pc.bankSelectMode">
                ${PEDALS_BANK_MODES.map(b =>
                  `<wa-option value="${b}">${b}</wa-option>`).join('')}
              </wa-select>
            </label>
          </div>
        </div>
      `
      html += this.renderCcEditor()
      html += this.renderValidationCallout()
      html += `
        <div class="pedal-detail-actions pedal-editor-save-row">
          <wa-button variant="brand" data-action="click->pedals#savePedal">Save</wa-button>
        </div>
      `
      return html
    }

    clearValidationFeedback () {
      this.validationErrors = []
      this.validationSuccess = null
      this.refreshValidationCallout()
    }

    refreshValidationCallout () {
      if (!this.hasDetailContainerTarget) return
      const root = this.detailContainerTarget
      const saveRow = root.querySelector('.pedal-editor-save-row, .pedal-json-save-row')
      if (!saveRow) return
      const html = this.renderValidationCallout()
      const existing = root.querySelector('.pedal-validation-callout')
      if (existing) {
        if (html) existing.outerHTML = html
        else existing.remove()
      } else if (html) {
        saveRow.insertAdjacentHTML('beforebegin', html)
      }
    }

    renderValidationCallout () {
      if (this.validationErrors.length) {
        return `<wa-callout variant="danger" class="pedal-validation-callout">
          <strong>Validation errors</strong>
          <ul class="pedal-validation-list">${this.validationErrors.map(e =>
            `<li><code>${this.escapeHtml(e.path)}</code>: ${this.escapeHtml(e.message)}</li>`).join('')}
          </ul>
        </wa-callout>`
      }
      if (this.validationSuccess) {
        return `<wa-callout variant="success" class="pedal-validation-callout">
          ${this.escapeHtml(this.validationSuccess)}
        </wa-callout>`
      }
      return ''
    }

    renderTokenCheckboxes (field, selected) {
      const set = new Set(selected || [])
      return `<div class="pedal-token-checks">${PEDALS_MIDI_TOKENS.map(token => `
        <label class="pedal-token-check">
          <input type="checkbox" ${set.has(token) ? 'checked' : ''}
            data-action="change->pedals#toggleToken" data-field="${field}" data-token="${token}">
          ${token}
        </label>`).join('')}</div>`
    }

    syncIdentityFromDevice (m) {
      if (!m?.device) return
      const manufacturer = (m.device.manufacturer || '').trim()
      const model = (m.device.model || '').trim()
      m.schemaVersion = '0.1.1'
      m.device.displayName = ''
      m.device.version = ''
      if (!manufacturer && !model) return
      m.title = model ? `${manufacturer} ${model}`.trim() : manufacturer
      const short = model || manufacturer
      m.displayName = short.length > 14 ? short.slice(0, 14) : short
    }

    patchField (event) {
      if (!this.editModel) return
      const field = event.currentTarget.dataset.field
      let value = event.currentTarget.value
      if (field === 'x_pc.indexBase' || field === 'x_pc.count' || field === 'x_midiChannel') {
        value = parseInt(value, 10)
        if (Number.isNaN(value)) return
      }
      this.setNestedField(this.editModel, field, value)
      if (field === 'device.manufacturer' || field === 'device.model') {
        this.syncIdentityFromDevice(this.editModel)
        this.refreshPedalDetailHeader()
      }
      this.clearValidationFeedback()
    }

    refreshPedalDetailHeader () {
      if (!this.editModel || !this.selectedSlug) return
      const titleEl = this.detailContainerTarget.querySelector('.pedal-detail-title')
      if (!titleEl) return
      const pedalName = this.pedalHeaderName(this.editModel)
      const manufacturer = this.pedalHeaderManufacturer(this.editModel)
      titleEl.innerHTML = `
        <h2>${this.escapeHtml(pedalName)}</h2>
        ${manufacturer ? `<span class="pedal-detail-subtitle">${this.escapeHtml(manufacturer)}</span>` : ''}
      `
    }

    toggleToken (event) {
      if (!this.editModel) return
      const field = event.currentTarget.dataset.field
      const token = event.currentTarget.dataset.token
      const arr = this.editModel[field] || (this.editModel[field] = [])
      const idx = arr.indexOf(token)
      if (event.currentTarget.checked) {
        if (idx < 0) arr.push(token)
      } else if (idx >= 0) {
        arr.splice(idx, 1)
      }
      this.clearValidationFeedback()
    }

    setNestedField (obj, path, value) {
      const parts = path.split('.')
      let cur = obj
      for (let i = 0; i < parts.length - 1; i++) {
        if (!cur[parts[i]]) cur[parts[i]] = {}
        cur = cur[parts[i]]
      }
      cur[parts[parts.length - 1]] = value
    }

    renderCcEditor () {
      const ccs = this.editModel.controlChangeCommands || []
      let html = `
        <div class="pedal-detail-section pedal-editor-section">
          <div class="pedal-cc-editor-header">
            <h3>Control Change Commands (${ccs.length})</h3>
            <wa-button size="small" variant="brand" class="pedal-add-btn"
                       data-action="click->pedals#addCc">Add CC</wa-button>
          </div>
      `
      ccs.forEach((cc, idx) => {
        html += this.renderCcRow(cc, idx)
      })
      this._expandCcIndex = undefined
      html += '</div>'
      return html
    }

    renderCcRow (cc, idx) {
      if (!cc.valueRange) cc.valueRange = { min: 0, max: 127 }
      const dv = cc.valueRange.discreteValues || []
      const open = dv.length > 0 || idx === this._expandCcIndex
      return `
        <div class="pedal-cc-row ${open ? 'is-open' : ''}" data-controller="collapsible"
             data-collapsible-open-class="is-open">
          <div class="pedal-cc-row-header">
            <button type="button" class="pedal-cc-collapse-btn"
                    data-action="click->collapsible#toggle">
              <wa-icon name="chevron-right" data-collapsible-target="icon"></wa-icon>
            </button>
            <span class="pedal-cc-row-title">CC ${cc.controlChangeNumber ?? 0}: ${this.escapeHtml(cc.name || '')}</span>
            <wa-button size="small" variant="danger" appearance="text"
                       data-action="click->pedals#removeCc" data-index="${idx}">Remove</wa-button>
          </div>
          <div class="pedal-cc-row-body" data-collapsible-target="panel" ${open ? '' : 'hidden'}>
            <div class="pedal-editor-grid pedal-cc-fields">
              <label>CC#<wa-input type="number" size="small" min="0" max="127"
                value="${cc.controlChangeNumber ?? 0}"
                data-action="input->pedals#patchCc" data-index="${idx}" data-field="controlChangeNumber"></wa-input></label>
              <label>Name (max 14)<wa-input size="small" maxlength="14"
                value="${this.escapeHtml(cc.name || '')}"
                data-action="input->pedals#patchCc" data-index="${idx}" data-field="name"></wa-input></label>
              <label>Min<wa-input type="number" size="small" min="0" max="127"
                value="${cc.valueRange.min ?? 0}"
                data-action="input->pedals#patchCc" data-index="${idx}" data-field="valueRange.min"></wa-input></label>
              <label>Max<wa-input type="number" size="small" min="0" max="127"
                value="${cc.valueRange.max ?? 127}"
                data-action="input->pedals#patchCc" data-index="${idx}" data-field="valueRange.max"></wa-input></label>
              <label class="pedal-cc-additional">Additional Info
                <wa-input size="small" value="${this.escapeHtml(cc.additionalInfo || '')}"
                  data-action="input->pedals#patchCc" data-index="${idx}" data-field="additionalInfo"></wa-input>
              </label>
            </div>
            <div class="pedal-discrete-editor">
              <div class="pedal-discrete-header">
                <span>Discrete values (${dv.length}) · range ${cc.valueRange.min ?? 0}–${cc.valueRange.max ?? 127}</span>
                <wa-button size="small" variant="brand" class="pedal-add-btn"
                  data-action="click->pedals#addDiscrete" data-index="${idx}">Add Value</wa-button>
              </div>
              <div class="pedal-discrete-list pedal-discrete-list-editor">
                ${dv.map((d, di) => `
                  <div class="pedal-discrete-item pedal-discrete-item-edit">
                    <span class="pedal-discrete-value-label">Value</span>
                    <wa-input type="number" size="small" min="0" max="127"
                      value="${d.value ?? 0}"
                      data-action="input->pedals#patchDiscrete" data-index="${idx}" data-dindex="${di}" data-field="value"></wa-input>
                    <span class="pedal-discrete-name-label">Name</span>
                    <wa-input size="small" placeholder="Name" value="${this.escapeHtml(d.name || '')}"
                      data-action="input->pedals#patchDiscrete" data-index="${idx}" data-dindex="${di}" data-field="name"></wa-input>
                    <wa-button size="small" variant="danger" appearance="text"
                      data-action="click->pedals#removeDiscrete" data-index="${idx}" data-dindex="${di}">×</wa-button>
                  </div>
                `).join('')}
              </div>
            </div>
          </div>
        </div>
      `
    }

    renderDiscreteValuesList (range) {
      const discreteValues = range?.discreteValues || []
      if (!discreteValues.length) {
        return `<span class="cc-range-continuous">${range?.min ?? 0}–${range?.max ?? 127} continuous</span>`
      }
      return `<div class="pedal-discrete-list">${discreteValues.map(v => `
        <div class="pedal-discrete-item">
          <span class="pedal-discrete-value">${v.value ?? 0}</span>
          <span class="pedal-discrete-name">${this.escapeHtml(v.name || '—')}</span>
        </div>`).join('')}</div>`
    }

    renderCcTable (ccCommands) {
      if (!ccCommands.length) return ''
      let html = `
        <div class="pedal-detail-section">
          <h3>Control Change Commands (${ccCommands.length})</h3>
          <div class="cc-table-container"><table class="cc-table cc-table-discrete"><thead><tr>
            <th>CC#</th><th>Name</th><th>Range</th><th>Values</th>
          </tr></thead><tbody>`
      for (const cc of ccCommands) {
        const range = cc.valueRange || { min: 0, max: 127 }
        const rangeStr = `${range.min ?? 0}–${range.max ?? 127}`
        html += `<tr>
          <td class="cc-number">${cc.controlChangeNumber}</td>
          <td>${this.escapeHtml(cc.name || 'Unnamed')}</td>
          <td class="cc-range-cell">${rangeStr}</td>
          <td class="cc-values-cell">${this.renderDiscreteValuesList(range)}</td>
        </tr>`
      }
      return html + '</tbody></table></div></div>'
    }

    renderNrpnTable (nrpnCommands) {
      if (!nrpnCommands?.length) return ''
      let html = `
        <div class="pedal-detail-section">
          <h3>NRPN Commands (${nrpnCommands.length})</h3>
          <div class="cc-table-container"><table class="cc-table"><thead><tr>
            <th>MSB:LSB</th><th>Name</th><th>Range</th>
          </tr></thead><tbody>`
      for (const nrpn of nrpnCommands) {
        const range = nrpn.valueRange || {}
        html += `<tr>
          <td class="cc-number">${nrpn.parameterNumber?.msb ?? 0}:${nrpn.parameterNumber?.lsb ?? 0}</td>
          <td>${this.escapeHtml(nrpn.name || 'Unnamed')}</td>
          <td>${range.min ?? 0}-${range.max ?? 16383}</td>
        </tr>`
      }
      return html + '</tbody></table></div></div>'
    }

    addCc () {
      if (!this.editModel) return
      if (!this.editModel.controlChangeCommands) {
        this.editModel.controlChangeCommands = []
      }
      this.editModel.controlChangeCommands.push({
        controlChangeNumber: 0,
        name: 'New CC',
        valueRange: { min: 0, max: 127 }
      })
      this._expandCcIndex = this.editModel.controlChangeCommands.length - 1
      this.clearValidationFeedback()
      this.renderPedalDetails(this.selectedSlug, true)
    }

    removeCc (event) {
      const idx = parseInt(event.currentTarget.dataset.index, 10)
      this.editModel.controlChangeCommands.splice(idx, 1)
      this.clearValidationFeedback()
      this.renderPedalDetails(this.selectedSlug, true)
    }

    patchCc (event) {
      const idx = parseInt(event.currentTarget.dataset.index, 10)
      const field = event.currentTarget.dataset.field
      let value = event.currentTarget.value
      const cc = this.editModel.controlChangeCommands[idx]
      if (field.startsWith('valueRange.')) {
        const sub = field.split('.')[1]
        if (!cc.valueRange) cc.valueRange = { min: 0, max: 127 }
        value = parseInt(value, 10)
        cc.valueRange[sub] = value
      } else if (field === 'controlChangeNumber') {
        cc.controlChangeNumber = parseInt(value, 10)
      } else {
        cc[field] = value
      }
      this.clearValidationFeedback()
    }

    addDiscrete (event) {
      const idx = parseInt(event.currentTarget.dataset.index, 10)
      const cc = this.editModel.controlChangeCommands[idx]
      if (!cc.valueRange) cc.valueRange = { min: 0, max: 127 }
      if (!cc.valueRange.discreteValues) cc.valueRange.discreteValues = []
      cc.valueRange.discreteValues.push({ name: 'Value', value: 0 })
      this.clearValidationFeedback()
      this.renderPedalDetails(this.selectedSlug, true)
    }

    removeDiscrete (event) {
      const idx = parseInt(event.currentTarget.dataset.index, 10)
      const di = parseInt(event.currentTarget.dataset.dindex, 10)
      const cc = this.editModel.controlChangeCommands[idx]
      cc.valueRange.discreteValues.splice(di, 1)
      if (cc.valueRange.discreteValues.length === 0) {
        delete cc.valueRange.discreteValues
      }
      this.clearValidationFeedback()
      this.renderPedalDetails(this.selectedSlug, true)
    }

    patchDiscrete (event) {
      const idx = parseInt(event.currentTarget.dataset.index, 10)
      const di = parseInt(event.currentTarget.dataset.dindex, 10)
      const field = event.currentTarget.dataset.field
      const dv = this.editModel.controlChangeCommands[idx].valueRange.discreteValues[di]
      dv[field] = field === 'value' ? parseInt(event.currentTarget.value, 10) : event.currentTarget.value
      this.clearValidationFeedback()
    }

    formatTrsType (trsType) {
      switch (trsType) {
        case 'TYPE_A': return 'Type A (Tip)'
        case 'TYPE_B': return 'Type B (Ring)'
        case 'TYPE_TS': return 'TS (Tip/Sleeve)'
        case 'BOTH': return 'Type A + B'
        default: return trsType || 'Unknown'
      }
    }

    formatBankMode (bankMode) {
      switch (bankMode) {
        case 'none': return 'None'
        case 'CC0': return 'CC0'
        case 'CC0_CC32': return 'CC0 + CC32'
        default: return bankMode || 'None'
      }
    }

    async loadSchema () {
      if (this._schema) return this._schema
      if (!this._schemaLoad) {
        this._schemaLoad = fetch('/schemas/open-midi-rtc-schema.json')
          .then(r => {
            if (!r.ok) throw new Error('Schema fetch failed')
            return r.json()
          })
          .then(s => {
            this._schema = s
            return s
          })
      }
      return this._schemaLoad
    }

    validatePedal (obj) {
      const errors = []
      const schema = this._schema
      if (schema && window.JsonSchemaValidator) {
        errors.push(...window.JsonSchemaValidator.validate(obj, schema))
      }
      errors.push(...this.lintPedal(obj))
      return errors
    }

    lintPedal (obj) {
      const errors = []
      if (obj.schemaVersion !== '0.1.1') {
        errors.push({ path: '/schemaVersion', message: 'must be "0.1.1"' })
      }
      if (!obj.x_pc) {
        errors.push({ path: '/x_pc', message: 'x_pc block is required' })
      } else {
        const mode = obj.x_pc.bankSelectMode
        if (mode && !PEDALS_BANK_MODES.includes(mode)) {
          errors.push({ path: '/x_pc/bankSelectMode', message: 'invalid bankSelectMode' })
        }
      }
      if (obj.x_midiTrs && !PEDALS_TRS_TYPES.includes(obj.x_midiTrs)) {
        errors.push({ path: '/x_midiTrs', message: `must be one of: ${PEDALS_TRS_TYPES.join(', ')}` })
      }
      for (const field of ['receives', 'transmits']) {
        for (const token of (obj[field] || [])) {
          if (!PEDALS_MIDI_TOKENS.includes(token)) {
            errors.push({ path: `/${field}`, message: `invalid token: ${token}` })
          }
        }
      }
      const ccs = obj.controlChangeCommands || []
      const seenCc = new Set()
      ccs.forEach((cc, i) => {
        const path = `/controlChangeCommands/${i}`
        if ((cc.name || '').length > 14) {
          errors.push({ path: `${path}/name`, message: 'name must be 14 characters or fewer' })
        }
        if (!cc.valueRange || cc.valueRange.min === undefined || cc.valueRange.max === undefined) {
          errors.push({ path: `${path}/valueRange`, message: 'min and max are required' })
        }
        const n = cc.controlChangeNumber
        if (n !== undefined && Number.isInteger(n)) {
          if (seenCc.has(n)) {
            errors.push({
              path: `${path}/controlChangeNumber`,
              message: `duplicate CC number ${n} (each controlChangeNumber may appear once)`
            })
          } else {
            seenCc.add(n)
          }
        }
      })
      return errors
    }

    sortControlChangeCommands (obj) {
      const ccs = obj?.controlChangeCommands
      if (!ccs?.length) return
      ccs.sort((a, b) => (a.controlChangeNumber ?? 0) - (b.controlChangeNumber ?? 0))
    }

    sortDiscreteValues (obj) {
      for (const cc of obj?.controlChangeCommands || []) {
        const dv = cc.valueRange?.discreteValues
        if (!dv?.length) continue
        dv.sort((a, b) => (a.value ?? 0) - (b.value ?? 0))
      }
    }

    syncEditModelFromJsonTextarea () {
      const ta = this.detailContainerTarget.querySelector('[data-pedals-target="jsonTextarea"]')
        || this.detailContainerTarget.querySelector('.pedal-json-textarea')
      if (!ta) return
      try {
        this.editModel = JSON.parse(ta.value)
      } catch (err) {
        throw new Error('Invalid JSON: ' + err.message)
      }
    }

    async savePedal () {
      if (!this.selectedIsUser || !this.selectedSlug) return
      try {
        this.validationSuccess = null
        if (this.viewJson) this.syncEditModelFromJsonTextarea()
        else this.syncIdentityFromDevice(this.editModel)
        this.sortControlChangeCommands(this.editModel)
        this.sortDiscreteValues(this.editModel)
        await this.loadSchema()
        this.validationErrors = this.validatePedal(this.editModel)
        if (this.validationErrors.length > 0) {
          this.validationSuccess = null
          this.renderPedalDetails(this.selectedSlug, true)
          return
        }

        const path = this.userPedalPathForSlug(this.selectedSlug)
        if (!path) throw new Error('Cannot resolve user pedal path')
        const prevSlug = this.selectedSlug
        const version = String(this.editModel.implementationVersion ?? '0')
        try {
          this.assertUserPedalWriteAllowed(this.editModel, prevSlug)
        } catch (err) {
          this.validationSuccess = null
          this.validationErrors = [{ path: '/slug', message: err.message }]
          this.renderPedalDetails(this.selectedSlug, true)
          return
        }

        this.connection.setTabsLocked(true, 'pedals')
        await this.assetsPutFile(path, this.editModel)
        await this.deleteDeviceCache(prevSlug)
        await this.regenerateUserManifest()
        await this.fetchManifests()
        this.currentDevice = this.cloneDevice(this.editModel)
        const newSlug = this.findSlugForPath(path)
        if (newSlug) {
          this.selectedSlug = newSlug
          if (this.currentSlug === prevSlug) this.currentSlug = newSlug
        }
        const info = this.deviceBySlug.get(this.selectedSlug)
        this.selectedIsUser = true
        this.validationErrors = []
        this.validationSuccess = 'Pedal JSON saved successfully.'
        this.renderTree()
        this.renderPedalDetails(this.selectedSlug, true)
      } catch (err) {
        console.error('Save failed:', err)
        const msg = err.message || 'Save failed'
        const title = msg.startsWith('Invalid JSON:') ? 'Invalid JSON' : 'Save failed'
        this.showMessageDialog(title, msg)
      } finally {
        this.connection.setTabsLocked(false)
      }
    }

    userPedalPathForSlug (slug) {
      const info = this.deviceBySlug.get(slug)
      if (!info?.entry) return null
      return this.deviceFilePath(info.entry, true)
    }

    findSlugForPath (path) {
      for (const [slug, info] of this.deviceBySlug) {
        if (info.isUser && this.deviceFilePath(info.entry, true) === path) return slug
      }
      return null
    }

    slugifyFilename (name) {
      return name.toLowerCase()
        .replace(/[^a-z0-9]+/g, '_')
        .replace(/^_|_$/g, '')
        .slice(0, 48) || 'pedal'
    }

    stripCopyOfPrefix (name) {
      let s = (name || '').trim()
      while (s.startsWith('Copy of ')) {
        s = s.slice('Copy of '.length).trim()
      }
      return s
    }

    stripDuplicateNumberSuffix (name) {
      return (name || '').replace(/\s\(\d+\)$/, '').trim()
    }

    nextDuplicateModelName (sourceModel, manufacturer) {
      let base = this.stripDuplicateNumberSuffix(this.stripCopyOfPrefix(sourceModel))
      if (!base) base = 'Pedal'
      const mfg = (manufacturer || 'User').trim() || 'User'
      for (let n = 1; n < 1000; n++) {
        const candidate = `${base} (${n})`
        const slug = this.userDeviceSlug({
          device: { manufacturer: mfg, model: candidate },
          implementationVersion: '0'
        })
        if (!this.deviceBySlug.has(slug)) return candidate
      }
      return `${base} (1000)`
    }

    userDeviceSlug (device, version) {
      const d = device?.device || {}
      const mfg = this.slugifyFilename(d.manufacturer || 'user')
      const model = this.slugifyFilename(d.model || 'pedal')
      const ver = String(version ?? device?.implementationVersion ?? '0')
      return `${mfg}.${model}@${ver}`
    }

    checkUserSlugConflict (device, allowSlug) {
      const slug = this.userDeviceSlug(device)
      if (!slug) return null
      if (allowSlug && slug === allowSlug) return null
      const info = this.deviceBySlug.get(slug)
      if (info?.isUser) return slug
      return null
    }

    slugConflictMessage (conflictSlug) {
      return `Cannot save: another user pedal already uses slug "${conflictSlug}". ` +
        'Change the model name or use Duplicate to create a new pedal file.'
    }

    assertUserPedalWriteAllowed (device, allowSlug) {
      const conflict = this.checkUserSlugConflict(device, allowSlug)
      if (conflict) throw new Error(this.slugConflictMessage(conflict))
    }

    async listUserFilenames () {
      try {
        const response = await this.assetsCommand(`LS ${PEDALS_USER_DIR}`)
        if (response.startsWith('ERROR')) return []
        const files = JSON.parse(response)
        return files.filter(f => f.type === 'file' && f.name.endsWith('.json')).map(f => f.name)
      } catch (err) {
        return []
      }
    }

    async uniqueUserFilename (baseName) {
      const existing = await this.listUserFilenames()
      let name = baseName
      let n = 1
      while (existing.includes(`${name}.json`)) {
        name = `${baseName}_${n++}`
      }
      return `${name}.json`
    }

    createMvpStub (displayName) {
      return {
        schemaVersion: '0.1.1',
        title: displayName,
        displayName: displayName.length > 14 ? displayName.slice(0, 14) : displayName,
        implementationVersion: '0',
        device: {
          displayName: '',
          manufacturer: 'User',
          model: displayName,
          version: ''
        },
        receives: ['PROGRAM_CHANGE'],
        transmits: [],
        controlChangeCommands: [],
        x_pc: { indexBase: 0, count: 128, bankSelectMode: 'none' },
        x_midiTrs: 'BOTH',
        x_midiChannel: 1
      }
    }

    showNewPedalDialog () {
      if (!this.hasNewPedalDialogTarget) return
      this.newPedalInputTarget.value = ''
      this.newPedalDialogTarget.open = true
      this.newPedalInputTarget.focus()
    }

    hideNewPedalDialog () {
      if (this.hasNewPedalDialogTarget) this.newPedalDialogTarget.open = false
    }

    async confirmNewPedal () {
      const name = this.newPedalInputTarget?.value?.trim()
      if (!name) return
      this.hideNewPedalDialog()
      try {
        this.connection.setTabsLocked(true, 'pedals')
        await this.assetsMkdir(PEDALS_USER_DIR)
        const filename = await this.uniqueUserFilename(this.slugifyFilename(name))
        const stub = this.createMvpStub(name)
        await this.loadSchema()
        const errors = this.validatePedal(stub)
        if (errors.length) {
          this.showMessageDialog('Validation failed',
            errors.map(e => `${e.path}: ${e.message}`).join('\n'))
          return
        }
        const path = `${PEDALS_USER_DIR}/${filename}`
        this.assertUserPedalWriteAllowed(stub, null)
        await this.assetsPutFile(path, stub)
        await this.regenerateUserManifest()
        await this.fetchManifests()
        const slug = this.findSlugForPath(path) || this.userDeviceSlug(stub)
        this.selectedSlug = slug
        this.selectedIsUser = true
        this.viewJson = false
        this.renderTree()
        await this.loadPedalDetails(slug)
      } catch (err) {
        console.error('New pedal failed:', err)
        this.showMessageDialog('New pedal failed', err.message || 'Failed to create pedal')
      } finally {
        this.connection.setTabsLocked(false)
      }
    }

    async duplicatePedal (event) {
      const slug = event.currentTarget.dataset.slug
      if (!slug) return
      const info = this.deviceBySlug.get(slug)
      let source = this.currentDevice
      if (info?.isUser && this.selectedSlug === slug && this.editModel) {
        source = this.editModel
      }
      if (!source) return
      try {
        this.connection.setTabsLocked(true, 'pedals')
        const copy = this.cloneDevice(source)
        copy.implementationVersion = '0'
        if (!copy.device) copy.device = { displayName: '', manufacturer: 'User', model: '', version: '' }
        copy.device.manufacturer = copy.device.manufacturer || 'User'
        const sourceModel = (copy.device.model || copy.title || copy.displayName || 'Pedal').trim()
        copy.device.model = this.nextDuplicateModelName(sourceModel, copy.device.manufacturer)
        this.syncIdentityFromDevice(copy)

        await this.assetsMkdir(PEDALS_USER_DIR)
        const filename = await this.uniqueUserFilename(
          this.slugifyFilename(copy.device.model))
        const path = `${PEDALS_USER_DIR}/${filename}`
        this.assertUserPedalWriteAllowed(copy, null)
        await this.assetsPutFile(path, copy)
        await this.regenerateUserManifest()
        await this.fetchManifests()
        const newSlug = this.findSlugForPath(path) || this.userDeviceSlug(copy)
        this.selectedSlug = newSlug
        this.selectedIsUser = true
        this.viewJson = false
        this.renderTree()
        await this.loadPedalDetails(newSlug)
      } catch (err) {
        console.error('Duplicate failed:', err)
        this.showMessageDialog('Duplicate failed', err.message || 'Duplicate failed')
      } finally {
        this.connection.setTabsLocked(false)
      }
    }

    showDeleteDialog (event) {
      const slug = event.currentTarget.dataset.slug
      const info = this.deviceBySlug.get(slug)
      if (!info?.isUser) return
      this._deleteSlug = slug
      this._deletePath = this.deviceFilePath(info.entry, true)
      if (this.hasDeletePedalNameTarget) {
        this.deletePedalNameTarget.textContent = this.getDeviceDisplayName(info.entry)
      }
      if (this.hasDeleteDialogTarget) this.deleteDialogTarget.open = true
    }

    hideDeleteDialog () {
      if (this.hasDeleteDialogTarget) this.deleteDialogTarget.open = false
      this._deleteSlug = null
      this._deletePath = null
    }

    showMessageDialog (title, message) {
      if (this.hasMessageDialogTarget) {
        this.messageDialogTarget.label = title || 'Error'
        this.messageDialogTarget.open = true
      }
      if (this.hasMessageDialogBodyTarget) {
        this.messageDialogBodyTarget.textContent = message || ''
      }
    }

    hideMessageDialog () {
      if (this.hasMessageDialogTarget) this.messageDialogTarget.open = false
    }

    async confirmDelete () {
      if (!this._deletePath) return
      const slug = this._deleteSlug
      try {
        this.connection.setTabsLocked(true, 'pedals')
        await this.assetsRm(this._deletePath)
        await this.deleteDeviceCache(slug)
        await this.regenerateUserManifest()
        await this.fetchManifests()
        this.hideDeleteDialog()
        if (this.currentSlug === slug) this.currentSlug = null
        this.selectedSlug = null
        this.editModel = null
        this.currentDevice = null
        this.renderTree()
        this.renderEmptyDetail()
      } catch (err) {
        console.error('Delete failed:', err)
        this.showMessageDialog('Delete failed', err.message || 'Delete failed')
      } finally {
        this.connection.setTabsLocked(false)
      }
    }

    async usePedal (event) {
      const slug = event.currentTarget.dataset.slug
      if (!slug) return
      const btn = event.currentTarget
      try {
        btn.disabled = true
        await this.connection.runSerialTask(async () => {
          const modeGranted = await this.connection._requestModeImpl('PEDALS')
          if (!modeGranted) return

          await this.sleep(150)
          await this.connection.sendRaw('PEDALS\n')
          const start = await this.readLine(3000)
          if (start !== 'PEDALS_STARTED') throw new Error(start)

          await this.connection.sendRaw(`SELECT ${slug}\n`)
          const response = await this.readLine(5000)

          await this.connection.sendRaw('EXIT\n')
          await this.readLine(2000)

          await this.ensureAssetsModeBody()

          if (response === 'OK') {
            this.currentSlug = slug
            this.renderTree()
            await this.loadPedalDetailsBody(slug)
          } else {
            throw new Error(`Failed to select pedal: ${response}`)
          }
        })
      } catch (err) {
        console.error('SELECT failed:', err)
        this.showMessageDialog('Activate failed', err.message || 'Error selecting pedal')
        try {
          await this.connection.runSerialTask(() => this.ensureAssetsModeBody())
        } catch (e) {}
      } finally {
        btn.disabled = false
      }
    }

    async loadDeviceAuthoring () {
      if (this._authoringText) return this._authoringText
      if (!this._authoringLoad) {
        this._authoringLoad = fetch(DEVICE_AUTHORING_URL)
          .then(res => {
            if (!res.ok) throw new Error(`Could not load prompt (${res.status})`)
            return res.text()
          })
          .then(text => {
            this._authoringText = text
            return text
          })
      }
      return this._authoringLoad
    }

    async copyLlmPrompt (event) {
      const btn = event?.currentTarget
      const prevLabel = btn?.textContent?.trim() || 'LLM Prompt'
      if (btn) {
        btn.disabled = true
        btn.textContent = 'Loading…'
      }
      try {
        const text = await this.loadDeviceAuthoring()
        if (!text?.trim()) throw new Error('Prompt file is empty')
        await navigator.clipboard.writeText(text)
        if (btn) btn.textContent = 'Copied!'
        setTimeout(() => {
          if (btn) btn.textContent = prevLabel
        }, 1500)
      } catch (err) {
        console.error('LLM prompt copy failed:', err)
        if (btn) btn.textContent = prevLabel
        this.showMessageDialog('Copy failed', err.message || 'Could not copy LLM prompt')
      } finally {
        if (btn) btn.disabled = false
      }
    }

    async refresh () {
      if (!this.connection.isConnected) return
      this._activateQueue = this._activateQueue.then(() =>
        this.connection.runSerialTask(async () => {
          this.connection.beginExclusiveSession()
          try {
            await this.ensureAssetsModeBody()
            await this.fetchManifestsBody()
            const slug = this.selectedSlug || this.currentSlug
            if (slug && this.deviceBySlug.has(slug)) {
              this.selectedSlug = slug
              const info = this.deviceBySlug.get(slug)
              this.selectedIsUser = info?.isUser ?? false
              this.renderTree()
              await this.loadPedalDetailsBody(slug)
            }
          } finally {
            this.connection.endExclusiveSession()
          }
        }))
      return this._activateQueue
    }

    readLine (timeout = 2000) {
      return this.connection.readLine(timeout)
    }
  }
)
