/* Storm Summoner - Info Controller */

application.register(
  'info',
  class extends BaseController {
    static targets = ['deviceCard', 'pedalCard']

    connect() {
      this.infoData = null
      this.isActivating = false
      this.releases = null

      // Fetch releases manifest for update checking
      this.fetchReleases()

      // Listen for connection changes
      this.connection.on('connection:changed', this.onConnectionChanged.bind(this))

      // Listen for tab activation (this is the only trigger for activate)
      document.addEventListener('app:tab-activated', (e) => {
        if (e.detail.tab === 'info' && this.connection.isConnected) {
          this.activate()
        }
      })

      // Listen for update completion to refresh info
      document.addEventListener('updater:complete', () => {
        if (this.connection.isConnected) {
          this.activate()
        }
      })

      // Event delegation for dynamically created buttons
      this.element.addEventListener('click', (e) => {
        const btn = e.target.closest('[data-action*="goToPedals"]')
        if (btn) this.goToPedals()

        const updateBtn = e.target.closest('[data-action*="goToUpdater"]')
        if (updateBtn) this.goToUpdater()
      })
    }

    async fetchReleases () {
      try {
        const response = await fetch('/releases.json')
        if (response.ok) {
          this.releases = await response.json()
        }
      } catch (err) {
        console.warn('Failed to load releases manifest:', err)
      }
    }

    onConnectionChanged({ connected }) {
      if (connected) {
        // Query INFO when connecting (if we're on the info tab)
        const tabGroup = document.querySelector('wa-tab-group')
        const activePanel = tabGroup?.querySelector('wa-tab-panel[active]')
        if (activePanel?.getAttribute('name') === 'info') {
          this.activate()
        }
      } else {
        this.infoData = null
        this.isActivating = false
        this.renderEmpty()
      }
    }

    async activate() {
      if (!this.connection.isConnected) return
      if (this.isActivating) return

      this.isActivating = true

      try {
        if (this.connection.currentMode) {
          await this.connection.exitMode()
          await this.sleep(300)
        }

        await this.sleep(100)

        const response = await this.connection.sendCommand('INFO', 5000)

        if (!response || response.startsWith('ERROR:')) {
          console.error('INFO command failed:', response)
          this.renderEmpty()
          return
        }

        this.infoData = JSON.parse(response)
        this.renderInfo()

        // Dispatch device info for other controllers
        document.dispatchEvent(new CustomEvent('device:info', {
          detail: {
            version: this.infoData.version,
            build: this.infoData.build,
            git: this.infoData.git,
            assets_checksum: this.infoData.assets_checksum
          }
        }))
      } catch (err) {
        console.error('Info activation error:', err)
        this.renderEmpty()
      } finally {
        this.isActivating = false
      }
    }

    renderEmpty() {
      this.deviceCardTarget.innerHTML = `
        <div class="empty-state">
          <wa-icon name="link-slash"></wa-icon>
          <p>Connect to view device info</p>
        </div>
      `
      this.pedalCardTarget.innerHTML = `
        <div class="empty-state">
          <wa-icon name="link-slash"></wa-icon>
          <p>Connect to view pedal info</p>
        </div>
      `
    }

    getLatestVersion() {
      if (!this.releases?.firmware?.length) return null
      return this.releases.firmware[0].version
    }

    hasNewerFirmware() {
      if (!this.infoData?.version || !this.releases?.firmware?.length) return false

      const current = this.parseVersion(this.infoData.version)
      const latest = this.parseVersion(this.releases.firmware[0].version)

      return latest.major > current.major ||
        (latest.major === current.major && latest.minor > current.minor)
    }

    getLatestAssetsChecksum() {
      if (!this.releases?.assets?.length) return null
      return this.releases.assets[0].checksum
    }

    hasNewerAssets() {
      if (!this.infoData?.assets_checksum || !this.releases?.assets?.length) return false
      return this.infoData.assets_checksum !== this.releases.assets[0].checksum
    }

    parseVersion(str) {
      const parts = str.split('.').map(Number)
      return {
        major: parts[0] || 0,
        minor: parts[1] || 0
      }
    }

    renderInfo() {
      if (!this.infoData) {
        this.renderEmpty()
        return
      }

      // Check for update availability
      const fwBanner = this.hasNewerFirmware()
        ? `<wa-callout variant="warning" class="update-banner">
            <wa-icon name="arrow-up-from-bracket" slot="icon"></wa-icon>
            <strong>Firmware update:</strong> v${this.getLatestVersion()}
            <wa-button size="small" variant="brand" appearance="outlined"
                       data-action="click->info#goToUpdater" style="margin-left: auto;">
              Update
            </wa-button>
          </wa-callout>`
        : ''

      const assetsBanner = this.hasNewerAssets()
        ? `<wa-callout variant="warning" class="update-banner">
            <wa-icon name="folder-arrow-up" slot="icon"></wa-icon>
            <strong>Assets update:</strong> ${this.getLatestAssetsChecksum()}
            <wa-button size="small" variant="brand" appearance="outlined"
                       data-action="click->info#goToUpdater" style="margin-left: auto;">
              Update
            </wa-button>
          </wa-callout>`
        : ''

      // Render device card
      this.deviceCardTarget.innerHTML = `
        ${fwBanner}
        ${assetsBanner}
        <div class="info-rows">
          <div class="info-row">
            <span class="info-label">Version</span>
            <span class="info-value">${this.infoData.version}</span>
          </div>
          <div class="info-row">
            <span class="info-label">Build</span>
            <span class="info-value">${this.infoData.build}</span>
          </div>
          <div class="info-row">
            <span class="info-label">Git</span>
            <span class="info-value mono">${this.infoData.git}</span>
          </div>
          <div class="info-row">
            <span class="info-label">Assets</span>
            <span class="info-value mono">${this.infoData.assets_checksum || '--'}</span>
          </div>
          <div class="info-row">
            <span class="info-label">Serial</span>
            <span class="info-value mono">${this.infoData.serial}</span>
          </div>
        </div>
      `

      // Render pedal card
      const pedal = this.infoData.pedal
      const trsDisplay = this.formatTrsType(pedal.trs_type)
      const bankDisplay = this.formatBankMode(pedal.bank_mode)

      const capabilities = []
      if (pedal.receives_pc) capabilities.push('PC')
      if (pedal.receives_clock) capabilities.push('Clock')
      if (pedal.receives_notes) capabilities.push('Notes')
      if (pedal.transmits_pc) capabilities.push('TX PC')

      this.pedalCardTarget.innerHTML = `
        <div class="info-rows">
          <div class="info-row">
            <span class="info-label">Name</span>
            <span class="info-value">${pedal.name}</span>
          </div>
          <div class="info-row">
            <span class="info-label">Manufacturer</span>
            <span class="info-value">${pedal.vendor}</span>
          </div>
          <div class="info-row">
            <span class="info-label">MIDI Channel</span>
            <span class="info-value">${pedal.midi_channel}</span>
          </div>
          <div class="info-row">
            <span class="info-label">TRS Type</span>
            <span class="info-value">${trsDisplay}</span>
          </div>
          <div class="info-row">
            <span class="info-label">Send Clock</span>
            <span class="info-value">${pedal.send_clock ? 'Yes' : 'No'}</span>
          </div>
          <div class="info-row">
            <span class="info-label">Capabilities</span>
            <span class="info-value">${capabilities.join(', ') || 'None'}</span>
          </div>
          <div class="info-row">
            <span class="info-label">Presets</span>
            <span class="info-value">${pedal.preset_count} (${pedal.preset_base}-based)</span>
          </div>
          <div class="info-row">
            <span class="info-label">Bank Mode</span>
            <span class="info-value">${bankDisplay}</span>
          </div>
        </div>
        <div class="info-card-actions">
          <wa-button size="small" variant="brand" appearance="outlined"
                     data-action="click->info#goToPedals">
            <wa-icon name="guitar" slot="prefix"></wa-icon>
            Change Pedal
          </wa-button>
        </div>
      `
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
        case 'none': return 'None (PC only)'
        case 'CC0': return 'CC0 + PC'
        case 'CC0_CC32': return 'CC0 + CC32 + PC'
        default: return bankMode || 'None'
      }
    }

    goToPedals() {
      document.dispatchEvent(
        new CustomEvent('app:navigate-tab', {
          detail: { tab: 'pedals', params: { slug: this.infoData?.pedal?.slug } }
        })
      )
    }

    goToUpdater() {
      document.dispatchEvent(
        new CustomEvent('app:navigate-tab', {
          detail: { tab: 'updater' }
        })
      )
    }
  }
)
