/* Storm Summoner - Updater Controller */

application.register(
  'updater',
  class extends BaseController {
    static targets = [
      'fwInput',
      'fwBtn',
      'fwProgress',
      'assetsInput',
      'assetsBtn',
      'assetsProgress',
      'resetBtn',
      'logContent'
    ]

    connect () {
      this.reader = null
      this.updateInProgress = false
      this.currentType = ''
      this.uploadData = null
      this.rxBuffer = ''

      // Listen for connection changes
      this.connection.on(
        'connection:changed',
        this.onConnectionChanged.bind(this)
      )

      // Listen for mode changes - stop reading if mode changed away
      this.connection.on('mode:changed', ({ mode }) => {
        if (mode !== 'UPDATE') {
          this.stopReading()
          if (this.updateInProgress) {
            this.updateInProgress = false
            this.currentType = ''
            this.connection.setTabsLocked(false)
          }
        }
      })
    }

    disconnect () {
      this.stopReading()
    }

    onConnectionChanged ({ connected }) {
      this.setControlsEnabled(connected)
      if (!connected) {
        this.stopReading()
        this.updateInProgress = false
        this.currentType = ''
      }
    }

    setControlsEnabled (enabled) {
      const hasPort = this.connection.isConnected
      this.fwBtnTarget.disabled = !enabled || !this.fwInputTarget.files[0]
      this.assetsBtnTarget.disabled =
        !enabled || !this.assetsInputTarget.files[0]
      this.resetBtnTarget.disabled = !enabled
      this.fwInputTarget.disabled = !hasPort
      this.assetsInputTarget.disabled = !hasPort
    }

    fwFileSelected () {
      if (!this.updateInProgress) {
        this.fwBtnTarget.disabled = !this.fwInputTarget.files[0]
      }
    }

    assetsFileSelected () {
      if (!this.updateInProgress) {
        this.assetsBtnTarget.disabled = !this.assetsInputTarget.files[0]
      }
    }

    async activate () {
      if (!this.connection.isConnected) {
        this.log('Connect to device first')
        return
      }

      try {
        const modeGranted = await this.connection.requestMode('UPDATE')
        if (!modeGranted) return

        this.startReading()
        this.log('Updater ready', 'success')
      } catch (err) {
        this.log(`Failed to activate: ${err.message}`, 'error')
      }
    }

    async startReading () {
      if (!this.connection.port?.readable) return
      if (this.reader) return // Already reading

      this.reader = this.connection.port.readable.getReader()
      const decoder = new TextDecoder()

      try {
        while (true) {
          const { value, done } = await this.reader.read()
          if (done) break
          if (value) this.handleData(decoder.decode(value, { stream: true }))
        }
      } catch (error) {
        if (this.updateInProgress) {
          this.log('Read error: ' + error.message, 'error')
        }
      } finally {
        try {
          this.reader?.releaseLock()
        } catch (e) {}
        this.reader = null
      }
    }

    async stopReading () {
      if (this.reader) {
        try {
          await this.reader.cancel()
        } catch (e) {}
        try {
          this.reader.releaseLock()
        } catch (e) {}
        this.reader = null
      }
    }

    handleData (data) {
      this.rxBuffer += data

      if (this.rxBuffer.includes('\n')) {
        const lines = this.rxBuffer.split('\n')
        this.rxBuffer = lines.pop()

        for (let line of lines) {
          line = line.trim()
          if (!line) continue

          this.log('DEVICE: ' + line)

          const activeProgress =
            this.currentType === 'FIRMWARE'
              ? this.fwProgressTarget
              : this.assetsProgressTarget

          if (line.startsWith('PROGRESS')) {
            const pct = parseInt(line.split(' ')[1])
            if (activeProgress) activeProgress.style.width = pct + '%'
          } else if (line === 'READY') {
            this.log('Device Ready. Starting upload...')
            this.uploadChunks()
          } else if (line === 'TRANSFER_COMPLETE') {
            this.log('Transfer complete. Committing...')
            this.send('COMMIT')
          } else if (line === 'SUCCESS') {
            this.log('Update Successful!', 'success')
            this.setControlsEnabled(true)
            this.updateInProgress = false
            this.currentType = ''
            this.connection.setTabsLocked(false)
          } else if (line === 'RESETTING') {
            this.log('Device is resetting...')
          } else if (line.startsWith('ERROR')) {
            this.updateInProgress = false
            this.currentType = ''
            this.setControlsEnabled(true)
            this.connection.setTabsLocked(false)
            this.log('Update Error: ' + line, 'error')
          }
        }
      }
    }

    async send (data) {
      this.log('HOST: ' + data)
      await this.connection.sendRaw(data + '\n')
    }

    async startFirmwareUpdate () {
      await this.startUpdate('FIRMWARE')
    }

    async startAssetsUpdate () {
      await this.startUpdate('ASSETS')
    }

    async startUpdate (type) {
      if (!this.connection.isConnected) {
        this.log('Not connected', 'error')
        return
      }

      const input =
        type === 'FIRMWARE' ? this.fwInputTarget : this.assetsInputTarget
      const file = input.files[0]
      if (!file) return

      // Request update mode if not already in it
      const modeGranted = await this.connection.requestMode('UPDATE')
      if (!modeGranted) return

      this.updateInProgress = true
      this.currentType = type
      this.connection.setTabsLocked(true, 'updater')
      this.setControlsEnabled(false)

      // Reset progress bars
      this.fwProgressTarget.style.width = '0%'
      this.assetsProgressTarget.style.width = '0%'

      const buffer = await file.arrayBuffer()
      this.uploadData = new Uint8Array(buffer)

      this.log(
        `Starting ${type} update: ${file.name} (${this.uploadData.length} bytes)`
      )

      // Start reading responses if not already
      if (!this.reader) {
        this.startReading()
      }

      // Send start command
      this.send(`${type} ${this.uploadData.length}`)
    }

    async uploadChunks () {
      const writer = this.connection.port.writable.getWriter()
      const chunkSize = 4096

      try {
        this.log('Writing binary data...')
        for (let i = 0; i < this.uploadData.length; i += chunkSize) {
          const chunk = this.uploadData.slice(i, i + chunkSize)
          await writer.write(chunk)
        }
      } catch (err) {
        this.log('Upload error: ' + err.message, 'error')
      } finally {
        writer.releaseLock()
      }
      this.log('Upload finished, waiting for device...')
    }

    async triggerReset () {
      if (!this.connection.isConnected) {
        this.log('Not connected', 'error')
        return
      }
      this.send('RESET')
    }
  }
)
