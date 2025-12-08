/* Storm Summoner - Display Controller */

// Protocol constants
const DISPLAY_MAGIC = 0xac01
const DISPLAY_HEADER_SIZE = 16

application.register(
  'display',
  class extends BaseController {
    static targets = [
      'canvas',
      'scaleSelect',
      'statsBtn',
      'saveBtn',
      'statsPanel',
      'resolution',
      'frameCount',
      'fps',
      'dataRate',
      'logContent'
    ]

    connect () {
      this.reader = null
      this.streaming = false
      this.activating = false // Prevent duplicate activations
      this.width = 128
      this.height = 128
      this.scale = 3
      this.showStats = false

      // Stats
      this.frames = 0
      this.bytes = 0
      this.startTime = null
      this.statsInterval = null

      // Buffer for incoming data
      this.buffer = new Uint8Array(0)

      // Canvas setup
      this.ctx = this.canvasTarget.getContext('2d')
      this.imageData = null
      this.updateCanvasSize()

      // Listen for connection changes
      this.connection.on(
        'connection:changed',
        this.onConnectionChanged.bind(this)
      )

      // Listen for mode changes - stop streaming if mode changed away
      this.connection.on('mode:changed', ({ mode }) => {
        if (mode !== 'DISPLAY') {
          this.streaming = false
          this.activating = false
          this.stopStream()
        }
      })

      // Listen for tab activation
      document.addEventListener('app:tab-activated', e => {
        if (
          e.detail.tab === 'display' &&
          this.connection.isConnected &&
          !this.streaming &&
          !this.activating
        ) {
          this.activate()
        }
      })
    }

    disconnect () {
      this.stopStream()
    }

    onConnectionChanged ({ connected }) {
      this.saveBtnTarget.disabled = !connected
      if (!connected) {
        this.activating = false
        this.stopStream()
      }
    }

    updateCanvasSize () {
      this.canvasTarget.width = this.width
      this.canvasTarget.height = this.height
      this.canvasTarget.style.width = `${this.width * this.scale}px`
      this.canvasTarget.style.height = `${this.height * this.scale}px`
      this.imageData = this.ctx.createImageData(this.width, this.height)
      // Fill with black
      for (let i = 3; i < this.imageData.data.length; i += 4) {
        this.imageData.data[i] = 255 // Alpha
      }
      this.ctx.putImageData(this.imageData, 0, 0)
    }

    changeScale () {
      this.scale = parseInt(this.scaleSelectTarget.value)
      this.canvasTarget.style.width = `${this.width * this.scale}px`
      this.canvasTarget.style.height = `${this.height * this.scale}px`
    }

    toggleStats () {
      this.showStats = !this.showStats
      this.statsPanelTarget.classList.toggle('hidden', !this.showStats)
      this.statsBtnTarget.textContent = this.showStats
        ? 'Hide Stats'
        : 'Show Stats'
    }

    async activate () {
      if (!this.connection.isConnected) {
        this.log('Connect to device first')
        return
      }

      if (this.activating || this.streaming) {
        return // Already activating or streaming
      }

      this.activating = true
      try {
        const modeGranted = await this.connection.requestMode('DISPLAY')
        if (!modeGranted) return

        await this.startStreamWithRetry()
      } catch (err) {
        this.log(`Failed to activate: ${err.message}`, 'error')
      } finally {
        this.activating = false
      }
    }

    async startStreamWithRetry (maxAttempts = 3) {
      for (let attempt = 1; attempt <= maxAttempts; attempt++) {
        if (attempt > 1) {
          this.log(`Retry attempt ${attempt}/${maxAttempts}...`)
          await this.sleep(500)
        }

        const success = await this.startStream()
        if (success) return

        // Clean up before retry
        this.stopStream()

        // Wait for stream to become unlocked
        await this.waitForStreamUnlock(2000)

        // Only drain if stream is unlocked
        if (!this.connection.port?.readable?.locked) {
          try {
            await this.connection.drainInput()
          } catch (e) {
            // Ignore drain errors
          }
        }
      }

      this.log(
        'Failed to start display stream after multiple attempts',
        'error'
      )
    }

    async waitForStreamUnlock (timeout = 2000) {
      const startTime = Date.now()
      while (Date.now() - startTime < timeout) {
        if (!this.connection.port?.readable?.locked) {
          return true
        }
        await this.sleep(100)
      }
      return false
    }

    async startStream () {
      try {
        // Ensure no stale reader
        if (this.reader) {
          try {
            await this.reader.cancel()
            this.reader.releaseLock()
          } catch (e) {}
          this.reader = null
        }

        // Wait for stream to be available
        if (this.connection.port?.readable?.locked) {
          this.log('Waiting for stream to unlock...')
          const unlocked = await this.waitForStreamUnlock(2000)
          if (!unlocked) {
            this.log('Stream still locked, cannot proceed', 'error')
            return false
          }
        }

        // Drain any stale data
        try {
          await this.connection.drainInput()
        } catch (e) {
          // Ignore drain errors
        }

        // Send DISPLAY command
        this.log('Sending DISPLAY command...')
        await this.connection.sendRaw('DISPLAY\n')

        // Read response with dedicated error handling
        let response = ''
        try {
          response = await this.readLineForDisplay(5000)
        } catch (readErr) {
          this.log(`Read error: ${readErr.message}`, 'error')
          return false
        }

        this.log(`Response: ${response}`)

        if (response.startsWith('DISPLAY_STARTED')) {
          const parts = response.split(' ')
          this.width = parseInt(parts[1]) || 128
          this.height = parseInt(parts[2]) || 128

          this.updateCanvasSize()
          this.resolutionTarget.textContent = `${this.width}x${this.height}`

          this.streaming = true
          this.frames = 0
          this.bytes = 0
          this.startTime = Date.now()
          this.buffer = new Uint8Array(0)

          this.log(`Streaming started: ${this.width}x${this.height}`, 'success')

          // Start reading loop
          this.readLoop()

          // Start stats update
          this.statsInterval = setInterval(() => this.updateStats(), 1000)
          return true
        } else {
          this.log(`Unexpected response: ${response || '(empty)'}`, 'error')
          return false
        }
      } catch (err) {
        this.log(`Stream start failed: ${err.message}`, 'error')
        return false
      }
    }

    stopStream () {
      this.streaming = false

      if (this.statsInterval) {
        clearInterval(this.statsInterval)
        this.statsInterval = null
      }

      if (this.reader) {
        try {
          this.reader.cancel()
        } catch (e) {}
        try {
          this.reader.releaseLock()
        } catch (e) {}
        this.reader = null
      }
    }

    async readLineForDisplay (timeout = 5000) {
      if (!this.connection.port?.readable) {
        throw new Error('Port not readable')
      }

      // Check if stream is already locked
      if (this.connection.port.readable.locked) {
        throw new Error('Stream is locked')
      }

      const reader = this.connection.port.readable.getReader()
      const decoder = new TextDecoder()
      let line = ''
      const startTime = Date.now()

      try {
        while (Date.now() - startTime < timeout) {
          const result = await Promise.race([
            reader.read(),
            this.sleep(50).then(() => ({ timeout: true }))
          ])

          if (result.timeout) continue
          if (result.done) break

          line += decoder.decode(result.value, { stream: true })
          const idx = line.indexOf('\n')
          if (idx !== -1) {
            return line.substring(0, idx).replace(/\r/g, '').trim()
          }
        }
      } finally {
        try {
          reader.releaseLock()
        } catch (e) {}
      }

      return line.replace(/\r/g, '').trim()
    }

    async readLoop () {
      if (!this.connection.port?.readable) {
        this.log('Port not readable', 'error')
        return
      }

      if (this.connection.port.readable.locked) {
        this.log('Stream already locked, waiting...', 'error')
        await this.sleep(200)
        if (this.connection.port.readable.locked) {
          this.log('Stream still locked, cannot start read loop', 'error')
          return
        }
      }

      try {
        this.reader = this.connection.port.readable.getReader()
      } catch (err) {
        this.log(`Failed to acquire reader: ${err.message}`, 'error')
        return
      }

      try {
        while (this.streaming) {
          const { value, done } = await this.reader.read()
          if (done) break

          if (value && value.length > 0) {
            this.appendBuffer(value)
            this.processFrames()
          }
        }
      } catch (err) {
        if (this.streaming) {
          this.log(`Read error: ${err.message}`, 'error')

          // If device is lost, trigger proper disconnect
          if (
            err.message.includes('lost') ||
            err.message.includes('disconnected')
          ) {
            this.streaming = false
            this.activating = false
            // Trigger connection state update
            try {
              await this.connection.disconnect()
            } catch (e) {}
          }
        }
      } finally {
        try {
          this.reader?.releaseLock()
        } catch (e) {}
        this.reader = null
      }
    }

    appendBuffer (data) {
      const newBuffer = new Uint8Array(this.buffer.length + data.length)
      newBuffer.set(this.buffer)
      newBuffer.set(data, this.buffer.length)
      this.buffer = newBuffer
    }

    processFrames () {
      while (this.buffer.length >= DISPLAY_HEADER_SIZE) {
        // Find magic bytes (0x01, 0xAC in little-endian)
        let magicPos = -1
        for (let i = 0; i <= this.buffer.length - 2; i++) {
          if (this.buffer[i] === 0x01 && this.buffer[i + 1] === 0xac) {
            magicPos = i
            break
          }
        }

        if (magicPos === -1) {
          this.buffer = this.buffer.slice(-1)
          return
        }

        if (magicPos > 0) {
          this.buffer = this.buffer.slice(magicPos)
        }

        if (this.buffer.length < DISPLAY_HEADER_SIZE) return

        // Parse header
        const view = new DataView(this.buffer.buffer, this.buffer.byteOffset)
        const x = view.getUint16(4, true)
        const y = view.getUint16(6, true)
        const w = view.getUint16(8, true)
        const h = view.getUint16(10, true)
        const payloadLen = view.getUint32(12, true)

        // Validate header
        if (!this.validHeader(x, y, w, h, payloadLen)) {
          this.buffer = this.buffer.slice(2)
          continue
        }

        const totalSize = DISPLAY_HEADER_SIZE + payloadLen
        if (this.buffer.length < totalSize) return

        // Extract payload
        const payload = this.buffer.slice(DISPLAY_HEADER_SIZE, totalSize)
        this.buffer = this.buffer.slice(totalSize)

        // Update framebuffer
        this.updateFramebuffer(x, y, w, h, payload)

        this.frames++
        this.bytes += totalSize
      }
    }

    validHeader (x, y, w, h, payloadLen) {
      return (
        x < this.width &&
        y < this.height &&
        w > 0 &&
        h > 0 &&
        x + w <= this.width &&
        y + h <= this.height &&
        payloadLen === w * h * 3
      )
    }

    updateFramebuffer (x, y, w, h, payload) {
      let src = 0
      for (let row = 0; row < h; row++) {
        for (let col = 0; col < w; col++) {
          const px = x + col
          const py = y + row
          const dst = (py * this.width + px) * 4

          // BGR -> RGBA
          this.imageData.data[dst + 2] = payload[src] // B -> B
          this.imageData.data[dst + 1] = payload[src + 1] // G -> G
          this.imageData.data[dst] = payload[src + 2] // R -> R
          this.imageData.data[dst + 3] = 255 // A
          src += 3
        }
      }

      this.ctx.putImageData(this.imageData, 0, 0)
    }

    updateStats () {
      if (!this.startTime) return

      const elapsed = (Date.now() - this.startTime) / 1000
      const fps = elapsed > 0 ? (this.frames / elapsed).toFixed(1) : 0
      const kbps = elapsed > 0 ? (this.bytes / 1024 / elapsed).toFixed(1) : 0

      this.frameCountTarget.textContent = this.frames.toLocaleString()
      this.fpsTarget.textContent = `${fps} fps`
      this.dataRateTarget.textContent = `${kbps} KB/s`
    }

    saveFrame () {
      const link = document.createElement('a')
      link.download = `frame_${Date.now()}.png`
      link.href = this.canvasTarget.toDataURL('image/png')
      link.click()
      this.log('Frame saved', 'success')
    }
  }
)
