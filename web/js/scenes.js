/* Storm Summoner - Scenes Controller */

const SCENES_LIST_SYNC_EVENTS = new Set([
  'scene_list_changed',
  'scene_reordered',
  'scene_changed'
])

application.register(
  'scenes',
  class extends BaseController {
    static targets = [
      'refreshBtn', 'addBtn', 'activeHeader', 'container', 'logContent',
      'deleteDialog', 'deleteSceneName',
      'nameDialog', 'nameInput', 'nameDialogConfirm', 'nameError'
    ]

    connect() {
      this.scenes = []
      this.selectedPosition = null
      this.panelMode = null
      this.inScenesMode = false
      this.pendingDeletePosition = null
      this.pendingRenamePosition = null  // null for add, position for rename
      this.pendingDuplicatePosition = null  // position when duplicating
      this.draggedElement = null
      this.notifyDebounce = null
      this._fetchScenesPromise = null

      // Listen for connection changes
      this.connection.on('connection:changed', this.onConnectionChanged.bind(this))

      // Listen for mode changes
      this.connection.on('mode:changed', ({ mode }) => {
        if (mode !== 'SCENES') {
          this.inScenesMode = false
        }
      })

      this._onCdcNotify = this.onCdcNotify.bind(this)
      document.addEventListener('cdc:notify', this._onCdcNotify)

      // Listen for tab activation
      document.addEventListener('app:tab-activated', async (e) => {
        if (e.detail.tab === 'scenes') {
          if (!this.connection.isConnected) return
          if (!this.inScenesMode) {
            await this.activate()
          } else {
            await this.openDetailPanelAfterListLoad()
          }
          return
        }
        if (this.connection.isConnected &&
            (this.inScenesMode || this.connection._deviceScenesActive ||
             this.connection.currentMode === 'SCENES')) {
          await this.leaveScenesModeFully()
        }
      })
    }

    disconnect () {
      document.removeEventListener('cdc:notify', this._onCdcNotify)
      if (this.notifyDebounce) clearTimeout(this.notifyDebounce)
    }

    onCdcNotify (e) {
      const kind = e.detail?.kind
      if (!SCENES_LIST_SYNC_EVENTS.has(kind)) return
      if (!this.connection.isConnected || !this.inScenesMode) return
      if (this.connection.isSerialBusy) return

      const activeTab = document.querySelector('wa-tab-group wa-tab[active]')
      if (activeTab?.getAttribute('panel') !== 'scenes') return

      if (this.notifyDebounce) clearTimeout(this.notifyDebounce)
      this.notifyDebounce = setTimeout(() => {
        this.notifyDebounce = null
        if (this.connection.isSerialBusy) return
        this.fetchScenes()
      }, 100)
    }

    onConnectionChanged({ connected }) {
      this.refreshBtnTarget.disabled = !connected
      this.addBtnTarget.disabled = !connected
      if (!connected) {
        this.inScenesMode = false
        this.selectedPosition = null
        this.panelMode = null
        this.renderEmpty()
      }
    }

    async activate() {
      if (!this.connection.isConnected) return

      try {
        await this.scenesSerial(async () => {
          await this.ensureScenesMode()
          await this.settleDelay()
          await this.fetchScenesBody()
        })
        await this.openDetailPanelAfterListLoad()
      } catch (err) {
        console.error('Scenes activation error:', err)
        this.log('Activation error: ' + err.message, 'error')
      }
    }

    scenesSerial(fn) {
      const impl = () => fn()
      return this.connection.isSerialBusy ? impl() : this.connection.runSerialTask(impl)
    }

    async ensureScenesMode() {
      if (this.inScenesMode) return

      if (this.connection.currentMode !== 'SCENES') {
        const modeGranted = await this.connection.requestMode('SCENES')
        if (!modeGranted) throw new Error('Could not enter SCENES mode')
      }
      await this.enterScenesMode()
    }

    async enterScenesMode() {
      await this.sleep(100)

      for (let attempt = 0; attempt < 2; attempt++) {
        await this.connection.sendRaw('SCENES\n')
        const response = await this.connection.readLine(3000)

        if (response === 'SCENES_STARTED') {
          this.inScenesMode = true
          this.log('Entered scenes mode')
          return
        }

        // Device may still be in SCENES after the scene panel sent EXIT; sync to idle.
        if (attempt === 0 && response?.includes('Unknown')) {
          await this.connection.sendRaw('EXIT\n')
          await this.sleep(150)
          await this.connection.drainInput()
          continue
        }

        throw new Error(`Unexpected response: ${response || '(no response)'}`)
      }
    }

    // Small delay to let serial buffers settle between operations
    async settleDelay() {
      await this.sleep(200)
    }

    readLine(timeout = 2000) {
      return this.connection.readLine(timeout)
    }

    normalizeSceneListEntry (scene) {
      const name = String(scene?.name ?? '').trim()
      return { ...scene, name: name || 'Untitled' }
    }

    updateActiveHeader () {
      if (!this.hasActiveHeaderTarget) return
      const n = (this.scenes || []).filter(s => s.active).length
      this.activeHeaderTarget.textContent = `Active Scenes (${n})`
    }

    async fetchScenesListOnce () {
      await this.connection.sendRaw('LIST\n')
      return this.readLine(5000)
    }

    async fetchScenes() {
      if (this._fetchScenesPromise) return this._fetchScenesPromise

      const run = () => this.fetchScenesBody()
      const task = this.connection.isSerialBusy
        ? run()
        : this.connection.runSerialTask(run)

      this._fetchScenesPromise = Promise.resolve(task).finally(() => {
        this._fetchScenesPromise = null
      })
      return this._fetchScenesPromise
    }

    async fetchScenesBody() {
      try {
        await this.ensureScenesMode()
        await this.settleDelay()
        let response = await this.fetchScenesListOnce()

        if (!response || response.startsWith('ERROR:')) {
          this.inScenesMode = false
          await this.ensureScenesMode()
          await this.settleDelay()
          response = await this.fetchScenesListOnce()
        }

        if (!response || response.startsWith('ERROR:')) {
          this.log('Failed to fetch scenes: ' + (response || 'no response'), 'error')
          return
        }

        if (response === 'SCENES_STOPPED' || !response.includes('[')) {
          this.inScenesMode = false
          await this.ensureScenesMode()
          await this.settleDelay()
          return this.fetchScenesBody()
        }

        // Extract just the JSON array in case there's extra data
        const jsonStart = response.indexOf('[')
        const jsonEnd = response.lastIndexOf(']')
        if (jsonStart === -1 || jsonEnd === -1 || jsonEnd < jsonStart) {
          console.error('Invalid JSON response:', response)
          this.log('Invalid response format', 'error')
          return
        }

        const jsonStr = response.substring(jsonStart, jsonEnd + 1)
        this.scenes = JSON.parse(jsonStr).map(s => this.normalizeSceneListEntry(s))
        this.renderScenes()
        this.log(`Loaded ${this.scenes.length} scenes`)
      } catch (err) {
        console.error('Error fetching scenes:', err)
        this.log('Error: ' + err.message, 'error')
      }
    }

    renderEmpty() {
      this.containerTarget.innerHTML = `
        <div class="empty-state">
          <wa-icon name="layer-group"></wa-icon>
          <p>Connect to view scenes</p>
        </div>
      `
      this.scenes = []
      this.updateActiveHeader()
      this.publishSceneList()
    }

    renderScenes() {
      this.updateActiveHeader()

      if (!this.scenes || this.scenes.length === 0) {
        this.containerTarget.innerHTML = `
          <div class="empty-state">
            <wa-icon name="layer-group"></wa-icon>
            <p>No scenes found</p>
            <p class="hint">Click "Add Scene" to create one</p>
          </div>
        `
        return
      }

      const activeScenes = this.scenes.filter(s => s.active)
      const inactiveScenes = this.scenes.filter(s => !s.active && !s.factory)
      const factoryScenes = this.scenes.filter(s => !s.active && s.factory)

      let html = ''

      html += `
        <div class="scenes-section active">
          <ul class="scene-list" data-section="active">
      `

      if (activeScenes.length === 0) {
        html += '<div class="scenes-empty">No active scenes</div>'
      } else {
        activeScenes.forEach((scene, displayIdx) => {
          html += this.renderSceneRow(scene, displayIdx + 1, true)
        })
      }

      html += '</ul></div>'

      // Inactive scenes section (if any)
      if (inactiveScenes.length > 0) {
        html += `
          <div class="scenes-section inactive">
            <div class="scenes-section-header">
              <wa-icon name="eye-slash"></wa-icon>
              Inactive Scenes (${inactiveScenes.length})
            </div>
            <ul class="scene-list" data-section="inactive">
        `

        inactiveScenes.forEach(scene => {
          html += this.renderSceneRow(scene, null, false)
        })

        html += '</ul></div>'
      }

      // Factory scenes section (if any)
      if (factoryScenes.length > 0) {
        html += `
          <div class="scenes-section factory">
            <div class="scenes-section-header">
              <wa-icon name="industry"></wa-icon>
              Factory (${factoryScenes.length})
            </div>
            <ul class="scene-list" data-section="factory">
        `

        factoryScenes.forEach(scene => {
          html += this.renderSceneRow(scene, null, false)
        })

        html += '</ul></div>'
      }

      this.containerTarget.innerHTML = html

      // Attach drag-and-drop listeners to active scenes
      this.attachDragListeners()
      this.highlightSelection()
      this.publishSceneList()
    }

    publishSceneList () {
      document.dispatchEvent(
        new CustomEvent('scenes:list-updated', { detail: { scenes: this.scenes } })
      )
    }

    highlightSelection () {
      if (!this.hasContainerTarget) return
      this.containerTarget.querySelectorAll('.scene-row').forEach(row => {
        const pos = parseInt(row.dataset.position, 10)
        row.classList.toggle('selected', pos === this.selectedPosition)
      })
    }

    stopRowClick (e) {
      e.stopPropagation()
    }

    openViewRow (e) {
      if (e.target.closest('.scene-drag-handle')) return
      const row = e.target.closest('.scene-row')
      if (!row) return
      const position = parseInt(row.dataset.position, 10)
      if (Number.isNaN(position)) return
      this.openScene(position, 'view')
    }

    openScene (position, mode) {
      if (Number.isNaN(position)) return
      void this.openSceneDispatch(position, mode)
    }

    async openSceneDispatch (position, mode) {
      this.selectedPosition = position
      this.panelMode = mode === 'edit' ? 'edit' : 'view'
      this.highlightSelection()
      if (this.scenes.length) this.renderScenes()

      let done = () => {}
      const loaded = new Promise(resolve => { done = resolve })

      await this.connection.runSerialTask(async () => {
        await this.leaveScenesModeInTask()
        document.dispatchEvent(
          new CustomEvent('scenes:open-scene', {
            detail: { position, mode, ready: done }
          })
        )
        await loaded
      })
    }

    async openDetailPanelAfterListLoad () {
      if (this.selectedPosition !== null) {
        await this.openSceneDispatch(this.selectedPosition, this.panelMode || 'view')
        return
      }
      const playing = this.scenes.find(s => s.current)
      if (!playing) return
      await this.openSceneDispatch(playing.position, 'view')
    }

    async selectPlayingSceneIfNeeded () {
      if (this.selectedPosition !== null) return
      await this.openDetailPanelAfterListLoad()
    }

    async leaveScenesModeForInspect () {
      await this.connection.runSerialTask(() => this.leaveScenesModeInTask())
    }

    async leaveScenesModeFully () {
      if (!this.inScenesMode && !this.connection._deviceScenesActive &&
          this.connection.currentMode !== 'SCENES') return

      this.inScenesMode = false
      const impl = async () => {
        await this.connection.ensureDeviceIdle()
      }
      if (this.connection.isSerialBusy) await impl()
      else await this.connection.runSerialTask(impl)
    }

    async leaveScenesModeInTask () {
      if (!this.inScenesMode && this.connection.currentMode !== 'SCENES') return

      this.inScenesMode = false
      if (this.connection.currentMode === 'SCENES') {
        this.connection._stopModeNotifyLoop()
        await this.connection._suspendModeNotify()
        this.connection.mode = null
        // The device is still in SCENES even though we present mode as null;
        // record it so a later mode change / exit sends EXIT to the device.
        this.connection._deviceScenesActive = true
        this.connection.emit('mode:changed', { mode: null })
      }
    }

    openView (e) {
      e.stopPropagation()
      const position = parseInt(e.currentTarget.dataset.position, 10)
      this.openScene(position, 'view')
    }

    openEdit (e) {
      e.stopPropagation()
      const position = parseInt(e.currentTarget.dataset.position, 10)
      this.openScene(position, 'edit')
    }

    printScene (e) {
      e.stopPropagation()
      const position = parseInt(e.currentTarget.dataset.position, 10)
      this.openScene(position, 'print')
    }

    downloadScene (e) {
      e.stopPropagation()
      const position = parseInt(e.currentTarget.dataset.position, 10)
      if (Number.isNaN(position)) return
      document.dispatchEvent(
        new CustomEvent('scenes:download-scene', { detail: { position } })
      )
    }

    renderSceneRow(scene, displayNumber, isActive) {
      const currentClass = scene.current ? ' current' : ''
      const selectedClass =
        scene.position === this.selectedPosition ? ' selected' : ''
      const positionDisplay = displayNumber !== null ? displayNumber : '-'
      const showActivate = isActive && !scene.current
      const showDelete = !scene.current
      const showDisable = isActive && !scene.current
      const isPanelScene = scene.position === this.selectedPosition
      const hideView = isPanelScene && this.panelMode === 'view'
      const hideEdit = isPanelScene && this.panelMode === 'edit'

      return `
        <li class="scene-row${currentClass}${selectedClass}"
            data-position="${scene.position}"
            data-index="${scene.index}"
            draggable="${isActive}"
            data-active="${isActive}"
            data-action="click->scenes#openViewRow">
          <div class="scene-drag-handle">
            <wa-icon name="grip-vertical"></wa-icon>
          </div>
          <div class="scene-position">${positionDisplay}</div>
          <div class="scene-name">
            <span>${this.escapeHtml(scene.name)}</span>
            ${scene.current ? '<span class="scene-current-badge">Playing</span>' : ''}
          </div>
          <div class="scene-actions" data-action="click->scenes#stopRowClick">
            ${showActivate ? `
              <wa-button size="small" appearance="text"
                         data-action="click->scenes#switchToScene"
                         data-position="${scene.position}"
                         title="Activate (switch playing scene)">
                <wa-icon name="play"></wa-icon>
              </wa-button>
            ` : ''}
            ${hideView ? '' : `
            <wa-button size="small" appearance="text"
                       data-action="click->scenes#openView"
                       data-position="${scene.position}"
                       title="View scene">
              <wa-icon name="eye"></wa-icon>
            </wa-button>
            `}
            ${hideEdit ? '' : `
            <wa-button size="small" appearance="text"
                       data-action="click->scenes#openEdit"
                       data-position="${scene.position}"
                       title="Edit scene">
              <wa-icon name="pen-to-square"></wa-icon>
            </wa-button>
            `}
            <wa-button size="small" appearance="text"
                       data-action="click->scenes#downloadScene"
                       data-position="${scene.position}"
                       title="Download scene JSON">
              <wa-icon name="download"></wa-icon>
            </wa-button>
            <wa-button size="small" appearance="text"
                       data-action="click->scenes#printScene"
                       data-position="${scene.position}"
                       title="Print scene inspect">
              <wa-icon name="print"></wa-icon>
            </wa-button>
            <wa-button size="small" appearance="text"
                       data-action="click->scenes#duplicate"
                       data-position="${scene.position}"
                       title="Duplicate">
              <wa-icon name="clone"></wa-icon>
            </wa-button>
            ${showDisable ? `
              <wa-button size="small" appearance="text"
                         data-action="click->scenes#deactivate"
                         data-position="${scene.position}"
                         title="Disable (remove from active set)">
                <wa-icon name="toggle-off"></wa-icon>
              </wa-button>
            ` : isActive ? '' : `
              <wa-button size="small" appearance="text"
                         data-action="click->scenes#activateScene"
                         data-position="${scene.position}"
                         title="Enable (add to active set)">
                <wa-icon name="toggle-on"></wa-icon>
              </wa-button>
            `}
            ${showDelete ? `
              <wa-button size="small" appearance="text" class="danger"
                         data-action="click->scenes#showDeleteDialog"
                         data-position="${scene.position}"
                         title="Delete">
                <wa-icon name="trash"></wa-icon>
              </wa-button>
            ` : ''}
          </div>
        </li>
      `
    }

    escapeHtml(text) {
      const div = document.createElement('div')
      div.textContent = text
      return div.innerHTML
    }

    attachDragListeners() {
      const rows = this.containerTarget.querySelectorAll('.scene-row[draggable="true"]')

      rows.forEach(row => {
        row.addEventListener('dragstart', this.onDragStart.bind(this))
        row.addEventListener('dragend', this.onDragEnd.bind(this))
        row.addEventListener('dragover', this.onDragOver.bind(this))
        row.addEventListener('dragleave', this.onDragLeave.bind(this))
        row.addEventListener('drop', this.onDrop.bind(this))
      })
    }

    onDragStart(e) {
      this.draggedElement = e.target.closest('.scene-row')
      if (this.draggedElement) {
        this.draggedElement.classList.add('dragging')
        e.dataTransfer.effectAllowed = 'move'
        e.dataTransfer.setData('text/plain', this.draggedElement.dataset.position)
      }
    }

    onDragEnd(e) {
      if (this.draggedElement) {
        this.draggedElement.classList.remove('dragging')
        this.draggedElement = null
      }
      // Remove all drag-over classes
      this.containerTarget.querySelectorAll('.drag-over').forEach(el => {
        el.classList.remove('drag-over')
      })
    }

    onDragOver(e) {
      e.preventDefault()
      const row = e.target.closest('.scene-row')
      if (row && row !== this.draggedElement && row.dataset.active === 'true') {
        row.classList.add('drag-over')
      }
    }

    onDragLeave(e) {
      const row = e.target.closest('.scene-row')
      if (row) {
        row.classList.remove('drag-over')
      }
    }

    async onDrop(e) {
      e.preventDefault()
      const targetRow = e.target.closest('.scene-row')
      if (!targetRow || targetRow === this.draggedElement) return

      targetRow.classList.remove('drag-over')

      const fromPosition = parseInt(e.dataTransfer.getData('text/plain'), 10)
      const toPosition = parseInt(targetRow.dataset.position, 10)

      if (fromPosition === toPosition) return

      await this.reorderScenes(fromPosition, toPosition)
    }

    async reorderScenes(fromPos, toPos) {
      this.log(`Reordering: ${fromPos} → ${toPos}`)

      try {
        await this.scenesSerial(async () => {
          await this.ensureScenesMode()
          await this.settleDelay()
          await this.connection.sendRaw(`REORDER ${fromPos} ${toPos}\n`)
          const response = await this.readLine(3000)

          if (response === 'OK') {
            this.log('Reorder successful')
          } else {
            this.log('Reorder failed: ' + (response || 'timeout'), 'error')
          }
          await this.fetchScenesBody()
        })
      } catch (err) {
        this.log('Error: ' + err.message, 'error')
        await this.fetchScenes()
      }
    }

    // Refresh button handler
    async refresh() {
      if (!this.connection.isConnected) return

      if (!this.inScenesMode) {
        await this.activate()
      } else {
        await this.fetchScenes()
      }
    }

    // Add scene button handler
    addScene() {
      if (!this.connection.isConnected) return

      this.pendingRenamePosition = null  // null means add new
      this.nameDialogTarget.label = 'Add Scene'
      this.nameInputTarget.value = ''
      this.nameErrorTarget.textContent = ''
      this.nameDialogTarget.open = true

      // Focus input after dialog opens
      setTimeout(() => {
        this.nameInputTarget.focus()
      }, 100)
    }

    // Start inline rename
    startRename(e) {
      const position = parseInt(e.currentTarget.dataset.position, 10)
      const scene = this.scenes.find(s => s.position === position)
      if (!scene) return

      this.pendingRenamePosition = position
      this.nameDialogTarget.label = 'Rename Scene'
      this.nameInputTarget.value = scene.name
      this.nameErrorTarget.textContent = ''
      this.nameDialogTarget.open = true

      setTimeout(() => {
        this.nameInputTarget.focus()
        this.nameInputTarget.select()
      }, 100)
    }

    hideNameDialog() {
      this.nameDialogTarget.open = false
      this.pendingRenamePosition = null
      this.pendingDuplicatePosition = null
    }

    // Check if a scene name already exists
    nameExists(name, excludePosition = null) {
      return this.scenes.some(s =>
        s.name.toLowerCase() === name.toLowerCase() &&
        s.position !== excludePosition
      )
    }

    // Generate a suggested name for duplicating
    suggestDuplicateName(originalName) {
      // Try "Name copy", then "Name copy 2", etc.
      let suggestion = `${originalName} copy`.slice(0, 16)
      if (!this.nameExists(suggestion)) return suggestion

      for (let i = 2; i < 100; i++) {
        suggestion = `${originalName.slice(0, 12)} copy ${i}`
        if (!this.nameExists(suggestion)) return suggestion
      }
      return suggestion
    }

    // Real-time validation as user types
    validateName() {
      const name = this.nameInputTarget.value.trim()
      const excludePos = this.pendingRenamePosition

      // Clear error if empty (let confirmName handle empty validation)
      if (!name) {
        this.nameErrorTarget.textContent = ''
        return true
      }

      if (name.length > 16) {
        this.nameErrorTarget.textContent = 'Name must be 16 characters or less'
        return false
      }

      if (this.nameExists(name, excludePos)) {
        this.nameErrorTarget.textContent = 'A scene with this name already exists'
        return false
      }

      this.nameErrorTarget.textContent = ''
      return true
    }

    // Shake the input to indicate error
    shakeInput() {
      this.nameInputTarget.classList.add('shake')
      setTimeout(() => this.nameInputTarget.classList.remove('shake'), 400)
    }

    async confirmName() {
      const name = this.nameInputTarget.value.trim()

      // Validate length
      if (!name) {
        this.nameErrorTarget.textContent = 'Name is required'
        this.shakeInput()
        return
      }

      if (name.length > 16) {
        this.nameErrorTarget.textContent = 'Name must be 16 characters or less'
        this.shakeInput()
        return
      }

      // Validate uniqueness (exclude current position for renames)
      const excludePos = this.pendingRenamePosition
      if (this.nameExists(name, excludePos)) {
        this.nameErrorTarget.textContent = 'A scene with this name already exists'
        this.shakeInput()
        return
      }

      if (window.SceneEditorUi?.isReservedSceneName?.(name)) {
        this.nameErrorTarget.textContent =
          '"manifest" is reserved (would overwrite manifest.json)'
        this.shakeInput()
        return
      }

      // Save positions before hideNameDialog() clears them
      const renamePosition = this.pendingRenamePosition
      const duplicatePosition = this.pendingDuplicatePosition
      this.hideNameDialog()

      if (duplicatePosition !== null) {
        // Duplicate with the given name
        await this.duplicateScene(duplicatePosition, name)
      } else if (renamePosition !== null) {
        // Rename existing scene
        await this.renameScene(renamePosition, name)
      } else {
        // Add new scene
        await this.createScene(name)
      }
    }

    async createScene(name) {
      this.log(`Creating scene: ${name}`)

      try {
        await this.scenesSerial(async () => {
          await this.ensureScenesMode()
          await this.settleDelay()
          await this.connection.sendRaw(`CREATE ${name}\n`)
          const response = await this.readLine(3000)

          if (response === 'OK') {
            this.log('Scene created')
          } else {
            this.log('Create failed: ' + (response || 'timeout'), 'error')
          }
          await this.fetchScenesBody()
        })
      } catch (err) {
        this.log('Error: ' + err.message, 'error')
        await this.fetchScenes()
      }
    }

    async renameScene(position, name) {
      this.log(`Renaming scene at position ${position} to "${name}"`)

      try {
        await this.scenesSerial(async () => {
          await this.ensureScenesMode()
          await this.settleDelay()
          await this.connection.sendRaw(`RENAME ${position} ${name}\n`)
          const response = await this.readLine(3000)

          if (response === 'OK') {
            this.log('Scene renamed')
          } else {
            this.log('Rename failed: ' + (response || 'timeout'), 'error')
          }
          await this.fetchScenesBody()
        })
      } catch (err) {
        this.log('Error: ' + err.message, 'error')
        await this.fetchScenes()
      }
    }

    // Duplicate handler - shows name dialog
    duplicate(e) {
      const position = parseInt(e.currentTarget.dataset.position, 10)
      const scene = this.scenes.find(s => s.position === position)
      if (!scene) return

      this.pendingDuplicatePosition = position
      this.nameDialogTarget.label = 'Duplicate Scene'
      this.nameInputTarget.value = this.suggestDuplicateName(scene.name)
      this.nameErrorTarget.textContent = ''
      this.nameDialogTarget.open = true

      setTimeout(() => {
        this.nameInputTarget.focus()
        this.nameInputTarget.select()
      }, 100)
    }

    // Actually duplicate the scene with given name
    async duplicateScene(position, name) {
      this.log(`Duplicating scene at position ${position} as "${name}"`)

      try {
        await this.scenesSerial(async () => {
          await this.ensureScenesMode()
          await this.settleDelay()
          await this.connection.sendRaw(`DUPLICATE ${position} ${name}\n`)
          const response = await this.readLine(3000)

          if (response === 'OK') {
            this.log('Scene duplicated')
          } else {
            this.log('Duplicate failed: ' + (response || 'timeout'), 'error')
          }
          await this.fetchScenesBody()
        })
      } catch (err) {
        this.log('Error: ' + err.message, 'error')
        await this.fetchScenes()
      }
    }

    async switchToScene(e) {
      const position = parseInt(e.currentTarget.dataset.position, 10)

      this.log(`Activating (playing) scene at position ${position}`)

      try {
        await this.scenesSerial(async () => {
          await this.ensureScenesMode()
          await this.settleDelay()
          await this.connection.sendRaw(`GOTO ${position}\n`)
          const response = await this.readLine(3000)

          if (response === 'OK') {
            this.log('Scene activated (now playing)')
          } else {
            this.log('Activate failed: ' + (response || 'timeout'), 'error')
          }
          await this.fetchScenesBody()
        })
      } catch (err) {
        this.log('Error: ' + err.message, 'error')
        await this.fetchScenes()
      }
    }

    // Activate handler (named activateScene to avoid conflict with activate())
    async activateScene(e) {
      const position = parseInt(e.currentTarget.dataset.position, 10)

      this.log(`Enabling scene at position ${position}`)

      try {
        await this.scenesSerial(async () => {
          await this.ensureScenesMode()
          await this.settleDelay()
          await this.connection.sendRaw(`ACTIVATE ${position}\n`)
          const response = await this.readLine(3000)

          if (response === 'OK') {
            this.log('Scene enabled')
          } else {
            this.log('Enable failed: ' + (response || 'timeout'), 'error')
          }
          await this.fetchScenesBody()
        })
      } catch (err) {
        this.log('Error: ' + err.message, 'error')
        await this.fetchScenes()
      }
    }

    // Deactivate handler
    async deactivate(e) {
      const position = parseInt(e.currentTarget.dataset.position, 10)
      const scene = this.scenes.find(s => s.position === position)
      if (scene?.current) {
        this.log('Cannot deactivate the playing scene', 'error')
        return
      }

      this.log(`Disabling scene at position ${position}`)

      try {
        await this.scenesSerial(async () => {
          await this.ensureScenesMode()
          await this.settleDelay()
          await this.connection.sendRaw(`DEACTIVATE ${position}\n`)
          const response = await this.readLine(3000)

          if (response === 'OK') {
            this.log('Scene disabled')
          } else {
            this.log('Disable failed: ' + (response || 'timeout'), 'error')
          }
          await this.fetchScenesBody()
        })
      } catch (err) {
        this.log('Error: ' + err.message, 'error')
        await this.fetchScenes()
      }
    }

    // Show delete confirmation dialog
    showDeleteDialog(e) {
      const position = parseInt(e.currentTarget.dataset.position, 10)
      const scene = this.scenes.find(s => s.position === position)
      if (!scene) return

      this.pendingDeletePosition = position
      this.deleteSceneNameTarget.textContent = scene.name
      this.deleteDialogTarget.open = true
    }

    hideDeleteDialog() {
      this.deleteDialogTarget.open = false
      this.pendingDeletePosition = null
    }

    async confirmDelete() {
      if (this.pendingDeletePosition === null) return

      const position = this.pendingDeletePosition
      this.hideDeleteDialog()

      this.log(`Deleting scene at position ${position}`)

      try {
        await this.scenesSerial(async () => {
          await this.ensureScenesMode()
          await this.settleDelay()
          await this.connection.sendRaw(`DELETE ${position}\n`)
          const response = await this.readLine(3000)

          if (response === 'OK') {
            this.log('Scene deleted')
          } else {
            this.log('Delete failed: ' + (response || 'timeout'), 'error')
          }
          await this.fetchScenesBody()
        })
      } catch (err) {
        this.log('Error: ' + err.message, 'error')
        await this.fetchScenes()
      }
    }

    // Logging
    log(message, type = 'info') {
      if (!this.hasLogContentTarget) return

      const entry = document.createElement('div')
      entry.className = `log-entry log-${type}`
      const time = new Date().toLocaleTimeString()
      entry.textContent = `[${time}] ${message}`
      this.logContentTarget.appendChild(entry)
      this.logContentTarget.scrollTop = this.logContentTarget.scrollHeight
    }

    clearLog() {
      if (this.hasLogContentTarget) {
        this.logContentTarget.innerHTML = ''
      }
    }
  }
)
