/* Storm Summoner - Config Controller (Schema-driven settings) */

application.register(
  'config',
  class extends BaseController {
    static targets = ['refreshBtn', 'container']

    connect() {
      this.schema = null
      this.values = {}
      this.inConfigMode = false
      this.rxBuffer = ''

      // Listen for connection changes
      this.connection.on('connection:changed', this.onConnectionChanged.bind(this))

      // Listen for mode changes
      this.connection.on('mode:changed', ({ mode }) => {
        if (mode !== 'CONFIG') {
          this.inConfigMode = false
        }
      })

      // Listen for tab activation
      document.addEventListener('app:tab-activated', (e) => {
        if (e.detail.tab === 'config' && this.connection.isConnected && !this.inConfigMode) {
          this.activate()
        }
      })
    }

    onConnectionChanged({ connected }) {
      this.refreshBtnTarget.disabled = !connected
      if (!connected) {
        this.inConfigMode = false
        this.renderEmpty()
      }
    }

    async activate() {
      if (!this.connection.isConnected) return

      try {
        // Load schema if not already loaded
        if (!this.schema) {
          await this.loadSchema()
        }

        const modeGranted = await this.connection.requestMode('CONFIG')
        if (!modeGranted) return

        await this.enterConfigMode()
        await this.fetchValues()
      } catch (err) {
        console.error('Config activation error:', err)
      }
    }

    async loadSchema() {
      try {
        const response = await fetch('/schemas/settings.schema.json')
        if (!response.ok) throw new Error(`HTTP ${response.status}`)
        this.schema = await response.json()
      } catch (err) {
        console.error('Failed to load schema:', err)
        this.containerTarget.innerHTML = `
          <div class="empty-state">
            <wa-icon name="triangle-exclamation"></wa-icon>
            <p>Failed to load settings schema</p>
            <p class="hint">Ensure /schemas/settings.schema.json is accessible</p>
          </div>
        `
        throw err
      }
    }

    async enterConfigMode() {
      await this.sleep(100)
      await this.connection.sendRaw('CONFIG\n')
      const response = await this.readLine(3000)

      if (response === 'CONFIG_STARTED') {
        this.inConfigMode = true
      } else {
        throw new Error(`Unexpected response: ${response}`)
      }
    }

    async readLine(timeout = 2000) {
      const reader = this.connection.port.readable.getReader()
      const decoder = new TextDecoder()
      let buffer = ''

      try {
        const startTime = Date.now()
        while (Date.now() - startTime < timeout) {
          const result = await Promise.race([
            reader.read(),
            this.sleep(50).then(() => ({ timeout: true }))
          ])
          if (result.timeout) continue
          if (result.done) break
          if (result.value) {
            const text = decoder.decode(result.value, { stream: true })
            for (const char of text) {
              if (char === '\n') {
                return buffer.replace('\r', '').trim()
              }
              buffer += char
            }
          }
        }
      } finally {
        try { reader.releaseLock() } catch (e) {}
      }
      return buffer.trim()
    }

    async fetchValues() {
      try {
        await this.connection.sendRaw('VALUES\n')
        const response = await this.readLine(5000)

        if (!response || response.startsWith('ERROR:')) {
          this.renderEmpty()
          return
        }

        this.values = JSON.parse(response)
        this.renderSettings()
      } catch (err) {
        console.error('Error fetching values:', err)
        this.renderEmpty()
      }
    }

    renderEmpty() {
      this.containerTarget.innerHTML = `
        <div class="empty-state">
          <wa-icon name="sliders"></wa-icon>
          <p>Connect to view device settings</p>
        </div>
      `
      this.values = {}
    }

    renderSettings() {
      if (!this.schema || !this.schema.categories) {
        this.renderEmpty()
        return
      }

      let html = '<div class="config-categories">'

      for (const category of this.schema.categories) {
        html += `
          <wa-details summary="${category.label}" open>
            <div class="config-category">
        `

        for (const setting of category.settings) {
          const value = this.values[setting.id]
          const isVisible = this.checkVisibility(setting)
          const visibilityStyle = isVisible ? '' : 'style="display: none;"'

          html += `
            <div class="config-setting" data-setting-id="${setting.id}" ${visibilityStyle}>
              <div class="config-setting-info">
                <label class="config-setting-label">${setting.label}</label>
                <span class="config-setting-desc">${setting.description || ''}</span>
              </div>
              <div class="config-setting-control">
                ${this.renderControl(setting, value)}
              </div>
            </div>
          `
        }

        html += `
            </div>
          </wa-details>
        `
      }

      html += '</div>'
      this.containerTarget.innerHTML = html

      // Add event listeners
      this.containerTarget.querySelectorAll('[data-config-control]').forEach((el) => {
        el.addEventListener('change', (e) => this.onValueChange(e))
      })
    }

    checkVisibility(setting) {
      // Check visible_when condition
      if (setting.visible_when) {
        return this.evaluateCondition(setting.visible_when)
      }

      // Check visible_when_any (OR condition)
      if (setting.visible_when_any) {
        return setting.visible_when_any.some((cond) => this.evaluateCondition(cond))
      }

      return true
    }

    evaluateCondition(condition) {
      const depValue = this.values[condition.id]
      let expected = condition.value

      // Handle boolean conditions: schema may use true/false but device sends 0/1
      if (expected === true) expected = 1
      else if (expected === false) expected = 0

      if (condition.operator === '!=') {
        return depValue !== expected
      }
      // Default: equality check
      return depValue === expected
    }

    renderControl(setting, value) {
      const id = `config-${setting.id.replace(/\./g, '-')}`

      switch (setting.type) {
        case 'calibration':
          return `
            <span class="config-calibration-note">
              <wa-icon name="info-circle"></wa-icon>
              Perform on device
            </span>
          `

        case 'toggle':
          const checked = value ? 'checked' : ''
          return `
            <wa-switch id="${id}" data-config-control data-setting-id="${setting.id}" ${checked}>
            </wa-switch>
          `

        case 'select':
          let optionsHtml = ''
          for (const opt of setting.options) {
            const selected = opt.value === value ? 'selected' : ''
            optionsHtml += `<wa-option value="${opt.value}" ${selected}>${opt.label}</wa-option>`
          }
          return `
            <wa-select id="${id}" data-config-control data-setting-id="${setting.id}" value="${value}">
              ${optionsHtml}
            </wa-select>
          `

        case 'number':
          const min = setting.min !== undefined ? setting.min : 0
          const max = setting.max !== undefined ? setting.max : 100
          const step = setting.step !== undefined ? setting.step : 1
          const unit = setting.unit || ''
          return `
            <wa-input type="number" id="${id}" data-config-control data-setting-id="${setting.id}"
                      value="${value}" min="${min}" max="${max}" step="${step}" size="small">
              ${unit ? `<span slot="suffix">${unit}</span>` : ''}
            </wa-input>
          `

        default:
          return `<span>${value}</span>`
      }
    }

    async onValueChange(event) {
      const el = event.target
      const settingId = el.dataset.settingId
      if (!settingId) return

      let value
      if (el.tagName === 'WA-SWITCH') {
        value = el.checked ? 1 : 0
      } else if (el.tagName === 'WA-SELECT') {
        value = parseInt(el.value)
      } else if (el.tagName === 'WA-INPUT') {
        value = parseInt(el.value)
      } else {
        return
      }

      try {
        await this.connection.sendRaw(`SET ${settingId} ${value}\n`)
        const response = await this.readLine()

        if (response === 'OK') {
          // Update local cache
          this.values[settingId] = value

          // Update visibility of dependent settings
          this.updateVisibility()
        } else {
          console.error(`Failed to set ${settingId}: ${response}`)
        }
      } catch (err) {
        console.error(`Failed to set ${settingId}:`, err)
      }
    }

    updateVisibility() {
      // Re-check visibility for all settings
      for (const category of this.schema.categories) {
        for (const setting of category.settings) {
          // Use specific selector to target wrapper div, not control element
          const el = this.containerTarget.querySelector(
            `.config-setting[data-setting-id="${setting.id}"]`
          )
          if (el) {
            const isVisible = this.checkVisibility(setting)
            el.style.display = isVisible ? '' : 'none'
          }
        }
      }
    }

    async refresh() {
      if (!this.connection.isConnected) return

      if (!this.inConfigMode) {
        await this.activate()
      } else {
        await this.fetchValues()
      }
    }
  }
)
