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

      // Listen for tab activation
      document.addEventListener('app:tab-activated', e => {
        if (e.detail.tab === 'midi') {
          this.activate()
        } else if (this.active) {
          this.deactivate()
        }
      })

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

      try {
        // Load device info BEFORE entering MIDI relay mode
        await this.loadDeviceInfo()

        // Request MIDI relay mode
        const modeGranted = await this.connection.requestMode('MIDI')
        if (!modeGranted) return

        // Send MIDI relay command (with CLOCK option if enabled)
        await this.sleep(100)
        const cmd = this.showClock ? 'MIDI CLOCK' : 'MIDI'
        await this.connection.sendRaw(cmd + '\n')

        // Start reading relay messages
        this.startCdcReadLoop()
      } catch (err) {
        console.error('CDC relay start failed:', err)
      }
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

    async startCdcReadLoop () {
      if (!this.connection.port?.readable) return
      if (this.connection.port.readable.locked) return

      this.cdcReadLoopActive = true
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

              if (line.startsWith('M:')) {
                this.processCdcMessage(line)
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

    processCdcMessage (msg) {
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
            'in'
          )
          break
        case 'note_on':
          if (data2 === 0) {
            this.addMessage(
              channel + 1,
              `Note Off: ${this.noteName(data1)}`,
              '',
              'note',
              'in'
            )
          } else {
            this.addMessage(
              channel + 1,
              `Note On: ${this.noteName(data1)}`,
              `vel ${data2}`,
              'note',
              'in'
            )
          }
          break
        case 'poly_aftertouch':
          this.addMessage(
            channel + 1,
            `Poly AT: ${this.noteName(data1)}`,
            data2,
            '',
            'in'
          )
          break
        case 'control_change':
          this.handleCC(channel + 1, data1, data2, 'in')
          break
        case 'program_change':
          this.addMessage(channel + 1, 'Program Change', data1, 'pc', 'in')
          break
        case 'channel_aftertouch':
          this.addMessage(channel + 1, 'Aftertouch', data1, '', 'in')
          break
        case 'pitch_bend':
          const bend = (data2 << 7) | data1
          this.addMessage(channel + 1, 'Pitch Bend', bend, '', 'in')
          break
        case 'clock':
          if (this.showClock) {
            this.addMessage('-', 'Clock', '', 'clock', 'in')
          }
          break
        case 'start':
          this.addMessage('-', 'Start', '', 'transport', 'in')
          break
        case 'continue':
          this.addMessage('-', 'Continue', '', 'transport', 'in')
          break
        case 'stop':
          this.addMessage('-', 'Stop', '', 'transport', 'in')
          break
        case 'sysex':
          if (hexSysex) {
            const bytes = []
            for (let i = 0; i < hexSysex.length; i += 2) {
              bytes.push(parseInt(hexSysex.substring(i, i + 2), 16))
            }
            this.processSysEx(bytes, 'in')
          } else {
            this.addMessage('-', 'SysEx', `${length} bytes`, 'sysex', 'in')
          }
          break
      }
    }

    // ========== Device Info & CC Names ==========

    async loadDeviceInfo () {
      if (!this.connection.isConnected) {
        console.log('[MIDI] loadDeviceInfo: not connected')
        return
      }

      try {
        // Exit any current mode first (e.g., ASSETS from previous tab)
        if (this.connection.currentMode) {
          console.log(
            '[MIDI] Exiting current mode:',
            this.connection.currentMode
          )
          await this.connection.exitMode()
          await this.sleep(100)
        }

        // Get current device slug
        console.log('[MIDI] Sending DEVICE command...')
        const response = await this.connection.sendCommand('DEVICE', 2000)
        console.log('[MIDI] DEVICE response:', response)

        if (!response.startsWith('DEVICE ')) {
          console.log('[MIDI] Invalid DEVICE response')
          return
        }

        // Parse slug: "meris.ottobit_jr@0" -> vendor="meris", pedal="ottobit_jr"
        let slug = response.substring(7).trim()
        slug = slug.replace(/@\d+$/, '') // Strip @N suffix
        this.deviceSlug = slug
        console.log('[MIDI] Device slug:', slug)

        const [vendor, pedal] = slug.split('.')
        if (!vendor || !pedal) {
          console.log('[MIDI] Invalid slug format')
          return
        }

        // Fetch device JSON via CDC assets
        // Path structure: /assets/devices/devices/<vendor>/<pedal>.json (mirrors midi-devices repo)
        const jsonPath = `devices/devices/${vendor}/${pedal}.json`
        console.log('[MIDI] Fetching:', jsonPath)
        const jsonResponse = await this.fetchDeviceJson(jsonPath)

        if (!jsonResponse) {
          console.log('[MIDI] Failed to fetch device JSON')
          return
        }

        console.log('[MIDI] Got JSON, length:', jsonResponse.length)

        // Parse CC map
        this.parseDeviceJson(jsonResponse)
        console.log('[MIDI] CC map entries:', Object.keys(this.ccMap).length)
      } catch (err) {
        console.error('[MIDI] Failed to load device info:', err)
      }
    }

    async fetchDeviceJson (path) {
      try {
        // Enter assets mode briefly
        console.log('[MIDI] Entering ASSETS mode...')
        await this.connection.sendRaw('ASSETS\n')

        // Wait for ASSETS_STARTED confirmation
        const modeResponse = await this.connection.readLine(2000)
        console.log('[MIDI] Mode response:', modeResponse)
        if (!modeResponse.includes('ASSETS_STARTED')) {
          console.log('[MIDI] Failed to enter ASSETS mode')
          return null
        }

        // Request file download
        console.log('[MIDI] Sending GET command...')
        await this.connection.sendRaw(`GET ${path}\n`)

        // Read response - expect "SIZE <size>" or error
        const sizeResponse = await this.connection.readLine(2000)
        console.log('[MIDI] GET response:', sizeResponse)

        if (!sizeResponse.startsWith('SIZE ')) {
          console.log('[MIDI] Not a SIZE response, exiting assets')
          await this.connection.sendRaw('EXIT\n')
          return null
        }

        const size = parseInt(sizeResponse.split(' ')[1])
        console.log('[MIDI] File size:', size)

        if (size <= 0 || size > 100000) {
          console.log('[MIDI] Invalid file size')
          await this.connection.sendRaw('EXIT\n')
          return null
        }

        // Read binary data
        const data = await this.connection.readBinary(size, 5000)
        const text = new TextDecoder().decode(data)
        console.log('[MIDI] Got file content, length:', text.length)

        // Exit assets mode
        await this.connection.sendRaw('EXIT\n')
        await this.sleep(50)

        return text
      } catch (err) {
        console.error('[MIDI] Failed to fetch device JSON:', err)
        try {
          await this.connection.sendRaw('EXIT\n')
        } catch (e) {}
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
