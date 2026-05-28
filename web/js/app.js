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
        this.port = null
        this.mode = null
        this._rxBuffer = ''
        this._lineQueue = []
        this._lineWaiters = []
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
        this.setTabsLocked(false)
        this.emit('connection:changed', { connected: false })
      }
    }

    // Request a mode - returns true if mode was entered
    async requestMode (newMode) {
      if (!this.port) throw new Error('Not connected')
      if (this.mode === newMode) return true

      // Exit current mode if any
      if (this.mode) {
        await this.exitMode()
      }

      // Enter new mode
      this.mode = newMode
      this.emit('mode:changed', { mode: newMode })
      return true
    }

    // Exit current mode
    async exitMode () {
      if (!this.mode || !this.port) return
      try {
        await this.sendRaw('EXIT\n')
        await this.sleep(100)
        await this.drainInput()
      } catch (err) {
        // Ignore exit errors
      }
      this.mode = null
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
      if (!this._rxPumpRunning && this.port) {
        this._startRxPump()
      }
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
      const run = async () => {
        if (!this.port) throw new Error('Not connected')
        this._lineQueue = []
        this._rejectLineWaiters()
        await this._suspendRxPump()

        const expectMarker = validator ? this._expectedJsonMarker(cmd) : null
        let rxBuf = ''
        const decoder = new TextDecoder()
        let jsonAcc = ''
        let lineCount = 0

        try {
          await this.sendRaw(cmd + '\n')
          const reader = this.port.readable.getReader()
          const deadline = Date.now() + timeout
          try {
            while (Date.now() < deadline) {
              const waitMs = Math.min(100, deadline - Date.now())
              if (waitMs <= 0) break
              const result = await Promise.race([
                reader.read(),
                this.sleep(waitMs).then(() => ({ timeout: true }))
              ])
              if (result.timeout) continue
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
          this._resumeRxPump()
        }
      }
      const result = this._commandChain.then(run, run)
      this._commandChain = result.then(() => {}, () => {})
      return result
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

    async readBinary (size, timeout = 30000) {
      await this._suspendRxPump()
      const reader = this.port.readable.getReader()
      const data = new Uint8Array(size)
      let received = 0
      const startTime = Date.now()

      try {
        while (received < size && Date.now() - startTime < timeout) {
          const { value, done } = await reader.read()
          if (done) break
          if (value) {
            data.set(value.slice(0, size - received), received)
            received += Math.min(value.length, size - received)
          }
        }
      } finally {
        reader.releaseLock()
        this._resumeRxPump()
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

      await this._suspendRxPump()
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
        this._resumeRxPump()
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
