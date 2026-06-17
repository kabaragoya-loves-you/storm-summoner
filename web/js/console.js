/* Storm Summoner - Console Controller */

application.register(
  'console',
  class extends BaseController {
    static targets = ['terminal', 'commandInput']

    connect () {
      this.reader = null
      this.readLoopActive = false
      this.inConsoleMode = false
      this.commandHistory = []
      this.historyIndex = -1

      // Listen for connection changes
      this.connection.on(
        'connection:changed',
        this.onConnectionChanged.bind(this)
      )

      // Listen for mode changes - reset our flag and stop read loop if mode changed away
      this.connection.on('mode:changed', ({ mode }) => {
        if (mode !== 'CONSOLE') {
          this.inConsoleMode = false
          this.stopReadLoop()
        }
      })

      // Listen for tab activation
      document.addEventListener('app:tab-activated', e => {
        if (
          e.detail.tab === 'console' &&
          this.connection.isConnected &&
          !this.inConsoleMode
        ) {
          this.activate()
        }
      })
    }

    disconnect () {
      this.stopReadLoop()
    }

    onConnectionChanged ({ connected }) {
      this.commandInputTarget.disabled = !connected
      if (!connected) {
        this.inConsoleMode = false
        this.stopReadLoop()
      }
    }

    async activate () {
      if (!this.connection.isConnected) {
        this.appendLine('Connect to device first', 'system')
        return
      }

      try {
        const modeGranted = await this.connection.requestMode('CONSOLE')
        if (!modeGranted) return

        await this.enterConsoleMode()
      } catch (err) {
        this.appendLine(`Failed to activate: ${err.message}`, 'error')
      }
    }

    async enterConsoleMode () {
      try {
        this.appendLine('Entering console mode...', 'system')

        await this.sleep(300)
        await this.connection.sendRaw('CONSOLE\n')

        this.inConsoleMode = true
        this.startReadLoop()
        this.commandInputTarget.disabled = false
        this.commandInputTarget.focus()
      } catch (err) {
        this.appendLine(`Console mode failed: ${err.message}`, 'error')
      }
    }

    async startReadLoop () {
      if (!this.connection.port?.readable) return

      this.readLoopActive = true
      const decoder = new TextDecoderStream()
      const readableStreamClosed = this.connection.port.readable.pipeTo(
        decoder.writable
      )
      this.reader = decoder.readable.getReader()

      let buffer = ''

      try {
        while (this.readLoopActive) {
          const { value, done } = await this.reader.read()
          if (done) break

          if (value) {
            buffer += value

            while (buffer.includes('\n')) {
              const idx = buffer.indexOf('\n')
              let line = buffer.substring(0, idx)
              buffer = buffer.substring(idx + 1)

              line = line.replace(/\r/g, '').trim()

              if (line) {
                // Async device notifications (clock/connections) ride the same
                // CDC stream and may arrive glued to the trailing "> " prompt.
                // Forward them to the global notify path instead of dumping
                // them into the console output.
                const evtIdx = line.indexOf('EVT:')
                const evtPrefix = evtIdx === -1 ? null : line.slice(0, evtIdx).trim()
                if (evtIdx !== -1 && (evtPrefix === '' || evtPrefix === '>')) {
                  this.connection.dispatchCdcNotify(line.slice(evtIdx))
                } else if (line === 'CONSOLE_STARTED' || line === 'CONSOLE_STOPPED') {
                  this.appendLine(`[${line}]`, 'system')
                } else if (line !== '>' && line !== '> ') {
                  this.appendLine(line)
                }
              }
            }
          }
        }
      } catch (error) {
        if (this.readLoopActive && error.name !== 'TypeError') {
          this.appendLine(`Read error: ${error.message}`, 'error')
        }
      } finally {
        if (this.readLoopActive) {
          this.readLoopActive = false
          this.appendLine('Connection lost', 'error')
          this.inConsoleMode = false
        }
      }
    }

    stopReadLoop () {
      this.readLoopActive = false
      if (this.reader) {
        try {
          this.reader.cancel()
          this.reader.releaseLock()
        } catch (e) {}
        this.reader = null
      }
    }

    async sendCommand (cmd) {
      if (!this.connection.port) return

      try {
        await this.connection.sendRaw(cmd + '\n')
      } catch (err) {
        this.appendLine(`Send error: ${err.message}`, 'error')
      }
    }

    handleKeydown (event) {
      if (event.key === 'Enter') {
        const cmd = this.commandInputTarget.value.trim()
        if (cmd) {
          this.commandHistory.push(cmd)
          this.historyIndex = this.commandHistory.length

          this.sendCommand(cmd)

          if (cmd.toLowerCase() === 'exit') {
            this.inConsoleMode = false
            setTimeout(() => {
              this.stopReadLoop()
            }, 500)
          }
        }
        this.commandInputTarget.value = ''
      } else if (event.key === 'ArrowUp') {
        event.preventDefault()
        if (this.historyIndex > 0) {
          this.historyIndex--
          this.commandInputTarget.value = this.commandHistory[this.historyIndex]
          this.commandInputTarget.setSelectionRange(
            this.commandInputTarget.value.length,
            this.commandInputTarget.value.length
          )
        }
      } else if (event.key === 'ArrowDown') {
        event.preventDefault()
        if (this.historyIndex < this.commandHistory.length - 1) {
          this.historyIndex++
          this.commandInputTarget.value = this.commandHistory[this.historyIndex]
        } else {
          this.historyIndex = this.commandHistory.length
          this.commandInputTarget.value = ''
        }
      }
    }

    focusInput () {
      if (!this.commandInputTarget.disabled) {
        this.commandInputTarget.focus()
      }
    }

    appendLine (text, className = 'output') {
      const line = document.createElement('div')
      line.className = `line ${className}`

      // Color-code ESP-IDF log levels
      if (text.match(/^[IWE] \(\d+\)/) || text.match(/^[IWE] \(/)) {
        if (text.startsWith('I ')) className = 'log-i'
        else if (text.startsWith('W ')) className = 'log-w'
        else if (text.startsWith('E ')) className = 'log-e'
        line.className = `line ${className}`
      }

      line.textContent = text
      this.terminalTarget.appendChild(line)
      this.terminalTarget.scrollTop = this.terminalTarget.scrollHeight
    }

    clearTerminal () {
      this.terminalTarget.innerHTML = ''
    }
  }
)
