/* Storm Summoner - Pedals Controller */

application.register(
  'pedals',
  class extends BaseController {
    static targets = ['searchInput', 'refreshBtn', 'treeContainer', 'detailContainer']

    connect() {
      this.manifest = null
      this.vendorTree = null
      this.currentSlug = null
      this.selectedSlug = null
      this.inPedalsMode = false
      this.isActivating = false

      // Listen for connection changes
      this.connection.on('connection:changed', this.onConnectionChanged.bind(this))

      // Listen for mode changes
      this.connection.on('mode:changed', ({ mode }) => {
        if (mode !== 'PEDALS') {
          this.inPedalsMode = false
        }
      })

      // Listen for tab activation
      document.addEventListener('app:tab-activated', (e) => {
        if (e.detail.tab === 'pedals' && this.connection.isConnected) {
          this.activate()
        }
      })

      // Listen for navigation with params (from Info tab)
      document.addEventListener('app:tab-params', (e) => {
        if (e.detail.tab === 'pedals' && e.detail.params?.slug) {
          this.currentSlug = e.detail.params.slug
          // If we're already loaded, select and show the pedal
          if (this.inPedalsMode && this.manifest) {
            this.selectedSlug = this.currentSlug
            this.renderTree()
            this.loadPedalDetails(this.currentSlug)
          }
        }
      })
    }

    onConnectionChanged({ connected }) {
      this.refreshBtnTarget.disabled = !connected
      if (!connected) {
        this.inPedalsMode = false
        this.isActivating = false
        this.manifest = null
        this.vendorTree = null
        this.renderEmptyTree()
        this.renderEmptyDetail()
      }
    }

    async activate() {
      if (!this.connection.isConnected) return
      if (this.isActivating) return // Prevent double activation
      if (this.inPedalsMode) return // Already in pedals mode

      this.isActivating = true

      try {
        // First, get current device slug via INFO (before entering PEDALS mode)
        if (!this.currentSlug) {
          await this.fetchCurrentDeviceFirst()
        }

        // Now enter PEDALS mode
        const modeGranted = await this.connection.requestMode('PEDALS')
        if (!modeGranted) return

        await this.enterPedalsMode()
        await this.fetchManifest()

        // Auto-select and load the current pedal if we have one
        if (this.currentSlug && this.manifest) {
          this.selectedSlug = this.currentSlug
          this.renderTree() // Update tree to show selection
          await this.loadPedalDetails(this.currentSlug)
        }
      } catch (err) {
        console.error('Pedals activation error:', err)
      } finally {
        this.isActivating = false
      }
    }

    async fetchCurrentDeviceFirst() {
      // Get current device info BEFORE entering PEDALS mode
      try {
        // Exit any other mode first
        if (this.connection.currentMode) {
          await this.connection.exitMode()
          await this.sleep(200)
        }
        await this.sleep(100)

        const response = await this.connection.sendCommand('INFO', 5000, (data) =>
          typeof data.version === 'string' && typeof data.build === 'number')
        if (response && !response.startsWith('ERROR:')) {
          const info = JSON.parse(response)
          if (info.pedal?.slug) {
            this.currentSlug = info.pedal.slug
          }
        }
      } catch (err) {
        console.warn('Failed to fetch current device:', err)
      }
    }

    async enterPedalsMode() {
      await this.sleep(150)
      await this.connection.sendRaw('PEDALS\n')
      const response = await this.readLine(3000)

      if (response === 'PEDALS_STARTED') {
        this.inPedalsMode = true
      } else {
        throw new Error(`Unexpected response: ${response}`)
      }
    }

    async fetchManifest() {
      try {
        await this.connection.sendRaw('MANIFEST\n')
        const sizeResponse = await this.readLine(5000)

        if (!sizeResponse || !sizeResponse.startsWith('SIZE ')) {
          console.error('Failed to get manifest size:', sizeResponse)
          this.renderEmptyTree()
          return
        }

        const size = parseInt(sizeResponse.substring(5))
        if (isNaN(size) || size <= 0) {
          console.error('Invalid manifest size:', size)
          this.renderEmptyTree()
          return
        }

        // Read binary data
        const data = await this.connection.readBinary(size, 30000)
        const text = new TextDecoder().decode(data)

        this.manifest = JSON.parse(text)
        this.buildVendorTree()
        this.renderTree()
      } catch (err) {
        console.error('Error fetching manifest:', err)
        this.renderEmptyTree()
      }
    }

    buildVendorTree() {
      if (!this.manifest?.devices) return

      // Group devices by vendor
      const vendors = {}
      for (const device of this.manifest.devices) {
        const vendor = device.vendor || 'Unknown'
        if (!vendors[vendor]) {
          vendors[vendor] = []
        }
        vendors[vendor].push(device)
      }

      // Sort vendors alphabetically, sort devices within each vendor
      const sortedVendors = Object.keys(vendors).sort((a, b) =>
        a.toLowerCase().localeCompare(b.toLowerCase())
      )

      this.vendorTree = sortedVendors.map(vendor => ({
        name: vendor,
        displayName: this.formatVendorName(vendor),
        devices: vendors[vendor].sort((a, b) =>
          (a.product || '').toLowerCase().localeCompare((b.product || '').toLowerCase())
        )
      }))
    }

    formatVendorName(vendor) {
      // Convert snake_case to Title Case
      return vendor
        .split('_')
        .map(word => word.charAt(0).toUpperCase() + word.slice(1))
        .join(' ')
    }

    renderEmptyTree() {
      this.treeContainerTarget.innerHTML = `
        <div class="empty-state">
          <wa-icon name="guitar"></wa-icon>
          <p>Connect to browse pedals</p>
        </div>
      `
    }

    renderEmptyDetail() {
      this.detailContainerTarget.innerHTML = `
        <div class="empty-state">
          <wa-icon name="circle-info"></wa-icon>
          <p>Select a pedal to view details</p>
        </div>
      `
    }

    renderTree() {
      if (!this.vendorTree || this.vendorTree.length === 0) {
        this.renderEmptyTree()
        return
      }

      let html = '<div class="pedal-tree">'

      for (const vendor of this.vendorTree) {
        const hasCurrentDevice = vendor.devices.some(d => d.slug === this.currentSlug)

        html += `
          <wa-details summary="${vendor.displayName}" ${hasCurrentDevice ? 'open' : ''}>
            <div class="pedal-list">
        `

        for (const device of vendor.devices) {
          const isCurrent = device.slug === this.currentSlug
          const isSelected = device.slug === this.selectedSlug

          html += `
            <div class="pedal-item ${isCurrent ? 'current' : ''} ${isSelected ? 'selected' : ''}"
                 data-action="click->pedals#selectPedal"
                 data-slug="${device.slug}">
              <span class="pedal-name">${this.getDeviceDisplayName(device)}</span>
              ${isCurrent ? '<wa-icon name="check" class="current-icon"></wa-icon>' : ''}
            </div>
          `
        }

        html += `
            </div>
          </wa-details>
        `
      }

      html += '</div>'
      this.treeContainerTarget.innerHTML = html
    }

    getDeviceDisplayName(device) {
      // Try to use a nice display name from the manifest
      if (device.product) {
        return device.product
          .split('_')
          .map(word => word.charAt(0).toUpperCase() + word.slice(1))
          .join(' ')
      }
      return device.slug || 'Unknown'
    }

    filterTree() {
      const query = this.searchInputTarget.value.toLowerCase().trim()

      if (!query) {
        // Reset all visibility
        this.treeContainerTarget.querySelectorAll('wa-details').forEach(el => {
          el.style.display = ''
          el.querySelectorAll('.pedal-item').forEach(item => {
            item.style.display = ''
          })
        })
        return
      }

      // Filter vendors and devices
      this.treeContainerTarget.querySelectorAll('wa-details').forEach(vendorEl => {
        const vendorName = vendorEl.getAttribute('summary').toLowerCase()
        const vendorMatches = vendorName.includes(query)

        let anyDeviceVisible = false
        vendorEl.querySelectorAll('.pedal-item').forEach(item => {
          const deviceName = item.querySelector('.pedal-name').textContent.toLowerCase()
          const deviceMatches = deviceName.includes(query)

          if (vendorMatches || deviceMatches) {
            item.style.display = ''
            anyDeviceVisible = true
          } else {
            item.style.display = 'none'
          }
        })

        vendorEl.style.display = anyDeviceVisible ? '' : 'none'
        if (anyDeviceVisible && query.length > 0) {
          vendorEl.open = true
        }
      })
    }

    async selectPedal(event) {
      const slug = event.currentTarget.dataset.slug
      if (!slug || slug === this.selectedSlug) return

      this.selectedSlug = slug
      this.renderTree() // Update selection highlighting

      await this.loadPedalDetails(slug)
    }

    async loadPedalDetails(slug) {
      try {
        this.detailContainerTarget.innerHTML = `
          <div class="loading-state">
            <wa-spinner></wa-spinner>
            <p>Loading pedal details...</p>
          </div>
        `

        await this.connection.sendRaw(`LOAD ${slug}\n`)
        const sizeResponse = await this.readLine(5000)

        if (!sizeResponse || sizeResponse.startsWith('ERROR:')) {
          this.detailContainerTarget.innerHTML = `
            <div class="empty-state">
              <wa-icon name="triangle-exclamation"></wa-icon>
              <p>${sizeResponse || 'Failed to load device'}</p>
            </div>
          `
          return
        }

        if (!sizeResponse.startsWith('SIZE ')) {
          throw new Error(`Unexpected response: ${sizeResponse}`)
        }

        const size = parseInt(sizeResponse.substring(5))
        const data = await this.connection.readBinary(size, 30000)
        const text = new TextDecoder().decode(data)

        const device = JSON.parse(text)
        this.renderPedalDetails(slug, device)
      } catch (err) {
        console.error('Error loading pedal details:', err)
        this.detailContainerTarget.innerHTML = `
          <div class="empty-state">
            <wa-icon name="triangle-exclamation"></wa-icon>
            <p>Error loading pedal details</p>
          </div>
        `
      }
    }

    renderPedalDetails(slug, device) {
      const isCurrent = slug === this.currentSlug

      // Extract basic info
      const title = device.title || device.displayName || 'Unknown Device'
      const manufacturer = device.device?.manufacturer || 'Unknown'
      const model = device.device?.model || ''
      const version = device.device?.version || ''

      // MIDI capabilities
      const receives = device.receives || []
      const transmits = device.transmits || []

      // TRS type
      const trsType = device.x_midiTrs || 'BOTH'
      const trsDisplay = this.formatTrsType(trsType)

      // Default channel
      const defaultChannel = device.x_midiChannel || 'Not specified'

      // Program change info
      const pcInfo = device.x_pc || {}
      const presetCount = pcInfo.count || 128
      const indexBase = pcInfo.indexBase || 0
      const bankMode = this.formatBankMode(pcInfo.bankSelectMode)

      // Control change commands
      const ccCommands = device.controlChangeCommands || []

      // NRPN commands
      const nrpnCommands = device.nrpnCommands || []

      let html = `
        <div class="pedal-detail">
          <div class="pedal-detail-header">
            <div class="pedal-detail-title">
              <h2>${title}</h2>
              <span class="pedal-detail-subtitle">${manufacturer}${model ? ' - ' + model : ''}</span>
            </div>
            ${isCurrent ? '<span class="current-badge">Current</span>' : ''}
          </div>

          <div class="pedal-detail-section">
            <h3>MIDI Configuration</h3>
            <div class="detail-grid">
              <div class="detail-item">
                <span class="detail-label">TRS Type</span>
                <span class="detail-value">${trsDisplay}</span>
              </div>
              <div class="detail-item">
                <span class="detail-label">Default Channel</span>
                <span class="detail-value">${defaultChannel}</span>
              </div>
              <div class="detail-item">
                <span class="detail-label">Receives</span>
                <span class="detail-value">${receives.length > 0 ? receives.join(', ') : 'None'}</span>
              </div>
              <div class="detail-item">
                <span class="detail-label">Transmits</span>
                <span class="detail-value">${transmits.length > 0 ? transmits.join(', ') : 'None'}</span>
              </div>
            </div>
          </div>

          <div class="pedal-detail-section">
            <h3>Program Change</h3>
            <div class="detail-grid">
              <div class="detail-item">
                <span class="detail-label">Presets</span>
                <span class="detail-value">${presetCount}</span>
              </div>
              <div class="detail-item">
                <span class="detail-label">Index Base</span>
                <span class="detail-value">${indexBase}</span>
              </div>
              <div class="detail-item">
                <span class="detail-label">Bank Select</span>
                <span class="detail-value">${bankMode}</span>
              </div>
            </div>
          </div>
      `

      // CC Commands table
      if (ccCommands.length > 0) {
        html += `
          <div class="pedal-detail-section">
            <h3>Control Change Commands (${ccCommands.length})</h3>
            <div class="cc-table-container">
              <table class="cc-table">
                <thead>
                  <tr>
                    <th>CC#</th>
                    <th>Name</th>
                    <th>Range</th>
                    <th>Values</th>
                  </tr>
                </thead>
                <tbody>
        `

        for (const cc of ccCommands) {
          const range = cc.valueRange || {}
          const rangeStr = `${range.min ?? 0}-${range.max ?? 127}`
          const discreteValues = range.discreteValues || []
          const valuesStr = discreteValues.length > 0
            ? discreteValues.map(v => v.name).join(', ')
            : 'Continuous'

          html += `
            <tr>
              <td class="cc-number">${cc.controlChangeNumber}</td>
              <td>${cc.name || 'Unnamed'}</td>
              <td>${rangeStr}</td>
              <td class="cc-values">${valuesStr}</td>
            </tr>
          `
        }

        html += `
                </tbody>
              </table>
            </div>
          </div>
        `
      }

      // NRPN Commands table
      if (nrpnCommands.length > 0) {
        html += `
          <div class="pedal-detail-section">
            <h3>NRPN Commands (${nrpnCommands.length})</h3>
            <div class="cc-table-container">
              <table class="cc-table">
                <thead>
                  <tr>
                    <th>MSB:LSB</th>
                    <th>Name</th>
                    <th>Range</th>
                  </tr>
                </thead>
                <tbody>
        `

        for (const nrpn of nrpnCommands) {
          const range = nrpn.valueRange || {}
          const rangeStr = `${range.min ?? 0}-${range.max ?? 16383}`

          html += `
            <tr>
              <td class="cc-number">${nrpn.parameterNumber?.msb ?? 0}:${nrpn.parameterNumber?.lsb ?? 0}</td>
              <td>${nrpn.name || 'Unnamed'}</td>
              <td>${rangeStr}</td>
            </tr>
          `
        }

        html += `
                </tbody>
              </table>
            </div>
          </div>
        `
      }

      // Actions
      html += `
          <div class="pedal-detail-actions">
            ${!isCurrent ? `
              <wa-button variant="brand" data-action="click->pedals#usePedal" data-slug="${slug}">
                <wa-icon name="check" slot="prefix"></wa-icon>
                Use This Pedal
              </wa-button>
            ` : `
              <wa-button variant="success" disabled>
                <wa-icon name="check" slot="prefix"></wa-icon>
                Currently Selected
              </wa-button>
            `}
          </div>
        </div>
      `

      this.detailContainerTarget.innerHTML = html
    }

    formatTrsType(trsType) {
      switch (trsType) {
        case 'TYPE_A': return 'Type A (Tip)'
        case 'TYPE_B': return 'Type B (Ring)'
        case 'TYPE_TS': return 'TS (Tip/Sleeve)'
        case 'BOTH': return 'Both A & B'
        default: return trsType || 'Unknown'
      }
    }

    formatBankMode(bankMode) {
      switch (bankMode) {
        case 'none': return 'None'
        case 'CC0': return 'CC0'
        case 'CC0_CC32': return 'CC0 + CC32'
        default: return bankMode || 'None'
      }
    }

    async usePedal(event) {
      const slug = event.currentTarget.dataset.slug
      if (!slug) return

      try {
        event.currentTarget.disabled = true
        event.currentTarget.innerHTML = '<wa-spinner></wa-spinner> Selecting...'

        await this.connection.sendRaw(`SELECT ${slug}\n`)
        const response = await this.readLine(5000)

        if (response === 'OK') {
          this.currentSlug = slug
          this.renderTree()
          this.renderPedalDetails(slug, await this.reloadCurrentDevice(slug))
        } else {
          console.error('SELECT failed:', response)
          alert(`Failed to select pedal: ${response}`)
          // Re-render to reset button
          await this.loadPedalDetails(slug)
        }
      } catch (err) {
        console.error('Error selecting pedal:', err)
        alert('Error selecting pedal')
        await this.loadPedalDetails(slug)
      }
    }

    async reloadCurrentDevice(slug) {
      // Reload the device JSON to update the detail view
      await this.connection.sendRaw(`LOAD ${slug}\n`)
      const sizeResponse = await this.readLine(5000)

      if (!sizeResponse || !sizeResponse.startsWith('SIZE ')) {
        throw new Error('Failed to reload device')
      }

      const size = parseInt(sizeResponse.substring(5))
      const data = await this.connection.readBinary(size, 30000)
      return JSON.parse(new TextDecoder().decode(data))
    }

    async refresh() {
      if (!this.connection.isConnected) return

      if (!this.inPedalsMode) {
        await this.activate()
      } else {
        await this.fetchManifest()
      }
    }

    // Use ConnectionManager's readLine method
    readLine(timeout = 2000) {
      return this.connection.readLine(timeout)
    }
  }
)
