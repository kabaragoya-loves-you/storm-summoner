/* Storm Summoner - Updater Controller */

application.register(
  'updater',
  class extends BaseController {
    static targets = [
      'currentFwVersion',
      'fwSelect',
      'fwApplyBtn',
      'fwInput',
      'fwBtn',
      'fwProgress',
      'fwProgressBar',
      'fwSuccess',
      'fwStatus',
      'currentAssetsVersion',
      'assetsSelect',
      'assetsApplyBtn',
      'assetsInput',
      'assetsBtn',
      'assetsProgress',
      'assetsProgressBar',
      'assetsSuccess',
      'assetsStatus',
      'resetBtn',
      'factoryResetBtn',
      'factoryResetDialog',
      'logContent'
    ]

    // Commit phase timing estimates (in ms) - based on measured times
    static FIRMWARE_COMMIT_TIME = 25000  // ~25 seconds for firmware flash
    static ASSETS_COMMIT_TIME = 100000   // ~100 seconds for assets flash

    connect () {
      this.reader = null
      this.updateInProgress = false
      this.currentType = ''
      this.uploadData = null
      this.rxBuffer = ''
      this.releases = null
      this.deviceVersion = null
      this.deviceAssetsChecksum = null
      this.pendingAssetsChecksum = null  // Checksum being uploaded
      this.commitStartTime = null
      this.commitAnimationFrame = null

      // Fetch releases manifest
      this.fetchReleases()

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

      // Listen for device info updates from info controller
      document.addEventListener('device:info', (e) => {
        if (e.detail?.version) {
          this.deviceVersion = e.detail.version
        }
        if (e.detail?.assets_checksum) {
          this.deviceAssetsChecksum = e.detail.assets_checksum
        }
        this.updateCurrentVersionDisplay()
      })
    }

    disconnect () {
      this.stopReading()
    }

    // Firmware/asset binaries and the manifest are served under stable,
    // version-named paths (e.g. storm-summoner-0.7.bin) that are overwritten
    // in place on every rebuild. A normal fetch can hand back a stale cached
    // copy, so the device flashes an old-but-valid image and reports success
    // while the running build never changes. Force a fresh network read.
    fetchFresh (url) {
      const bust = (url.includes('?') ? '&' : '?') + '_=' + Date.now()
      return fetch(url + bust, { cache: 'no-store' })
    }

    async fetchReleases () {
      try {
        const response = await this.fetchFresh('/releases.json')
        if (response.ok) {
          this.releases = await response.json()
          this.populateFirmwareDropdown()
          this.populateAssetsDropdown()
          this.log('Loaded releases manifest', 'success')
        } else {
          this.log('No releases manifest found', 'warning')
        }
      } catch (err) {
        this.log('Failed to load releases: ' + err.message, 'error')
      }
    }

    populateFirmwareDropdown () {
      if (!this.releases?.firmware?.length) {
        this.fwSelectTarget.innerHTML = '<wa-option disabled>No versions available</wa-option>'
        return
      }

      const options = this.releases.firmware.map(fw => {
        const dateStr = this.formatDate(fw.date)
        return `<wa-option value="${fw.filename}">v${fw.version} (${dateStr})</wa-option>`
      }).join('')

      this.fwSelectTarget.innerHTML = options
      // wa-select doesn't auto-select the first option when populated via
      // innerHTML and doesn't always emit a `change` for the visible default
      // value, which was leaving the Apply button stuck disabled. Set the
      // value explicitly on the next tick (after wa-option slot wiring) and
      // re-evaluate gating ourselves.
      const first = this.releases.firmware[0]?.filename
      if (first) requestAnimationFrame(() => {
        try { this.fwSelectTarget.value = first } catch (e) {}
        this.fwVersionSelected()
      })
    }

    populateAssetsDropdown () {
      if (!this.releases?.assets?.length) {
        this.assetsSelectTarget.innerHTML = '<wa-option disabled>No versions available</wa-option>'
        return
      }

      const options = this.releases.assets.map(asset => {
        const dateStr = this.formatDate(asset.date)
        return `<wa-option value="${asset.filename}">${dateStr} (${asset.checksum})</wa-option>`
      }).join('')

      this.assetsSelectTarget.innerHTML = options
      const first = this.releases.assets[0]?.filename
      if (first) requestAnimationFrame(() => {
        try { this.assetsSelectTarget.value = first } catch (e) {}
        this.assetsVersionSelected()
      })
    }

    // Read the wa-select value robustly. wa-select sometimes leaves `.value`
    // empty even after the user picks an option (the selectedOptions list is
    // populated but the property hasn't reflected it yet). Fall back to the
    // first selected option's value.
    selectValue (target) {
      if (target.value) return target.value
      if (target.selectedOptions?.length) return target.selectedOptions[0].value
      return ''
    }

    formatDate (dateStr) {
      try {
        const date = new Date(dateStr + 'T00:00:00')
        return date.toLocaleDateString('en-US', {
          year: 'numeric',
          month: 'short',
          day: 'numeric'
        })
      } catch {
        return dateStr
      }
    }

    updateCurrentVersionDisplay () {
      if (this.hasCurrentFwVersionTarget) {
        this.currentFwVersionTarget.textContent = this.deviceVersion || '--'
      }
      if (this.hasCurrentAssetsVersionTarget) {
        this.currentAssetsVersionTarget.textContent = this.deviceAssetsChecksum || '--'
      }
    }

    onConnectionChanged ({ connected }) {
      this.setControlsEnabled(connected)
      if (!connected) {
        const wasUpdating = this.updateInProgress
        const updateType = this.currentType

        // Stop any ongoing operations
        this.stopReading()

        // Stop commit animation
        this.commitStartTime = null
        if (this.commitAnimationFrame) {
          cancelAnimationFrame(this.commitAnimationFrame)
          this.commitAnimationFrame = null
        }

        // Reset state
        this.updateInProgress = false
        this.currentType = ''
        this.pendingAssetsChecksum = null
        this.deviceVersion = null
        this.deviceAssetsChecksum = null

        // Hide progress and status elements
        this.fwProgressBarTarget.classList.add('hidden')
        this.fwStatusTarget.classList.add('hidden')
        this.fwSuccessTarget.classList.add('hidden')
        this.assetsProgressBarTarget.classList.add('hidden')
        this.assetsStatusTarget.classList.add('hidden')
        this.assetsSuccessTarget.classList.add('hidden')

        // Reset progress bars
        this.fwProgressTarget.style.width = '0%'
        this.assetsProgressTarget.style.width = '0%'

        // Unlock tabs
        this.connection.setTabsLocked(false)

        // Log disconnect
        if (wasUpdating) {
          this.log(`${updateType} update interrupted - device disconnected`, 'error')
        }

        this.updateCurrentVersionDisplay()
      }
    }

    setControlsEnabled (enabled) {
      const hasPort = this.connection.isConnected
      const hasReleases = this.releases !== null

      // Dropdown-based updates
      this.fwApplyBtnTarget.disabled = !enabled || !this.selectValue(this.fwSelectTarget)
      this.assetsApplyBtnTarget.disabled = !enabled || !this.selectValue(this.assetsSelectTarget)

      // File-based updates
      this.fwBtnTarget.disabled = !enabled || !this.fwInputTarget.files[0]
      this.assetsBtnTarget.disabled = !enabled || !this.assetsInputTarget.files[0]

      // Other controls
      this.resetBtnTarget.disabled = !enabled
      this.factoryResetBtnTarget.disabled = !enabled

      // Inputs
      this.fwInputTarget.disabled = !hasPort
      this.assetsInputTarget.disabled = !hasPort
      this.fwSelectTarget.disabled = !hasReleases
      this.assetsSelectTarget.disabled = !hasReleases
    }

    fwVersionSelected () {
      if (!this.updateInProgress) {
        this.fwApplyBtnTarget.disabled = !this.selectValue(this.fwSelectTarget) || !this.connection.isConnected
      }
    }

    assetsVersionSelected () {
      if (!this.updateInProgress) {
        this.assetsApplyBtnTarget.disabled = !this.selectValue(this.assetsSelectTarget) || !this.connection.isConnected
      }
    }

    fwFileSelected () {
      if (!this.updateInProgress) {
        this.fwBtnTarget.disabled = !this.fwInputTarget.files[0] || !this.connection.isConnected
      }
    }

    assetsFileSelected () {
      if (!this.updateInProgress) {
        this.assetsBtnTarget.disabled = !this.assetsInputTarget.files[0] || !this.connection.isConnected
      }
    }

    async applyFirmware () {
      const filename = this.selectValue(this.fwSelectTarget)
      if (!filename) {
        this.log('No firmware version selected', 'error')
        return
      }

      this.log(`Downloading firmware: ${filename}`)

      try {
        const response = await this.fetchFresh(`/binaries/${filename}`)
        if (!response.ok) throw new Error(`HTTP ${response.status}`)

        const buffer = await response.arrayBuffer()
        this.uploadData = new Uint8Array(buffer)
        this.log(`Downloaded ${this.formatSize(this.uploadData.length)}`)

        await this.startUpdateWithData('FIRMWARE')
      } catch (err) {
        this.log(`Download failed: ${err.message}`, 'error')
      }
    }

    async applyAssets () {
      const filename = this.selectValue(this.assetsSelectTarget)
      if (!filename) {
        this.log('No assets version selected', 'error')
        return
      }

      // Extract checksum from filename (e.g., "assets-2e9a1904.bin" -> "2e9a1904")
      const match = filename.match(/assets-([a-f0-9]{8})\.bin/i)
      this.pendingAssetsChecksum = match ? match[1].toLowerCase() : null

      this.log(`Downloading assets: ${filename}`)

      try {
        const response = await this.fetchFresh(`/binaries/${filename}`)
        if (!response.ok) throw new Error(`HTTP ${response.status}`)

        const buffer = await response.arrayBuffer()
        this.uploadData = new Uint8Array(buffer)
        this.log(`Downloaded ${this.formatSize(this.uploadData.length)}`)

        await this.startUpdateWithData('ASSETS')
      } catch (err) {
        this.log(`Download failed: ${err.message}`, 'error')
        this.pendingAssetsChecksum = null
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
      if (this.reader) return

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
            this.log('Transfer complete. Writing to flash...')
            this.send('COMMIT')
            this.startCommitPhaseAnimation()
          } else if (line === 'SUCCESS') {
            this.log('Update Successful!', 'success')
            this.onUpdateComplete()
          } else if (line === 'RESETTING') {
            this.log('Device is resetting...')
          } else if (line.startsWith('ERROR')) {
            // Stop commit animation
            this.commitStartTime = null
            if (this.commitAnimationFrame) {
              cancelAnimationFrame(this.commitAnimationFrame)
              this.commitAnimationFrame = null
            }
            // Hide status
            this.fwStatusTarget.classList.add('hidden')
            this.assetsStatusTarget.classList.add('hidden')

            this.updateInProgress = false
            this.currentType = ''
            this.pendingAssetsChecksum = null
            this.setControlsEnabled(true)
            this.connection.setTabsLocked(false)
            this.log('Update Error: ' + line, 'error')
          }
        }
      }
    }

    onUpdateComplete () {
      const updateType = this.currentType

      // Stop commit animation
      this.commitStartTime = null
      if (this.commitAnimationFrame) {
        cancelAnimationFrame(this.commitAnimationFrame)
        this.commitAnimationFrame = null
      }

      // Show success message, hide progress bar and status
      if (updateType === 'FIRMWARE') {
        this.fwProgressBarTarget.classList.add('hidden')
        this.fwStatusTarget.classList.add('hidden')
        this.fwSuccessTarget.classList.remove('hidden')
      } else if (updateType === 'ASSETS') {
        this.assetsProgressBarTarget.classList.add('hidden')
        this.assetsStatusTarget.classList.add('hidden')
        this.assetsSuccessTarget.classList.remove('hidden')
        // Update local display immediately for assets (device doesn't reset)
        if (this.pendingAssetsChecksum) {
          this.deviceAssetsChecksum = this.pendingAssetsChecksum
          this.updateCurrentVersionDisplay()
          // Also notify other controllers of the new assets checksum
          document.dispatchEvent(new CustomEvent('device:info', {
            detail: {
              version: this.deviceVersion,
              assets_checksum: this.deviceAssetsChecksum
            }
          }))
        }
      }
      this.pendingAssetsChecksum = null

      this.setControlsEnabled(true)
      this.updateInProgress = false
      this.currentType = ''
      this.connection.setTabsLocked(false)

      // Stop our reader and clear mode (device is already back to idle after SUCCESS)
      this.stopReading()
      this.connection.mode = null

      // Dispatch event to refresh device info after update
      document.dispatchEvent(new CustomEvent('updater:complete'))
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

      const input = type === 'FIRMWARE' ? this.fwInputTarget : this.assetsInputTarget
      const file = input.files[0]
      if (!file) return

      const buffer = await file.arrayBuffer()
      this.uploadData = new Uint8Array(buffer)

      await this.startUpdateWithData(type)
    }

    async startUpdateWithData (type) {
      if (!this.connection.isConnected) {
        this.log('Not connected', 'error')
        return
      }

      if (!this.uploadData || this.uploadData.length === 0) {
        this.log('No data to upload', 'error')
        return
      }

      const modeGranted = await this.connection.requestMode('UPDATE')
      if (!modeGranted) return

      this.updateInProgress = true
      this.currentType = type
      this.connection.setTabsLocked(true, 'updater')
      this.setControlsEnabled(false)

      // Reset progress bars, status, and hide success messages
      this.fwProgressTarget.style.width = '0%'
      this.assetsProgressTarget.style.width = '0%'
      this.fwProgressBarTarget.classList.remove('hidden')
      this.assetsProgressBarTarget.classList.remove('hidden')
      this.fwSuccessTarget.classList.add('hidden')
      this.assetsSuccessTarget.classList.add('hidden')

      // Show upload status
      const statusTarget = type === 'FIRMWARE' ? this.fwStatusTarget : this.assetsStatusTarget
      statusTarget.textContent = 'Uploading to device...'
      statusTarget.classList.remove('hidden', 'committing')

      // Reset commit animation state
      this.commitStartTime = null
      if (this.commitAnimationFrame) {
        cancelAnimationFrame(this.commitAnimationFrame)
        this.commitAnimationFrame = null
      }

      this.log(`Starting ${type} update (${this.formatSize(this.uploadData.length)})`)

      if (!this.reader) {
        this.startReading()
      }

      // Include checksum for assets updates
      if (type === 'ASSETS' && this.pendingAssetsChecksum) {
        this.send(`${type} ${this.uploadData.length} ${this.pendingAssetsChecksum}`)
      } else {
        this.send(`${type} ${this.uploadData.length}`)
      }
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

    startCommitPhaseAnimation () {
      const activeProgress =
        this.currentType === 'FIRMWARE'
          ? this.fwProgressTarget
          : this.assetsProgressTarget
      const statusTarget =
        this.currentType === 'FIRMWARE'
          ? this.fwStatusTarget
          : this.assetsStatusTarget

      // Show commit phase status
      statusTarget.textContent = 'Writing to flash...'
      statusTarget.classList.add('committing')

      // Get expected commit duration
      const commitDuration =
        this.currentType === 'FIRMWARE'
          ? this.constructor.FIRMWARE_COMMIT_TIME
          : this.constructor.ASSETS_COMMIT_TIME

      this.commitStartTime = Date.now()

      // Animate progress bar from 0 to 95% during expected commit time
      const animateCommit = () => {
        if (!this.commitStartTime) return

        const elapsed = Date.now() - this.commitStartTime
        const progress = Math.min(95, (elapsed / commitDuration) * 95)
        activeProgress.style.width = `${progress}%`

        // Update status text with remaining time (countdown)
        const remainingSecs = Math.max(0, Math.ceil((commitDuration - elapsed) / 1000))
        if (remainingSecs > 0) {
          statusTarget.textContent = `Writing to flash... ~${remainingSecs}s remaining`
        } else {
          statusTarget.textContent = 'Finalizing...'
        }

        if (elapsed < commitDuration || progress < 95) {
          this.commitAnimationFrame = requestAnimationFrame(animateCommit)
        }
      }

      // Reset progress bar to 0 for commit phase
      activeProgress.style.width = '0%'
      this.commitAnimationFrame = requestAnimationFrame(animateCommit)
    }

    async triggerReset () {
      if (!this.connection.isConnected) {
        this.log('Not connected', 'error')
        return
      }
      this.send('RESET')
    }

    showFactoryResetDialog () {
      this.factoryResetDialogTarget.open = true
    }

    hideFactoryResetDialog () {
      this.factoryResetDialogTarget.open = false
    }

    async confirmFactoryReset () {
      this.hideFactoryResetDialog()

      if (!this.connection.isConnected) {
        this.log('Not connected', 'error')
        return
      }

      this.log('Initiating factory reset...')

      try {
        const modeGranted = await this.connection.requestMode('CONFIG')
        if (!modeGranted) {
          this.log('Failed to request config mode', 'error')
          return
        }

        await this.sleep(100)
        await this.connection.sendRaw('CONFIG\n')
        const enterResponse = await this.readLine(3000)

        if (enterResponse !== 'CONFIG_STARTED') {
          this.log(`Failed to enter config mode: ${enterResponse}`, 'error')
          return
        }

        await this.connection.sendRaw('FACTORY_RESET\n')
        const response = await this.readLine(5000)

        if (response === 'OK') {
          this.log('Factory reset complete. Device is restarting...', 'success')
        } else {
          this.log(`Factory reset failed: ${response}`, 'error')
        }
      } catch (err) {
        this.log(`Factory reset error: ${err.message}`, 'error')
      }
    }

    async readLine (timeout = 2000) {
      const reader = this.connection.port.readable.getReader()
      const decoder = new TextDecoder()
      let buffer = ''

      try {
        const startTime = Date.now()
        while (Date.now() - startTime < timeout) {
          const result = await Promise.race([
            reader.read(),
            this.sleep(50).then(() => ({ timeout: true }))
          ])
          if (result.timeout) continue
          if (result.done) break
          if (result.value) {
            const text = decoder.decode(result.value, { stream: true })
            for (const char of text) {
              if (char === '\n') {
                return buffer.replace('\r', '').trim()
              }
              buffer += char
            }
          }
        }
      } finally {
        try { reader.releaseLock() } catch (e) {}
      }
      return buffer.trim()
    }
  }
)
