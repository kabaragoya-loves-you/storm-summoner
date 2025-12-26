/* Storm Summoner - Main Application */

// Connection Manager - Singleton for WebSerial connection
window.ConnectionManager = (function () {
  let instance = null

  class ConnectionManager {
    constructor () {
      this.port = null
      this.mode = null // ASSETS, CONSOLE, DISPLAY, UPDATE, RPC
      this.listeners = new Map()
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
        this.port = null
        this.mode = null
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

    async sendCommand (cmd, timeout = 30000) {
      if (!this.port) throw new Error('Not connected')

      const reader = this.port.readable.getReader()
      const decoder = new TextDecoder()

      try {
        await this.sendRaw(cmd + '\n')

        let buffer = ''
        const startTime = Date.now()

        while (Date.now() - startTime < timeout) {
          const { value, done } = await reader.read()
          if (done) break

          if (value?.length > 0) {
            buffer += decoder.decode(value, { stream: true })
            const idx = buffer.indexOf('\n')
            if (idx !== -1) {
              return buffer.substring(0, idx).replace(/\r/g, '').trim()
            }
          }
        }
        return buffer.replace(/\r/g, '').trim()
      } finally {
        reader.releaseLock()
      }
    }

    async readLine (timeout = 10000) {
      if (!this.port?.readable) return null

      const reader = this.port.readable.getReader()
      const decoder = new TextDecoder()
      let buffer = ''
      const startTime = Date.now()

      try {
        while (Date.now() - startTime < timeout) {
          const { value, done } = await reader.read()
          if (done) break
          if (value) {
            buffer += decoder.decode(value, { stream: true })
            const idx = buffer.indexOf('\n')
            if (idx !== -1) {
              return buffer.substring(0, idx).replace(/\r/g, '').trim()
            }
          }
        }
        return buffer.replace(/\r/g, '').trim()
      } finally {
        reader.releaseLock()
      }
    }

    async readBinary (size, timeout = 30000) {
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
      if (!this.port?.readable) return
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
        tabGroup.show(tabName)
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
