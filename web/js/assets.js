/* Storm Summoner - Assets Controller */

application.register(
  'assets',
  class extends BaseController {
    static targets = [
      'breadcrumb',
      'fileList',
      'refreshBtn',
      'newFolderBtn',
      'deleteSelectedBtn',
      'uploadZipBtn',
      'downloadZipBtn',
      'zipInput',
      'selectAllCheckbox',
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
      'readonlyBanner',
      'deleteDialog',
      'deleteDialogMessage',
      'viewerModal',
      'viewerTitle',
      'viewerMeta',
      'viewerBody'
    ]

    // Refuse to render anything larger than this in the viewer; the file is
    // still downloadable.
    static VIEWER_MAX_BYTES = 512 * 1024

    // Roots known to the device. The Assets tab shows / as the userdata root;
    // commands are translated to USERDATA_BASE_PATH behind the scenes.
    // ASSETS_ROOT is replaced wholesale by the Assets OTA flow.
    static USERDATA_ROOT = '/userdata'
    static ASSETS_ROOT = '/assets'
    static DISPLAY_ROOT = '/'
    static ASSETS_MODE_TABS = new Set(['pedals', 'assets'])

    /** UI path (/) -> device path (/userdata). */
    toDevicePath (displayPath) {
      const p = displayPath ?? this.constructor.DISPLAY_ROOT
      if (p === '/' || p === '') return this.constructor.USERDATA_ROOT
      if (p === this.constructor.USERDATA_ROOT ||
          p.startsWith(`${this.constructor.USERDATA_ROOT}/`)) {
        return p
      }
      if (p.startsWith('/')) {
        return `${this.constructor.USERDATA_ROOT}${p}`
      }
      return `${this.constructor.USERDATA_ROOT}/${p}`
    }

    /** Device or legacy UI path -> display path rooted at / */
    toDisplayPath (path) {
      const root = this.constructor.USERDATA_ROOT
      if (!path || path === root || path === `${root}/`) {
        return this.constructor.DISPLAY_ROOT
      }
      if (path.startsWith(`${root}/`)) {
        return path.slice(root.length) || this.constructor.DISPLAY_ROOT
      }
      return path
    }

    connect () {
      this.currentPath = this.constructor.DISPLAY_ROOT
      this.files = []
      this.inAssetsMode = false
      this.renameTarget = null
      this.userdataAvailable = true
      this.selectedPaths = new Set()
      this._bulkDeleteBusy = false

      // Esc closes the file viewer. Bound on document because focus is usually
      // still on the file row, not inside the modal.
      this._onViewerKeydown = (e) => {
        if (e.key !== 'Escape' || !this.isViewerOpen()) return
        e.preventDefault()
        this.hideViewer()
      }
      document.addEventListener('keydown', this._onViewerKeydown)

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
        } else if (
          !this.constructor.ASSETS_MODE_TABS.has(e.detail.tab) &&
          (this.inAssetsMode || this.connection.currentMode === 'ASSETS')
        ) {
          this.leaveAssetsMode()
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
        if (this.hasDownloadZipBtnTarget) this.downloadZipBtnTarget.disabled = true
        if (this.hasDeleteSelectedBtnTarget) this.deleteSelectedBtnTarget.disabled = true
        this.selectedPaths.clear()
        this.inAssetsMode = false
      } else {
        this.applyWritableState()
      }
    }

    isWritablePath (path) {
      if (!path) return false
      const display = this.toDisplayPath(path)
      return display !== this.constructor.ASSETS_ROOT &&
        !display.startsWith(`${this.constructor.ASSETS_ROOT}/`)
    }

    isCurrentPathWritable () {
      if (this.currentPath === '/' || this.currentPath === '') return true
      return this.isWritablePath(this.currentPath)
    }

    isHiddenEntry (file, atRoot) {
      if (!file?.name || file.name.startsWith('.')) return true
      if (file.name === 'manifest.json') return true
      if (atRoot && file.type === 'dir' && file.name === 'cache') return true
      return false
    }

    isProtectedFilename (name) {
      return name === 'manifest.json'
    }

    isManifestProtectedPath (displayPath) {
      const p = this.toDisplayPath(displayPath)
      return p === '/scenes' || p.startsWith('/scenes/') ||
        p === '/devices' || p.startsWith('/devices/')
    }

    // Mirrors button-disable + read-only banner state to the current path.
    // Called whenever currentPath or connection state changes.
    applyWritableState () {
      const writable = this.isCurrentPathWritable() && this.connection.isConnected
      this.newFolderBtnTarget.disabled = !writable
      this.uploadZipBtnTarget.disabled = !writable
      if (this.hasDownloadZipBtnTarget) {
        this.downloadZipBtnTarget.disabled = !this.connection.isConnected
      }
      if (this.hasUploadZoneTarget) {
        this.uploadZoneTarget.classList.toggle('disabled', !writable)
      }
      if (this.hasReadonlyBannerTarget) {
        const showBanner = this.connection.isConnected
          && this.currentPath !== '/'
          && !writable
        this.readonlyBannerTarget.style.display = showBanner ? 'flex' : 'none'
      }
      this.updateBulkDeleteBtn()
    }

    clearSelection () {
      this.selectedPaths.clear()
      this.updateBulkDeleteBtn()
      this.syncSelectAllCheckbox()
    }

    updateBulkDeleteBtn () {
      if (!this.hasDeleteSelectedBtnTarget) return
      const writable = this.isCurrentPathWritable() && this.connection.isConnected
      this.deleteSelectedBtnTarget.disabled =
        !writable || this._bulkDeleteBusy || this.selectedPaths.size <= 1
    }

    selectableRowElements () {
      if (!this.hasFileListTarget) return []
      return [...this.fileListTarget.querySelectorAll('[data-controller~="assets-row"]')]
    }

    rowControllerFor (el) {
      return this.application.getControllerForElementAndIdentifier(el, 'assets-row')
    }

    onRowToggled (e) {
      const { path, selected } = e.detail || {}
      if (!path) return
      if (selected) this.selectedPaths.add(path)
      else this.selectedPaths.delete(path)
      this.updateBulkDeleteBtn()
      this.syncSelectAllCheckbox()
    }

    syncSelectAllCheckbox () {
      if (!this.hasSelectAllCheckboxTarget) return
      const rows = this.selectableRowElements()
      const n = rows.length
      const selected = this.selectedPaths.size
      this.selectAllCheckboxTarget.disabled = n === 0 || this._bulkDeleteBusy
      this.selectAllCheckboxTarget.checked = n > 0 && selected === n
      this.selectAllCheckboxTarget.indeterminate = selected > 0 && selected < n
    }

    toggleSelectAll (e) {
      e?.stopPropagation()
      const checked = !!this.selectAllCheckboxTarget?.checked
      this.selectedPaths.clear()
      for (const el of this.selectableRowElements()) {
        const ctrl = this.rowControllerFor(el)
        if (!ctrl) continue
        ctrl.selectedValue = checked
        if (checked) this.selectedPaths.add(ctrl.pathValue)
      }
      this.updateBulkDeleteBtn()
      this.syncSelectAllCheckbox()
    }

    async activate () {
      if (!this.connection.isConnected) {
        this.log('Connect to device first')
        return
      }

      try {
        await this.connection.runSerialTask(async () => {
          await this.connection._requestModeImpl('ASSETS')
          await this.enterAssetsModeBody()
        })
      } catch (err) {
        this.log(`Failed to activate: ${err.message}`, 'error')
      }
    }

    async leaveAssetsMode () {
      if (!this.inAssetsMode && this.connection.currentMode !== 'ASSETS') return
      try {
        if (this.connection.currentMode) {
          await this.connection.exitMode()
          await this.sleep(200)
        }
      } catch (err) {
        console.warn('Assets leave mode:', err)
      }
      this.inAssetsMode = false
    }

    async enterAssetsModeBody () {
      try {
        this.log('Entering assets mode...')
        await this.sleep(50)
        await this.connection.sendRaw('ASSETS\n')
        const line = await this.connection._readLineBody(5000)
        if (!line?.includes('ASSETS_STARTED')) {
          throw new Error(`Timeout waiting for ASSETS_STARTED (got: ${line || 'nothing'})`)
        }
        this.inAssetsMode = true
        this.log('Assets mode ready', 'success')
        this.currentPath = this.constructor.DISPLAY_ROOT
        this.updateBreadcrumb()
        this.applyWritableState()
        await this.loadDirectoryBody()
        await this.loadStatsBody()
      } catch (err) {
        this.log(`Assets mode failed: ${err.message}`, 'error')
      }
    }

    async sendCommand (cmd, timeout = 30000) {
      return this.connection.sendCommand(cmd, timeout)
    }

    async loadDirectoryBody () {
      const devicePath = this.toDevicePath(this.currentPath)
      const response = await this.connection._sendCommandImpl(`LS ${devicePath}`)

      if (!response || response.startsWith('ERROR')) {
        this.log(response || 'Empty LS response', 'error')
        return
      }

      this.files = JSON.parse(response)
      if (!Array.isArray(this.files)) {
        this.files = [this.files]
      }
      this.renderFileList()

      const fileCount = this.files.filter(f => !this.isHiddenEntry(
        f, this.toDisplayPath(this.currentPath) === '/'
      ) && f.type === 'file').length
      const dirCount = this.files.filter(f => !this.isHiddenEntry(
        f, this.toDisplayPath(this.currentPath) === '/'
      ) && f.type === 'dir').length
      this.fileCountTarget.textContent =
        `${fileCount} files, ${dirCount} folders`
    }

    // LS dispatches to the device under /userdata; the UI shows / as root.
    async loadDirectory () {
      try {
        await this.connection.runSerialTask(() => this.loadDirectoryBody())
      } catch (err) {
        this.log(`Failed to list directory: ${err.message}`, 'error')
      }
    }

    // Device response shape for DF:
    //   {"assets":{"total":N,"used":N,"free":N},
    //    "userdata":{"total":N,"used":N,"free":N,"available":bool}}
    // userdata.available is false when the userdata partition mount fails;
    // hide the userdata bar and surface a small "missing" warning instead.
    async loadStatsBody () {
      try {
        await this.sleep(100)
        const response = await this.connection._sendCommandImpl('DF')

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

    async loadStats () {
      return this.connection.runSerialTask(() => this.loadStatsBody())
    }

    // Navigation
    async navigateTo (event) {
      const path = event.currentTarget?.dataset?.path || '/'
      this.currentPath = path
      this.updateBreadcrumb()
      this.applyWritableState()
      await this.loadDirectory()
    }

    // Breadcrumb: / is the userdata root; segments are display paths.
    updateBreadcrumb () {
      const display = this.toDisplayPath(this.currentPath)
      let html = `<button data-action="click->assets#navigateTo" data-path="/">/</button>`
      const parts = display.split('/').filter(p => p)
      let accumulated = ''
      for (let i = 0; i < parts.length; i++) {
        accumulated += '/' + parts[i]
        if (i > 0) html += `<span>/</span>`
        html += `<button data-action="click->assets#navigateTo"`
          + ` data-path="${accumulated}">${parts[i]}</button>`
      }
      this.breadcrumbTarget.innerHTML = html
    }

    renderFileList () {
      this.clearSelection()
      const atRoot = this.toDisplayPath(this.currentPath) === '/'
      const showSelectAll = !atRoot && this.isCurrentPathWritable()
      const selectAllCell = showSelectAll
        ? `<div class="file-check">
            <input type="checkbox" data-assets-target="selectAllCheckbox"
              data-action="click->assets#stopEventPropagation change->assets#toggleSelectAll"
              title="Select all" aria-label="Select all">
          </div>`
        : `<span class="file-check-spacer" aria-hidden="true"></span>`
      const header = `
      <div class="file-header">
        ${selectAllCell}
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
        this.syncSelectAllCheckbox()
        return
      }

      const sorted = [...this.files].sort((a, b) => {
          if (a.type !== b.type) return a.type === 'dir' ? -1 : 1
          return a.name.localeCompare(b.name)
        })

      const visible = sorted.filter(f => !this.isHiddenEntry(f, atRoot))

      if (visible.length === 0) {
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
        this.syncSelectAllCheckbox()
        return
      }

      const items = visible
        .map(file => {
          const isDir = file.type === 'dir'
          const icon = isDir ? 'folder' : 'file'
          const iconClass = isDir ? 'folder' : 'file'
          const size = isDir ? '--' : this.formatSize(file.size)
          const fullPath = atRoot
            ? `/${file.name}`
            : `${this.toDisplayPath(this.currentPath)}/${file.name}`.replace(/\/+/g, '/')
          const rowWritable = this.isWritablePath(fullPath)
          const mutable = rowWritable && !atRoot
          // Only files get checkboxes; folder deletes stay on the row button so
          // recursive removal is always an explicit, single-target action.
          const selectable = mutable && !isDir
          const unavailable = false
          const nameSuffix = ''

          const renameBtn = mutable
            ? `<button data-action="click->assets#showRename"`
              + ` data-path="${fullPath}"`
              + ` data-name="${file.name}">Rename</button>`
            : ''
          const deleteBtn = mutable
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
          // Folders navigate from the whole row; files open the viewer from the
          // name only, so the action buttons stay unambiguous.
          const nameAttrs = isDir
            ? ''
            : `data-action="click->assets#viewFile" data-path="${fullPath}"`
          const nameClass = isDir ? 'file-name' : 'file-name viewable'
          const checkCell = selectable
            ? `<div class="file-check"
                data-action="click->assets-row#stopPropagation">
                <input type="checkbox" data-assets-row-target="checkbox"
                  data-action="click->assets-row#stopPropagation change->assets-row#toggle"
                  aria-label="Select ${file.name}">
              </div>`
            : `<span class="file-check-spacer" aria-hidden="true"></span>`
          const rowController = selectable
            ? `data-controller="assets-row"`
              + ` data-assets-row-path-value="${fullPath}"`
              + ` data-assets-row-type-value="${file.type}"`
              + ` data-assets-row-name-value="${file.name}"`
              + ` data-assets-row-selected-value="false"`
            : ''

          return `
        <div class="file-item" data-path="${fullPath}" data-type="${file.type}"
          data-name="${file.name}" ${rowController} ${clickAction}>
          ${checkCell}
          <div class="${nameClass}" ${nameAttrs}>
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
      this.syncSelectAllCheckbox()
      this.updateBulkDeleteBtn()
    }

    stopEventPropagation (e) {
      e?.stopPropagation()
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
        const { data } = await this.connection.fetchSizedTransfer(
          `GET ${this.toDevicePath(path)}`)
        const size = data.length

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

    // A file is treated as binary if it carries a NUL or an implausible run of
    // control bytes; rendering those into a <pre> produces noise, not content.
    looksBinary (bytes) {
      const limit = Math.min(bytes.length, 4096)
      let controls = 0
      for (let i = 0; i < limit; i++) {
        const b = bytes[i]
        if (b === 0) return true
        if (b < 0x09 || (b > 0x0d && b < 0x20)) controls++
      }
      return limit > 0 && controls / limit > 0.1
    }

    async viewFile (event) {
      event.preventDefault()
      event.stopPropagation()
      const path = event.currentTarget.dataset.path
      if (!path) return
      const name = path.split('/').pop()

      // A close or a second click while the transfer is in flight must win over
      // the late result, which would otherwise reopen or overwrite the viewer.
      const token = (this._viewerToken ?? 0) + 1
      this._viewerToken = token
      const update = (meta, body) => {
        if (this._viewerToken === token) this.showViewer(name, meta, body)
      }

      this.showViewer(name, '', 'Loading...')

      try {
        const { data } = await this.connection.fetchSizedTransfer(
          `GET ${this.toDevicePath(path)}`)

        const meta = this.formatSize(data.length)
        if (data.length > this.constructor.VIEWER_MAX_BYTES) {
          update(meta, `File is too large to preview (${meta}). Use Download instead.`)
          return
        }
        if (this.looksBinary(data)) {
          update(meta, `Binary file (${meta}). Use Download instead.`)
          return
        }

        update(meta, new TextDecoder('utf-8').decode(data))
      } catch (err) {
        update('', `Failed to read file: ${err.message}`)
        this.log(`View failed: ${err.message}`, 'error')
      }
    }

    showViewer (title, meta, body) {
      if (!this.hasViewerModalTarget) return
      this.viewerTitleTarget.textContent = title
      this.viewerMetaTarget.textContent = meta
      this.viewerBodyTarget.textContent = body
      this.viewerBodyTarget.scrollTop = 0
      this.viewerModalTarget.classList.add('open')
    }

    hideViewer () {
      if (!this.hasViewerModalTarget) return
      this._viewerToken = (this._viewerToken ?? 0) + 1
      this.viewerModalTarget.classList.remove('open')
      // Drop the contents so a large preview isn't retained in the DOM.
      this.viewerBodyTarget.textContent = ''
    }

    isViewerOpen () {
      return this.hasViewerModalTarget &&
        this.viewerModalTarget.classList.contains('open')
    }

    showDeleteConfirm (title, message) {
      if (!this.hasDeleteDialogTarget) {
        console.warn('Delete dialog unavailable:', message)
        return Promise.resolve(false)
      }
      return new Promise(resolve => {
        this._deleteConfirmResolver = resolve
        this.deleteDialogTarget.label = title || 'Delete'
        if (this.hasDeleteDialogMessageTarget) {
          this.deleteDialogMessageTarget.textContent = message || ''
        }
        this.deleteDialogTarget.open = true
      })
    }

    resolveDeleteConfirm (accepted) {
      if (this.hasDeleteDialogTarget) this.deleteDialogTarget.open = false
      const resolve = this._deleteConfirmResolver
      this._deleteConfirmResolver = null
      resolve?.(accepted)
    }

    cancelDeleteConfirm () {
      this.resolveDeleteConfirm(false)
    }

    confirmDeleteConfirm () {
      this.resolveDeleteConfirm(true)
    }

    async inspectDeletePlan (path, type) {
      let useRmrf = false
      let contentCount = 0
      if (type === 'dir') {
        try {
          const lsResponse = await this.sendCommand(`LS ${this.toDevicePath(path)}`)
          const contents = JSON.parse(lsResponse)
          contentCount = contents.length
          if (contentCount > 0) useRmrf = true
        } catch (_) {
          // If LS fails, try normal delete
        }
      }
      return { useRmrf, contentCount }
    }

    async deletePathOnDevice (path, type, plan = null) {
      const { useRmrf } = plan || await this.inspectDeletePlan(path, type)
      const cmd = useRmrf
        ? `RMRF ${this.toDevicePath(path)}`
        : `RM ${this.toDevicePath(path)}`
      const response = await this.sendCommand(cmd)
      return { ok: response === 'OK', response, useRmrf }
    }

    removeFileRow (path) {
      const row = this.fileListTarget?.querySelector(
        `.file-item[data-path="${CSS.escape(path)}"]`)
      row?.remove()
      this.selectedPaths.delete(path)
      this.files = this.files.filter(f => {
        const atRoot = this.toDisplayPath(this.currentPath) === '/'
        const fullPath = atRoot
          ? `/${f.name}`
          : `${this.toDisplayPath(this.currentPath)}/${f.name}`.replace(/\/+/g, '/')
        return fullPath !== path
      })
      this.updateBulkDeleteBtn()
      this.syncSelectAllCheckbox()
    }

    async deleteFile (event) {
      event.stopPropagation()
      const path = event.currentTarget.dataset.path
      const name = path.split('/').pop()
      const type = event.currentTarget.closest('.file-item')?.dataset.type

      const title = type === 'dir' ? 'Delete Folder' : 'Delete File'
      let message = `Delete "${name}"?`
      const plan = await this.inspectDeletePlan(path, type)
      if (type === 'dir' && plan.contentCount > 0) {
        message =
          `"${name}" contains ${plan.contentCount} item(s). Delete folder and ALL contents?`
      }

      if (!await this.showDeleteConfirm(title, message)) return

      try {
        const { ok, response, useRmrf } =
          await this.deletePathOnDevice(path, type, plan)
        if (ok) {
          this.log(
            useRmrf ? `Deleted ${name} and contents` : `Deleted ${name}`,
            'success'
          )
          await this.loadDirectory()
          await this.loadStats()
        } else {
          this.log(response, 'error')
        }
      } catch (err) {
        this.log(`Delete failed: ${err.message}`, 'error')
      }
    }

    async deleteSelected () {
      if (this._bulkDeleteBusy || this.selectedPaths.size <= 1) return
      if (!this.isCurrentPathWritable() || !this.connection.isConnected) return

      const items = this.selectableRowElements()
        .map(el => this.rowControllerFor(el))
        .filter(ctrl => ctrl && this.selectedPaths.has(ctrl.pathValue))
        .map(ctrl => ({
          path: ctrl.pathValue,
          type: ctrl.typeValue,
          name: ctrl.nameValue
        }))

      if (items.length <= 1) return

      const title = 'Delete Files'
      const message = `Delete ${items.length} selected files?`
      if (!await this.showDeleteConfirm(title, message)) return

      this._bulkDeleteBusy = true
      this.updateBulkDeleteBtn()
      this.syncSelectAllCheckbox()

      let deleted = 0
      try {
        for (const item of items) {
          try {
            const { ok, response } =
              await this.deletePathOnDevice(item.path, item.type)
            if (!ok) {
              this.log(response || `Failed to delete ${item.name}`, 'error')
              break
            }
            this.log(`Deleted ${item.name}`, 'success')
            deleted++
            this.removeFileRow(item.path)
          } catch (err) {
            this.log(`Delete failed for ${item.name}: ${err.message}`, 'error')
            break
          }
        }
      } finally {
        this._bulkDeleteBusy = false
        // Removing the last row leaves no rows to re-sync, so draw the empty state.
        if (!this.fileListTarget.querySelector('.file-item')) this.renderFileList()
        this.updateBulkDeleteBtn()
        this.syncSelectAllCheckbox()
        if (deleted > 0) await this.loadStats()
      }
    }

    async downloadZipAtPath (displayPath, downloadName) {
      this.log(`Creating archive of ${downloadName}...`)

      try {
        const { data } = await this.connection.fetchSizedTransfer(
          `ZIP ${this.toDevicePath(displayPath)}`, {
            lineTimeout: 60000,
            binaryTimeout: 60000
          })
        const size = data.length

        if (data.length !== size) {
          this.log(`Incomplete download: ${data.length}/${size} bytes`, 'error')
          return
        }

        const blob = new Blob([data], { type: 'application/zip' })
        const url = URL.createObjectURL(blob)
        const a = document.createElement('a')
        a.href = url
        a.download = `${downloadName}.zip`
        a.click()
        URL.revokeObjectURL(url)

        this.log(
          `Downloaded ${downloadName}.zip (${this.formatSize(size)})`,
          'success'
        )
      } catch (err) {
        this.log(`Archive failed: ${err.message}`, 'error')
      }
    }

    async downloadCurrentFolder () {
      const display = this.toDisplayPath(this.currentPath)
      const name = display === '/'
        ? 'userdata'
        : display.split('/').filter(Boolean).pop() || 'folder'
      await this.downloadZipAtPath(this.currentPath, name)
    }

    async archiveFolder (event) {
      event.stopPropagation()
      const path = event.currentTarget.dataset.path
      const folderName = event.currentTarget.dataset.name
      await this.downloadZipAtPath(path, folderName)
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

      const destPath = this.toDevicePath(this.currentPath)
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
          this.log(`Extracted ${file.name} to ${this.toDisplayPath(destPath)}`, 'success')
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
      if (file.name === 'manifest.json' && this.isManifestProtectedPath(this.currentPath)) {
        this.log('manifest.json is a protected system file', 'error')
        return
      }
      const remotePath = this.toDevicePath(
        this.currentPath === '/'
          ? `/${file.name}`
          : `${this.toDisplayPath(this.currentPath)}/${file.name}`.replace(/\/+/g, '/'))
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
      if (this.isProtectedFilename(name)) {
        this.log('That name is reserved', 'error')
        return
      }

      const path = this.toDevicePath(
        this.currentPath === '/'
          ? `/${name}`
          : `${this.toDisplayPath(this.currentPath)}/${name}`.replace(/\/+/g, '/'))

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
      if (this.isProtectedFilename(newName)) {
        this.log('manifest.json is a protected system file', 'error')
        return
      }

      const displayDir = this.toDisplayPath(this.renameTarget)
      const dir = displayDir.substring(0, displayDir.lastIndexOf('/')) || '/'
      const newDisplayPath = dir === '/'
        ? `/${newName}`
        : `${dir}/${newName}`.replace(/\/+/g, '/')

      try {
        const response = await this.sendCommand(
          `MV ${this.toDevicePath(this.renameTarget)} ${this.toDevicePath(newDisplayPath)}`)

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
