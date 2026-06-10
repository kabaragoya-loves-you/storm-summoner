/* Storm Summoner - Info Controller */

application.register(
  'info',
  class extends BaseController {
    static targets = ['deviceCard', 'pedalCard', 'sceneCard']

    connect () {
      this.infoData = null
      this.clockData = null
      this._transportBusy = false
      this.isActivating = false
      this._loadGeneration = 0
      this.releases = null
      this._onTabActivated = e => {
        if (e.detail.tab === 'info' && this.connection.isConnected) {
          this._loadGeneration++
          this.activate()
        }
      }
      this._notifyDebounce = null
      this._onCdcNotify = e => {
        const kind = e.detail?.kind
        if (kind === 'clock') {
          if (!this.connection.isConnected) return
          if (e.detail.clock) {
            this.clockData = e.detail.clock
            this.renderSceneCard()
          }
          return
        }
        if (kind === 'connections') {
          if (!this.connection.isConnected) return
          if (e.detail.connections) {
            if (!this.infoData) this.infoData = {}
            this.infoData.connections = e.detail.connections
            this.renderDeviceConnections()
          }
          return
        }
        if (
          kind !== 'scene_changed' &&
          kind !== 'scene_updated' &&
          kind !== 'scene_list_changed' &&
          kind !== 'scene_reordered'
        )
          return
        if (!this.connection.isConnected) return
        const activeTab = document.querySelector('wa-tab-group wa-tab[active]')
        if (activeTab?.getAttribute('panel') !== 'info') return
        if (this._notifyDebounce) clearTimeout(this._notifyDebounce)
        this._notifyDebounce = setTimeout(() => {
          this._notifyDebounce = null
          this.activate()
        }, 200)
      }

      this.fetchReleases()

      this.connection.on(
        'connection:changed',
        this.onConnectionChanged.bind(this)
      )

      document.addEventListener('app:tab-activated', this._onTabActivated)
      document.addEventListener('cdc:notify', this._onCdcNotify)

      document.addEventListener('updater:complete', () => {
        if (this.connection.isConnected) {
          this.activate()
        }
      })

      this.element.addEventListener('click', e => {
        const btn = e.target.closest('[data-action*="goToPedals"]')
        if (btn) this.goToPedals()

        const updateBtn = e.target.closest('[data-action*="goToUpdater"]')
        if (updateBtn) this.goToUpdater()

        const transportBtn = e.target.closest('[data-transport-cmd]')
        if (transportBtn) {
          const cmd = transportBtn.getAttribute('data-transport-cmd')
          if (cmd) this.sendTransport(cmd)
        }

        const navBtn = e.target.closest('[data-nav-cmd]')
        if (navBtn) {
          const cmd = navBtn.getAttribute('data-nav-cmd')
          if (cmd) this.sendNav(cmd)
        }
      })
    }

    disconnect () {
      document.removeEventListener('app:tab-activated', this._onTabActivated)
      document.removeEventListener('cdc:notify', this._onCdcNotify)
      if (this._notifyDebounce) clearTimeout(this._notifyDebounce)
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

    onConnectionChanged ({ connected }) {
      if (connected) {
        const tabGroup = document.querySelector('wa-tab-group')
        const activePanel = tabGroup?.querySelector('wa-tab-panel[active]')
        if (activePanel?.getAttribute('name') === 'info') {
          this.activate()
        }
      } else {
        this.infoData = null
        this.clockData = null
        this.isActivating = false
        this.renderEmpty()
      }
    }

    async activate () {
      if (!this.connection.isConnected) return
      if (this.isActivating) return

      const gen = this._loadGeneration
      this.isActivating = true

      try {
        const response = await this.connection.runSerialTask(async () => {
          if (gen !== this._loadGeneration) return null
          await this.connection.ensureDeviceIdle()
          if (gen !== this._loadGeneration) return null
          await this.sleep(100)
          return this.connection._sendCommandImpl(
            'INFO',
            5000,
            data =>
              typeof data.version === 'string' && typeof data.build === 'number'
          )
        })

        if (gen !== this._loadGeneration) return

        if (!response || response.startsWith('ERROR:')) {
          console.error('INFO command failed:', response)
          this.renderEmpty()
          return
        }

        this.infoData = JSON.parse(response)
        if (this.infoData.clock) this.clockData = this.infoData.clock
        console.log('Device INFO:', this.infoData)
        this.renderInfo()

        document.dispatchEvent(
          new CustomEvent('device:info', {
            detail: {
              version: this.infoData.version,
              build: this.infoData.build,
              git: this.infoData.git,
              assets_checksum: this.infoData.assets_checksum
            }
          })
        )
      } catch (err) {
        if (gen !== this._loadGeneration) return
        console.error('Info activation error:', err)
        this.renderEmpty()
      } finally {
        this.isActivating = false
      }
    }

    async sendNav (cmd) {
      if (!this.connection.isConnected) return
      const op = cmd.toUpperCase()
      if (op !== 'PREV' && op !== 'NEXT') return
      try {
        const response = await this.connection.runSerialTask(async () => {
          await this.connection.ensureDeviceIdle()
          return this.connection._sendCommandImpl(`NAV ${op}`, 3000)
        })
        if (response !== 'OK') {
          console.error('Navigation command failed:', response)
          return
        }
        await this.activate()
      } catch (err) {
        console.error('Navigation command error:', err)
      }
    }

    async sendTransport (cmd) {
      if (!this.connection.isConnected || this._transportBusy) return
      const op = cmd.toUpperCase()
      this._transportBusy = true
      try {
        const response = await this.connection.runSerialTask(async () => {
          await this.connection.ensureDeviceIdle()
          return this.connection._sendCommandImpl(`TRANSPORT ${op}`, 3000)
        })
        if (response !== 'OK') {
          console.error('Transport command failed:', response)
        }
      } catch (err) {
        console.error('Transport command error:', err)
      } finally {
        this._transportBusy = false
      }
    }

    renderEmpty () {
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
      this.sceneCardTarget.innerHTML = `
        <div class="empty-state">
          <wa-icon name="link-slash"></wa-icon>
          <p>Connect to view scene info</p>
        </div>
      `
    }

    getLatestVersion () {
      if (!this.releases?.firmware?.length) return null
      return this.releases.firmware[0].version
    }

    hasNewerFirmware () {
      if (!this.infoData?.version || !this.releases?.firmware?.length)
        return false

      const current = this.parseVersion(this.infoData.version)
      const latest = this.parseVersion(this.releases.firmware[0].version)

      return (
        latest.major > current.major ||
        (latest.major === current.major && latest.minor > current.minor)
      )
    }

    getLatestAssetsChecksum () {
      if (!this.releases?.assets?.length) return null
      return this.releases.assets[0].checksum
    }

    hasNewerAssets () {
      if (!this.infoData?.assets_checksum || !this.releases?.assets?.length)
        return false
      if (this.infoData.assets_checksum === 'unknown') return true
      return this.infoData.assets_checksum !== this.releases.assets[0].checksum
    }

    parseVersion (str) {
      const parts = str.split('.').map(Number)
      return {
        major: parts[0] || 0,
        minor: parts[1] || 0
      }
    }

    formatJackStatus (connected) {
      const on = !!connected
      return `<span class="jack-status ${on ? 'jack-connected' : 'jack-disconnected'}">${
        on ? 'Connected' : 'Disconnected'
      }</span>`
    }

    renderDeviceConnectionsHtml () {
      const c = this.infoData?.connections || {}
      let html = `
        <div class="info-row">
          <span class="info-label">USB</span>
          <span class="info-value">${this.formatJackStatus(c.usb)}</span>
        </div>
        <div class="info-row">
          <span class="info-label">CV In</span>
          <span class="info-value">${this.formatJackStatus(c.cv)}</span>
        </div>
        <div class="info-row">
          <span class="info-label">Expression</span>
          <span class="info-value">${this.formatJackStatus(c.expression)}</span>
        </div>
        <div class="info-row">
          <span class="info-label">MIDI In</span>
          <span class="info-value">${this.formatJackStatus(c.midi_in)}</span>
        </div>`
      if (this.infoData?.cv_range) {
        html += `
        <div class="info-row">
          <span class="info-label">CV range</span>
          <span class="info-value">${this.infoData.cv_range}</span>
        </div>`
      }
      return html
    }

    renderDeviceConnections () {
      const block = this.deviceCardTarget.querySelector('[data-info-connections]')
      if (block) block.innerHTML = this.renderDeviceConnectionsHtml()
    }

    renderInfo () {
      if (!this.infoData) {
        this.renderEmpty()
        return
      }

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
            <wa-icon name="circle-arrow-up" slot="icon"></wa-icon>
            <strong>Assets update:</strong> ${this.getLatestAssetsChecksum()}
            <wa-button size="small" variant="brand" appearance="outlined"
                       data-action="click->info#goToUpdater" style="margin-left: auto;">
              Update
            </wa-button>
          </wa-callout>`
        : ''

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
            <span class="info-value mono">${this.formatAssetsChecksum(
              this.infoData.assets_checksum
            )}</span>
          </div>
          <div class="info-row">
            <span class="info-label">Serial</span>
            <span class="info-value mono">${this.infoData.serial}</span>
          </div>
          <div class="info-subsection" data-info-connections>
            ${this.renderDeviceConnectionsHtml()}
          </div>
        </div>
      `

      const pedal = this.infoData.pedal
      if (!pedal) {
        this.pedalCardTarget.innerHTML = `
          <div class="empty-state">
            <p>Pedal info unavailable</p>
          </div>
        `
      } else {
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
              <span class="info-value">${
                capabilities.join(', ') || 'None'
              }</span>
            </div>
            <div class="info-row">
              <span class="info-label">Presets</span>
              <span class="info-value">${pedal.preset_count} (${
          pedal.preset_base
        }-based)</span>
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

      this.renderSceneCard()
    }

    renderSceneClockRows () {
      const clock = this.clockData
      if (!clock) return ''

      const ts = clock.time_signature || {}
      const num = ts.numerator || 4
      const den = ts.denominator || 4
      const useTransport = !!clock.use_transport

      const flagRow = () => {
        if (!clock.flag_enabled) return ''
        const raised = !!clock.flag
        const icon = raised ? 'house-flag' : 'house-chimney-window'
        const label = raised ? 'Raised' : 'Lowered'
        return `
        <div class="info-row">
          <span class="info-label">Flag</span>
          <span class="info-value info-flag-value">
            <wa-icon name="${icon}"></wa-icon> ${label}
          </span>
        </div>`
      }

      let rows = `
        <div class="info-row">
          <span class="info-label">BPM</span>
          <span class="info-value">${clock.bpm ?? '--'}</span>
        </div>
        <div class="info-row">
          <span class="info-label">Time Signature</span>
          <span class="info-value">${num}/${den}</span>
        </div>`

      if (useTransport) {
        const playing = clock.transport === 'playing'
        const transportClass = playing ? 'clock-playing' : 'clock-stopped'
        rows += `
        <div class="info-row">
          <span class="info-label">Transport</span>
          <span class="info-value ${transportClass}">${
          playing ? 'Playing' : 'Stopped'
        }</span>
        </div>
        <div class="info-row">
          <span class="info-label">Position</span>
          <span class="info-value">Bar ${clock.bar ?? 1}, Beat ${
          clock.beat ?? 1
        }</span>
        </div>
        ${flagRow()}
        <div class="info-transport-actions">
          <wa-button size="small" variant="brand"
                     appearance="${playing ? 'filled' : 'outlined'}"
                     data-transport-cmd="play"
                     title="${playing ? 'Restart from top' : 'Play'}">
            <wa-icon name="play" slot="prefix"></wa-icon>
            Play
          </wa-button>
          <wa-button size="small" variant="neutral"
                     appearance="${playing ? 'outlined' : 'filled'}"
                     data-transport-cmd="stop"
                     title="Stop">
            <wa-icon name="stop" slot="prefix"></wa-icon>
            Stop
          </wa-button>
          <wa-button size="small" variant="danger" appearance="outlined"
                     data-transport-cmd="record"
                     title="${playing ? 'Punch-in (MMC strobe)' : 'Play + record strobe'}">
            <wa-icon name="circle" slot="prefix"></wa-icon>
            Record
          </wa-button>
        </div>`
      } else {
        rows += `
        <div class="info-row">
          <span class="info-label">Beat</span>
          <span class="info-value">${clock.beat ?? 1}</span>
        </div>
        ${flagRow()}`
      }

      return rows
    }

    renderSceneNavRow () {
      const mode = this.infoData?.scene?.mode
      if (!mode || mode === 'single') return ''

      const isPreset = mode === 'preset_sync'
      const prevLabel = isPreset ? 'Prev Preset' : 'Prev Scene'
      const nextLabel = isPreset ? 'Next Preset' : 'Next Scene'

      return `
        <div class="info-nav-actions">
          <wa-button size="small" variant="neutral" appearance="outlined"
                     data-nav-cmd="prev">
            <wa-icon name="chevron-left" slot="prefix"></wa-icon>
            ${prevLabel}
          </wa-button>
          <wa-button size="small" variant="neutral" appearance="outlined"
                     data-nav-cmd="next">
            ${nextLabel}
            <wa-icon name="chevron-right" slot="suffix"></wa-icon>
          </wa-button>
        </div>`
    }

    renderSceneCard () {
      const scene = this.infoData?.scene
      const clockRows = this.renderSceneClockRows()
      const navRow = this.renderSceneNavRow()

      if (!scene && !clockRows && !navRow) {
        this.sceneCardTarget.innerHTML = `
          <div class="empty-state">
            <p>No scene loaded</p>
          </div>
        `
        return
      }

      let sceneRows = ''
      if (scene) {
        const name = scene.name || 'Untitled'
        const ordinal = scene.active_ordinal || 0
        const total = scene.active_count || 0
        const positionLine =
          total > 0 ? `Scene ${ordinal} of ${total}` : 'Scene —'

        sceneRows = `
          <div class="info-row">
            <span class="info-label">Name</span>
            <span class="info-value">${name}</span>
          </div>
          <div class="info-row">
            <span class="info-label">Scene</span>
            <span class="info-value">${positionLine}</span>
          </div>`
      }

      this.sceneCardTarget.innerHTML = `
        ${navRow}
        <div class="info-rows">
          ${sceneRows}
          ${clockRows}
        </div>
      `
    }

    formatTrsType (trsType) {
      switch (trsType) {
        case 'TYPE_A':
          return 'Type A (Tip)'
        case 'TYPE_B':
          return 'Type B (Ring)'
        case 'TYPE_TS':
          return 'TS (Tip/Sleeve)'
        case 'BOTH':
          return 'Both A & B'
        default:
          return trsType || 'Unknown'
      }
    }

    formatBankMode (bankMode) {
      switch (bankMode) {
        case 'none':
          return 'None (PC only)'
        case 'CC0':
          return 'CC0 + PC'
        case 'CC0_CC32':
          return 'CC0 + CC32 + PC'
        default:
          return bankMode || 'None'
      }
    }

    formatAssetsChecksum (checksum) {
      if (!checksum) return '--'
      return checksum
    }

    goToPedals () {
      document.dispatchEvent(
        new CustomEvent('app:navigate-tab', {
          detail: {
            tab: 'pedals',
            params: { slug: this.infoData?.pedal?.slug }
          }
        })
      )
    }

    goToUpdater () {
      document.dispatchEvent(
        new CustomEvent('app:navigate-tab', {
          detail: { tab: 'updater' }
        })
      )
    }
  }
)
