/* Storm Summoner - Assets Controller */

application.register(
  'assets',
  class extends BaseController {
    static targets = [
      'breadcrumb',
      'fileList',
      'refreshBtn',
      'newFolderBtn',
      'uploadZipBtn',
      'zipInput',
      'uploadZone',
      'fileInput',
      'statsBar',
      'assetsUsageBar',
      'assetsUsageText',
      'userdataUsageBar',
      'userdataUsageText',
      'userdataStats',
      'userdataMissing',
      'fileCount',
      'logContent',
      'newFolderModal',
      'newFolderInput',
      'renameModal',
      'renameInput',
      'readonlyBanner'
    ]

    // Roots known to the device. Mutating commands are only allowed under
    // USERDATA_ROOT; ASSETS_ROOT is replaced wholesale by the Assets OTA flow.
    // Keep these in sync with components/assets_manager/include/assets_manager.h
    // (ASSETS_BASE_PATH / USERDATA_BASE_PATH).
    static USERDATA_ROOT = '/userdata'
    static ASSETS_ROOT = '/assets'

    connect () {
      // Default to the writable root so users land in a usable view rather
      // than staring at the two-root index they have to click into.
      this.currentPath = this.constructor.USERDATA_ROOT
      this.files = []
      this.inAssetsMode = false
      this.renameTarget = null
      this.userdataAvailable = true

      // Listen for connection changes
      this.connection.on(
        'connection:changed',
        this.onConnectionChanged.bind(this)
      )

      // Listen for mode changes - reset our flag if mode changed away from assets
      this.connection.on('mode:changed', ({ mode }) => {
        if (mode !== 'ASSETS') this.inAssetsMode = false
      })

      // Listen for tab activation
      document.addEventListener('app:tab-activated', e => {
        if (
          e.detail.tab === 'assets' &&
          this.connection.isConnected &&
          !this.inAssetsMode
        ) {
          this.activate()
        }
      })
    }

    onConnectionChanged ({ connected }) {
      this.refreshBtnTarget.disabled = !connected
      this.statsBarTarget.style.display = connected ? 'flex' : 'none'
      // Mutation buttons are also gated by isCurrentPathWritable() once we
      // know which root we're in; on disconnect we just hard-disable.
      if (!connected) {
        this.newFolderBtnTarget.disabled = true
        this.uploadZipBtnTarget.disabled = true
        this.inAssetsMode = false
      } else {
        this.applyWritableState()
      }
    }

    isWritablePath (path) {
      if (!path) return false
      const root = this.constructor.USERDATA_ROOT
      return path === root || path.startsWith(root + '/')
    }

    isCurrentPathWritable () {
      // The two-root index ('/') is itself read-only — you can only descend.
      if (this.currentPath === '/' || this.currentPath === '') return false
      return this.isWritablePath(this.currentPath)
    }

    // Mirrors button-disable + read-only banner state to the current path.
    // Called whenever currentPath or connection state changes.
    applyWritableState () {
      const writable = this.isCurrentPathWritable() && this.connection.isConnected
      this.newFolderBtnTarget.disabled = !writable
      this.uploadZipBtnTarget.disabled = !writable
      if (this.hasUploadZoneTarget) {
        this.uploadZoneTarget.classList.toggle('disabled', !writable)
      }
      if (this.hasReadonlyBannerTarget) {
        const showBanner = this.connection.isConnected
          && this.currentPath !== '/'
          && !writable
        this.readonlyBannerTarget.style.display = showBanner ? 'flex' : 'none'
      }
    }

    async activate () {
      if (!this.connection.isConnected) {
        this.log('Connect to device first')
        return
      }

      try {
        const modeGranted = await this.connection.requestMode('ASSETS')
        if (!modeGranted) return

        await this.enterAssetsMode()
      } catch (err) {
        this.log(`Failed to activate: ${err.message}`, 'error')
      }
    }

    async enterAssetsMode () {
      try {
        this.log('Entering assets mode...')

        // Wait for device init
        await this.sleep(500)
        await this.connection.drainInput()

        // Send ASSETS command
        await this.connection.sendRaw('ASSETS\n')

        // Wait for ASSETS_STARTED
        const startTime = Date.now()
        while (Date.now() - startTime < 5000) {
          const line = await this.readLineWithTimeout(500)
          if (!line) continue
          if (line.includes('ASSETS_STARTED')) {
            this.inAssetsMode = true
            this.log('Assets mode ready', 'success')
            await this.navigateTo({
              currentTarget: { dataset: { path: this.constructor.USERDATA_ROOT } }
            })
            await this.loadStats()
            return
          }
        }
        throw new Error('Timeout waiting for ASSETS_STARTED')
      } catch (err) {
        this.log(`Assets mode failed: ${err.message}`, 'error')
      }
    }

    async readLineWithTimeout (timeout = 2000) {
      if (!this.connection.port?.readable) return null

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
        reader.releaseLock()
      }
      return line.replace(/\r/g, '').trim() || null
    }

    async sendCommand (cmd, timeout = 30000) {
      return this.connection.sendCommand(cmd, timeout)
    }

    // Phase 4 device response shape:
    //   {"assets":{"total":N,"used":N,"free":N},
    //    "userdata":{"total":N,"used":N,"free":N,"available":bool}}
    // userdata.available is false during the v(N+2) degraded boot path; we
    // hide the userdata bar and surface a small "missing" warning instead.
    async loadStats () {
      try {
        await this.sleep(100)
        const response = await this.sendCommand('DF')

        if (!response || response.startsWith('ERROR')) return

        const stats = JSON.parse(response)
        if (!stats || !stats.assets) return

        const a = stats.assets
        const aPct = a.total > 0 ? ((a.used / a.total) * 100).toFixed(1) : '0.0'
        this.assetsUsageBarTarget.style.width = `${aPct}%`
        this.assetsUsageTextTarget.textContent =
          `${this.formatSize(a.used)} / ${this.formatSize(a.total)}`

        const u = stats.userdata || { available: false, total: 0, used: 0 }
        this.userdataAvailable = !!u.available
        if (this.hasUserdataStatsTarget && this.hasUserdataMissingTarget) {
          this.userdataStatsTarget.style.display = u.available ? 'flex' : 'none'
          this.userdataMissingTarget.style.display = u.available ? 'none' : 'inline'
        }
        if (u.available) {
          const uPct = u.total > 0 ? ((u.used / u.total) * 100).toFixed(1) : '0.0'
          this.userdataUsageBarTarget.style.width = `${uPct}%`
          this.userdataUsageTextTarget.textContent =
            `${this.formatSize(u.used)} / ${this.formatSize(u.total)}`
        }
        this.applyWritableState()
      } catch (err) {
        // DF parse error - ignore silently
      }
    }

    // Navigation
    async navigateTo (event) {
      const path = event.currentTarget?.dataset?.path || '/'
      this.currentPath = path
      this.updateBreadcrumb()
      this.applyWritableState()
      await this.loadDirectory()
    }

    // Breadcrumb root is a literal "/" link that takes the user to the
    // two-root index; from there they descend into either /assets or
    // /userdata. Each prefix segment of currentPath becomes a clickable crumb.
    updateBreadcrumb () {
      let html = `<button data-action="click->assets#navigateTo" data-path="/">/</button>`
      const parts = this.currentPath.split('/').filter(p => p)
      let accumulated = ''
      for (const part of parts) {
        accumulated += '/' + part
        html += `<span>/</span>`
          + `<button data-action="click->assets#navigateTo"`
          + ` data-path="${accumulated}">${part}</button>`
      }
      this.breadcrumbTarget.innerHTML = html
    }

    // LS dispatches to the device, which has special-case behavior for "/"
    // (returns the two synthetic mount entries `assets` and `userdata`). The
    // entries gain a `readonly` flag and the userdata entry gets `available`.
    async loadDirectory () {
      try {
        const response = await this.sendCommand(`LS ${this.currentPath}`)

        if (response.startsWith('ERROR')) {
          this.log(response, 'error')
          return
        }

        this.files = JSON.parse(response)
        this.renderFileList()

        const fileCount = this.files.filter(f => f.type === 'file').length
        const dirCount = this.files.filter(f => f.type === 'dir').length
        this.fileCountTarget.textContent =
          `${fileCount} files, ${dirCount} folders`
      } catch (err) {
        this.log(`Failed to list directory: ${err.message}`, 'error')
      }
    }

    renderFileList () {
      const header = `
      <div class="file-header">
        <span>Name</span>
        <span style="text-align: right">Size</span>
        <span style="text-align: right">Actions</span>
      </div>
    `

      if (this.files.length === 0) {
        this.fileListTarget.innerHTML =
          header +
          `
        <div class="empty-state">
          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5">
            <path d="M3 7v10a2 2 0 002 2h14a2 2 0 002-2V9a2 2 0 00-2-2h-6l-2-2H5a2 2 0 00-2 2z"/>
          </svg>
          <p>Empty folder</p>
        </div>
      `
        return
      }

      const sorted = [...this.files]
        .filter(f => !f.name.startsWith('.'))
        .sort((a, b) => {
          if (a.type !== b.type) return a.type === 'dir' ? -1 : 1
          return a.name.localeCompare(b.name)
        })

      const atRoot = this.currentPath === '/' || this.currentPath === ''

      const items = sorted
        .map(file => {
          const isDir = file.type === 'dir'
          const icon = isDir ? 'folder-fill' : 'file-earmark'
          const iconClass = isDir ? 'folder' : 'file'
          const size = isDir ? '--' : this.formatSize(file.size)
          const fullPath =
            atRoot ? `/${file.name}`
              : `${this.currentPath}/${file.name}`
          // Per-row writability: at the synthetic root we trust the device's
          // `readonly` hint (assets=true, userdata=false). Anywhere else,
          // writability is a function of the path itself.
          const rowWritable = atRoot
            ? !file.readonly && file.available !== false
            : this.isWritablePath(fullPath)
          const unavailable = atRoot && file.name === 'userdata'
            && file.available === false
          const nameSuffix = unavailable
            ? ` <span class="badge warn">missing — re-run System Update</span>`
            : (atRoot && file.readonly)
              ? ` <span class="badge">read-only</span>`
              : ''

          const renameBtn = rowWritable && !atRoot
            ? `<button data-action="click->assets#showRename"`
              + ` data-path="${fullPath}"`
              + ` data-name="${file.name}">Rename</button>`
            : ''
          const deleteBtn = rowWritable && !atRoot
            ? `<button class="delete"`
              + ` data-action="click->assets#deleteFile"`
              + ` data-path="${fullPath}">Delete</button>`
            : ''
          const archiveBtn = isDir && !unavailable
            ? `<button data-action="click->assets#archiveFolder"`
              + ` data-path="${fullPath}"`
              + ` data-name="${file.name}">Archive</button>`
            : ''
          const downloadBtn = !isDir
            ? `<button data-action="click->assets#downloadFile"`
              + ` data-path="${fullPath}">Download</button>`
            : ''
          const clickAction = isDir && !unavailable
            ? `data-action="click->assets#openFolder"` : ''

          return `
        <div class="file-item" data-path="${fullPath}" data-type="${file.type}" data-name="${file.name}" ${clickAction}>
          <div class="file-name">
            <wa-icon name="${icon}" class="file-icon ${iconClass}"></wa-icon>
            <span>${file.name}</span>${nameSuffix}
          </div>
          <div class="file-size">${size}</div>
          <div class="file-actions">
            ${archiveBtn}
            ${downloadBtn}
            ${renameBtn}
            ${deleteBtn}
          </div>
        </div>
      `
        })
        .join('')

      this.fileListTarget.innerHTML = header + items
    }

    openFolder (event) {
      event.preventDefault()
      event.stopPropagation()
      const path = event.currentTarget.dataset.path
      this.navigateTo({ currentTarget: { dataset: { path } } })
    }

    async refresh () {
      await this.loadStats()
      await this.loadDirectory()
      this.log('Refreshed')
    }

    // File operations
    async downloadFile (event) {
      event.stopPropagation()
      const path = event.currentTarget.dataset.path
      const filename = path.split('/').pop()

      this.log(`Downloading ${filename}...`)

      try {
        const response = await this.sendCommand(`GET ${path}`)

        if (response.startsWith('ERROR')) {
          this.log(response, 'error')
          return
        }

        if (!response.startsWith('SIZE ')) {
          this.log(`Unexpected response: ${response}`, 'error')
          return
        }

        const size = parseInt(response.split(' ')[1])
        const data = await this.connection.readBinary(size)

        const blob = new Blob([data])
        const url = URL.createObjectURL(blob)
        const a = document.createElement('a')
        a.href = url
        a.download = filename
        a.click()
        URL.revokeObjectURL(url)

        this.log(`Downloaded ${filename} (${this.formatSize(size)})`, 'success')
      } catch (err) {
        this.log(`Download failed: ${err.message}`, 'error')
      }
    }

    async deleteFile (event) {
      event.stopPropagation()
      const path = event.currentTarget.dataset.path
      const name = path.split('/').pop()
      const type = event.currentTarget.closest('.file-item')?.dataset.type

      if (type === 'dir') {
        try {
          const lsResponse = await this.sendCommand(`LS ${path}`)
          const contents = JSON.parse(lsResponse)

          if (contents.length > 0) {
            if (
              !confirm(
                `"${name}" contains ${contents.length} item(s).\n\nDelete folder and ALL contents?`
              )
            ) {
              return
            }

            const response = await this.sendCommand(`RMRF ${path}`)
            if (response === 'OK') {
              this.log(`Deleted ${name} and contents`, 'success')
              await this.loadDirectory()
              await this.loadStats()
            } else {
              this.log(response, 'error')
            }
            return
          }
        } catch (err) {
          // If LS fails, try normal delete
        }
      }

      if (!confirm(`Delete "${name}"?`)) return

      try {
        const response = await this.sendCommand(`RM ${path}`)

        if (response === 'OK') {
          this.log(`Deleted ${name}`, 'success')
          await this.loadDirectory()
          await this.loadStats()
        } else {
          this.log(response, 'error')
        }
      } catch (err) {
        this.log(`Delete failed: ${err.message}`, 'error')
      }
    }

    async archiveFolder (event) {
      event.stopPropagation()
      const path = event.currentTarget.dataset.path
      const folderName = event.currentTarget.dataset.name

      this.log(`Creating archive of ${folderName}...`)

      try {
        const response = await this.sendCommand(`ZIP ${path}`, 60000)

        if (response.startsWith('ERROR')) {
          this.log(response, 'error')
          return
        }

        if (!response.startsWith('SIZE ')) {
          this.log(`Unexpected response: ${response}`, 'error')
          return
        }

        const size = parseInt(response.split(' ')[1])
        this.log(`Archive size: ${this.formatSize(size)}, downloading...`)

        const data = await this.connection.readBinary(size, 60000)

        if (data.length !== size) {
          this.log(`Incomplete download: ${data.length}/${size} bytes`, 'error')
          return
        }

        const blob = new Blob([data], { type: 'application/zip' })
        const url = URL.createObjectURL(blob)
        const a = document.createElement('a')
        a.href = url
        a.download = `${folderName}.zip`
        a.click()
        URL.revokeObjectURL(url)

        this.log(
          `Downloaded ${folderName}.zip (${this.formatSize(size)})`,
          'success'
        )
      } catch (err) {
        this.log(`Archive failed: ${err.message}`, 'error')
      }
    }

    // Upload
    selectZipFile () {
      this.zipInputTarget.click()
    }

    async extractZip (event) {
      const file = event.target.files[0]
      if (!file) return
      event.target.value = ''

      if (!this.isCurrentPathWritable()) {
        this.log(`Cannot extract under ${this.currentPath} (read-only)`, 'error')
        return
      }

      const destPath = this.currentPath
      this.log(
        `Uploading ${file.name} (${this.formatSize(
          file.size
        )}) for extraction...`
      )

      this.connection.setTabsLocked(true, 'assets')
      try {
        const data = new Uint8Array(await file.arrayBuffer())
        const response = await this.sendCommand(
          `EXTRACT ${destPath} ${data.length}`
        )

        if (response !== 'READY') {
          this.log(`Device not ready: ${response}`, 'error')
          return
        }

        await this.connection.sendBinary(data)
        this.log('Extracting...')

        const result = await this.connection.readLine(60000)

        if (result === 'OK') {
          this.log(`Extracted ${file.name} to ${destPath}`, 'success')
          await this.loadDirectory()
          await this.loadStats()
        } else {
          this.log(`Extraction failed: ${result}`, 'error')
        }
      } catch (err) {
        this.log(`Extract failed: ${err.message}`, 'error')
      } finally {
        this.connection.setTabsLocked(false)
      }
    }

    dragOver (event) {
      event.preventDefault()
      this.uploadZoneTarget.classList.add('dragover')
    }

    dragLeave (event) {
      event.preventDefault()
      this.uploadZoneTarget.classList.remove('dragover')
    }

    drop (event) {
      event.preventDefault()
      this.uploadZoneTarget.classList.remove('dragover')
      if (!this.isCurrentPathWritable()) {
        this.log(`Cannot upload under ${this.currentPath} (read-only)`, 'error')
        return
      }
      const files = event.dataTransfer.files
      if (files.length) this.doUpload(files)
    }

    selectFiles () {
      if (!this.isCurrentPathWritable()) {
        this.log(`Cannot upload under ${this.currentPath} (read-only)`, 'error')
        return
      }
      this.fileInputTarget.click()
    }

    uploadFiles (event) {
      const files = event.target.files
      if (files.length) this.doUpload(files)
      event.target.value = ''
    }

    async doUpload (files) {
      if (!this.isCurrentPathWritable()) {
        this.log(`Cannot upload under ${this.currentPath} (read-only)`, 'error')
        return
      }
      this.connection.setTabsLocked(true, 'assets')
      try {
        for (const file of files) {
          await this.uploadFile(file)
        }
        await this.loadDirectory()
        await this.loadStats()
      } finally {
        this.connection.setTabsLocked(false)
      }
    }

    async uploadFile (file) {
      const remotePath =
        this.currentPath === '/'
          ? `/${file.name}`
          : `${this.currentPath}/${file.name}`
      this.log(`Uploading ${file.name} (${this.formatSize(file.size)})...`)

      try {
        const response = await this.sendCommand(
          `PUT ${remotePath} ${file.size}`
        )

        if (response !== 'READY') {
          this.log(`Upload failed: ${response}`, 'error')
          return
        }

        const data = new Uint8Array(await file.arrayBuffer())
        await this.connection.sendBinary(data)

        const result = await this.connection.readLine(30000)

        if (result === 'OK') {
          this.log(`Uploaded ${file.name}`, 'success')
        } else {
          this.log(`Upload failed: ${result}`, 'error')
        }
      } catch (err) {
        this.log(`Upload failed: ${err.message}`, 'error')
      }
    }

    // Modals
    showNewFolder () {
      if (!this.isCurrentPathWritable()) {
        this.log(`Cannot create folders under ${this.currentPath}`, 'error')
        return
      }
      this.newFolderInputTarget.value = ''
      this.newFolderModalTarget.classList.add('open')
      this.newFolderInputTarget.focus()
    }

    hideNewFolder () {
      this.newFolderModalTarget.classList.remove('open')
    }

    async createFolder () {
      const name = this.newFolderInputTarget.value.trim()
      if (!name) return

      const path =
        this.currentPath === '/' ? `/${name}` : `${this.currentPath}/${name}`

      try {
        const response = await this.sendCommand(`MKDIR ${path}`)

        if (response === 'OK') {
          this.log(`Created folder ${name}`, 'success')
          this.hideNewFolder()
          await this.loadDirectory()
        } else {
          this.log(response, 'error')
        }
      } catch (err) {
        this.log(`Create folder failed: ${err.message}`, 'error')
      }
    }

    showRename (event) {
      event.stopPropagation()
      this.renameTarget = event.currentTarget.dataset.path
      this.renameInputTarget.value = event.currentTarget.dataset.name
      this.renameModalTarget.classList.add('open')
      this.renameInputTarget.focus()
      this.renameInputTarget.select()
    }

    hideRename () {
      this.renameModalTarget.classList.remove('open')
      this.renameTarget = null
    }

    async doRename () {
      const newName = this.renameInputTarget.value.trim()
      if (!newName || !this.renameTarget) return

      const dir = this.renameTarget.substring(
        0,
        this.renameTarget.lastIndexOf('/')
      )
      const newPath = dir + '/' + newName

      try {
        const response = await this.sendCommand(
          `MV ${this.renameTarget} ${newPath}`
        )

        if (response === 'OK') {
          this.log(`Renamed to ${newName}`, 'success')
          this.hideRename()
          await this.loadDirectory()
        } else {
          this.log(response, 'error')
        }
      } catch (err) {
        this.log(`Rename failed: ${err.message}`, 'error')
      }
    }
  }
)
