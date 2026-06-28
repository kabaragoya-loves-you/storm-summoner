/* Info tab pedal settings — MIDI channel, TRS type, send clock (auto-save) */

application.register(
  'info-pedal-settings',
  class extends BaseController {
    static values = {
      midiChannel: Number,
      trsType: String,
      sendClock: Boolean,
      busy: Boolean
    }

    connect () {
      this._lastGood = {
        midiChannel: this.midiChannelValue,
        trsType: this.trsTypeValue,
        sendClock: this.sendClockValue
      }
      this._onChange = this.handleChange.bind(this)
      this.element.addEventListener('change', this._onChange)
    }

    disconnect () {
      this.element.removeEventListener('change', this._onChange)
    }

    busyValueChanged (busy) {
      this.element.classList.toggle('is-busy', !!busy)
      document.dispatchEvent(new CustomEvent('info-pedal-settings:busy', {
        detail: { busy: !!busy }
      }))
    }

    handleChange (event) {
      const el = event.target
      if (el.tagName !== 'WA-SELECT') return
      if (this.busyValue) {
        this.revertSelect(el)
        return
      }

      const field = el.dataset.pedalField
      if (!field) return

      if (field === 'midiChannel') {
        const ch = parseInt(el.value, 10)
        if (Number.isNaN(ch) || ch < 1 || ch > 16) {
          this.revertSelect(el)
          return
        }
        if (ch === this._lastGood.midiChannel) return
        this.save(`PEDAL_SET CHANNEL ${ch}`, () => {
          this.midiChannelValue = ch
          this._lastGood.midiChannel = ch
          this.emitChanged({ midi_channel: ch })
        }, () => this.revertSelect(el))
        return
      }

      if (field === 'trsType') {
        const trs = String(el.value || '')
        if (!trs || trs === this._lastGood.trsType) return
        this.save(`PEDAL_SET TRS ${trs}`, () => {
          this.trsTypeValue = trs
          this._lastGood.trsType = trs
          this.emitChanged({ trs_type: trs })
        }, () => this.revertSelect(el))
        return
      }

      if (field === 'sendClock') {
        const send = el.value === '1'
        if (send === this._lastGood.sendClock) return
        this.save(`PEDAL_SET SEND_CLOCK ${send ? 1 : 0}`, () => {
          this.sendClockValue = send
          this._lastGood.sendClock = send
          this.emitChanged({ send_clock: send })
        }, () => this.revertSelect(el))
      }
    }

    async save (command, onSuccess, onFailure) {
      if (!this.connection.isConnected) {
        onFailure?.()
        return
      }

      this.busyValue = true
      try {
        await this.connection.runSerialTask(async () => {
          await this.connection.ensureDeviceIdle()
          await this.connection.sendRaw(`${command}\n`)
          const response = await this.connection._readLineBody(5000)
          if (response !== 'OK') throw new Error(response || 'No response')
        })
        onSuccess()
      } catch (err) {
        console.warn('Pedal setting save failed:', err)
        onFailure?.()
      } finally {
        this.busyValue = false
      }
    }

    emitChanged (partial) {
      document.dispatchEvent(new CustomEvent('info-pedal-settings:changed', {
        detail: partial
      }))
    }

    revertSelect (el) {
      const field = el.dataset.pedalField
      if (field === 'midiChannel') {
        el.value = String(this._lastGood.midiChannel)
      } else if (field === 'trsType') {
        el.value = this._lastGood.trsType
      } else if (field === 'sendClock') {
        el.value = this._lastGood.sendClock ? '1' : '0'
      }
    }
  }
)
