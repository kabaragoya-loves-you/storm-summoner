/* Storm Summoner - MIDI Controller */

application.register(
  'midi',
  class extends BaseController {
    static targets = [
      'messageList',
      'emptyState',
      'clockBtn',
      'statusDot',
      'statusText'
    ]

    // Note names for display
    static NOTE_NAMES = [
      'C',
      'C#',
      'D',
      'D#',
      'E',
      'F',
      'F#',
      'G',
      'G#',
      'A',
      'A#',
      'B'
    ]

    // MMC command names
    static MMC_COMMANDS = {
      0x01: 'Stop',
      0x02: 'Play',
      0x03: 'Deferred Play',
      0x04: 'Fast Forward',
      0x05: 'Rewind',
      0x06: 'Record Strobe',
      0x07: 'Record Exit',
      0x08: 'Record Pause',
      0x09: 'Pause',
      0x0a: 'Eject'
    }

    // MIDI event types from firmware
    static EVENT_TYPES = {
      0: 'note_off',
      1: 'note_on',
      2: 'poly_aftertouch',
      3: 'control_change',
      4: 'program_change',
      5: 'channel_aftertouch',
      6: 'pitch_bend',
      7: 'time_code',
      8: 'song_position',
      9: 'song_select',
      10: 'tune_request',
      11: 'sysex',
      12: 'clock',
      13: 'tick',
      14: 'start',
      15: 'continue',
      16: 'stop',
      17: 'reset',
      18: 'active_sensing'
    }

    connect () {
      this.midiAccess = null
      this.midiInput = null
      this.showClock = false
      this.active = false
      this.ccMap = {}
      this.deviceSlug = null
      this.cdcReader = null
      this.cdcReadLoopActive = false
      this._loadDeviceInfoPromise = null
      this._relayStarting = false

      // Listen for tab activation
      document.addEventListener('app:tab-activated', e => {
        if (e.detail.tab === 'midi') {
          this.activate()
        }
      })

      this.connection.registerTabLeaveHandler('midi', () => this.deactivate())

      // Listen for connection changes
      this.connection.on('connection:changed', ({ connected }) => {
        if (connected && this.active) {
          this.startCdcRelay()
        } else if (!connected) {
          this.stopCdcRelay()
        }
      })
    }

    disconnect () {
      this.deactivate()
    }

    async activate () {
      if (this.active) return
      this.active = true

      this.updateStatus('Initializing...', false)

      // Start WebMIDI for OUT messages
      try {
        this.midiAccess = await navigator.requestMIDIAccess({ sysex: true })
        this.midiAccess.onstatechange = () => this.autoConnectStormSummoner()
        this.autoConnectStormSummoner()
      } catch (err) {
        this.updateStatus(`WebMIDI error: ${err.message}`, false)
      }

      // Start CDC relay for IN messages (if connected)
      if (this.connection.isConnected) {
        await this.startCdcRelay()
      }
    }

    async deactivate () {
      if (!this.active) return
      this.active = false

      // Stop WebMIDI
      if (this.midiInput) {
        this.midiInput.onmidimessage = null
        this.midiInput = null
      }

      // Stop CDC relay
      await this.stopCdcRelay()

      if (this.connection.isConnected) {
        try {
          await this.connection.runSerialTask(async () => {
            if (this.connection.mode === 'MIDI') {
              await this.connection._exitModeImpl()
            }
            await this.connection.ensureDeviceIdle()
          })
        } catch (err) {
          console.warn('MIDI tab leave cleanup:', err)
        }
      }

      this.updateStatus('Inactive', false)
    }

    // ========== WebMIDI (OUT messages) ==========

    autoConnectStormSummoner () {
      if (!this.midiAccess) return

      for (const [id, input] of this.midiAccess.inputs) {
        if (
          input.name.toLowerCase().includes('storm') ||
          input.name.toLowerCase().includes('summoner')
        ) {
          this.connectToInput(input)
          return
        }
      }

      if (this.midiAccess.inputs.size > 0) {
        const [, input] = this.midiAccess.inputs.entries().next().value
        this.connectToInput(input)
        return
      }

      this.updateStatus('No MIDI inputs found', false)
    }

    connectToInput (input) {
      if (this.midiInput) {
        this.midiInput.onmidimessage = null
      }

      this.midiInput = input
      this.midiInput.onmidimessage = this.onMidiMessage.bind(this)
      this.updateStatus(input.name, true)
      this.hideEmptyState()
    }

    onMidiMessage (event) {
      const data = event.data
      if (!data || data.length === 0) return

      const status = data[0]

      if (status >= 0xf0) {
        this.handleSystemMessage(status, data, 'out')
        return
      }

      const msgType = status & 0xf0
      const channel = (status & 0x0f) + 1

      switch (msgType) {
        case 0x80:
          this.addMessage(
            channel,
            `Note Off: ${this.noteName(data[1])}`,
            '',
            'note',
            'out'
          )
          break
        case 0x90:
          if (data[2] === 0) {
            this.addMessage(
              channel,
              `Note Off: ${this.noteName(data[1])}`,
              '',
              'note',
              'out'
            )
          } else {
            this.addMessage(
              channel,
              `Note On: ${this.noteName(data[1])}`,
              `vel ${data[2]}`,
              'note',
              'out'
            )
          }
          break
        case 0xa0:
          this.addMessage(
            channel,
            `Poly AT: ${this.noteName(data[1])}`,
            data[2],
            '',
            'out'
          )
          break
        case 0xb0:
          this.handleCC(channel, data[1], data[2], 'out')
          break
        case 0xc0:
          this.addMessage(channel, 'Program Change', data[1], 'pc', 'out')
          break
        case 0xd0:
          this.addMessage(channel, 'Aftertouch', data[1], '', 'out')
          break
        case 0xe0:
          const bend = (data[2] << 7) | data[1]
          this.addMessage(channel, 'Pitch Bend', bend, '', 'out')
          break
      }
    }

    handleSystemMessage (status, data, direction) {
      switch (status) {
        case 0xf0:
          this.processSysEx(data, direction)
          break
        case 0xf8:
          if (this.showClock) {
            this.addMessage('-', 'Clock', '', 'clock', direction)
          }
          break
        case 0xfa:
          this.addMessage('-', 'Start', '', 'transport', direction)
          break
        case 0xfb:
          this.addMessage('-', 'Continue', '', 'transport', direction)
          break
        case 0xfc:
          this.addMessage('-', 'Stop', '', 'transport', direction)
          break
        case 0xfe:
          break
        case 0xff:
          this.addMessage('-', 'System Reset', '', '', direction)
          break
      }
    }

    processSysEx (data, direction) {
      if (data.length >= 6 && data[1] === 0x7f && data[3] === 0x06) {
        const command = data[4]
        const deviceId = data[2]
        const cmdName =
          this.constructor.MMC_COMMANDS[command] ||
          `Unknown (0x${command.toString(16)})`
        const idStr = deviceId === 0x7f ? 'all' : deviceId.toString()
        this.addMessage(
          '-',
          `MMC: ${cmdName}`,
          `dev ${idStr}`,
          'mmc',
          direction
        )
      } else {
        let hex = Array.from(data.slice(0, 12))
          .map(b => b.toString(16).padStart(2, '0').toUpperCase())
          .join(' ')
        if (data.length > 12) hex += '...'
        this.addMessage('-', 'SysEx', hex, 'sysex', direction)
      }
    }

    // ========== CDC Relay (IN messages) ==========

    async startCdcRelay () {
      if (!this.connection.isConnected || !this.active) return
      if (this._relayStarting) return
      this._relayStarting = true

      try {
        const loaded = await this.loadDeviceInfo()
        if (!loaded) {
          this.updateStatus('Device lookup failed', false)
          return
        }

        await this.connection.runSerialTask(async () => {
          await this.connection.ensureDeviceIdle()
          if (this.connection._pumpSuspended || !this.connection._rxPumpRunning) {
            this.connection._resumeRxPump()
          }
          await this.connection._armRxPump(2000)
          const cmd = this.showClock ? 'MIDI CLOCK' : 'MIDI'
          this.connection.clearPendingRx()
          await this.connection.sendRaw(cmd + '\n')
          const banner = await this.connection._readPumpLineBody(3000)
          if (banner !== 'MIDI_STARTED') {
            throw new Error(`MIDI relay failed: ${banner || '(no response)'}`)
          }
          await this.connection._requestModeImpl('MIDI')
        })

        const unlocked = await this.waitForStreamUnlock(2000)
        if (!unlocked) {
          this.updateStatus('Serial stream locked', false)
          return
        }

        this.startCdcReadLoop()
      } catch (err) {
        console.error('CDC relay start failed:', err)
        try {
          await this.connection.recoverSerialState()
        } catch (recoverErr) {
          console.warn('Serial recovery after MIDI relay error:', recoverErr)
        }
      } finally {
        this._relayStarting = false
      }
    }

    async waitForStreamUnlock (timeout = 2000) {
      const startTime = Date.now()
      while (Date.now() - startTime < timeout) {
        if (!this.connection.port?.readable?.locked) return true
        await this.sleep(50)
      }
      return false
    }

    async stopCdcRelay () {
      this.cdcReadLoopActive = false

      if (this.cdcReader) {
        try {
          await this.cdcReader.cancel()
          this.cdcReader.releaseLock()
        } catch (e) {}
        this.cdcReader = null
      }
    }

    startCdcReadLoop () {
      if (!this.connection.port?.readable) return false
      if (this.connection.port.readable.locked) return false

      this.cdcReadLoopActive = true
      void this._runCdcReadLoop()
      return true
    }

    async _runCdcReadLoop () {
      const decoder = new TextDecoderStream()

      try {
        const readableStreamClosed = this.connection.port.readable.pipeTo(
          decoder.writable
        )
        this.cdcReader = decoder.readable.getReader()

        let buffer = ''

        while (this.cdcReadLoopActive && this.active) {
          const { value, done } = await this.cdcReader.read()
          if (done) break

          if (value) {
            buffer += value

            while (buffer.includes('\n')) {
              const idx = buffer.indexOf('\n')
              let line = buffer.substring(0, idx).replace(/\r/g, '').trim()
              buffer = buffer.substring(idx + 1)

              if (line.startsWith('MO:')) {
                this.processCdcMessage('M:' + line.slice(3), 'out')
              } else if (line.startsWith('M:')) {
                this.processCdcMessage(line, 'in')
              } else if (line.startsWith('EVT:')) {
                this.connection.dispatchCdcNotify(line)
              } else if (line === 'MIDI_STARTED') {
                // Ready
              }
            }
          }
        }
      } catch (err) {
        if (this.cdcReadLoopActive) {
          console.error('CDC read error:', err)
        }
      } finally {
        this.cdcReadLoopActive = false
        if (this.cdcReader) {
          try {
            this.cdcReader.releaseLock()
          } catch (e) {}
          this.cdcReader = null
        }
      }
    }

    processCdcMessage (msg, direction = 'in') {
      // Format: M:<type>,<channel>,<data1>,<data2>,<length>[,<hex_sysex>]
      const parts = msg.substring(2).split(',')
      if (parts.length < 5) return

      const type = parseInt(parts[0])
      const channel = parseInt(parts[1])
      const data1 = parseInt(parts[2])
      const data2 = parseInt(parts[3])
      const length = parseInt(parts[4])
      const hexSysex = parts[5]

      const eventType = this.constructor.EVENT_TYPES[type]

      switch (eventType) {
        case 'note_off':
          this.addMessage(
            channel + 1,
            `Note Off: ${this.noteName(data1)}`,
            '',
            'note',
            direction
          )
          break
        case 'note_on':
          if (data2 === 0) {
            this.addMessage(
              channel + 1,
              `Note Off: ${this.noteName(data1)}`,
              '',
              'note',
              direction
            )
          } else {
            this.addMessage(
              channel + 1,
              `Note On: ${this.noteName(data1)}`,
              `vel ${data2}`,
              'note',
              direction
            )
          }
          break
        case 'poly_aftertouch':
          this.addMessage(
            channel + 1,
            `Poly AT: ${this.noteName(data1)}`,
            data2,
            '',
            direction
          )
          break
        case 'control_change':
          this.handleCC(channel + 1, data1, data2, direction)
          break
        case 'program_change':
          this.addMessage(channel + 1, 'Program Change', data1, 'pc', direction)
          break
        case 'channel_aftertouch':
          this.addMessage(channel + 1, 'Aftertouch', data1, '', direction)
          break
        case 'pitch_bend':
          const bend = (data2 << 7) | data1
          this.addMessage(channel + 1, 'Pitch Bend', bend, '', direction)
          break
        case 'clock':
          if (this.showClock) {
            this.addMessage('-', 'Clock', '', 'clock', direction)
          }
          break
        case 'start':
          this.addMessage('-', 'Start', '', 'transport', direction)
          break
        case 'continue':
          this.addMessage('-', 'Continue', '', 'transport', direction)
          break
        case 'stop':
          this.addMessage('-', 'Stop', '', 'transport', direction)
          break
        case 'sysex':
          if (hexSysex) {
            const bytes = []
            for (let i = 0; i < hexSysex.length; i += 2) {
              bytes.push(parseInt(hexSysex.substring(i, i + 2), 16))
            }
            this.processSysEx(bytes, direction)
          } else {
            this.addMessage('-', 'SysEx', `${length} bytes`, 'sysex', direction)
          }
          break
      }
    }

    // ========== Device Info & CC Names ==========

    async loadDeviceInfo () {
      if (!this.connection.isConnected) {
        console.log('[MIDI] loadDeviceInfo: not connected')
        return false
      }
      if (this._loadDeviceInfoPromise) return this._loadDeviceInfoPromise
      this._loadDeviceInfoPromise = this._loadDeviceInfoBody()
        .finally(() => { this._loadDeviceInfoPromise = null })
      return this._loadDeviceInfoPromise
    }

    _applyCachedCcMap (slug, cached) {
      this.deviceSlug = slug
      this.ccMap = cached
    }

    async _loadDeviceInfoBody () {
      try {
        return await this.connection.runSerialTask(async () => {
          await this.connection.ensureDeviceIdle()
          this.connection.clearPendingRx()
          if (this.connection._pumpSuspended || !this.connection._rxPumpRunning) {
            this.connection._resumeRxPump()
          }
          await this.connection._armRxPump(2000)

          const cachedSlug = this.connection.getPedalCcCacheSlug()
          if (cachedSlug) {
            const cached = this.connection.getPedalCcCache(cachedSlug)
            if (cached) {
              this._applyCachedCcMap(cachedSlug, cached)
              console.log('[MIDI] Using cached CC map (skip DEVICE):',
                Object.keys(cached).length, 'entries')
              return true
            }
          }

          console.log('[MIDI] Sending DEVICE command...')
          let slug = await this.connection._queryDeviceSlug()

          if (!slug && cachedSlug) {
            const fallback = this.connection.getPedalCcCache(cachedSlug)
            if (fallback) {
              this._applyCachedCcMap(cachedSlug, fallback)
              console.log('[MIDI] DEVICE failed; using cached CC map')
              return true
            }
          }

          if (!slug) {
            console.log('[MIDI] Invalid DEVICE response')
            return false
          }

          this.deviceSlug = slug
          console.log('[MIDI] Device slug:', slug)

          const cached = this.connection.getPedalCcCache(slug)
          if (cached) {
            this.ccMap = cached
            console.log('[MIDI] Using cached CC map:', Object.keys(cached).length, 'entries')
            return true
          }

          const [vendor, pedal] = slug.split('.')
          if (!vendor || !pedal) {
            console.log('[MIDI] Invalid slug format')
            return false
          }

          const jsonPaths = [
            `/assets/devices/devices/${vendor}/${pedal}.json`,
            `devices/devices/${vendor}/${pedal}.json`
          ]
          let jsonResponse = null
          for (const jsonPath of jsonPaths) {
            console.log('[MIDI] Fetching:', jsonPath)
            jsonResponse = await this.fetchDeviceJsonInTask(jsonPath)
            if (jsonResponse) break
          }

          if (!jsonResponse) {
            console.log('[MIDI] Failed to fetch device JSON')
            return false
          }

          console.log('[MIDI] Got JSON, length:', jsonResponse.length)
          this.parseDeviceJson(jsonResponse)
          this.connection.setPedalCcCache(slug, this.ccMap)
          console.log('[MIDI] CC map entries:', Object.keys(this.ccMap).length)
          await this.connection.ensureDeviceIdle()
          return true
        })
      } catch (err) {
        console.error('[MIDI] Failed to load device info:', err)
        try {
          await this.connection.recoverSerialState()
        } catch (recoverErr) {
          console.warn('Serial recovery after device lookup error:', recoverErr)
        }
        return false
      }
    }

    async fetchDeviceJsonInTask (path) {
      try {
        await this.connection._ensureAssetsReadyDedicated()
        const { data } = await this.connection._fetchSizedTransferImpl(`GET ${path}`)
        const text = new TextDecoder().decode(data)
        return text
      } catch (err) {
        console.error('[MIDI] Failed to fetch device JSON:', err)
        return null
      }
    }

    parseDeviceJson (jsonText) {
      try {
        const data = JSON.parse(jsonText)
        this.ccMap = {}

        if (data.controlChangeCommands) {
          for (const cc of data.controlChangeCommands) {
            const ccNum = cc.controlChangeNumber
            const discreteValues = this.parseDiscreteValues(cc.valueRange)
            this.ccMap[ccNum] = {
              name: cc.name,
              discreteValues: discreteValues
            }
          }
        }
      } catch (err) {
        console.error('Failed to parse device JSON:', err)
      }
    }

    parseDiscreteValues (valueRange) {
      if (!valueRange || !valueRange.discreteValues) return null
      return valueRange.discreteValues
        .map(dv => ({ value: dv.value, name: dv.name }))
        .sort((a, b) => b.value - a.value) // Sort descending for matching
    }

    handleCC (channel, ccNum, value, direction) {
      const ccInfo = this.ccMap[ccNum]
      let message, valueStr

      if (ccInfo) {
        message = `${ccInfo.name} (CC ${ccNum})`
        valueStr = this.formatCCValue(value, ccInfo.discreteValues)
      } else {
        message = `CC ${ccNum}`
        valueStr = value.toString()
      }

      this.addMessage(channel, message, valueStr, 'cc', direction)
    }

    formatCCValue (value, discreteValues) {
      if (!discreteValues || discreteValues.length === 0)
        return value.toString()
      const match = discreteValues.find(dv => value >= dv.value)
      return match ? `${match.name} (${value})` : value.toString()
    }

    // ========== Helpers ==========

    noteName (noteNum) {
      const octave = Math.floor(noteNum / 12) - 1
      const note = this.constructor.NOTE_NAMES[noteNum % 12]
      return `${note}${octave}`
    }

    addMessage (channel, message, value, type = '', direction = 'out') {
      this.hideEmptyState()

      const row = document.createElement('div')
      row.className = `midi-item ${type}`

      const now = new Date()
      const time =
        now.toTimeString().substring(0, 8) +
        '.' +
        now.getMilliseconds().toString().padStart(3, '0')

      const dirLabel = direction.toUpperCase()
      row.innerHTML = `
        <span class="time">${time}</span>
        <span class="dir ${direction}">${dirLabel}</span>
        <span class="channel">${channel}</span>
        <span class="message">${this.escapeHtml(message)}</span>
        <span class="value">${this.escapeHtml(String(value))}</span>
      `

      this.messageListTarget.appendChild(row)
      this.messageListTarget.scrollTop = this.messageListTarget.scrollHeight

      while (
        this.messageListTarget.querySelectorAll('.midi-item').length > 500
      ) {
        const first = this.messageListTarget.querySelector('.midi-item')
        if (first) first.remove()
      }
    }

    escapeHtml (text) {
      const div = document.createElement('div')
      div.textContent = text
      return div.innerHTML
    }

    hideEmptyState () {
      if (this.hasEmptyStateTarget) {
        this.emptyStateTarget.style.display = 'none'
      }
    }

    updateStatus (text, connected) {
      this.statusTextTarget.textContent = text
      this.statusDotTarget.classList.toggle('connected', connected)
    }

    toggleClock () {
      this.showClock = !this.showClock
      this.clockBtnTarget.classList.toggle('active', this.showClock)
    }

    clear () {
      const items = this.messageListTarget.querySelectorAll('.midi-item')
      items.forEach(item => item.remove())

      if (this.hasEmptyStateTarget) {
        this.emptyStateTarget.style.display = ''
      }
    }
  }
)
