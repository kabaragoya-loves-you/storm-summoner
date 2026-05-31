/* Storm Summoner - Main Application */

// Connection Manager - Singleton for WebSerial connection
window.ConnectionManager = (function () {
  let instance = null

  class ConnectionManager {
    constructor () {
      this.port = null
      this.mode = null // ASSETS, CONSOLE, DISPLAY, UPDATE, RPC
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
      this._serialDepth = 0
      this._modeNotifyRunning = false
      this._modeNotifyReader = null
      this._modeNotifySuspended = false
      this._exclusiveSession = 0
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

    // Serialize every WebSerial transaction. One task at a time — never bypass
    // the queue (reentrant bypass caused concurrent reader locks mid-await).
    runSerialTask (fn) {
      const run = async () => {
        this._serialDepth++
        try {
          return await fn()
        } finally {
          this._serialDepth--
        }
      }

      const result = this._commandChain.then(run, run)
      this._commandChain = result.then(() => {}, () => {})
      return result
    }

    // Connect to device
    async connect () {
      if (this.port) return true

      try {
        this.port = await navigator.serial.requestPort({
          filters: [{ usbVendorId: 0x303a }]
        })
        await this.port.open({ baudRate: 115200 })
        await this.port.setSignals({
          dataTerminalReady: true,
          requestToSend: true
        })

        // Listen for unexpected disconnection
        navigator.serial.addEventListener('disconnect', this.onPortDisconnect)

        this._startRxPump()
        await this.sleep(50)
        await this.drainInput()
        this.emit('connection:changed', { connected: true })
        return true
      } catch (err) {
        this.port = null
        throw err
      }
    }

    // Handle unexpected port disconnection
    onPortDisconnect = event => {
      if (event.target === this.port) {
        console.log('USB device disconnected')
        this._stopRxPump()
        this._stopModeNotifyLoop()
        this.port = null
        this.mode = null
        this._rxBuffer = ''
        this._lineQueue = []
        this._lineWaiters = []
        this._commandChain = Promise.resolve()
        this._serialDepth = 0
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
        this._rxBuffer = ''
        this._lineQueue = []
        this._lineWaiters = []
        this._commandChain = Promise.resolve()
        this._serialDepth = 0
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

      if (this.mode) {
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

    async _exitModeImpl () {
      if (!this.mode || !this.port) return
      this._stopModeNotifyLoop()
      await this._suspendModeNotify()
      this.mode = null
      this.emit('mode:changed', { mode: null })
      try {
        await this.sendRaw('EXIT\n')
        await this.sleep(100)
        await this.drainInput()
      } catch (err) {
        // Ignore exit errors
      }
      this._resumeRxPump()
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

    // Low-level I/O
    async sendRaw (data) {
      if (!this.port?.writable) throw new Error('Port not writable')
      const writer = this.port.writable.getWriter()
      try {
        await writer.write(new TextEncoder().encode(data))
      } finally {
        writer.releaseLock()
      }
    }

    _takeLineFromBuffer () {
      const idx = this._rxBuffer.indexOf('\n')
      if (idx === -1) return null
      const line = this._rxBuffer.substring(0, idx).replace(/\r/g, '').trim()
      this._rxBuffer = this._rxBuffer.substring(idx + 1)
      return line
    }

    dispatchCdcNotify (line) {
      if (!line.startsWith('EVT:')) return false
      const parts = line.split(':')
      if (parts.length < 3) return true
      const kind = parts[1]
      const index = parseInt(parts[2], 10)
      document.dispatchEvent(new CustomEvent('cdc:notify', {
        detail: { kind, index: Number.isNaN(index) ? -1 : index }
      }))
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
        if (this.dispatchCdcNotify(line)) continue
        this._deliverLine(line)
      }
    }

    _tryDequeueLine () {
      if (this._lineQueue.length > 0) return this._lineQueue.shift()
      while (true) {
        const line = this._takeLineFromBuffer()
        if (line === null) return null
        if (this.dispatchCdcNotify(line)) continue
        return line
      }
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
      // ASSETS mode never emits unsolicited EVT lines (firmware cdc_may_push_notify()
      // excludes it). A background reader here only competes with fetchSizedTransfer /
      // readLine for port.readable and steals binary-transfer bytes mid-stream.
      if (this.mode === 'ASSETS') return
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
      if (cmd === 'INFO') return '"version"'
      if (cmd === 'SCENE_INSPECT') return '"text"'
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

    async sendCommand (cmd, timeout = 30000, validator = null) {
      return this.runSerialTask(() => this._sendCommandImpl(cmd, timeout, validator))
    }

    async _sendCommandImpl (cmd, timeout = 30000, validator = null) {
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
      let rxBuf = ''
      const decoder = new TextDecoder()
      let jsonAcc = ''
      let lineCount = 0

      try {
        await this.sendRaw(cmd + '\n')
        const reader = this.port.readable.getReader()
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
              lineCount++

              if (rawLine.startsWith('EVT:')) {
                this.dispatchCdcNotify(rawLine)
                continue
              }
              if (this._isNoiseLine(rawLine)) continue

              if (rawLine.startsWith('ERROR:')) {
                this._mergeRxTail(rxBuf)
                return rawLine
              }
              if (!validator) {
                if (!rawLine.includes('{')) {
                  this._mergeRxTail(rxBuf)
                  return rawLine
                }
              }
              if (!rawLine.includes('{') && !rawLine.includes('"')) continue

              jsonAcc += rawLine
              const candidate = this._repairJsonLine(jsonAcc)
              const json = this._extractFirstJson(candidate)
              try {
                const data = JSON.parse(json)
                if (validator && !validator(data)) {
                  if (expectMarker && !jsonAcc.includes(expectMarker)) continue
                  continue
                }
                this._mergeRxTail(rxBuf)
                return json
              } catch (e) {
                if (expectMarker && !jsonAcc.includes(expectMarker)) continue
              }
            }
          }
        } finally {
          reader.releaseLock()
        }

        this._mergeRxTail(rxBuf)
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
      if (this.mode !== null) return this._readLineExclusive(timeout)

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
      const reader = this.port.readable.getReader()
      const decoder = new TextDecoder()
      let buffer = ''

      const takeResponseLine = () => {
        while (true) {
          const idx = buffer.indexOf('\n')
          if (idx === -1) return null
          const line = buffer.substring(0, idx).replace(/\r/g, '').trim()
          buffer = buffer.substring(idx + 1)
          if (!line) continue
          if (this.dispatchCdcNotify(line)) continue
          if (/^I \(/.test(line)) continue
          return line
        }
      }

      try {
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
        try { reader.releaseLock() } catch (e) {}
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

      const reader = this.port.readable.getReader()
      let buffer = new Uint8Array(0)
      let responseLine = ''

      try {
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
              if (line.startsWith('EVT:')) {
                this.dispatchCdcNotify(line)
                continue
              }
              if (this._isNoiseLine(line)) continue
              responseLine = line
              break
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
        try { reader.releaseLock() } catch (e) {}
        if (hadMode) this._resumeModeNotifyIfAllowed()
        else this._resumeRxPump()
      }
    }

    async readBinary (size, timeout = null) {
      await this._suspendRxPump()
      await this._waitForPumpReaderRelease()
      await this._suspendModeNotify()
      await this._waitForModeNotifyReaderRelease()
      const reader = this.port.readable.getReader()
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
        reader.releaseLock()
        if (this.mode !== null) this._resumeModeNotifyIfAllowed()
        else this._resumeRxPump()
      }
      return data.slice(0, received)
    }

    async sendBinary (data) {
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

      // If connected, activate the currently visible tab
      if (connected) {
        this.activateCurrentTab()
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
