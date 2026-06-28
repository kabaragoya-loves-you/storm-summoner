/* Storm Summoner - Settings Controller */

application.register(
  'settings',
  class extends BaseController {
    static targets = [
      'refreshBtn',
      'downloadBtn',
      'uploadBtn',
      'jsonInput',
      'settingsList',
      'logContent'
    ]

    connect () {
      this.reader = null
      this.readLoopActive = false
      this.inSettingsMode = false
      this.settings = {}
      this.rxBuffer = ''

      // Listen for connection changes
      this.connection.on(
        'connection:changed',
        this.onConnectionChanged.bind(this)
      )

      // Listen for mode changes
      this.connection.on('mode:changed', ({ mode }) => {
        if (mode !== 'SETTINGS') this.inSettingsMode = false
      })

      // Listen for tab activation
      document.addEventListener('app:tab-activated', e => {
        if (
          e.detail.tab === 'settings' &&
          this.connection.isConnected &&
          !this.inSettingsMode
        ) {
          this.activate()
        } else if (e.detail.tab !== 'settings' && this.inSettingsMode) {
          void this.leaveSettingsMode()
        }
      })
    }

    disconnect () {
      this.inSettingsMode = false
    }

    onConnectionChanged ({ connected }) {
      this.setControlsEnabled(connected)
      if (!connected) {
        this.inSettingsMode = false
        this.renderEmpty()
      }
    }

    setControlsEnabled (enabled) {
      this.refreshBtnTarget.disabled = !enabled
      this.downloadBtnTarget.disabled =
        !enabled || Object.keys(this.settings).length === 0
      this.uploadBtnTarget.disabled = !enabled
    }

    async activate () {
      if (!this.connection.isConnected) {
        this.log('Connect to device first', 'error')
        return
      }

      try {
        await this.connection.runSerialTask(async () => {
          await this.connection.ensureDeviceIdle()
          this.connection.clearPendingRx()
          if (this.connection._pumpSuspended || !this.connection._rxPumpRunning) {
            this.connection._resumeRxPump()
          }
          await this.connection._armRxPump(2000)
          await this.enterSettingsModeBody()
          await this.fetchSettingsBody()
        })
      } catch (err) {
        this.log(`Failed to activate: ${err.message}`, 'error')
      }
    }

    async leaveSettingsMode () {
      if (!this.inSettingsMode) return
      try {
        await this.connection.runSerialTask(async () => {
          await this.connection.ensureDeviceIdle()
          await this.connection.sendRaw('EXIT\n')
          await this.connection._waitForSerialBanner('SETTINGS_STOPPED', 3000)
          this.connection.clearPendingRx()
        })
      } catch (err) {
        console.warn('Settings leave mode:', err)
      }
      this.inSettingsMode = false
    }

    async enterSettingsModeBody () {
      this.log('Entering settings mode...')
      await this.sleep(100)
      let response = ''
      for (let attempt = 0; attempt < 2; attempt++) {
        if (attempt > 0) {
          await this.connection.ensureDeviceIdle()
          this.connection.clearPendingRx()
        }
        await this.connection.sendRaw('SETTINGS\n')
        response = await this.connection._waitForSerialBanner('SETTINGS_STARTED', 5000)
        if (response === 'SETTINGS_STARTED') break
      }
      if (response !== 'SETTINGS_STARTED') {
        throw new Error(`Unexpected response: ${response}`)
      }
      this.inSettingsMode = true
      this.log('Settings mode active')
    }

    async fetchSettingsBody () {
      this.log('Fetching settings...')
      await this.connection.sendRaw('DUMP\n')
      const response = await this.connection._readJsonLineViaPump(10000)

      if (!response) {
        this.log('No response from device', 'error')
        this.renderEmpty()
        return
      }

      if (response.startsWith('ERROR:')) {
        this.log(response, 'error')
        this.renderEmpty()
        return
      }

      try {
        this.settings = JSON.parse(response)
        this.renderSettings()
        this.log(`Loaded ${Object.keys(this.settings).length} settings`)
        this.setControlsEnabled(true)
      } catch (e) {
        this.log('Failed to parse settings JSON', 'error')
        console.error(
          'JSON parse error:',
          e,
          'Response:',
          response.substring(0, 200)
        )
        this.renderEmpty()
      }
    }

    async fetchSettings () {
      try {
        await this.connection.runSerialTask(() => this.fetchSettingsBody())
      } catch (err) {
        this.log(`Error fetching settings: ${err.message}`, 'error')
        this.renderEmpty()
      }
    }

    renderEmpty () {
      this.settingsListTarget.innerHTML = `
        <div class="empty-state">
          <wa-icon name="sliders"></wa-icon>
          <p>Connect to view settings</p>
        </div>
      `
      this.settings = {}
      this.setControlsEnabled(this.connection.isConnected)
    }

    renderSettings () {
      if (Object.keys(this.settings).length === 0) {
        this.settingsListTarget.innerHTML = `
          <div class="empty-state">
            <wa-icon name="sliders"></wa-icon>
            <p>No settings found</p>
          </div>
        `
        return
      }

      let html = '<div class="settings-grid">'

      for (const [key, value] of Object.entries(this.settings)) {
        const type = this.detectType(value)
        const inputHtml = this.renderInput(key, value, type)

        html += `
          <div class="setting-row">
            <label class="setting-key" title="${key}">${key}</label>
            <div class="setting-value">${inputHtml}</div>
            <span class="setting-type">${type}</span>
          </div>
        `
      }

      html += '</div>'
      this.settingsListTarget.innerHTML = html

      // Add event listeners
      this.settingsListTarget.querySelectorAll('input, select').forEach(el => {
        el.addEventListener('change', e => this.onValueChange(e))
      })
    }

    detectType (value) {
      if (typeof value === 'boolean') return 'bool'
      if (typeof value === 'number') {
        if (Number.isInteger(value)) {
          if (value >= 0 && value <= 255) return 'u8'
          if (value >= 0 && value <= 65535) return 'u16'
          return 'u32'
        }
        return 'number'
      }
      if (typeof value === 'string') return 'str'
      if (typeof value === 'object' && value._blob) return 'blob'
      return 'unknown'
    }

    renderInput (key, value, type) {
      const id = `setting-${key.replace(/[^a-zA-Z0-9]/g, '_')}`

      switch (type) {
        case 'bool':
          return `
            <select id="${id}" data-key="${key}" data-type="${type}">
              <option value="true" ${value ? 'selected' : ''}>true</option>
              <option value="false" ${!value ? 'selected' : ''}>false</option>
            </select>
          `
        case 'blob':
          return `<input type="text" id="${id}" value="${value._blob}" readonly 
                         title="Blob (base64) - edit via CLI">`
        case 'str':
          return `<input type="text" id="${id}" data-key="${key}" data-type="${type}" 
                         value="${this.escapeHtml(value)}">`
        default:
          return `<input type="number" id="${id}" data-key="${key}" data-type="${type}" 
                         value="${value}">`
      }
    }

    escapeHtml (str) {
      return str
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;')
    }

    async onValueChange (event) {
      const el = event.target
      const key = el.dataset.key
      const type = el.dataset.type
      let value = el.value

      if (!key || !type) return

      this.log(`Setting ${key} = ${value}`)

      try {
        await this.connection.runSerialTask(async () => {
          await this.connection.sendRaw(`SET ${type} ${key} ${value}\n`)
          const response = await this.connection._readLineBody(3000)

          if (response === 'OK') {
            if (type === 'bool') {
              this.settings[key] = value === 'true'
            } else if (type === 'str') {
              this.settings[key] = value
            } else {
              this.settings[key] = parseInt(value, 10)
            }
            this.log(`${key} updated`)
          } else {
            this.log(`Failed: ${response}`, 'error')
          }
        })
      } catch (err) {
        this.log(`Failed to set ${key}: ${err.message}`, 'error')
      }
    }

    async refresh () {
      if (!this.connection.isConnected) return

      if (!this.inSettingsMode) {
        await this.activate()
      } else {
        await this.fetchSettings()
      }
    }

    downloadJson () {
      if (Object.keys(this.settings).length === 0) {
        this.log('No settings to export', 'error')
        return
      }

      const json = JSON.stringify(this.settings, null, 2)
      const blob = new Blob([json], { type: 'application/json' })
      const url = URL.createObjectURL(blob)

      const a = document.createElement('a')
      a.href = url
      a.download = 'storm-summoner-settings.json'
      a.click()

      URL.revokeObjectURL(url)
      this.log('Settings exported to JSON file')
    }

    selectJsonFile () {
      this.jsonInputTarget.click()
    }

    async importJson (event) {
      const file = event.target.files[0]
      if (!file) return

      try {
        const text = await file.text()
        const json = JSON.parse(text)

        if (typeof json !== 'object' || Array.isArray(json)) {
          throw new Error('Invalid JSON format')
        }

        this.log(`Importing ${Object.keys(json).length} settings...`)

        // Send individual SET commands for each key
        let count = 0
        let errors = 0

        await this.connection.runSerialTask(async () => {
          for (const [key, value] of Object.entries(json)) {
            let type, valStr

            if (typeof value === 'boolean') {
              type = 'bool'
              valStr = value.toString()
            } else if (typeof value === 'number' && Number.isInteger(value)) {
              if (value >= 0 && value <= 255) type = 'u8'
              else if (value >= 0 && value <= 65535) type = 'u16'
              else type = 'u32'
              valStr = value.toString()
            } else if (typeof value === 'string') {
              type = 'str'
              valStr = value
            } else if (typeof value === 'object' && value._blob) {
              type = 'blob'
              valStr = value._blob
            } else {
              this.log(`Skipping ${key}: unsupported type`, 'warn')
              continue
            }

            await this.connection.sendRaw(`SET ${type} ${key} ${valStr}\n`)
            const response = await this.connection._readLineBody(1000)

            if (response === 'OK') count++
            else {
              errors++
              this.log(`Failed to set ${key}: ${response}`, 'error')
            }
          }
        })

        this.log(
          `Imported ${count} settings` +
            (errors > 0 ? ` (${errors} errors)` : '')
        )

        // Refresh to show updated values
        await this.fetchSettings()
      } catch (err) {
        this.log(`Import failed: ${err.message}`, 'error')
      }

      // Reset file input
      event.target.value = ''
    }
  }
)
