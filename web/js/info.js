/* Storm Summoner - Info Controller */

application.register(
  'info',
  class extends BaseController {
    static targets = ['deviceCard', 'pedalCard', 'sceneCard']

    connect () {
      this.infoData = null
      this.clockData = null
      this._transportBusy = false
      this._sceneActionBusy = false
      this._flagOverrideUntil = 0
      this._flagOverrideValue = false
      this._pendingFlagCmd = null
      this._bpmInputEditing = false
      this._bpmInputDraft = null
      this.isActivating = false
      this._activatePending = false
      this._loadGeneration = 0
      this.releases = null
      this._lastPedalSlug = null
      this._pedalSettingsBusy = false
      this._onPedalSettingsChanged = e => {
        if (this.infoData?.pedal) Object.assign(this.infoData.pedal, e.detail)
      }
      this._onPedalSettingsBusy = e => {
        this._pedalSettingsBusy = !!e.detail?.busy
      }
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
            const incoming = e.detail.clock
            const prevClock = this.clockData
            const prevFlag = prevClock?.flag
            const prevAllow = prevClock?.allow_fractional_bpm
            const overrideActive =
              this._flagOverrideUntil > 0 && Date.now() < this._flagOverrideUntil
            const appliedFlag = overrideActive ? this._flagOverrideValue : incoming.flag
            this.clockData = { ...incoming, flag: appliedFlag }
            if (this.clockData.allow_fractional_bpm == null) {
              this.clockData.allow_fractional_bpm =
                prevAllow ?? this.infoData?.clock?.allow_fractional_bpm ?? false
            }
            this.applyClockDisplay(prevClock, prevFlag, appliedFlag, incoming)
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
      document.addEventListener('info-pedal-settings:changed', this._onPedalSettingsChanged)
      document.addEventListener('info-pedal-settings:busy', this._onPedalSettingsBusy)

      document.addEventListener('updater:complete', () => {
        if (this.connection.isConnected) {
          this.activate()
        }
      })

      this.element.addEventListener('click', e => {
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

        const sceneBtnClosest = e.target.closest('[data-scene-cmd]')
        const scenePath = e.composedPath?.() ?? []
        const sceneBtn = sceneBtnClosest ||
          scenePath.find(el => el?.matches?.('[data-scene-cmd]'))
        if (sceneBtn) {
          const cmd = sceneBtn.getAttribute('data-scene-cmd')
          if (cmd) this.sendSceneAction(cmd)
        }
      })

      this.element.addEventListener('focusin', e => {
        if (this._bpmInputFromEvent(e)) this._bpmInputEditing = true
      })

      this.element.addEventListener('focusout', e => {
        const wa = this._bpmInputFromEvent(e)
        if (wa) {
          this._bpmInputEditing = false
          this._bpmInputDraft = wa.value
        }
      })

      this.element.addEventListener('input', e => {
        const wa = this._bpmInputFromEvent(e)
        if (wa) this.maskBpmInput(wa)
      })
    }

    disconnect () {
      document.removeEventListener('app:tab-activated', this._onTabActivated)
      document.removeEventListener('cdc:notify', this._onCdcNotify)
      document.removeEventListener('info-pedal-settings:changed', this._onPedalSettingsChanged)
      document.removeEventListener('info-pedal-settings:busy', this._onPedalSettingsBusy)
      if (this._notifyDebounce) clearTimeout(this._notifyDebounce)
    }

    async fetchReleases () {
      try {
        // Bypass the HTTP cache: releases.json is overwritten in place on every
        // rebuild, and a stale copy would misreport the latest available build.
        const response = await fetch('/releases.json?_=' + Date.now(), { cache: 'no-store' })
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
        this._bpmInputEditing = false
        this._bpmInputDraft = null
        this.isActivating = false
        this.renderEmpty()
      }
    }

    async activate () {
      if (!this.connection.isConnected) return
      // A load is already running. Remember the request instead of dropping it:
      // a newer trigger (e.g. app:tab-activated bumping _loadGeneration during
      // reconnect) would otherwise invalidate the in-flight load while never
      // being serviced itself, leaving the panels empty. We re-run below.
      if (this.isActivating) {
        this._activatePending = true
        return
      }

      this.isActivating = true

      try {
        do {
          this._activatePending = false
          const gen = this._loadGeneration

          let response
          try {
            response = await this.connection.runSerialTask(async () => {
              if (gen !== this._loadGeneration) return null
              await this.connection.ensureDeviceIdle({ leavePumpSuspended: true })
              if (gen !== this._loadGeneration) return null
              await this.sleep(100)
              const resp = await this.connection._sendCommandViaPump(
                'INFO',
                5000,
                data =>
                  typeof data.version === 'string' &&
                    typeof data.build === 'number'
              )
              await this.connection._releasePumpAfterCommand()
              this.connection._resumeRxPump()
              return resp
            })
          } catch (err) {
            if (!this.connection.isConnected) return
            // Stale generation: a newer load is pending and will render.
            if (gen !== this._loadGeneration) continue
            console.error('Info activation error:', err)
            this.renderEmpty()
            continue
          }

          // Generation advanced mid-load: discard this result; the pending
          // re-run will service the newest generation.
          if (gen !== this._loadGeneration) continue
          if (!this.connection.isConnected) return

          if (!response || response.startsWith('ERROR:')) {
            console.error('INFO command failed:', response)
            this.renderEmpty()
            continue
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
                assets_checksum: this.infoData.assets_checksum,
                programming: this.infoData.programming,
                pedal: this.infoData.pedal || null
              }
            })
          )
        } while (this._activatePending && this.connection.isConnected)
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
          if (this.connection.mode || this.connection._deviceScenesActive) {
            await this.connection.ensureDeviceIdle({ leavePumpSuspended: true })
          }
          return this.connection._sendOkCommandViaPump(`NAV ${op}`, 5000)
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
          if (this.connection.mode || this.connection._deviceScenesActive) {
            await this.connection.ensureDeviceIdle({ leavePumpSuspended: true })
          }
          return this.connection._sendOkCommandViaPump(`TRANSPORT ${op}`, 5000)
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

    async sendSceneAction (sceneCmd) {
      const isFlag = sceneCmd === 'flag-raise' || sceneCmd === 'flag-lower'
      if (!this.connection.isConnected) {
        console.warn('Scene command skipped: not connected')
        return
      }
      if (this._sceneActionBusy) {
        if (isFlag) this._pendingFlagCmd = sceneCmd
        console.warn('Scene command skipped: previous command in progress')
        return
      }

      let cdcCmd = null
      let bpmSetValue = null
      if (sceneCmd === 'downbeat') {
        cdcCmd = 'TEMPO DOWNBEAT'
      } else if (sceneCmd === 'flag-raise') {
        cdcCmd = 'FLAG RAISE'
      } else if (sceneCmd === 'flag-lower') {
        cdcCmd = 'FLAG LOWER'
      } else if (sceneCmd === 'bpm-set') {
        const wa = this.sceneCardTarget.querySelector('[data-scene-bpm-input]')
        const allowFrac = !!this.clockData?.allow_fractional_bpm
        const raw = wa?.value?.trim()
        if (!raw) {
          console.error('BPM value required')
          return
        }
        const num = allowFrac ? parseFloat(raw) : parseInt(raw, 10)
        if (Number.isNaN(num)) {
          console.error('Invalid BPM value')
          return
        }
        const clamped = ActionCatalog.clampBpmForDevice(num, allowFrac)
        bpmSetValue = clamped
        cdcCmd = `TEMPO BPM ${ActionCatalog.formatBpmDisplay(clamped)}`
      } else {
        return
      }

      this._sceneActionBusy = true
      const runCdc = async () => this.connection.runSerialTask(async () => {
        if (this.connection.mode || this.connection._deviceScenesActive) {
          await this.connection.ensureDeviceIdle({ leavePumpSuspended: true })
        }
        return this.connection._sendOkCommandViaPump(cdcCmd, 5000)
      })
      try {
        let response = await runCdc()
        if (isFlag && response !== 'OK' && !(response && response.startsWith('ERROR:'))) {
          for (let retry = 1; retry <= 2 && response !== 'OK'; retry++) {
            await this.connection.sleep(80)
            response = await runCdc()
          }
        }
        if (response !== 'OK') {
          const detail = response || 'no response (timeout)'
          console.warn(`Scene command ${sceneCmd} failed:`, detail)
          return
        }
        if (this.clockData) {
          if (sceneCmd === 'flag-raise') {
            this._flagOverrideValue = true
            this._flagOverrideUntil = Date.now() + 800
            this.clockData = { ...this.clockData, flag: true }
            this.renderSceneCard()
          } else if (sceneCmd === 'flag-lower') {
            this._flagOverrideValue = false
            this._flagOverrideUntil = Date.now() + 800
            this.clockData = { ...this.clockData, flag: false }
            this.renderSceneCard()
          } else if (sceneCmd === 'downbeat') {
            this.clockData = { ...this.clockData, beat: 1 }
            this.renderSceneCard()
          }
        }
        if (sceneCmd === 'bpm-set') {
          this._bpmInputDraft = null
          this._bpmInputEditing = false
          if (bpmSetValue != null && this.clockData) {
            this.clockData = { ...this.clockData, bpm: bpmSetValue }
            this.renderSceneCard()
          }
        }
      } catch (err) {
        console.error('Scene command error:', err)
      } finally {
        this._sceneActionBusy = false
        const pending = this._pendingFlagCmd
        if (pending) {
          this._pendingFlagCmd = null
          void this.sendSceneAction(pending)
        }
      }
    }

    applyClockDisplay (prevClock, prevFlag, appliedFlag, incoming) {
      const flagChanged = !!prevFlag !== !!appliedFlag
      const transportChanged =
        (prevClock?.transport ?? null) !== (incoming.transport ?? null)
      const beatChanged =
        (prevClock?.beat ?? null) !== (incoming.beat ?? null) ||
        (prevClock?.bar ?? null) !== (incoming.bar ?? null)
      const bpmChanged = (prevClock?.bpm ?? null) !== (incoming.bpm ?? null)

      if (flagChanged || transportChanged || bpmChanged ||
          !this.sceneCardTarget.querySelector('[data-scene-flag-status]')) {
        this.renderSceneCard()
        return
      }

      if (beatChanged) {
        const beatEl = this.sceneCardTarget.querySelector('[data-scene-beat-display]')
        if (beatEl) beatEl.textContent = String(incoming.beat ?? 1)
        const posEl = this.sceneCardTarget.querySelector('[data-scene-position-display]')
        if (posEl) {
          posEl.textContent = `Bar ${incoming.bar ?? 1}, Beat ${incoming.beat ?? 1}`
        }
      }
    }

    getSceneBpmInputValue (clock) {
      if (this._bpmInputEditing && this._bpmInputDraft != null) return this._bpmInputDraft
      return ActionCatalog.formatBpmDisplay(clock?.bpm ?? 120)
    }

    _bpmInputFromEvent (e) {
      const path = e.composedPath?.() ?? []
      return path.find(el => el?.matches?.('[data-scene-bpm-input]')) ?? null
    }

    maskBpmInput (wa) {
      const allowFrac = !!this.clockData?.allow_fractional_bpm
      let raw = String(wa.value ?? '')
      if (allowFrac) {
        raw = raw.replace(/[^\d.]/g, '')
        const dot = raw.indexOf('.')
        if (dot >= 0) {
          const whole = raw.slice(0, dot)
          const frac = raw.slice(dot + 1).replace(/\./g, '').slice(0, 1)
          raw = frac.length ? `${whole}.${frac}` : `${whole}.`
        }
      } else {
        raw = raw.replace(/\D/g, '')
      }
      wa.value = raw
      this._bpmInputDraft = raw
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
        this._lastPedalSlug = null
        this.pedalCardTarget.innerHTML = `
          <div class="empty-state">
            <p>Pedal info unavailable</p>
          </div>
        `
      } else {
        this.renderPedalCard(pedal)
      }

      this.renderSceneCard()
    }

    renderPedalCard (pedal) {
      const card = this.pedalCardTarget
      const slugChanged = this._lastPedalSlug !== pedal.slug
      const skipSettings = !slugChanged && this._pedalSettingsBusy
      const staticEl = card.querySelector('[data-info-pedal-static]')
      const settingsHost = card.querySelector('[data-info-pedal-settings-host]')

      if (!staticEl || !settingsHost) {
        card.innerHTML = this.buildPedalCardHtml(pedal)
        this._lastPedalSlug = pedal.slug
        return
      }

      staticEl.outerHTML = this.buildPedalStaticHtml(pedal)
      if (!skipSettings) {
        settingsHost.outerHTML = this.buildPedalSettingsHtml(pedal)
      }
      this._lastPedalSlug = pedal.slug
    }

    buildPedalCardHtml (pedal) {
      return `
        ${this.buildPedalStaticHtml(pedal)}
        ${this.buildPedalSettingsHtml(pedal)}
      `
    }

    buildPedalStaticHtml (pedal) {
      const capabilities = []
      if (pedal.receives_pc) capabilities.push('PC')
      if (pedal.receives_clock) capabilities.push('Clock')
      if (pedal.receives_notes) capabilities.push('Notes')
      if (pedal.transmits_pc) capabilities.push('TX PC')
      const bankDisplay = this.formatBankMode(pedal.bank_mode)
      const ccCount = pedal.cc_count != null ? pedal.cc_count : '—'

      return `
        <div class="info-rows" data-info-pedal-static>
          <div class="info-row">
            <span class="info-label">Name</span>
            <span class="info-value">${pedal.name}</span>
          </div>
          <div class="info-row">
            <span class="info-label">Manufacturer</span>
            <span class="info-value">${pedal.vendor}</span>
          </div>
          <div class="info-row">
            <span class="info-label">Capabilities</span>
            <span class="info-value">${capabilities.join(', ') || 'None'}</span>
          </div>
          <div class="info-row">
            <span class="info-label">Presets</span>
            <span class="info-value">${pedal.preset_count} (${
        pedal.preset_base
      }-based)</span>
          </div>
          <div class="info-row">
            <span class="info-label">CC commands</span>
            <span class="info-value">${ccCount}</span>
          </div>
          <div class="info-row">
            <span class="info-label">Bank Mode</span>
            <span class="info-value">${bankDisplay}</span>
          </div>
        </div>
      `
    }

    buildPedalSettingsHtml (pedal) {
      const ch = pedal.midi_channel ?? 1
      const trs = pedal.trs_type || 'TYPE_A'
      const sendClock = !!pedal.send_clock
      const formatTrs = window.PedalCatalog?.formatTrsLabel ||
        (t => this.formatTrsType(t))

      let channelOptions = ''
      for (let n = 1; n <= 16; n++) {
        const selected = n === ch ? 'selected' : ''
        channelOptions += `<wa-option value="${n}" ${selected}>${n}</wa-option>`
      }

      const trsTypes = ['TYPE_A', 'TYPE_B', 'TYPE_TS', 'BOTH']
      let trsOptions = ''
      for (const t of trsTypes) {
        const selected = t === trs ? 'selected' : ''
        trsOptions += `<wa-option value="${t}" ${selected}>${formatTrs(t)}</wa-option>`
      }

      const clockVal = sendClock ? '1' : '0'
      const clockOptions = `
        <wa-option value="1" ${sendClock ? 'selected' : ''}>Yes</wa-option>
        <wa-option value="0" ${!sendClock ? 'selected' : ''}>No</wa-option>
      `

      return `
        <div class="info-pedal-settings"
             data-info-pedal-settings-host
             data-controller="info-pedal-settings"
             data-info-pedal-settings-midi-channel-value="${ch}"
             data-info-pedal-settings-trs-type-value="${trs}"
             data-info-pedal-settings-send-clock-value="${sendClock}">
          <div class="info-row info-row-control">
            <span class="info-label">MIDI Channel</span>
            <wa-select size="small" value="${ch}" data-pedal-field="midiChannel">
              ${channelOptions}
            </wa-select>
          </div>
          <div class="info-row info-row-control">
            <span class="info-label">TRS Type</span>
            <wa-select size="small" value="${trs}" data-pedal-field="trsType">
              ${trsOptions}
            </wa-select>
          </div>
          <div class="info-row info-row-control">
            <span class="info-label">Send Clock</span>
            <wa-select size="small" value="${clockVal}" data-pedal-field="sendClock">
              ${clockOptions}
            </wa-select>
          </div>
        </div>
      `
    }

    renderSceneClockRows () {
      const clock = this.clockData
      if (!clock) return ''

      const ts = clock.time_signature || {}
      const num = ts.numerator || 4
      const den = ts.denominator || 4
      const useTransport = !!clock.use_transport
      const allowFrac = !!clock.allow_fractional_bpm
      const bpmInputVal = this.getSceneBpmInputValue(clock)

      const downbeatBtn = `
            <wa-button class="info-scene-btn" variant="neutral" appearance="outlined"
                       data-scene-cmd="downbeat" title="Reset beat to 1">
              Downbeat
            </wa-button>`

      const sceneActionRow = (label, btnHtml, statusHtml) => `
        <div class="info-row info-row-scene info-row-scene-3col">
          <span class="info-label">${label}</span>
          <div class="info-scene-mid">${btnHtml}</div>
          <span class="info-value info-scene-status">${statusHtml}</span>
        </div>`

      const flagRow = () => {
        if (!clock.flag_enabled) return ''
        const raised = !!clock.flag
        const icon = raised ? 'house-flag' : 'house-chimney-window'
        const label = raised ? 'Raised' : 'Lowered'
        const flagCmd = raised ? 'flag-lower' : 'flag-raise'
        const flagBtnLabel = raised ? 'Lower' : 'Raise'
        const flagBtn = `
            <wa-button class="info-scene-btn" variant="neutral" appearance="outlined"
                       data-scene-cmd="${flagCmd}">
              ${flagBtnLabel}
            </wa-button>`
        const flagStatus = `
            <span class="info-flag-value" data-scene-cmd="${flagCmd}" data-scene-flag-status role="button">
              ${label} <wa-icon name="${icon}"></wa-icon>
            </span>`
        return sceneActionRow('Flag', flagBtn, flagStatus)
      }

      let rows = `
        <div class="info-row info-row-scene">
          <span class="info-label">BPM</span>
          <div class="info-scene-value">
            <span class="info-value" data-scene-bpm-display>${
              ActionCatalog.formatBpmDisplay(clock.bpm)
            }</span>
            <span class="info-scene-inline">
              <wa-icon name="arrow-right" class="info-scene-arrow"></wa-icon>
              <wa-input class="info-scene-input" inputmode="decimal"
                        data-scene-bpm-input
                        data-allow-fractional="${allowFrac ? '1' : '0'}"
                        value="${bpmInputVal}"></wa-input>
              <wa-button class="info-scene-btn" variant="brand" appearance="outlined"
                         data-scene-cmd="bpm-set">
                Set
              </wa-button>
            </span>
          </div>
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
        ${sceneActionRow(
          'Position',
          downbeatBtn,
          `<span data-scene-position-display>Bar ${clock.bar ?? 1}, Beat ${clock.beat ?? 1}</span>`
        )}
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
        ${sceneActionRow('Beat', downbeatBtn, `<span data-scene-beat-display>${clock.beat ?? 1}</span>`)}
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
          total > 0 ? `${ordinal} of ${total}` : '—'

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

    goToUpdater () {
      document.dispatchEvent(
        new CustomEvent('app:navigate-tab', {
          detail: { tab: 'updater' }
        })
      )
    }
  }
)
