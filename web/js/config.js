/* Storm Summoner - Config Controller (Schema-driven settings) */

application.register(
  'config',
  class extends BaseController {
    static targets = ['refreshBtn', 'container']

    connect () {
      this.schema = null
      this.values = {}
      this.inConfigMode = false
      this._midiNoteOptions = null

      this.connection.on('connection:changed', this.onConnectionChanged.bind(this))

      this.connection.on('mode:changed', ({ mode }) => {
        if (mode !== 'CONFIG') {
          this.inConfigMode = false
        }
      })

      document.addEventListener('app:tab-activated', (e) => {
        if (e.detail.tab === 'config' && this.connection.isConnected && !this.inConfigMode) {
          this.activate()
        } else if (
          e.detail.tab !== 'config' &&
          (this.inConfigMode || this.connection.currentMode === 'CONFIG')
        ) {
          this.leaveConfigMode()
        }
      })
    }

    onConnectionChanged ({ connected }) {
      this.refreshBtnTarget.disabled = !connected
      if (!connected) {
        this.inConfigMode = false
        this.renderEmpty()
      }
    }

    async leaveConfigMode () {
      if (!this.inConfigMode && this.connection.currentMode !== 'CONFIG') return
      try {
        if (this.connection.currentMode) {
          await this.connection.exitMode()
          await this.sleep(200)
        }
      } catch (err) {
        console.warn('Config leave mode:', err)
      }
      this.inConfigMode = false
    }

    async activate () {
      if (!this.connection.isConnected) return

      try {
        await this.loadSchema()

        await this.connection.runSerialTask(async () => {
          await this.connection._requestModeImpl('CONFIG')
          await this.enterConfigModeBody()
          await this.fetchValuesBody()
        })
      } catch (err) {
        console.error('Config activation error:', err)
      }
    }

    async loadSchema () {
      try {
        const response = await fetch('/schemas/settings.schema.json', { cache: 'no-store' })
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

    async enterConfigModeBody () {
      await this.sleep(100)
      await this.connection.sendRaw('CONFIG\n')
      const response = await this.connection.readLine(3000)

      if (response === 'CONFIG_STARTED') {
        this.inConfigMode = true
      } else {
        throw new Error(`Unexpected response: ${response}`)
      }
    }

    async fetchValuesBody () {
      await this.connection.sendRaw('VALUES\n')
      const response = await this.connection.readLine(5000)

      if (!response || response.startsWith('ERROR:')) {
        this.renderEmpty()
        return
      }

      this.values = JSON.parse(response)
      this.renderSettings()
    }

    renderEmpty () {
      this.containerTarget.innerHTML = `
        <div class="empty-state">
          <wa-icon name="sliders"></wa-icon>
          <p>Connect to view device settings</p>
        </div>
      `
      this.values = {}
    }

    renderSettings () {
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
          const isVisible = this.checkVisibility(setting)
          const visibilityStyle = isVisible ? '' : 'style="display: none;"'

          html += `
            <div class="config-setting" data-setting-id="${setting.id}" ${visibilityStyle}>
              <div class="config-setting-info">
                <label class="config-setting-label">${setting.label}</label>
                <span class="config-setting-desc">${setting.description || ''}</span>
              </div>
              <div class="config-setting-control">
                ${this.renderControl(setting)}
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

      this.containerTarget.querySelectorAll('[data-config-control]').forEach((el) => {
        el.addEventListener('change', (e) => this.onValueChange(e))
      })
    }

    checkVisibility (setting) {
      if (setting.visible_when) {
        return this.evaluateCondition(setting.visible_when)
      }

      if (setting.visible_when_any) {
        return setting.visible_when_any.some((cond) => this.evaluateCondition(cond))
      }

      return true
    }

    evaluateCondition (condition) {
      const depValue = this.values[condition.id]
      let expected = condition.value

      if (expected === true) expected = 1
      else if (expected === false) expected = 0

      if (condition.operator === '!=') {
        return Number(depValue) !== Number(expected)
      }
      return Number(depValue) === Number(expected)
    }

    findSetting (settingId) {
      if (!this.schema?.categories) return null
      for (const category of this.schema.categories) {
        for (const setting of category.settings) {
          if (setting.id === settingId) return setting
        }
      }
      return null
    }

    parseNumberSettingValue (setting, raw) {
      const step = setting?.step !== undefined ? setting.step : 1
      if (step < 1) {
        let value = parseFloat(raw)
        if (Number.isNaN(value)) return NaN
        value = Math.round(value / step) * step
        return Math.round(value * 1000) / 1000
      }
      return parseInt(raw, 10)
    }

    settingValue (setting) {
      const raw = this.values[setting.id]
      if (raw !== undefined && raw !== null) return raw
      if (setting.default !== undefined) return setting.default
      return setting.type === 'toggle' ? 0 : ''
    }

    optionSelected (optValue, current) {
      return Number(optValue) === Number(current)
    }

    midiNoteLabel (n) {
      const names = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']
      const note = Number(n)
      const octave = Math.floor(note / 12) - 1
      return `${names[note % 12]}${octave} (${note})`
    }

    midiNoteOptions () {
      if (this._midiNoteOptions) return this._midiNoteOptions
      this._midiNoteOptions = Array.from({ length: 128 }, (_, i) => ({
        value: i,
        label: this.midiNoteLabel(i)
      }))
      return this._midiNoteOptions
    }

    renderSelect (id, settingId, value, options) {
      const current = this.settingValue({ id: settingId, default: value })
      let optionsHtml = ''
      for (const opt of options) {
        const selected = this.optionSelected(opt.value, current) ? 'selected' : ''
        optionsHtml += `<wa-option value="${opt.value}" ${selected}>${opt.label}</wa-option>`
      }
      return `
        <wa-select id="${id}" data-config-control data-setting-id="${settingId}" value="${current}">
          ${optionsHtml}
        </wa-select>
      `
    }

    renderControl (setting, value) {
      const id = `config-${setting.id.replace(/\./g, '-')}`
      const current = this.settingValue(setting)

      switch (setting.type) {
        case 'calibration':
          return `
            <span class="config-calibration-note">
              <wa-icon name="info-circle"></wa-icon>
              Perform on device
            </span>
          `

        case 'toggle':
        case 'boolean': {
          const checked = Number(current) !== 0 ? 'checked' : ''
          return `
            <wa-switch id="${id}" data-config-control data-setting-id="${setting.id}" ${checked}>
            </wa-switch>
          `
        }

        case 'midi_note':
          return this.renderSelect(id, setting.id, current, this.midiNoteOptions())

        case 'select':
          return this.renderSelect(id, setting.id, current, setting.options)

        case 'number': {
          const min = setting.min !== undefined ? setting.min : 0
          const max = setting.max !== undefined ? setting.max : 100
          const step = setting.step !== undefined ? setting.step : 1
          const unit = setting.unit || ''
          const display = current === '' ? '' : String(current)
          return `
            <wa-input type="number" id="${id}" data-config-control data-setting-id="${setting.id}"
                      value="${display}" min="${min}" max="${max}" step="${step}" size="small">
              ${unit ? `<span slot="suffix">${unit}</span>` : ''}
            </wa-input>
          `
        }

        default:
          return `<span>${current}</span>`
      }
    }

    async onValueChange (event) {
      const el = event.target
      const settingId = el.dataset.settingId
      if (!settingId) return

      let value
      if (el.tagName === 'WA-SWITCH') {
        value = el.checked ? 1 : 0
      } else if (el.tagName === 'WA-SELECT') {
        value = parseInt(el.value, 10)
      } else if (el.tagName === 'WA-INPUT') {
        const setting = this.findSetting(settingId)
        value = this.parseNumberSettingValue(setting, el.value)
        if (Number.isNaN(value)) return
      } else {
        return
      }

      if (Number.isNaN(value)) return

      try {
        await this.connection.runSerialTask(async () => {
          if (!this.inConfigMode) {
            await this.connection._requestModeImpl('CONFIG')
            await this.enterConfigModeBody()
          }
          await this.connection.sendRaw(`SET ${settingId} ${value}\n`)
          const response = await this.connection.readLine(5000)

          if (response === 'OK') {
            this.values[settingId] = value
            this.updateVisibility()
          } else {
            console.error(`Failed to set ${settingId}: ${response}`)
          }
        })
      } catch (err) {
        console.error(`Failed to set ${settingId}:`, err)
      }
    }

    updateVisibility () {
      for (const category of this.schema.categories) {
        for (const setting of category.settings) {
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

    async refresh () {
      if (!this.connection.isConnected) return

      if (!this.inConfigMode) {
        await this.activate()
      } else {
        try {
          await this.connection.runSerialTask(() => this.fetchValuesBody())
        } catch (err) {
          console.error('Config refresh error:', err)
        }
      }
    }
  }
)
