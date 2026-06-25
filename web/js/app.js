/* Storm Summoner - Main Application */

// Connection Manager - Singleton for WebSerial connection
window.ConnectionManager = (function () {
  let instance = null

  class ConnectionManager {
    constructor () {
      this.port = null
      this.mode = null // ASSETS, CONSOLE, DISPLAY, UPDATE, RPC
      // The Scenes flow keeps the device in SCENES mode while presenting
      // this.mode as null (so SCENE_INSPECT can run through the rx pump). This
      // flag remembers that the device side is still in SCENES so that a later
      // mode change / exit knows to send EXIT even though this.mode is null.
      this._deviceScenesActive = false
      this.listeners = new Map()
      // Persistent receive buffer. Without this, readLine/sendCommand would
      // each open a fresh reader, read a chunk that may contain MULTIPLE
      // lines, return the first line, and silently drop everything after the
      // first '\n'. That was corrupting any caller that tried to consume a
      // burst of lines back-to-back (firmware OTA PROGRESS, COMMIT/SUCCESS
      // splits, etc.).
      this._rxBuffer = ''
      this._rxDecoder = new TextDecoder()
      this._rxPumpRunning = false
      this._rxPumpReader = null
      this._pumpSuspended = false
      this._lineQueue = []
      this._lineWaiters = []
      this._commandChain = Promise.resolve()
      this._writeChain = Promise.resolve()
      this._serialDepth = 0
      this._serialTaskReentrant = false
      this._modeNotifyRunning = false
      this._modeNotifyReader = null
      this._modeNotifySuspended = false
      this._exclusiveSession = 0
      this._connectingPromise = null
    }

    beginExclusiveSession () {
      this._exclusiveSession++
    }

    endExclusiveSession () {
      this._exclusiveSession = Math.max(0, this._exclusiveSession - 1)
      if (this._exclusiveSession === 0 && this.mode) this._resumeModeNotify()
    }

    _resumeModeNotifyIfAllowed () {
      if (this.mode && this._exclusiveSession === 0) this._resumeModeNotify()
    }

    // Event handling
    on (event, callback) {
      if (!this.listeners.has(event)) this.listeners.set(event, [])
      this.listeners.get(event).push(callback)
    }

    off (event, callback) {
      if (!this.listeners.has(event)) return
      const callbacks = this.listeners.get(event)
      const index = callbacks.indexOf(callback)
      if (index > -1) callbacks.splice(index, 1)
    }

    emit (event, data) {
      if (!this.listeners.has(event)) return
      this.listeners.get(event).forEach(cb => cb(data))
    }

    // Connection state
    get isConnected () {
      return this.port !== null
    }

    get currentMode () {
      return this.mode
    }

    get isSerialBusy () {
      return this._serialDepth > 0
    }

    // Serialize every WebSerial transaction. Only bypass the queue for nested
    // calls from inside an active serial task (readLine during sendCommand).
    // _serialDepth alone is NOT enough — a second top-level task during an
    // in-flight first task must still queue, or getWriter/getReader collide.
    runSerialTask (fn) {
      if (this._serialTaskReentrant) return fn()

      const run = async () => {
        this._serialDepth++
        this._serialTaskReentrant = true
        try {
          return await fn()
        } finally {
          this._serialTaskReentrant = false
          this._serialDepth--
        }
      }

      const result = this._commandChain.then(run, run)
      this._commandChain = result.then(() => {}, () => {})
      return result
    }

    clearPendingRx () {
      this._rxBuffer = ''
      this._lineQueue = []
      this._rejectLineWaiters()
    }

    // Connect to device. Re-entrancy guard: a second click (or a duplicate
    // button event) while a connect is already in flight must NOT start a
    // parallel flow. Two concurrent connect()s would each open a requestPort
    // chooser and call open() on the same device, which surfaces as
    // "NotFoundError: No port selected by the user" and competing opens.
    async connect () {
      if (this.port) return true
      if (this._connectingPromise) return this._connectingPromise

      this._connectingPromise = this._connectFlow()
      try {
        return await this._connectingPromise
      } finally {
        this._connectingPromise = null
      }
    }

    async _connectFlow () {
      // Port selection is the one step that requires a user gesture, so it must
      // happen exactly once. A failure here (chooser dismissed / no port) is a
      // real user-facing error with nothing to retry.
      const port = await navigator.serial.requestPort({
        filters: [{ usbVendorId: 0x303a }]
      })

      // Opening + the initial drain can transiently fail right after a device
      // reset (USB re-enumeration, stray boot output, or a serialized command
      // racing for the readable reader). Retry the setup WITHOUT re-prompting
      // the chooser so the user never has to manually reconnect.
      const maxAttempts = 3
      let lastErr = null
      for (let attempt = 1; attempt <= maxAttempts; attempt++) {
        try {
          await this._openAndInit(port)
          this.emit('connection:changed', { connected: true })
          return true
        } catch (err) {
          lastErr = err
          await this._abortConnect(port)
          if (attempt < maxAttempts) await this.sleep(250)
        }
      }
      throw lastErr
    }

    async _openAndInit (port) {
      await port.open({ baudRate: 115200 })
      await port.setSignals({
        dataTerminalReady: true,
        requestToSend: true
      })
      this.port = port

      // Listen for unexpected disconnection
      navigator.serial.addEventListener('disconnect', this.onPortDisconnect)

      this._startRxPump()
      await this.sleep(50)

      // Run the initial drain/EXIT/drain through the serial task chain. drainInput
      // acquires the port's single readable reader; if a command (e.g. info
      // activation firing on reconnect) grabs that reader concurrently, getReader
      // throws "already locked to a reader". Serializing here makes the setup
      // mutually exclusive with every other serial transaction.
      await this.runSerialTask(async () => {
        await this.drainInput()
        // Clear any CDC mode left over from a prior browser session.
        await this.sendRaw('EXIT\n')
        await this.sleep(150)
        await this.drainInput()
      })
    }

    async _abortConnect (port) {
      this._stopRxPump()
      this._stopModeNotifyLoop()
      navigator.serial.removeEventListener('disconnect', this.onPortDisconnect)
      this.port = null
      this.mode = null
      this._deviceScenesActive = false
      this._rxBuffer = ''
      this._lineQueue = []
      this._rejectLineWaiters()
      try {
        await port.close()
      } catch (e) {}
    }

    // Handle unexpected port disconnection
    onPortDisconnect = event => {
      if (event.target === this.port) {
        console.log('USB device disconnected')
        this._stopRxPump()
        this._stopModeNotifyLoop()
        this._releaseAllReaders()
        this.port = null
        this.mode = null
        this._deviceScenesActive = false
        this._rxBuffer = ''
        this._lineQueue = []
        this._lineWaiters = []
        this._commandChain = Promise.resolve()
        this._writeChain = Promise.resolve()
        this._serialDepth = 0
        this._serialTaskReentrant = false
        this.setTabsLocked(false)
        navigator.serial.removeEventListener(
          'disconnect',
          this.onPortDisconnect
        )
        this.emit('connection:changed', { connected: false })
      }
    }

    // Disconnect from device
    async disconnect () {
      if (!this.port) return

      this._stopRxPump()
      this._stopModeNotifyLoop()
      this._releaseAllReaders()
      navigator.serial.removeEventListener('disconnect', this.onPortDisconnect)

      try {
        if (this.mode) {
          await this.exitMode()
        }
        await this.port.close()
      } catch (err) {
        // Ignore close errors
      } finally {
        this.port = null
        this.mode = null
        this._deviceScenesActive = false
        this._rxBuffer = ''
        this._lineQueue = []
        this._lineWaiters = []
        this._commandChain = Promise.resolve()
        this._writeChain = Promise.resolve()
        this._serialDepth = 0
        this._serialTaskReentrant = false
        this.setTabsLocked(false)
        this.emit('connection:changed', { connected: false })
      }
    }

    // Request a mode - returns true if mode was entered
    async requestMode (newMode) {
      return this.runSerialTask(() => this._requestModeImpl(newMode))
    }

    async _requestModeImpl (newMode) {
      if (!this.port) throw new Error('Not connected')
      if (this.mode === newMode) return true

      if (this.mode || this._deviceScenesActive) {
        await this._exitModeImpl()
      }

      if (newMode) {
        await this._suspendRxPump()
        this._lineQueue = []
        this._rejectLineWaiters()
        this._rxBuffer = ''
      }

      this.mode = newMode
      if (newMode) this._startModeNotifyLoop()
      this.emit('mode:changed', { mode: newMode })
      return true
    }

    // Exit current mode
    async exitMode () {
      return this.runSerialTask(() => this._exitModeImpl())
    }

    async _exitModeImpl (options = {}) {
      const leavePumpSuspended = options.leavePumpSuspended === true
      if ((!this.mode && !this._deviceScenesActive) || !this.port) return
      this._stopModeNotifyLoop()
      await this._suspendModeNotify()
      this.mode = null
      this._deviceScenesActive = false
      this.emit('mode:changed', { mode: null })
      try {
        await this.sendRaw('EXIT\n')
        await this.sleep(100)
        await this.drainInput()
      } catch (err) {
        // Ignore exit errors
      }
      if (!leavePumpSuspended) this._resumeRxPump()
    }

    async ensureDeviceIdle (options = {}) {
      const leavePumpSuspended = options.leavePumpSuspended === true
      if (!this.port) return
      if (this.mode || this._deviceScenesActive) {
        await this._exitModeImpl({ leavePumpSuspended })
        await this.sleep(200)
        return
      }
      if (leavePumpSuspended) {
        await this._armRxPump()
        await this.sendRaw('EXIT\n')
        await this.sleep(150)
        const exitDeadline = Date.now() + 500
        while (Date.now() < exitDeadline) {
          const remaining = exitDeadline - Date.now()
          if (remaining <= 0) break
          const line = await this.readLine(Math.min(100, remaining))
          if (!line) continue
          if (line === 'OK' || line.startsWith('ERROR:')) break
          if (line === 'SCENES_STOPPED' || line === 'SCENES_STARTED' ||
              line === 'CONFIG_STOPPED' || line === 'SETTINGS_STOPPED' ||
              line === 'PEDALS_STOPPED') break
        }
        this._lineQueue = []
        this._rejectLineWaiters()
        this._rxBuffer = ''
        return
      }
      // Host state is idle; the device may still be in a CDC mode.
      const pumpWasRunning = this._rxPumpRunning && !this._pumpSuspended
      if (pumpWasRunning) await this._suspendRxPump()
      try {
        await this.sendRaw('EXIT\n')
        await this.sleep(150)
        await this.drainInput()
      } finally {
        if (pumpWasRunning && !leavePumpSuspended) this._resumeRxPump()
      }
    }

    // Lock/unlock tabs during critical operations
    setTabsLocked (locked, exceptTab = null) {
      const tabGroup = document.querySelector('wa-tab-group')
      if (!tabGroup) return

      const tabs = tabGroup.querySelectorAll('wa-tab')
      tabs.forEach(tab => {
        const panel = tab.getAttribute('panel')
        if (panel !== exceptTab) {
          tab.disabled = locked
        }
      })
    }

    // Low-level I/O — writes are serialized on their own chain so two serial
    // tasks can never interleave getWriter() on the single WritableStream.
    async sendRaw (data) {
      if (!this.port?.writable) throw new Error('Port not writable')
      const run = async () => {
        const writer = this.port.writable.getWriter()
        try {
          await writer.write(new TextEncoder().encode(data))
        } finally {
          writer.releaseLock()
        }
      }
      const result = this._writeChain.then(run, run)
      this._writeChain = result.then(() => {}, () => {})
      return result
    }

    _takeLineFromBuffer () {
      const idx = this._rxBuffer.indexOf('\n')
      if (idx === -1) return null
      const line = this._rxBuffer.substring(0, idx).replace(/\r/g, '').trim()
      this._rxBuffer = this._rxBuffer.substring(idx + 1)
      return line
    }

    _isGluedSerialPrefix (prefix) {
      if (prefix === 'OK' || prefix === 'READY' || prefix === 'SUCCESS' ||
          prefix === 'CANCELLED' || prefix === 'RESETTING') return true
      if (prefix.startsWith('ERROR:') || prefix.startsWith('SIZE ')) return true
      return prefix === 'SCENES_STARTED' || prefix === 'SCENES_STOPPED' ||
        prefix === 'CONFIG_STARTED' || prefix === 'CONFIG_STOPPED' ||
        prefix === 'SETTINGS_STARTED' || prefix === 'SETTINGS_STOPPED' ||
        prefix === 'PEDALS_STARTED' || prefix === 'PEDALS_STOPPED' ||
        prefix === 'CONSOLE_STARTED' || prefix === 'DISPLAY_STOPPED' ||
        prefix === 'MIDI_STOPPED'
    }

    _expandSerialLines (line) {
      if (!line) return ['']
      const evtIdx = line.indexOf('EVT:')
      if (evtIdx <= 0) return [line]
      const prefix = line.slice(0, evtIdx)
      if (!this._isGluedSerialPrefix(prefix)) return [line]
      const tail = line.slice(evtIdx)
      return prefix.length > 0 ? [prefix, tail] : [tail]
    }

    dispatchCdcNotify (line) {
      if (!line.startsWith('EVT:')) return false
      const parts = line.split(':')
      if (parts.length < 3) return true
      const kind = parts[1]
      const detail = { kind, index: -1 }

      if (kind === 'clock' && parts.length >= 9) {
        const bpmX10 = parseInt(parts[2], 10) || 0
        detail.clock = {
          bpm: bpmX10 / 10,
          transport: parts[3] === '1' ? 'playing' : 'stopped',
          bar: parseInt(parts[4], 10) || 1,
          beat: parseInt(parts[5], 10) || 1,
          time_signature: {
            numerator: parseInt(parts[6], 10) || 4,
            denominator: parseInt(parts[7], 10) || 4
          },
          use_transport: parts[8] === '1'
        }
        if (parts.length >= 11) {
          detail.clock.flag_enabled = parts[9] === '1'
          detail.clock.flag = parts[10] === '1'
        }
      } else if (kind === 'connections' && parts.length >= 6) {
        detail.connections = {
          usb: parts[2] === '1',
          cv: parts[3] === '1',
          expression: parts[4] === '1',
          midi_in: parts[5] === '1'
        }
      } else if (kind === 'connections' && parts.length >= 5) {
        detail.connections = {
          usb: parts[2] === '1',
          cv: parts[3] === '1',
          expression: parts[4] === '1',
          midi_in: false
        }
      } else {
        const index = parseInt(parts[2], 10)
        detail.index = Number.isNaN(index) ? -1 : index
      }

      document.dispatchEvent(new CustomEvent('cdc:notify', { detail }))
      return true
    }

    _deliverLine (line) {
      if (this._lineWaiters.length > 0) {
        const waiter = this._lineWaiters.shift()
        if (waiter.timer) clearTimeout(waiter.timer)
        waiter.resolve(line)
      } else {
        this._lineQueue.push(line)
      }
    }

    _processIncomingLines () {
      while (true) {
        const line = this._takeLineFromBuffer()
        if (line === null) break
        for (const sub of this._expandSerialLines(line)) {
          if (this.dispatchCdcNotify(sub)) continue
          this._deliverLine(sub)
        }
      }
    }

    _tryDequeueLine () {
      if (this._lineQueue.length > 0) return this._lineQueue.shift()
      while (true) {
        const line = this._takeLineFromBuffer()
        if (line === null) return null
        for (const sub of this._expandSerialLines(line)) {
          if (this.dispatchCdcNotify(sub)) continue
          return sub
        }
      }
    }

    _releaseAllReaders () {
      this._releaseRxPumpReader()
      this._releaseModeNotifyReader()
    }

    async _acquireDedicatedReader () {
      if (!this.port?.readable) throw new Error('Not connected')
      for (let attempt = 0; attempt < 4; attempt++) {
        try {
          return this.port.readable.getReader()
        } catch (err) {
          const msg = String(err?.message || err)
          if (!msg.includes('locked') && !msg.includes('Reader')) throw err
          this._releaseAllReaders()
          await this.sleep(80 * (attempt + 1))
        }
      }
      throw new Error('Serial reader busy')
    }

    async _prepareConfigClientMode () {
      if (this.mode === 'CONFIG') {
        this._stopModeNotifyLoop()
        await this._suspendModeNotify()
        this.mode = null
        this.emit('mode:changed', { mode: null })
      } else if (this.mode) {
        await this._exitModeImpl()
      } else if (!this._rxPumpRunning) {
        this._resumeRxPump()
      }
    }

    async _prepareScenesClientMode () {
      if (this.mode === 'SCENES') {
        this._stopModeNotifyLoop()
        await this._suspendModeNotify()
        this.mode = null
        this.emit('mode:changed', { mode: null })
      } else if (this.mode) {
        await this._exitModeImpl()
      } else if (!this._rxPumpRunning) {
        this._resumeRxPump()
      }
    }

    /** Release the rx pump reader after a one-shot pump command (e.g. INFO). */
    async _releasePumpAfterCommand () {
      await this._suspendRxPump()
    }

    // Reset pump/mode readers after a mid-transfer dropout or failed acquire.
    async recoverSerialState () {
      if (!this.port) return false
      const impl = async () => {
        this._releaseAllReaders()
        this._pumpSuspended = false
        this._modeNotifySuspended = false
        this._lineQueue = []
        this._rejectLineWaiters()
        if (this.mode || this._deviceScenesActive) {
          this._stopModeNotifyLoop()
          try {
            await this.sendRaw('EXIT\n')
            await this.sleep(150)
          } catch (e) {}
          this.mode = null
          this._deviceScenesActive = false
          this.emit('mode:changed', { mode: null })
        }
        this._rxBuffer = ''
        if (!this._rxPumpRunning) this._startRxPump()
        else this._resumeRxPump()
        await this._armRxPump(2000)
        return true
      }
      return this.runSerialTask(impl)
    }

    _isRecoverableSerialError (err) {
      const msg = String(err?.message || err)
      return msg.includes('No response') ||
        msg.includes('Not connected') ||
        msg.includes('Serial reader busy') ||
        msg.includes('locked') ||
        msg.includes('Incomplete download') ||
        msg.includes('Failed to fetch')
    }

    _releaseRxPumpReader () {
      if (!this._rxPumpReader) return
      try {
        this._rxPumpReader.releaseLock()
      } catch (e) {}
      this._rxPumpReader = null
    }

    async _waitForPumpReaderRelease (ms = 500) {
      const deadline = Date.now() + ms
      while (this._rxPumpReader && Date.now() < deadline) {
        await this.sleep(10)
      }
    }

    async _suspendRxPump () {
      this._pumpSuspended = true
      const reader = this._rxPumpReader
      if (reader) {
        try {
          await reader.cancel()
        } catch (e) {}
      }
      await this._waitForPumpReaderRelease()
      if (this._rxPumpReader) this._releaseRxPumpReader()
    }

    _resumeRxPump () {
      this._pumpSuspended = false
      if (this.port) this._processIncomingLines()
      if (!this._rxPumpRunning && this.port && this.mode === null) {
        this._startRxPump()
      }
    }

    _releaseModeNotifyReader () {
      if (!this._modeNotifyReader) return
      try {
        this._modeNotifyReader.releaseLock()
      } catch (e) {}
      this._modeNotifyReader = null
    }

    async _waitForModeNotifyReaderRelease (ms = 500) {
      const deadline = Date.now() + ms
      while (this._modeNotifyReader && Date.now() < deadline) {
        await this.sleep(10)
      }
    }

    async _suspendModeNotify () {
      if (!this.mode) return
      this._modeNotifySuspended = true
      const reader = this._modeNotifyReader
      if (reader) {
        try {
          await reader.cancel()
        } catch (e) {}
      }
      await this._waitForModeNotifyReaderRelease()
      if (this._modeNotifyReader) this._releaseModeNotifyReader()
    }

    _resumeModeNotify () {
      this._modeNotifySuspended = false
      if (this.port) this._processIncomingLines()
      if (!this._modeNotifyRunning && this.mode && this.port) {
        this._startModeNotifyLoop()
      }
    }

    _startModeNotifyLoop () {
      if (this._modeNotifyRunning || !this.mode) return
      // These modes own the readable stream themselves and must not share it
      // with a background reader, which would compete for the single port
      // reader and swallow bytes out from under them:
      //   ASSETS  - request/response lines + binary transfers
      //   CONFIG  - request/response lines
      //   DISPLAY - binary frames
      //   UPDATE  - updater controller's dedicated reader (firmware handshake)
      //   CONSOLE - pipeTo() on port.readable
      //   MIDI    - pipeTo() on port.readable for the relay loop
      //   SETTINGS- its own getReader()-based readLine
      if (this.mode === 'ASSETS' || this.mode === 'CONFIG' ||
          this.mode === 'DISPLAY' || this.mode === 'UPDATE' ||
          this.mode === 'CONSOLE' || this.mode === 'MIDI' ||
          this.mode === 'SETTINGS' || this.mode === 'SCENES') return
      this._modeNotifyRunning = true
      this._modeNotifyLoop()
    }

    _stopModeNotifyLoop () {
      this._modeNotifyRunning = false
      const reader = this._modeNotifyReader
      if (reader) {
        reader.cancel().catch(() => {})
      }
      this._releaseModeNotifyReader()
    }

    async _modeNotifyLoop () {
      while (this._modeNotifyRunning && this.port && this.mode !== null) {
        if (this._modeNotifySuspended || !this.port.readable) {
          await this.sleep(30)
          continue
        }

        let reader = null
        try {
          reader = this.port.readable.getReader()
          this._modeNotifyReader = reader

          while (
            this._modeNotifyRunning &&
            !this._modeNotifySuspended &&
            this.mode !== null &&
            this.port
          ) {
            const { value, done } = await reader.read()
            if (this._modeNotifySuspended) break
            if (done) break
            if (value?.length > 0) {
              this._rxBuffer += this._rxDecoder.decode(value, { stream: true })
              this._processIncomingLines()
            }
          }
        } catch (err) {
          // Ignore read errors during reader handoff
        } finally {
          if (reader) {
            try {
              reader.releaseLock()
            } catch (e) {}
          }
          this._modeNotifyReader = null
        }

        await this.sleep(20)
      }
      this._releaseModeNotifyReader()
      this._modeNotifyRunning = false
    }

    _rejectLineWaiters () {
      const waiters = this._lineWaiters
      this._lineWaiters = []
      for (const waiter of waiters) waiter.resolve('')
    }

    _startRxPump () {
      if (this._rxPumpRunning) return
      this._rxPumpRunning = true
      this._rxPumpLoop()
    }

    _stopRxPump () {
      this._rxPumpRunning = false
      this._releaseRxPumpReader()
      this._lineQueue = []
      this._rejectLineWaiters()
    }

    async _rxPumpLoop () {
      while (this._rxPumpRunning && this.port) {
        if (this._pumpSuspended || this.mode !== null || !this.port.readable) {
          await this.sleep(30)
          continue
        }

        let reader = null
        try {
          reader = this.port.readable.getReader()
          this._rxPumpReader = reader

          while (
            this._rxPumpRunning &&
            !this._pumpSuspended &&
            this.mode === null &&
            this.port
          ) {
            const { value, done } = await reader.read()
            if (this._pumpSuspended) break
            if (done) break
            if (value?.length > 0) {
              this._rxBuffer += this._rxDecoder.decode(value, { stream: true })
              this._processIncomingLines()
            }
          }
        } catch (err) {
          // Ignore pump read errors during reader handoff
        } finally {
          if (reader) {
            try {
              reader.releaseLock()
            } catch (e) {}
          }
          this._rxPumpReader = null
        }

        await this.sleep(20)
      }
      this._releaseRxPumpReader()
      this._rxPumpRunning = false
    }

    _repairJsonLine (line) {
      const t = line.trim()
      if (t.startsWith('{')) return t
      if (t.startsWith('"')) return '{' + t
      return t
    }

    _isNoiseLine (line) {
      if (!line) return true
      if (line.startsWith('EVT:')) return true
      if (/^I \(/.test(line)) return true
      return false
    }

    _expectedJsonMarker (cmd) {
      const base = cmd.split(/\s+/)[0]
      if (base === 'INFO') return '"version"'
      if (base === 'SCENE_INSPECT') return '"text"'
      return null
    }

    _extractFirstJson (line) {
      const start = line.indexOf('{')
      if (start === -1) return line
      let depth = 0
      let inString = false
      let escape = false
      for (let i = start; i < line.length; i++) {
        const c = line[i]
        if (escape) {
          escape = false
          continue
        }
        if (c === '\\' && inString) {
          escape = true
          continue
        }
        if (c === '"') {
          inString = !inString
          continue
        }
        if (inString) continue
        if (c === '{') depth++
        else if (c === '}') {
          depth--
          if (depth === 0) return line.slice(start, i + 1)
        }
      }
      return line.slice(start)
    }

    _mergeRxTail (rxBuf) {
      if (!rxBuf) return
      this._rxBuffer = rxBuf + this._rxBuffer
      this._processIncomingLines()
    }

    _takeJsonLineFromBuffer (buffer) {
      while (true) {
        const idx = buffer.indexOf('\n')
        if (idx === -1) return { line: null, buffer }
        const line = buffer.substring(0, idx).replace(/\r/g, '').trim()
        buffer = buffer.substring(idx + 1)
        if (!line) continue
        if (line.startsWith('EVT:')) {
          this.dispatchCdcNotify(line)
          continue
        }
        if (this._isNoiseLine(line)) continue
        if (line === 'SCENES_STOPPED' || line === 'SCENES_STARTED' ||
            line === 'CONFIG_STOPPED' || line === 'SETTINGS_STOPPED' ||
            line === 'PEDALS_STOPPED') continue
        return { line, buffer }
      }
    }

    // Legacy helper — prefer _sendAndReadJsonLine.
    async _readOneJsonLine (timeout = 30000) {
      const reader = this.port.readable.getReader()
      const decoder = new TextDecoder()
      let buffer = this._rxBuffer
      this._rxBuffer = ''
      const deadline = Date.now() + timeout
      let pendingRead = null

      try {
        while (Date.now() < deadline) {
          let taken = this._takeJsonLineFromBuffer(buffer)
          buffer = taken.buffer
          if (taken.line) return taken.line

          const trimmed = buffer.trim()
          if (trimmed.startsWith('{') && trimmed.endsWith('}')) return trimmed

          if (!pendingRead) pendingRead = reader.read()
          const waitMs = Math.min(100, deadline - Date.now())
          if (waitMs <= 0) break
          const result = await Promise.race([
            pendingRead,
            this.sleep(waitMs).then(() => ({ timeout: true }))
          ])
          if (result.timeout) continue
          pendingRead = null
          if (result.done) break
          if (result.value?.length) {
            buffer += decoder.decode(result.value, { stream: true })
          }
        }
      } finally {
        this._rxBuffer = buffer + this._rxBuffer
        try { reader.releaseLock() } catch (e) {}
      }
      const trimmed = buffer.trim()
      if (trimmed.startsWith('{') && trimmed.endsWith('}')) return trimmed
      return ''
    }

    // sendRaw before locking readable — matches INFO path; holding a reader
    // during sendRaw can orphan in-flight reads and drop the response.
    async _sendAndReadJsonLine (cmd, timeout = 30000) {
      if (!this.port) throw new Error('Not connected')
      this._lineQueue = []
      this._rejectLineWaiters()
      await this._suspendRxPump()
      await this._waitForPumpReaderRelease()
      const hadMode = this.mode !== null
      if (hadMode) {
        await this._suspendModeNotify()
        await this._waitForModeNotifyReaderRelease()
      }

      this._rxBuffer = ''
      const decoder = new TextDecoder()
      let buffer = ''
      let reader = null

      try {
        await this.sendRaw(cmd + '\n')
        reader = this.port.readable.getReader()

        const deadline = Date.now() + timeout
        let pendingRead = null
        while (Date.now() < deadline) {
          let taken = this._takeJsonLineFromBuffer(buffer)
          buffer = taken.buffer
          if (taken.line) return taken.line

          const trimmed = buffer.trim()
          if (trimmed.startsWith('{') && trimmed.endsWith('}')) return trimmed

          if (!pendingRead) pendingRead = reader.read()
          const waitMs = Math.min(100, deadline - Date.now())
          if (waitMs <= 0) break
          const result = await Promise.race([
            pendingRead,
            this.sleep(waitMs).then(() => ({ timeout: true }))
          ])
          if (result.timeout) continue
          pendingRead = null
          if (result.done) break
          if (result.value?.length) {
            buffer += decoder.decode(result.value, { stream: true })
          }
        }

        const trimmed = buffer.trim()
        if (trimmed.startsWith('{') && trimmed.endsWith('}')) return trimmed
        console.error('Serial JSON command timeout', {
          cmd,
          bytes: buffer.length,
          head: buffer.slice(0, 120)
        })
        return ''
      } finally {
        this._rxBuffer = buffer + this._rxBuffer
        if (reader) {
          try { reader.releaseLock() } catch (e) {}
        }
        if (hadMode) this._resumeModeNotifyIfAllowed()
        else this._resumeRxPump()
      }
    }

    async _armRxPump (timeoutMs = 1000) {
      this._resumeRxPump()
      const deadline = Date.now() + timeoutMs
      while (!this._rxPumpReader && Date.now() < deadline) await this.sleep(20)
    }

    // Send a command expecting OK or ERROR: through the persistent rx pump.
    // Unlike _sendCommandImpl, this never acquires a dedicated reader after a
    // prior command, avoiding a Chromium Web Serial stall. Caller must already
    // be inside a serial task with mode === null.
    async _sendOkCommandViaPump (cmd, timeout = 30000) {
      if (!this.port) throw new Error('Not connected')

      await this._armRxPump()
      this._lineQueue = []
      this._rejectLineWaiters()
      this._rxBuffer = ''

      await this.sendRaw(cmd + '\n')

      const deadline = Date.now() + timeout
      while (Date.now() < deadline) {
        const remaining = deadline - Date.now()
        if (remaining <= 0) break
        const line = await this.readLine(Math.min(1000, remaining))
        if (!line) continue
        if (line === 'OK') return 'OK'
        if (line.startsWith('ERROR:')) return line
        if (line === 'SCENES_STOPPED' || line === 'SCENES_STARTED' ||
            line === 'CONFIG_STOPPED' || line === 'SETTINGS_STOPPED' ||
            line === 'PEDALS_STOPPED') continue
      }
      return ''
    }

    // Send a command and read a single JSON response line through the persistent
    // rx pump (mode === null). Unlike _sendCommandImpl, this never acquires a
    // fresh dedicated reader right after a cancel(), which avoids a Chromium
    // Web Serial stall where a pending read() does not wake on newly-arrived
    // bytes. Caller must already be inside a serial task and have mode === null.
    async _sendCommandViaPump (cmd, timeout = 30000, validator = null, options = null) {
      if (!this.port) throw new Error('Not connected')
      const preludeExit = options?.preludeExit === true

      // Make the single pump reader the active consumer and wait until it has
      // actually acquired the port reader (steady-state pull, not post-cancel).
      this._resumeRxPump()
      const armDeadline = Date.now() + 1000
      while (!this._rxPumpReader && Date.now() < armDeadline) await this.sleep(20)

      if (preludeExit) await this.sendRaw('EXIT\n')
      await this.sendRaw(cmd + '\n')

      const expectMarker = validator ? this._expectedJsonMarker(cmd) : null
      const deadline = Date.now() + timeout
      let acc = ''
      while (Date.now() < deadline) {
        const line = await this.readLine(Math.min(1000, Math.max(1, deadline - Date.now())))
        if (!line) continue
        if (line.startsWith('ERROR:')) return line
        if (line === 'SCENES_STOPPED' || line === 'SCENES_STARTED' ||
            line === 'CONFIG_STOPPED' || line === 'SETTINGS_STOPPED' ||
            line === 'PEDALS_STOPPED') continue
        if (!line.includes('{') && !line.includes('"')) continue

        acc += line
        try {
          const data = JSON.parse(this._extractFirstJson(this._repairJsonLine(acc)))
          if (!validator || validator(data)) return JSON.stringify(data)
        } catch (_) {
          if (expectMarker && !acc.includes(expectMarker)) { acc = ''; continue }
          if (line.endsWith('}')) acc = ''
        }
      }
      return ''
    }

    async _tryParseCommandJson (rawLine, validator) {
      if (!rawLine.startsWith('{') && !rawLine.startsWith('[')) return null
      try {
        const data = JSON.parse(rawLine)
        if (validator && !validator(data)) return null
        return rawLine
      } catch (_) {
        return null
      }
    }

    async sendCommand (cmd, timeout = 30000, validator = null) {
      return this.runSerialTask(() => this._sendCommandImpl(cmd, timeout, validator))
    }

    async _sendCommandImpl (cmd, timeout = 30000, validator = null, options = null) {
      const preludeExit = options?.preludeExit === true
      if (!this.port) throw new Error('Not connected')
      this._lineQueue = []
      this._rejectLineWaiters()
      await this._suspendRxPump()
      await this._waitForPumpReaderRelease()
      const hadMode = this.mode !== null
      if (hadMode) {
        await this._suspendModeNotify()
        await this._waitForModeNotifyReaderRelease()
      }

      const expectMarker = validator ? this._expectedJsonMarker(cmd) : null
      this._rxBuffer = ''
      let rxBuf = ''
      const decoder = new TextDecoder()
      let jsonAcc = ''

      try {
        if (preludeExit) {
          await this.sendRaw('EXIT\n')
          await this.sleep(150)
        }
        await this.sendRaw(cmd + '\n')
        let reader = null
        try {
          reader = await this._acquireDedicatedReader()
        } catch (getErr) {
          throw getErr
        }
        const deadline = Date.now() + timeout
        let pendingRead = null
        try {
          while (Date.now() < deadline) {
            if (!pendingRead) pendingRead = reader.read()
            const waitMs = Math.min(100, deadline - Date.now())
            if (waitMs <= 0) break
            const result = await Promise.race([
              pendingRead,
              this.sleep(waitMs).then(() => ({ timeout: true }))
            ])
            if (result.timeout) continue
            pendingRead = null
            if (result.done) break
            if (!result.value?.length) continue

            rxBuf += decoder.decode(result.value, { stream: true })
            while (true) {
              const idx = rxBuf.indexOf('\n')
              if (idx === -1) break
              const rawLine = rxBuf.substring(0, idx).replace(/\r/g, '').trim()
              rxBuf = rxBuf.substring(idx + 1)
              if (!rawLine) continue

              for (const line of this._expandSerialLines(rawLine)) {
                if (line.startsWith('EVT:')) {
                  this.dispatchCdcNotify(line)
                  continue
                }
                if (this._isNoiseLine(line)) continue
                if (line === 'SCENES_STOPPED' || line === 'SCENES_STARTED' ||
                    line === 'CONFIG_STOPPED' || line === 'SETTINGS_STOPPED' ||
                    line === 'PEDALS_STOPPED') continue

                const direct = this._tryParseCommandJson(line, validator)
                if (direct) {
                  this._mergeRxTail(rxBuf)
                  return direct
                }

                if (line.startsWith('ERROR:')) {
                  this._mergeRxTail(rxBuf)
                  return line
                }
                if (!validator && line.startsWith('[')) {
                  try {
                    JSON.parse(line)
                    this._mergeRxTail(rxBuf)
                    return line
                  } catch (_) { /* accumulate below */ }
                }
                if (!validator) {
                  if (!line.includes('{') && !line.includes('[')) {
                    this._mergeRxTail(rxBuf)
                    return line
                  }
                }
                if (!line.includes('{') && !line.includes('"')) continue

                jsonAcc += line
                const candidate = this._repairJsonLine(jsonAcc)
                const json = this._extractFirstJson(candidate)
                try {
                  const data = JSON.parse(json)
                  if (validator && !validator(data)) {
                    if (expectMarker && !jsonAcc.includes(expectMarker)) {
                      jsonAcc = ''
                      continue
                    }
                    continue
                  }
                  this._mergeRxTail(rxBuf)
                  return json
                } catch (e) {
                  if (expectMarker && !jsonAcc.includes(expectMarker)) {
                    jsonAcc = ''
                    continue
                  }
                  if (line.endsWith('}')) jsonAcc = ''
                }
              }
            }
          }
        } finally {
          if (reader) {
            try { reader.releaseLock() } catch (e) {}
          }
        }

        this._mergeRxTail(rxBuf)
        if (jsonAcc) {
          const fallback = this._tryParseCommandJson(
            this._extractFirstJson(this._repairJsonLine(jsonAcc)),
            validator
          )
          if (fallback) return fallback
        }
        return ''
      } finally {
        if (hadMode) this._resumeModeNotifyIfAllowed()
        else this._resumeRxPump()
      }
    }

    // Pull the next CR/LF-terminated line from the device. Anything that
    // arrived after the line terminator stays in this._rxBuffer for the
    // next call -- this is how we avoid losing back-to-back lines that
    // happen to share a single USB chunk.
    _waitForLine (ms) {
      return new Promise(resolve => {
        const line = this._tryDequeueLine()
        if (line !== null) {
          resolve(line)
          return
        }
        const entry = {
          resolve: (l) => {
            clearTimeout(entry.timer)
            resolve(l)
          }
        }
        entry.timer = setTimeout(() => {
          const idx = this._lineWaiters.indexOf(entry)
          if (idx >= 0) {
            this._lineWaiters.splice(idx, 1)
            resolve(null)
          }
        }, ms)
        this._lineWaiters.push(entry)
      })
    }

    async readLine (timeout = 10000) {
      if (!this.port?.readable) return null
      return this.runSerialTask(() => this._readLineBody(timeout))
    }

    _usesPumpLineReader () {
      const exclusiveModes = new Set([
        'ASSETS', 'DISPLAY', 'UPDATE', 'CONSOLE', 'MIDI', 'PEDALS'
      ])
      return this.mode === null || !exclusiveModes.has(this.mode)
    }

    async _readLineBody (timeout = 10000) {
      if (!this._usesPumpLineReader()) return this._readLineExclusive(timeout)

      if (!this._rxPumpRunning || this._pumpSuspended) {
        this._resumeRxPump()
        await this._armRxPump(Math.min(500, timeout))
      }

      const deadline = Date.now() + timeout
      while (Date.now() < deadline) {
        const line = this._tryDequeueLine()
        if (line !== null) return line

        const remaining = deadline - Date.now()
        if (remaining <= 0) break
        const waited = await this._waitForLine(Math.min(50, remaining))
        if (waited !== null) return waited
      }

      return ''
    }

    async _readLineExclusive (timeout = 10000) {
      await this._suspendRxPump()
      await this._suspendModeNotify()
      let reader = null
      const decoder = new TextDecoder()
      let buffer = ''

      const takeResponseLine = () => {
        while (true) {
          const idx = buffer.indexOf('\n')
          if (idx === -1) return null
          const composite = buffer.substring(0, idx).replace(/\r/g, '').trim()
          buffer = buffer.substring(idx + 1)
          if (!composite) continue
          for (const line of this._expandSerialLines(composite)) {
            if (this.dispatchCdcNotify(line)) continue
            if (/^I \(/.test(line)) continue
            return line
          }
        }
      }

      try {
        reader = await this._acquireDedicatedReader()
        const deadline = Date.now() + timeout
        let pendingRead = null

        while (Date.now() < deadline) {
          const ready = takeResponseLine()
          if (ready !== null) return ready

          if (!pendingRead) pendingRead = reader.read()

          const remainingTime = Math.max(deadline - Date.now(), 0)
          const result = await Promise.race([
            pendingRead,
            this.sleep(Math.min(remainingTime, 100)).then(() => ({ timeout: true }))
          ])

          if (result.timeout) continue

          pendingRead = null
          if (result.done) break
          if (result.value) {
            buffer += decoder.decode(result.value, { stream: true })
          }
        }
      } finally {
        if (reader) {
          try { reader.releaseLock() } catch (e) {}
        }
        this._resumeModeNotifyIfAllowed()
      }
      return buffer.trim()
    }

    _concatBytes (a, b) {
      if (!a?.length) return b || new Uint8Array(0)
      if (!b?.length) return a
      const out = new Uint8Array(a.length + b.length)
      out.set(a)
      out.set(b, a.length)
      return out
    }

    _takeLineFromBytes (buffer) {
      for (let i = 0; i < buffer.length; i++) {
        if (buffer[i] !== 0x0a) continue
        let end = i
        if (end > 0 && buffer[end - 1] === 0x0d) end--
        const line = new TextDecoder().decode(buffer.slice(0, end)).trim()
        const rest = buffer.slice(i + 1)
        return { line, rest }
      }
      return { line: null, rest: buffer }
    }

    _takeFromBinary (arr, data, state) {
      let pos = 0
      let progress = false
      while (pos < arr.length && state.consumed < state.total) {
        const take = Math.min(arr.length - pos, state.total - state.consumed)
        if (state.received < state.size) {
          const dataTake = Math.min(take, state.size - state.received)
          data.set(arr.subarray(pos, pos + dataTake), state.received)
          state.received += dataTake
        }
        pos += take
        state.consumed += take
        progress = true
      }
      return { rest: arr.subarray(pos), progress }
    }

    // SIZE line + raw binary in one exclusive read (MANIFEST/GET). Avoids resuming
    // the mode-notify reader between sendCommand and readBinary (stream lock race).
    async fetchSizedTransfer (cmd, options = {}) {
      return this.runSerialTask(() => this._fetchSizedTransferImpl(cmd, options))
    }

    _binaryStallTimeoutMs (size, explicit) {
      if (explicit != null) return explicit
      // USB FS bulk is typically ~1 MB/s; this is idle-stall budget, not total transfer time.
      const scaled = 5000 + Math.ceil(size / 16384) * 750
      return Math.min(20000, Math.max(8000, scaled))
    }

    async _fetchSizedTransferImpl (cmd, options = {}) {
      const lineTimeout = options.lineTimeout ?? 10000
      let binaryStallMs = options.binaryTimeout ?? null

      if (!this.port) throw new Error('Not connected')
      this._lineQueue = []
      this._rejectLineWaiters()
      await this._suspendRxPump()
      await this._waitForPumpReaderRelease()
      const hadMode = this.mode !== null
      if (hadMode) {
        await this._suspendModeNotify()
        await this._waitForModeNotifyReaderRelease()
      }

      let reader = null
      let buffer = new Uint8Array(0)
      let responseLine = ''

      try {
        reader = await this._acquireDedicatedReader()
        await this.sendRaw(cmd + '\n')
        const lineDeadline = Date.now() + lineTimeout
        let pendingRead = null

        while (!responseLine && Date.now() < lineDeadline) {
          if (!pendingRead) pendingRead = reader.read()
          const waitMs = Math.min(100, lineDeadline - Date.now())
          if (waitMs <= 0) break
          const result = await Promise.race([
            pendingRead,
            this.sleep(waitMs).then(() => ({ timeout: true }))
          ])
          if (result.timeout) continue
          pendingRead = null
          if (result.done) break
          if (result.value?.length) {
            buffer = this._concatBytes(buffer, result.value)
            while (true) {
              const { line, rest } = this._takeLineFromBytes(buffer)
              buffer = rest
              if (!line) break
              for (const sub of this._expandSerialLines(line)) {
                if (sub.startsWith('EVT:')) {
                  this.dispatchCdcNotify(sub)
                  continue
                }
                if (this._isNoiseLine(sub)) continue
                responseLine = sub
                break
              }
              if (responseLine) break
            }
          }
        }

        if (!responseLine) throw new Error('No response')
        if (responseLine.startsWith('ERROR:')) throw new Error(responseLine)
        if (!responseLine.startsWith('SIZE ')) {
          throw new Error(`Unexpected response: ${responseLine}`)
        }

        const size = parseInt(responseLine.split(' ')[1], 10)
        if (Number.isNaN(size) || size < 0) {
          throw new Error(`Invalid SIZE: ${responseLine}`)
        }

        if (binaryStallMs == null) {
          binaryStallMs = this._binaryStallTimeoutMs(size, null)
        }

        const termLen = (size > 0 && size % 64 === 0) ? 1 : 0
        const total = size + termLen

        const data = new Uint8Array(size)
        const binState = { consumed: 0, received: 0, size, total }
        let carry = buffer
        let binDeadline = Date.now() + binaryStallMs

        const drainCarry = () => {
          if (!carry.length) return false
          const { rest, progress } = this._takeFromBinary(carry, data, binState)
          carry = rest
          if (progress) binDeadline = Date.now() + binaryStallMs
          return progress
        }

        drainCarry()
        while (binState.consumed < total && Date.now() < binDeadline) {
          if (binState.consumed >= total) break
          const { value, done } = await reader.read()
          if (done) break
          if (value?.length) carry = this._concatBytes(carry, value)
          drainCarry()
        }
        drainCarry()

        const { received, consumed } = binState
        if (received !== size) {
          throw new Error(`Incomplete download: ${received}/${size} bytes`)
        }

        if (carry.length) {
          this._rxBuffer = new TextDecoder().decode(carry) + this._rxBuffer
          this._processIncomingLines()
        }

        return { line: responseLine, data }
      } finally {
        if (reader) {
          try { reader.releaseLock() } catch (e) {}
        }
        if (hadMode) this._resumeModeNotifyIfAllowed()
        else this._resumeRxPump()
      }
    }

    async readBinary (size, timeout = null) {
      await this._suspendRxPump()
      await this._waitForPumpReaderRelease()
      await this._suspendModeNotify()
      await this._waitForModeNotifyReaderRelease()
      let reader = null
      const data = new Uint8Array(size)
      // Firmware appends a 1-byte short-packet terminator when the payload is an
      // exact multiple of the 64-byte FS bulk packet size; consume and discard it.
      const termLen = (size > 0 && size % 64 === 0) ? 1 : 0
      const total = size + termLen
      const stallMs = this._binaryStallTimeoutMs(size, timeout)
      let received = 0
      let consumed = 0
      let deadline = Date.now() + stallMs

      try {
        reader = await this._acquireDedicatedReader()
        while (consumed < total && Date.now() < deadline) {
          const { value, done } = await reader.read()
          if (done) break
          if (value?.length) {
            if (received < size) {
              const dataTake = Math.min(value.length, size - received)
              data.set(value.slice(0, dataTake), received)
              received += dataTake
            }
            const take = Math.min(value.length, total - consumed)
            consumed += take
            if (take > 0) deadline = Date.now() + stallMs
          }
        }
      } finally {
        if (reader) {
          try { reader.releaseLock() } catch (e) {}
        }
        if (this.mode !== null) this._resumeModeNotifyIfAllowed()
        else this._resumeRxPump()
      }
      return data.slice(0, received)
    }

    async sendBinary (data) {
      const run = async () => {
        const writer = this.port.writable.getWriter()
        const chunkSize = 1024
        try {
          for (let i = 0; i < data.length; i += chunkSize) {
            await writer.write(data.slice(i, i + chunkSize))
          }
        } finally {
          writer.releaseLock()
        }
      }
      const result = this._writeChain.then(run, run)
      this._writeChain = result.then(() => {}, () => {})
      return result
    }

    async drainInput () {
      // Clear any line-fragments buffered by readLine, then swallow any
      // bytes that the device still has in flight. Both have to happen --
      // otherwise a stale partial line would be returned by the next
      // readLine call as if it were fresh data.
      this._rxBuffer = ''
      this._rxDecoder = new TextDecoder()
      this._lineQueue = []
      this._rejectLineWaiters()
      if (!this.port?.readable) return

      const hadMode = this.mode !== null
      await this._suspendRxPump()
      if (hadMode) await this._suspendModeNotify()
      const reader = this.port.readable.getReader()
      try {
        while (true) {
          const result = await Promise.race([
            reader.read(),
            this.sleep(200).then(() => ({ timeout: true }))
          ])
          if (result.timeout || result.done) break
        }
      } catch (e) {
        // Ignore drain errors
      } finally {
        reader.releaseLock()
        if (hadMode) this._resumeModeNotifyIfAllowed()
        else this._resumeRxPump()
      }
    }

    sleep (ms) {
      return new Promise(resolve => setTimeout(resolve, ms))
    }

    // Get readable stream for continuous reading
    getReader () {
      if (!this.port?.readable) return null
      return this.port.readable.getReader()
    }
  }

  return {
    getInstance () {
      if (!instance) instance = new ConnectionManager()
      return instance
    }
  }
})()

// Initialize Stimulus application
const { Application, Controller } = Stimulus
window.application = Application.start()

// Base controller with common functionality
window.BaseController = class extends Controller {
  get connection () {
    return ConnectionManager.getInstance()
  }

  log (message, type = '') {
    const logContent = this.hasLogContentTarget ? this.logContentTarget : null
    if (!logContent) return

    const entry = document.createElement('div')
    entry.className = `log-entry ${type}`
    entry.textContent = `[${new Date().toLocaleTimeString()}] ${message}`
    logContent.appendChild(entry)
    logContent.scrollTop = logContent.scrollHeight
  }

  clearLog () {
    if (this.hasLogContentTarget) {
      this.logContentTarget.innerHTML = ''
    }
  }

  formatSize (bytes) {
    if (bytes === 0) return '0 B'
    const units = ['B', 'KB', 'MB', 'GB']
    const exp = Math.min(
      Math.floor(Math.log(bytes) / Math.log(1024)),
      units.length - 1
    )
    return `${(bytes / Math.pow(1024, exp)).toFixed(exp === 0 ? 0 : 1)} ${
      units[exp]
    }`
  }

  sleep (ms) {
    return new Promise(resolve => setTimeout(resolve, ms))
  }
}

// App controller for global state
application.register(
  'app',
  class extends Controller {
    static targets = ['statusDot', 'statusText', 'connectionBtn']

    connect () {
      this.connection = ConnectionManager.getInstance()
      this.connection.on(
        'connection:changed',
        this.onConnectionChanged.bind(this)
      )

      // Check WebSerial support
      if (!navigator.serial) {
        this.statusTextTarget.textContent = 'WebSerial not supported'
        this.connectionBtnTarget.disabled = true
      }

      // Listen for tab changes on the wa-tab-group
      const tabGroup = document.querySelector('wa-tab-group')
      if (tabGroup) {
        tabGroup.addEventListener('wa-tab-show', this.onTabShow.bind(this))
      }
    }

    onConnectionChanged ({ connected }) {
      this.statusDotTarget.classList.toggle('connected', connected)
      this.statusTextTarget.textContent = connected
        ? 'Connected'
        : 'Disconnected'

      // Update button appearance and text
      const btn = this.connectionBtnTarget
      if (connected) {
        btn.textContent = 'Disconnect'
        btn.variant = 'danger'
      } else {
        btn.textContent = 'Connect'
        btn.variant = 'success'
      }

      if (connected) {
        void this.connection.recoverSerialState().finally(() => {
          this.activateCurrentTab()
        })
      } else {
        this.navigateToTab('info')
      }
    }

    onTabShow (event) {
      // Dispatch custom event that tab controllers listen for
      const panelName = event.detail?.name || event.target?.panel
      if (panelName) {
        document.dispatchEvent(
          new CustomEvent('app:tab-activated', { detail: { tab: panelName } })
        )
      }
    }

    activateCurrentTab () {
      const tabGroup = document.querySelector('wa-tab-group')
      if (!tabGroup) return
      // Get the active tab's panel name
      const activeTab = tabGroup.querySelector('wa-tab[active]')
      const panelName = activeTab?.getAttribute('panel') || 'assets'
      document.dispatchEvent(
        new CustomEvent('app:tab-activated', { detail: { tab: panelName } })
      )
    }

    async toggleConnection () {
      try {
        if (this.connection.isConnected) {
          await this.connection.disconnect()
        } else {
          await this.connection.connect()
        }
      } catch (err) {
        console.error('Connection toggle failed:', err)
      }
    }

    // Navigate to a specific tab
    navigateToTab (tabName, params = {}) {
      const tabGroup = document.querySelector('wa-tab-group')
      if (tabGroup) {
        // WebAwesome tabs don't have a show() method - click the tab directly
        const tab = tabGroup.querySelector(`wa-tab[panel="${tabName}"]`)
        if (tab) {
          tab.click()
        }
        // Emit event for the tab to handle params
        document.dispatchEvent(
          new CustomEvent('app:tab-params', {
            detail: { tab: tabName, params }
          })
        )
      }
    }
  }
)

// Listen for cross-tab navigation
document.addEventListener('app:navigate-tab', e => {
  const { tab, params } = e.detail
  const appController = application.getControllerForElementAndIdentifier(
    document.querySelector('[data-controller~="app"]'),
    'app'
  )
  if (appController) {
    appController.navigateToTab(tab, params)
  }
})
