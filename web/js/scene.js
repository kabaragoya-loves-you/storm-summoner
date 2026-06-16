/* Storm Summoner - Scene inspect + editor (Scenes tab right panel) */

application.register(
  'scene',
  class extends BaseController {
    static targets = [
      'inspectView', 'inspectText', 'editView', 'truncatedBanner',
      'copySource', 'copyBtn', 'editorToolbar',
      'editorContainer', 'editorTitle', 'sectionLayoutBtn', 'sectionLayoutIcon',
      'validationBox',
      'programmingBanner', 'saveBtn', 'revertBtn', 'saveStatus',
      'pedalPickerDialog', 'pedalVendorSelect', 'pedalSelect',
      'pedalChangeWarningDialog', 'reloadConfirmDialog', 'reloadConfirmMessage',
      'messageDialog', 'messageDialogBody'
    ]

    connect () {
      this.editing = false
      this._loadGeneration = 0
      this.notifyDebounce = null
      this.editModel = null
      this.inspectModel = null
      this._inspectText = ''
      this._inspectTruncated = false
      this.baselineJson = null
      this.dirty = false
      this.editPosition = null
      this.sceneList = []
      this.deviceProgramming = false
      this.deviceContext = {
        sceneMode: 2,
        deviceMode: 0,
        confirmChange: 0,
        midiControl: false,
        flagEnabled: false,
        globalPedal: null
      }
      this.pedalCatalog = null
      this._pedalCatalogLoad = null
      this.deviceDefinition = null
      this._openEditorSections = new Set()
      this._sectionLayoutPhase = 0
      this._pendingPedalSlug = null
      this._pendingPedalInherited = false
      this.validationErrors = []
      this._schema = null
      this._schemaLoad = null
      this._saveStatusTimer = null
      this._programmingSyncGen = 0
      this._programmingFetchPromise = null
      this._programmingPollTimer = null
      this._programmingExitDebounce = null

      this._onConnectionChanged = this.onConnectionChanged.bind(this)
      this._onTabActivated = this.onTabActivated.bind(this)
      this._onCdcNotify = this.onCdcNotify.bind(this)
      this._onModeChanged = this.onModeChanged.bind(this)
      this._onOpenScene = this.onOpenScene.bind(this)
      this._onSceneListUpdated = this.onSceneListUpdated.bind(this)
      this._onDownloadScene = this.onDownloadScene.bind(this)

      this.connection.on('connection:changed', this._onConnectionChanged)
      this.connection.on('mode:changed', this._onModeChanged)
      document.addEventListener('app:tab-activated', this._onTabActivated)
      document.addEventListener('cdc:notify', this._onCdcNotify)
      document.addEventListener('scenes:open-scene', this._onOpenScene)
      document.addEventListener('scenes:list-updated', this._onSceneListUpdated)
      document.addEventListener('scenes:download-scene', this._onDownloadScene)

      this._onEditorSectionToggle = (e) => {
        const el = e.target
        if (el.tagName !== 'DETAILS' || !el.classList.contains('scene-editor-section')) return
        const title = el.dataset.section
        if (!title) return
        if (el.open) this._openEditorSections.add(title)
        else this._openEditorSections.delete(title)
      }
      if (this.hasEditorContainerTarget) {
        this.editorContainerTarget.addEventListener('toggle', this._onEditorSectionToggle, true)
      }
    }

    disconnect () {
      this.connection.off('connection:changed', this._onConnectionChanged)
      this.connection.off('mode:changed', this._onModeChanged)
      document.removeEventListener('app:tab-activated', this._onTabActivated)
      document.removeEventListener('cdc:notify', this._onCdcNotify)
      document.removeEventListener('scenes:open-scene', this._onOpenScene)
      document.removeEventListener('scenes:list-updated', this._onSceneListUpdated)
      document.removeEventListener('scenes:download-scene', this._onDownloadScene)
      if (this.notifyDebounce) clearTimeout(this.notifyDebounce)
      if (this.printFrame) {
        this.printFrame.remove()
        this.printFrame = null
      }
      if (this._saveStatusTimer) {
        clearTimeout(this._saveStatusTimer)
        this._saveStatusTimer = null
      }
      if (this._programmingExitDebounce) {
        clearTimeout(this._programmingExitDebounce)
        this._programmingExitDebounce = null
      }
      this.stopProgrammingPoll()
      if (this.hasEditorContainerTarget && this._onEditorSectionToggle) {
        this.editorContainerTarget.removeEventListener('toggle', this._onEditorSectionToggle, true)
      }
    }

    onSceneListUpdated (e) {
      this.sceneList = e.detail?.scenes || []
      this.updatePanelTitle()
      if (this.editing && this.editModel) this.renderEditor()
    }

    onOpenScene (e) {
      const { position, mode, ready } = e.detail || {}
      if (position === undefined || position === null) {
        if (ready) ready()
        return
      }
      void (async () => {
        try {
          if (this.dirty && position !== this.editPosition) {
            const ok = await this.confirmDiscardChanges('Discard unsaved changes?')
            if (!ok) return
          }
          await this.openSceneAt(position, mode)
        } finally {
          if (ready) ready()
        }
      })()
    }

    async openSceneAt (position, mode) {
      this.editPosition = position
      if (mode === 'edit') {
        this.showEditMode()
      } else if (mode === 'print') {
        await this.openForPrint()
      } else {
        await this.showViewMode()
      }
    }

    applyInspectResponse (response) {
      if (!response || response.startsWith('ERROR:')) {
        if (!response) {
          console.error('SCENE_INSPECT: no serial data received (device may have sent a response)')
        }
        this.renderError(response || 'No response from device')
        return false
      }

      let data
      try {
        data = JSON.parse(response)
      } catch (err) {
        console.error('Scene inspect JSON parse error:', err, response.slice(0, 120))
        this.renderError('Invalid inspect response from device')
        return false
      }
      if (typeof data.text !== 'string') {
        this.renderError('Invalid inspect response from device')
        return false
      }
      if (data.midi_control_enabled != null) {
        this.deviceContext.midiControl = !!data.midi_control_enabled
      }
      let text = data.text || ''
      if (data.midi_control_enabled === false) {
        text = this.filterMidiControlInspectText(text)
      }
      this.renderInspect(text, !!data.truncated)
      return true
    }

    filterMidiControlInspectText (text) {
      if (this.deviceContext.midiControl) return text
      const paragraphs = text.split(/\n\n+/)
      const kept = paragraphs.filter(para => {
        const trimmed = para.trim()
        if (!trimmed) return false
        const firstLine = trimmed.split('\n')[0]
        return !/^Trigger \d+ \(CC \d+\)/.test(firstLine)
      })
      return kept.join('\n\n')
    }

    async showViewMode () {
      this.editing = false
      this.inspectViewTarget.classList.remove('hidden')
      this.editViewTarget.classList.add('hidden')
      if (this.hasEditorToolbarTarget) {
        this.editorToolbarTarget.classList.add('hidden')
      }
      this.updateProgrammingLock()
      if (this.connection.isConnected && this.editPosition !== null) {
        await this.fetchInspectForPosition(this.editPosition)
      }
    }

    showEditMode () {
      this.editing = true
      this.inspectViewTarget.classList.add('hidden')
      this.editViewTarget.classList.remove('hidden')
      if (this.hasEditorToolbarTarget) {
        this.editorToolbarTarget.classList.remove('hidden')
      }
      this.updateProgrammingLock()
      if (this.connection.isConnected) this.loadSceneForEdit()
    }

    async openForPrint () {
      this.editing = false
      this.inspectViewTarget.classList.remove('hidden')
      this.editViewTarget.classList.add('hidden')
      if (this.hasEditorToolbarTarget) {
        this.editorToolbarTarget.classList.add('hidden')
      }
      await this.fetchInspectForPosition(this.editPosition)
      this.print()
    }

    onConnectionChanged ({ connected }) {
      if (!connected) {
        this.editing = false
        this.dirty = false
        this.baselineJson = null
        this.editModel = null
        this.editPosition = null
        this.deviceProgramming = false
        this.stopProgrammingPoll()
        this.pedalCatalog = null
        this._pedalCatalogLoad = null
        this.deviceContext.globalPedal = null
        this.renderDisconnected()
        this.showViewMode()
      } else {
        this.fetchDeviceProgramming()
      }
    }

    onTabActivated (e) {
      if (e.detail.tab !== 'scenes') return
      if (!this.connection.isConnected) {
        this.renderDisconnected()
        return
      }
      if (this.editPosition !== null && this.editing) {
        this.loadSceneForEdit()
      }
    }

    onModeChanged ({ mode }) {
      // ASSETS/CONFIG modes don't run a background CDC reader, so EVT:programming
      // lines can be missed while those tabs hold the port. Re-sync on exit.
      if (mode === null && this.connection.isConnected &&
          !this.connection.isSerialBusy) {
        void this.fetchDeviceProgramming()
      }
    }

    onCdcNotify (e) {
      const kind = e.detail?.kind
      if (kind === 'programming') {
        const active = e.detail?.index === 1
        const wasProgramming = this.deviceProgramming
        this.setDeviceProgramming(active)
        if (wasProgramming && !active) this.onProgrammingModeEnded()
        return
      }
      if (kind !== 'scene_changed' && kind !== 'scene_updated') return
      if (!this.connection.isConnected) return
      if (this.deviceProgramming) return
      if (this.connection.isSerialBusy) return
      if (this.editing && this.dirty) return

      const activeTab = document.querySelector('wa-tab-group wa-tab[active]')
      if (activeTab?.getAttribute('panel') !== 'scenes') return
      if (this.editPosition === null) return

      if (this.notifyDebounce) clearTimeout(this.notifyDebounce)
      this.notifyDebounce = setTimeout(() => {
        this.notifyDebounce = null
        if (this.connection.isSerialBusy) return
        if (this.editing) this.loadSceneForEdit()
        else this.fetchInspectForPosition(this.editPosition)
      }, 100)
    }

    onProgrammingModeEnded () {
      if (this._programmingExitDebounce) clearTimeout(this._programmingExitDebounce)
      this._programmingExitDebounce = setTimeout(() => {
        this._programmingExitDebounce = null
        void this.refreshEditorFromDeviceAfterProgramming()
      }, 150)
    }

    async refreshEditorFromDeviceAfterProgramming () {
      if (!this.connection.isConnected || this.editPosition === null) return
      const activeTab = document.querySelector('wa-tab-group wa-tab[active]')
      if (activeTab?.getAttribute('panel') !== 'scenes') return
      if (this.connection.isSerialBusy) {
        this.onProgrammingModeEnded()
        return
      }
      if (this.editing && this.dirty) {
        const ok = await this.confirmDiscardChanges(
          'The device was edited in programming mode. Discard unsaved web changes and reload from the device?'
        )
        if (!ok) return
      }
      if (this.editing) await this.loadSceneForEdit()
      else await this.fetchInspectForPosition(this.editPosition)
    }

    setDeviceProgramming (active) {
      const next = !!active
      if (next === this.deviceProgramming) return false
      this._programmingSyncGen++
      this.deviceProgramming = next
      this.updateProgrammingLock()
      return true
    }

    async fetchDeviceProgramming () {
      if (!this.connection.isConnected) return
      if (this._programmingFetchPromise) return this._programmingFetchPromise
      this._programmingFetchPromise = this._fetchDeviceProgrammingImpl()
        .finally(() => { this._programmingFetchPromise = null })
      return this._programmingFetchPromise
    }

    async _fetchDeviceProgrammingImpl () {
      const syncGen = this._programmingSyncGen
      const wasProgramming = this.deviceProgramming
      try {
        const response = await this.connection.runSerialTask(async () => {
          if (this.connection.currentMode) {
            await this.connection._exitModeImpl()
            await this.sleep(300)
          }
          return this.connection._sendCommandImpl('INFO', 15000, (d) =>
            typeof d.programming === 'boolean')
        })
        if (syncGen !== this._programmingSyncGen) return
        if (!response || response.startsWith('ERROR:')) return
        const data = JSON.parse(response)
        const next = !!data.programming
        this.setDeviceProgramming(next)
        if (wasProgramming && !next) this.onProgrammingModeEnded()
      } catch (_) { /* ignore */ }
    }

    syncProgrammingPoll () {
      const shouldPoll = this.connection.isConnected && this.editing
      if (shouldPoll) {
        if (this._programmingPollTimer) return
        this._programmingPollTimer = setInterval(() => {
          if (!this.connection.isConnected || !this.editing) {
            this.stopProgrammingPoll()
            return
          }
          // Don't yank ASSETS/CONFIG (or other modes) just to poll INFO.
          if (this.connection.currentMode) return
          void this.fetchDeviceProgramming()
        }, 2000)
      } else {
        this.stopProgrammingPoll()
      }
    }

    stopProgrammingPoll () {
      if (!this._programmingPollTimer) return
      clearInterval(this._programmingPollTimer)
      this._programmingPollTimer = null
    }

    updateProgrammingLock () {
      this.syncProgrammingPoll()
      if (!this.hasProgrammingBannerTarget) return
      if (this.deviceProgramming && this.editing) {
        this.programmingBannerTarget.classList.remove('hidden')
      } else {
        this.programmingBannerTarget.classList.add('hidden')
      }
      const locked = this.deviceProgramming && this.editing
      if (this.hasEditorContainerTarget) {
        this.editorContainerTarget.classList.toggle('scene-editor-locked', locked)
      }
      if (this.hasSaveBtnTarget) this.saveBtnTarget.disabled = locked || !this.dirty
    }

    updatePanelTitle () {
      if (!this.hasEditorTitleTarget || this.editPosition === null) return
      const pos = Number(this.editPosition)
      const row = this.sceneList.find(s => s.position === pos)
      const modelName = this.editModel?.name
      const label = row
        ? `${row.name}${row.current ? ' (playing)' : ''}`
        : (modelName || 'Scene')
      this.editorTitleTarget.textContent = label
    }

    updateSectionLayoutButton () {
      if (!this.hasSectionLayoutBtnTarget || !this.hasSectionLayoutIconTarget) return
      const configs = [
        { icon: 'minus', library: 'system', label: 'Collapse all sections' },
        { icon: 'plus', library: 'system', label: 'Expand all sections' },
        { icon: 'filter', library: null, label: 'Reset section layout' }
      ]
      const cfg = configs[this._sectionLayoutPhase] || configs[0]
      this.sectionLayoutIconTarget.setAttribute('name', cfg.icon)
      if (cfg.library) {
        this.sectionLayoutIconTarget.setAttribute('library', cfg.library)
        this.sectionLayoutIconTarget.setAttribute('variant', 'solid')
      } else {
        this.sectionLayoutIconTarget.removeAttribute('library')
        this.sectionLayoutIconTarget.removeAttribute('variant')
      }
      this.sectionLayoutBtnTarget.setAttribute('title', cfg.label)
      this.sectionLayoutBtnTarget.setAttribute('aria-label', cfg.label)
    }

    cycleSectionLayout () {
      if (!this.editing || !this.editModel) return
      this._sectionLayoutPhase = (this._sectionLayoutPhase + 1) % 3
      let openSections
      if (this._sectionLayoutPhase === 0) {
        openSections = SceneEditorUi.sectionsWithContent(this)
      } else if (this._sectionLayoutPhase === 1) {
        openSections = new Set()
      } else {
        openSections = SceneEditorUi.allSectionTitles(this)
      }
      this._openEditorSections = openSections
      this.renderEditor(openSections)
      this.updateSectionLayoutButton()
    }

    onDownloadScene (e) {
      const position = e.detail?.position
      if (position === undefined || position === null) return
      void this.downloadSceneAtPosition(position)
    }

    saveJsonBlob (model, position) {
      const json = JSON.stringify(model, null, 2)
      const blob = new Blob([json], { type: 'application/json' })
      const url = URL.createObjectURL(blob)
      const safeName = String(model.name || 'scene')
        .replace(/[^\w.-]+/g, '_')
        .replace(/^_+|_+$/g, '')
        .slice(0, 40) || 'scene'
      const a = document.createElement('a')
      a.href = url
      a.download = `${safeName}-${position}.json`
      a.click()
      URL.revokeObjectURL(url)
    }

    async downloadSceneAtPosition (position) {
      if (!this.connection.isConnected) return
      const pos = Number(position)

      if (this.editing && this.editPosition === pos && this.editModel) {
        this.saveJsonBlob(this.editModel, pos)
        return
      }

      try {
        const result = await this.connection.runSerialTask(async () => {
          if (this.connection.currentMode) {
            await this.connection._exitModeImpl()
            await this.sleep(300)
          }
          await this.ensureDeviceIdleInTask()
          return this.connection._fetchSizedTransferImpl(`SCENE_GET ${pos}`, {
            lineTimeout: 30000,
            binaryTimeout: 120000
          })
        })
        if (!result?.data?.length) {
          this.showMessageDialog('Download failed', 'Failed to download scene')
          return
        }
        const model = JSON.parse(new TextDecoder().decode(result.data))
        this.saveJsonBlob(model, pos)
      } catch (err) {
        console.error('Scene download error:', err)
        this.showMessageDialog('Download failed', err.message || 'Download failed')
      }
    }

    seedConfirmPendingAction (actionPath) {
      const action = this.getAtPath(actionPath)
      if (!action || action.type !== 'confirm_pending') return
      ActionCatalog.clearRepeatFields(action)
      if (this.deviceContext.sceneMode === 2 && !action.confirm_target) {
        this.setAtPath(`${actionPath}.confirm_target`, 'preset')
      }
    }

    seedNoteAction (actionPath) {
      const action = this.getAtPath(actionPath)
      if (!action || action.type !== 'note') return
      if (action.note == null) action.note = ActionCatalog.NOTE_RANDOM
      if (action.velocity == null) action.velocity = 100
      if (action.voices == null) action.voices = 1
      if (action.bass == null) action.bass = false
      if (action.random_floor == null) action.random_floor = 36
      if (action.random_ceiling == null) action.random_ceiling = 96
      if (action.aftertouch == null) action.aftertouch = true
    }

    seedRandomizeAction (actionPath) {
      DeviceControls.seedRandomizeAction(this, actionPath)
    }

    seedPianoPedalAction (actionPath) {
      const action = this.getAtPath(actionPath)
      if (!action || action.type !== 'piano_pedal') return
      action.cc = ActionCatalog.resolvePianoPedalCc(action.cc)
    }

    seedTouchwheelAction (actionPath) {
      const action = this.getAtPath(actionPath)
      if (!action || action.type !== 'touchwheel') return
      const variant = action.variant || 'hold'
      const clampMode = (n) => {
        const x = Number(n)
        if (Number.isNaN(x) || x < 0 || x > 12) return 0
        return Math.round(x)
      }

      ActionCatalog.clearRepeatFields(action)
      delete action.timing
      delete action.timing_beat
      delete action.raise_flag

      if (variant === 'hold') {
        delete action.num_modes
        delete action.modes
        action.mode = clampMode(action.mode ?? 0)
        if (action.release_to_original) {
          delete action.mode2
        } else {
          delete action.release_to_original
          action.mode2 = clampMode(action.mode2 ?? 0)
        }
      } else if (variant === 'cycle') {
        delete action.mode
        delete action.mode2
        delete action.release_to_original
        const cur = Array.isArray(action.modes) ? action.modes : []
        const stepCount = Math.max(2, Math.min(8, Math.max(cur.length, action.num_modes || 2)))
        const steps = []
        for (let i = 0; i < stepCount; i++) steps.push(clampMode(cur[i]))
        action.modes = steps
        action.num_modes = steps.length
      }
    }

    seedLfoAction (actionPath) {
      const action = this.getAtPath(actionPath)
      if (!action || action.type !== 'lfo') return
      const v = action.variant || 'modify'
      if (action.slot == null) action.slot = 1
      if (v === 'modify') ActionCatalog.seedLfoModifyFields(action)
      else ActionCatalog.clearLfoModifyFields(action)
      if (v === 'start' || v === 'stop') ActionCatalog.clearRepeatFields(action)
    }

    seedConsolidatedAction (actionPath) {
      const action = this.getAtPath(actionPath)
      if (!action?.type) return
      const device = this.deviceDefinition
      const t = action.type
      const v = action.variant || ActionCatalog.defaultVariant(t)
      const firstCc = () => DeviceControls.firstParameterCc(device)

      if (t === 'clock') {
        if (v === 'burst') {
          if (action.speed_percent == null) action.speed_percent = 100
        } else if (action.start_enabled == null) {
          action.start_enabled = false
        }
      } else if (t === 'cut') {
        if (!action.cut_mode) action.cut_mode = 'both'
      } else if (t === 'ui') {
        if (v === 'set') {
          if (action.module == null) action.module = 0
        } else if (v === 'hold') {
          if (action.module == null) action.module = 0
          if (action.module2 == null) action.module2 = 0
        } else if (v === 'cycle') {
          const n = ActionCatalog.uiStepCount(action)
          const steps = Array.isArray(action.modules) ? action.modules.slice() : []
          while (steps.length < n) steps.push(0)
          action.modules = steps.slice(0, n)
          action.num_modules = n
        }
      } else if (t === 'param') {
        const cc = firstCc()
        if (action.target == null) action.target = 'touchwheel'
        if (v === 'hold') {
          if (action.param == null) action.param = cc
          if (action.release_to_original ||
              (action.param2 === undefined && action.release_to_original !== false)) {
            action.release_to_original = true
            delete action.param2
          } else if (action.param2 == null) {
            action.param2 = cc
          }
        } else if (v === 'cycle') {
          ActionCatalog.clearRepeatFields(action)
          delete action.timing
          delete action.timing_beat
          const n = ActionCatalog.paramStepCount(action)
          const steps = Array.isArray(action.params) ? action.params.slice() : []
          while (steps.length < n) steps.push(cc)
          action.params = steps.slice(0, n)
          action.num_params = n
        }
      } else if (t === 'rtg' || t === 'sample_hold') {
        ActionCatalog.normalizeEngineAction(action)
      } else if (t === 'punch_in') {
        const cc = firstCc()
        if (action.start_cc == null) action.start_cc = cc
        if (action.start_value == null) action.start_value = 127
        if (action.finish_cc == null) action.finish_cc = cc
        if (action.finish_value == null) action.finish_value = 0
        if (!action.duration) action.duration = '1_bar'
      } else if (t === 'flag_ceremony') {
        const cc = firstCc()
        if (action.flag_up_cc == null) action.flag_up_cc = cc
        if (action.flag_up_value == null) action.flag_up_value = 127
        if (action.flag_down_cc == null) action.flag_down_cc = cc
        if (action.flag_down_value == null) action.flag_down_value = 0
      } else if (t === 'boomerang') {
        if (!action.output_type) action.output_type = 'cc'
        if (!action.target_mode) action.target_mode = 'explicit'
        if (!action.start_mode) action.start_mode = 'current'
        if (action.attack_mode == null) action.attack_mode = 'instant'
        if (action.sustain_mode == null) action.sustain_mode = 'instant'
        if (action.release_mode == null) action.release_mode = 'instant'
      }
    }

    seedTempoAction (actionPath) {
      const action = this.getAtPath(actionPath)
      if (!action || action.type !== 'tempo') return
      const variant = action.variant || 'set'
      const clampBpm = (n) => {
        const x = Number(n)
        if (Number.isNaN(x) || x < 20 || x > 300) return 120
        return Math.round(x)
      }

      if (variant === 'set') {
        delete action.press_bpm
        delete action.release_bpm
        delete action.num_tempos
        delete action.tempos
        if (action.bpm == null) action.bpm = 120
      } else if (variant === 'hold') {
        delete action.bpm
        delete action.num_tempos
        delete action.tempos
        action.press_bpm = clampBpm(action.press_bpm)
        action.release_bpm = clampBpm(action.release_bpm)
      } else if (variant === 'cycle') {
        delete action.bpm
        delete action.press_bpm
        delete action.release_bpm
        const cur = Array.isArray(action.tempos) ? action.tempos : []
        const stepCount = Math.max(2, Math.min(8, Math.max(cur.length, action.num_tempos || 2)))
        const steps = []
        for (let i = 0; i < stepCount; i++) steps.push(clampBpm(cur[i]))
        action.tempos = steps
        action.num_tempos = steps.length
      } else {
        delete action.press_bpm
        delete action.release_bpm
        delete action.num_tempos
        delete action.tempos
      }
    }

    normalizeCvGateModel (model) {
      if (!model) return false
      let changed = false
      if (model.cv_input_mode === 'note') {
        const cvVel = model.cv_velocity_mode ||
          (model.touchwheel_mode === 'velocity' ? 'touchwheel' : 'fixed')
        if ((!model.cv_velocity_mode || model.cv_velocity_mode === 'fixed') &&
            model.touchwheel_mode === 'velocity') {
          model.cv_velocity_mode = 'touchwheel'
          changed = true
        }
        if (model.touchwheel_mode === 'velocity' && cvVel !== 'touchwheel') {
          const restored = model.touchwheel_mode_prev || 'pads'
          if (model.touchwheel_mode !== restored) {
            model.touchwheel_mode = restored
            changed = true
          }
        } else if (cvVel === 'touchwheel' && model.touchwheel_mode !== 'velocity' &&
            !model.touchwheel_mode_prev) {
          model.touchwheel_mode_prev = model.touchwheel_mode || 'pads'
          changed = true
        }
      }
      return changed
    }

    normalizeBeforeSave (model) {
      if (typeof model.name === 'string') model.name = model.name.trim()
      this.normalizeCvGateModel(model)
      if (model.touchpads) {
        model.touchpads.forEach(tp => {
          this.normalizeTouchpadMapping(tp)
        })
      }
      DeviceControls.normalizeControlActionsInModel(this.deviceDefinition, model)
      DeviceControls.normalizePresetActionsInModel(this.deviceDefinition, model)
      DeviceControls.normalizeRandomizeActionsInModel(this.deviceDefinition, model)
      ActionCatalog.normalizeTempoActionsInModel(model)
      ActionCatalog.normalizeTouchwheelActionsInModel(model)
      ActionCatalog.normalizeLfoActionsInModel(model)
      ActionCatalog.normalizeSimpleActionsInModel(model)
      ActionCatalog.normalizeRepeatActionsInModel(model)
      if (model.device_id === '' || model.device_id == null) delete model.device_id
    }

    normalizeTouchpadMapping (tp) {
      if (!tp) return
      const action = tp.action
      const legacy = tp.actions?.[0]
      const hasAction = action?.type && action.type !== 'none'
      const hasLegacy = legacy?.type && legacy.type !== 'none'
      if (hasAction) {
        tp.action = action
      } else if (hasLegacy) {
        tp.action = legacy
      } else if (action) {
        tp.action = action
      } else if (legacy) {
        tp.action = legacy
      } else {
        tp.action = { type: 'none' }
      }
      delete tp.actions
    }

    async fetchSceneListInTask () {
      try {
        await this.connection.sendRaw('SCENES\n')
        const started = await this.connection.readLine(5000)
        if (started !== 'SCENES_STARTED') return false
        await this.connection.sendRaw('LIST\n')
        const response = await this.connection.readLine(5000)
        await this.connection.sendRaw('EXIT\n')
        await this.sleep(150)
        if (!response || !response.includes('[')) return false
        const jsonStart = response.indexOf('[')
        const jsonEnd = response.lastIndexOf(']')
        if (jsonStart === -1 || jsonEnd === -1) return false
        this.sceneList = JSON.parse(response.substring(jsonStart, jsonEnd + 1)).map(s => ({
          ...s,
          name: String(s?.name ?? '').trim() || 'Untitled'
        }))
        return true
      } catch (err) {
        console.warn('Scene editor: scene list skipped:', err.message)
        return false
      }
    }

    async fetchGlobalPedalInTask () {
      const response = await this.connection._sendCommandImpl('INFO', 15000, (data) =>
        typeof data.version === 'string')
      if (!response || response.startsWith('ERROR:')) return
      const info = JSON.parse(response)
      if (typeof info.programming === 'boolean') {
        this.setDeviceProgramming(info.programming)
      }
      if (info.pedal) {
        this.deviceContext.globalPedal = {
          slug: info.pedal.slug || '',
          name: info.pedal.name || 'Unknown',
          vendor: info.pedal.vendor || '',
          midi_channel: Number(info.pedal.midi_channel) || 1,
          trs_type: info.pedal.trs_type || 'TYPE_A'
        }
      }
    }

    captureOpenEditorSections () {
      const open = new Set(this._openEditorSections)
      if (!this.hasEditorContainerTarget) return open
      this.editorContainerTarget.querySelectorAll('details.scene-editor-section').forEach(el => {
        const title = el.dataset.section ||
          el.querySelector('summary')?.textContent?.trim()
        if (!title) return
        if (el.open) open.add(title)
        else open.delete(title)
      })
      this._openEditorSections = open
      return open
    }

    async loadDeviceDefinitionInTask () {
      const slug = this.editModel?.device_id || this.deviceContext.globalPedal?.slug || ''
      if (!slug) {
        this.deviceDefinition = null
        return
      }
      try {
        await PedalCatalog.ensureAssetsModeBody(this.connection)
        if (!this.pedalCatalog) {
          this.pedalCatalog = await PedalCatalog.fetchManifestsInAssets(this.connection)
        }
        this.deviceDefinition = await PedalCatalog.fetchDeviceJson(
          this.connection, this.pedalCatalog, slug)
      } catch (err) {
        console.warn('Scene editor: device definition skipped:', err.message)
        this.deviceDefinition = null
      } finally {
        await this.connection.sendRaw('EXIT\n')
        await this.sleep(200)
        await this.connection.drainInput?.()
      }
    }

    async ensurePedalCatalog () {
      if (this.pedalCatalog) return this.pedalCatalog
      if (this._pedalCatalogLoad) return this._pedalCatalogLoad
      this._pedalCatalogLoad = PedalCatalog.fetchCatalog(this.connection)
        .then(catalog => {
          this.pedalCatalog = catalog
          this._pedalCatalogLoad = null
          return catalog
        })
        .catch(err => {
          this._pedalCatalogLoad = null
          throw err
        })
      return this._pedalCatalogLoad
    }

    async openPedalPicker () {
      if (this.deviceProgramming) return
      try {
        await this.ensurePedalCatalog()
      } catch (err) {
        console.error('Pedal catalog load failed:', err)
        this.showMessageDialog('Pedal list', err.message || 'Failed to load pedal list')
        return
      }
      this.populatePedalPickerSelects()
      if (this.hasPedalPickerDialogTarget) this.pedalPickerDialogTarget.open = true
    }

    closePedalPicker () {
      if (this.hasPedalPickerDialogTarget) this.pedalPickerDialogTarget.open = false
    }

    showMessageDialog (title, message) {
      if (this.hasMessageDialogTarget) {
        this.messageDialogTarget.label = title || 'Error'
        this.messageDialogTarget.open = true
      }
      if (this.hasMessageDialogBodyTarget) {
        this.messageDialogBodyTarget.textContent = message || ''
      }
    }

    hideMessageDialog () {
      if (this.hasMessageDialogTarget) this.messageDialogTarget.open = false
    }

    closePedalWarning () {
      if (this.hasPedalChangeWarningDialogTarget) {
        this.pedalChangeWarningDialogTarget.open = false
      }
      this._pendingPedalSlug = null
      this._pendingPedalInherited = false
    }

    confirmDiscardChanges (message) {
      if (!this.hasReloadConfirmDialogTarget) {
        console.warn('Discard confirm dialog unavailable:', message)
        return Promise.resolve(false)
      }
      if (this._discardConfirmResolver) this.resolveDiscardConfirm(false)
      return new Promise(resolve => {
        this._discardConfirmResolver = resolve
        this.reloadConfirmMessageTarget.textContent = message
        this.reloadConfirmDialogTarget.open = true
      })
    }

    resolveDiscardConfirm (accepted) {
      const resolve = this._discardConfirmResolver
      this._discardConfirmResolver = null
      if (this.hasReloadConfirmDialogTarget) {
        this.reloadConfirmDialogTarget.open = false
      }
      resolve?.(accepted)
    }

    cancelReloadConfirm () {
      this.resolveDiscardConfirm(false)
    }

    confirmReloadDiscard () {
      this.resolveDiscardConfirm(true)
    }

    onReloadConfirmDismiss () {
      if (this._discardConfirmResolver) this.resolveDiscardConfirm(false)
    }

    populatePedalPickerSelects () {
      if (!this.hasPedalVendorSelectTarget) return
      const global = this.deviceContext.globalPedal
      const globalName = global?.name || 'Default'
      const catalog = this.pedalCatalog
      const deviceId = this.editModel?.device_id || ''

      let vendorHtml = `<option value="__inherited__">Inherited — ${this.escapeHtml(globalName)}</option>`
      vendorHtml += '<option value="__user__">User Devices</option>'
      for (const v of (catalog?.vendorTree || [])) {
        vendorHtml += `<option value="${this.escapeHtml(v.name)}">${this.escapeHtml(v.displayName)}</option>`
      }
      this.pedalVendorSelectTarget.innerHTML = vendorHtml

      let vendorKey = '__inherited__'
      if (deviceId) {
        vendorKey = PedalCatalog.findVendorForSlug(catalog, deviceId) || '__user__'
      }
      this.pedalVendorSelectTarget.value = vendorKey
      this.rebuildPedalPickerPedalSelect(vendorKey, deviceId)
    }

    onPedalVendorChange () {
      if (!this.hasPedalVendorSelectTarget) return
      this.rebuildPedalPickerPedalSelect(this.pedalVendorSelectTarget.value, '')
    }

    rebuildPedalPickerPedalSelect (vendorKey, selectedSlug) {
      if (!this.hasPedalSelectTarget) return
      const inherited = vendorKey === '__inherited__'
      this.pedalSelectTarget.classList.toggle('hidden', inherited)
      this.pedalSelectTarget.disabled = inherited
      if (inherited) {
        this.pedalSelectTarget.innerHTML = ''
        return
      }

      let devices = []
      if (vendorKey === '__user__') {
        devices = this.pedalCatalog?.userDevices || []
      } else {
        const vendor = this.pedalCatalog?.vendorTree?.find(v => v.name === vendorKey)
        devices = vendor?.devices || []
      }

      let html = ''
      for (const d of devices) {
        const name = PedalCatalog.getDeviceDisplayName(d)
        const sel = d.slug === selectedSlug ? ' selected' : ''
        html += `<option value="${this.escapeHtml(d.slug)}"${sel}>${this.escapeHtml(name)}</option>`
      }
      this.pedalSelectTarget.innerHTML = html
      if (devices.length && !devices.some(d => d.slug === selectedSlug)) {
        this.pedalSelectTarget.selectedIndex = 0
      }
    }

    confirmPedalPicker () {
      if (!this.hasPedalVendorSelectTarget) return
      const vendorKey = this.pedalVendorSelectTarget.value
      const currentId = this.editModel?.device_id || ''

      if (vendorKey === '__inherited__') {
        if (currentId) {
          this._pendingPedalSlug = ''
          this._pendingPedalInherited = true
          this.closePedalPicker()
          if (this.hasPedalChangeWarningDialogTarget) {
            this.pedalChangeWarningDialogTarget.open = true
          } else {
            this.applyPedalChange()
          }
        } else {
          this.closePedalPicker()
        }
        return
      }

      const slug = this.hasPedalSelectTarget ? this.pedalSelectTarget.value : ''
      if (!slug) {
        this.showMessageDialog('Select a pedal', 'Choose a pedal from the list.')
        return
      }
      if (slug === currentId) {
        this.closePedalPicker()
        return
      }

      this._pendingPedalSlug = slug
      this._pendingPedalInherited = false
      this.closePedalPicker()
      if (this.hasPedalChangeWarningDialogTarget) {
        this.pedalChangeWarningDialogTarget.open = true
      } else {
        this.applyPedalChange()
      }
    }

    async applyPedalChange () {
      if (!this.editModel) return
      if (this._pendingPedalInherited || this._pendingPedalSlug === '') {
        delete this.editModel.device_id
      } else if (this._pendingPedalSlug) {
        this.editModel.device_id = this._pendingPedalSlug
      }
      this._pendingPedalSlug = null
      this._pendingPedalInherited = false
      this.closePedalWarning()
      try {
        await this.connection.runSerialTask(() => this.loadDeviceDefinitionInTask())
      } catch (err) {
        console.warn('Scene editor: device reload skipped:', err.message)
      }
      DeviceControls.normalizeControlActionsInModel(this.deviceDefinition, this.editModel)
      DeviceControls.normalizePresetActionsInModel(this.deviceDefinition, this.editModel)
      this.markDirty()
      this.renderEditor()
    }

    getAtPath (path) {
      if (!this.editModel || !path) return undefined
      const parts = path.split('.')
      let cur = this.editModel
      for (const p of parts) {
        if (cur == null) return undefined
        const idx = Number(p)
        cur = Number.isInteger(idx) && String(idx) === p ? cur[idx] : cur[p]
      }
      return cur
    }

    setAtPath (path, value) {
      const parts = path.split('.')
      let cur = this.editModel
      for (let i = 0; i < parts.length - 1; i++) {
        const p = parts[i]
        const idx = Number(p)
        const key = Number.isInteger(idx) && String(idx) === p ? idx : p
        if (cur[key] == null) {
          const next = parts[i + 1]
          const nextIdx = Number(next)
          cur[key] = Number.isInteger(nextIdx) && String(nextIdx) === next ? [] : {}
        }
        cur = cur[key]
      }
      const last = parts[parts.length - 1]
      const lastIdx = Number(last)
      cur[Number.isInteger(lastIdx) && String(lastIdx) === last ? lastIdx : last] = value
    }

    getEffectiveMidiChannel () {
      const global = this.deviceContext.globalPedal?.midi_channel || 1
      if (this.deviceContext.deviceMode !== 1) return global
      const sceneMidi = this.editModel?.midi_channel ?? 0
      return sceneMidi > 0 ? sceneMidi : global
    }

    flashSaveStatus (message = 'Changes saved') {
      if (!this.hasSaveStatusTarget) return
      if (this._saveStatusTimer) clearTimeout(this._saveStatusTimer)
      this.saveStatusTarget.textContent = message
      this.saveStatusTarget.classList.remove('hidden')
      this._saveStatusTimer = setTimeout(() => {
        this.saveStatusTarget.classList.add('hidden')
        this.saveStatusTarget.textContent = ''
        this._saveStatusTimer = null
      }, 2500)
    }

    markDirty () {
      this.dirty = JSON.stringify(this.editModel) !== this.baselineJson
      if (this.hasSaveBtnTarget) {
        this.saveBtnTarget.disabled = this.deviceProgramming || !this.dirty
      }
      if (this.hasRevertBtnTarget) this.revertBtnTarget.disabled = !this.dirty
    }

    isNumericScenePath (path) {
      if (path === 'midi_channel' || path === 'trs_type' || path === 'note_channel') return true
      if (/touchwheel_(tempo_nudge_(pct|return|direction)|aftertouch_return)$/.test(path)) return true
      if (/tempo_nudge_(pct|direction)$/.test(path)) return true
      if (/^audio_config\.(sensitivity|attack_ms|release_ms|threshold)$/.test(path)) return true
      if (path === 'cv_trigger_threshold' || path === 'cv_trigger_debounce_ms') return true
      if (/\.values(\.\d+)+$/.test(path)) return true
      if (/\.presets(\.\d+)+$/.test(path)) return true
      return /\.(note|base_note|note_range|velocity|mode|mode2|num_modes|modes|slot|waveform|rate_mode|rate_hz_x100|sync_mult_x1000|division|polarity|floor|ceiling|resolution_mode|manual_steps|module|module2|num_modules|modules|param|param2|num_params|params|speed_percent|start_cc|start_value|finish_cc|finish_value|flag_up_cc|flag_up_value|flag_down_cc|flag_down_value|cc_number|target_value|attack_time_ms|sustain_time_ms|release_time_ms|attack_curve|release_curve|attack_curve_slope|release_curve_slope|random_floor|random_ceiling|voices|cc|value|value2|number|press_preset|release_preset|probability|pattern_length|release_threshold_ms|morph_manual_steps|glide)(\.\d+)?$/.test(path)
    }

    patchSelect (e) {
      if (this.deviceProgramming) return
      const path = e.target.dataset.scenePath
      if (!path) return
      let val = e.target.value

      if (path === '__touchwheel_user_mode') {
        const specByKey = {
          disabled: { enabled: false },
          pads: { touchwheel_mode: 'pads', enabled: false },
          control_change: {
            touchwheel_mode: 'continuous',
            output_type: 'cc',
            touchwheel_style: 'endless',
            enabled: true,
            touchwheel: { enabled: true, output_type: 'cc', cc_numbers: [0], num_cc_numbers: 0 }
          },
          program_change: { touchwheel_mode: 'program_change', enabled: true },
          tempo: {
            touchwheel_mode: 'set_tempo',
            touchwheel_style: 'endless',
            enabled: true,
            touchwheel_tempo_floor: 20,
            touchwheel_tempo_ceiling: 300
          },
          pitch_bend: { touchwheel_mode: 'pitch_bend', enabled: true },
          aftertouch: {
            touchwheel_mode: 'aftertouch',
            touchwheel_style: 'odometer',
            enabled: true,
            touchwheel_aftertouch_return: 1
          },
          notes: { touchwheel_mode: 'continuous', output_type: 'note', touchwheel_style: 'odometer', enabled: true },
          double_cc: { touchwheel_mode: 'double_cc', touchwheel_style: 'endless', enabled: true },
          velocity: { touchwheel_mode: 'velocity', enabled: true },
          lfo_rate: { touchwheel_mode: 'lfo_rate', touchwheel_style: 'odometer', enabled: true },
          lfo_depth: { touchwheel_mode: 'lfo_depth', touchwheel_style: 'odometer', enabled: true },
          rtg_rate: { touchwheel_mode: 'rtg_rate', touchwheel_style: 'odometer', enabled: true },
          tempo_nudge: {
            touchwheel_mode: 'continuous',
            output_type: 'tempo_nudge',
            touchwheel_style: 'bipolar',
            enabled: true,
            touchwheel_tempo_nudge_return: 0,
            touchwheel_tempo_nudge_direction: 0
          }
        }
        const spec = specByKey[val]
        if (!spec) return
        if (!this.editModel.touchwheel) this.editModel.touchwheel = {}
        if (spec.touchwheel) Object.assign(this.editModel.touchwheel, spec.touchwheel)
        if (spec.touchwheel_mode) this.editModel.touchwheel_mode = spec.touchwheel_mode
        if (spec.output_type) this.editModel.touchwheel.output_type = spec.output_type
        if (spec.touchwheel_tempo_floor != null) {
          this.editModel.touchwheel_tempo_floor = spec.touchwheel_tempo_floor
        }
        if (spec.touchwheel_tempo_ceiling != null) {
          this.editModel.touchwheel_tempo_ceiling = spec.touchwheel_tempo_ceiling
        }
        if (spec.touchwheel_tempo_nudge_return != null) {
          this.editModel.touchwheel_tempo_nudge_return = spec.touchwheel_tempo_nudge_return
        }
        if (spec.touchwheel_aftertouch_return != null) {
          this.editModel.touchwheel_aftertouch_return = spec.touchwheel_aftertouch_return
        }
        if (spec.touchwheel_style) this.editModel.touchwheel_style = spec.touchwheel_style
        if (spec.touchwheel_tempo_nudge_direction != null) {
          this.editModel.touchwheel_tempo_nudge_direction = spec.touchwheel_tempo_nudge_direction
        }
        if (spec.enabled === false) {
          this.editModel.touchwheel.enabled = false
        } else if (spec.enabled) {
          this.editModel.touchwheel.enabled = true
        }
        if (val === 'lfo_rate' || val === 'lfo_depth') {
          if (!this.editModel.touchwheel_lfo_target) this.editModel.touchwheel_lfo_target = 'both'
        }
        this.markDirty()
        this.renderEditor()
        return
      }

      if (path === 'touchwheel_tempo_nudge_direction') {
        const dir = Number(val)
        this.editModel.touchwheel_tempo_nudge_direction = dir
        if (dir === 0) this.editModel.touchwheel_style = 'bipolar'
        else if (this.editModel.touchwheel_style === 'bipolar') {
          this.editModel.touchwheel_style = 'odometer'
        }
        this.markDirty()
        this.renderEditor()
        return
      }

      if (path === '__expression_user_mode') {
        const specByKey = {
          disabled: { expression_mode: 'none', enabled: false },
          control_change: { expression_mode: 'expression', output_type: 'cc', enabled: true },
          sustain: { expression_mode: 'sustain', enabled: false },
          sostenuto: { expression_mode: 'sostenuto', enabled: false },
          switch: { expression_mode: 'switch', enabled: false },
          lfo_rate: { expression_mode: 'expression', output_type: 'lfo_rate', enabled: true },
          lfo_depth: { expression_mode: 'expression', output_type: 'lfo_depth', enabled: true },
          notes: { expression_mode: 'expression', output_type: 'note', enabled: true },
          tempo_nudge: { expression_mode: 'expression', output_type: 'tempo_nudge', enabled: true }
        }
        const spec = specByKey[val]
        if (!spec) return
        if (!this.editModel.expression) this.editModel.expression = { enabled: true, output_type: 'cc' }
        this.editModel.expression_mode = spec.expression_mode
        if (spec.output_type) this.editModel.expression.output_type = spec.output_type
        this.editModel.expression.enabled = spec.enabled
        if ((val === 'lfo_rate' || val === 'lfo_depth') && !this.editModel.expression.lfo_target) {
          this.editModel.expression.lfo_target = 'both'
        }
        this.markDirty()
        this.renderEditor()
        return
      }

      if (path === '__cv_user_mode') {
        const specByKey = {
          disabled: { cv_input_mode: 'none', enabled: false },
          control_change: { cv_input_mode: 'cv', output_type: 'cc', enabled: true },
          lfo_rate: { cv_input_mode: 'cv', output_type: 'lfo_rate', enabled: true },
          lfo_depth: { cv_input_mode: 'cv', output_type: 'lfo_depth', enabled: true },
          notes: { cv_input_mode: 'cv', output_type: 'note', enabled: true },
          tempo_nudge: { cv_input_mode: 'cv', output_type: 'tempo_nudge', enabled: true },
          cv_gate: { cv_input_mode: 'note', enabled: false },
          audio: { cv_input_mode: 'audio', enabled: false },
          trigger: { cv_input_mode: 'trigger', enabled: false }
        }
        const spec = specByKey[val]
        if (!spec) return
        if (!this.editModel.cv) this.editModel.cv = { enabled: true, output_type: 'cc' }
        this.editModel.cv_input_mode = spec.cv_input_mode
        if (spec.output_type) this.editModel.cv.output_type = spec.output_type
        this.editModel.cv.enabled = spec.enabled
        if ((val === 'lfo_rate' || val === 'lfo_depth') && !this.editModel.cv.lfo_target) {
          this.editModel.cv.lfo_target = 'both'
        }
        if (val === 'audio' && !this.editModel.audio_config) {
          this.editModel.audio_config = {
            range: 'bi5v',
            sensitivity: 128,
            attack_ms: 10,
            release_ms: 200,
            threshold: 5,
            polarity: 'attract'
          }
        }
        this.markDirty()
        this.renderEditor()
        return
      }

      if (path === '__proximity_user_mode') {
        const specByKey = {
          disabled: { enabled: false },
          control_change: { output_type: 'cc', enabled: true },
          notes_theremin: { output_type: 'note', enabled: true },
          lfo_rate: { output_type: 'lfo_rate', enabled: true },
          lfo_depth: { output_type: 'lfo_depth', enabled: true },
          tempo_nudge: { output_type: 'tempo_nudge', enabled: true }
        }
        const spec = specByKey[val]
        if (!spec) return
        if (!this.editModel.proximity) {
          this.editModel.proximity = { enabled: false, output_type: 'cc' }
        }
        if (spec.output_type) this.editModel.proximity.output_type = spec.output_type
        this.editModel.proximity.enabled = spec.enabled
        if ((val === 'lfo_rate' || val === 'lfo_depth') && !this.editModel.proximity.lfo_target) {
          this.editModel.proximity.lfo_target = 'both'
        }
        this.markDirty()
        this.renderEditor()
        return
      }

      if (path === '__als_user_mode') {
        const specByKey = {
          disabled: { enabled: false },
          control_change: { output_type: 'cc', enabled: true },
          notes: { output_type: 'note', enabled: true },
          lfo_rate: { output_type: 'lfo_rate', enabled: true },
          lfo_depth: { output_type: 'lfo_depth', enabled: true },
          tempo_nudge: { output_type: 'tempo_nudge', enabled: true }
        }
        const spec = specByKey[val]
        if (!spec) return
        if (!this.editModel.als) this.editModel.als = { enabled: false, output_type: 'cc' }
        if (spec.output_type) this.editModel.als.output_type = spec.output_type
        this.editModel.als.enabled = spec.enabled
        if ((val === 'lfo_rate' || val === 'lfo_depth') && !this.editModel.als.lfo_target) {
          this.editModel.als.lfo_target = 'both'
        }
        this.markDirty()
        this.renderEditor()
        return
      }

      if (path === '__lfo1_user_mode' || path === '__lfo2_user_mode') {
        const n = path === '__lfo1_user_mode' ? 1 : 2
        const cfgKey = n === 1 ? 'lfo1_config' : 'lfo2_config'
        const mapKey = n === 1 ? 'lfo1' : 'lfo2'
        const specByKey = {
          disabled: { enabled: false },
          control_change: { output_type: 'cc', enabled: true },
          notes: { output_type: 'note', enabled: true },
          lfo2_rate: { output_type: 'lfo2_rate', enabled: true },
          lfo2_depth: { output_type: 'lfo2_depth', enabled: true },
          lfo1_rate: { output_type: 'lfo1_rate', enabled: true },
          lfo1_depth: { output_type: 'lfo1_depth', enabled: true },
          rtg_rate: { output_type: 'rtg_rate', enabled: true },
          sh_rate: { output_type: 'sh_rate', enabled: true },
          pitch_bend: { output_type: 'pitch_bend', enabled: true }
        }
        const spec = specByKey[val]
        if (!spec) return
        if (!this.editModel[cfgKey]) {
          this.editModel[cfgKey] = { enabled: false, waveform: 'sine', rate_mode: 'free' }
        }
        if (!this.editModel[mapKey]) {
          this.editModel[mapKey] = { enabled: false, output_type: 'cc' }
        }
        this.editModel[cfgKey].enabled = spec.enabled
        this.editModel[mapKey].enabled = spec.enabled
        if (spec.output_type) this.editModel[mapKey].output_type = spec.output_type
        this.markDirty()
        this.renderEditor()
        return
      }

      if (path === '__note_track_user_mode') {
        const specByKey = {
          disabled: { enabled: false },
          control_change: { output_type: 'cc', enabled: true },
          lfo_rate: { output_type: 'lfo_rate', enabled: true },
          lfo_depth: { output_type: 'lfo_depth', enabled: true },
          pitch_bend: { output_type: 'pitch_bend', enabled: true },
          tempo_nudge: { output_type: 'tempo_nudge', enabled: true }
        }
        const spec = specByKey[val]
        if (!spec) return
        if (!this.editModel.note_track) {
          this.editModel.note_track = { enabled: false, output_type: 'cc' }
        }
        this.editModel.note_track.enabled = spec.enabled
        if (spec.output_type) this.editModel.note_track.output_type = spec.output_type
        if ((val === 'lfo_rate' || val === 'lfo_depth') && !this.editModel.note_track.lfo_target) {
          this.editModel.note_track.lfo_target = 'both'
        }
        this.markDirty()
        this.renderEditor()
        return
      }

      if (path === '__tilt_x_user_mode' || path === '__tilt_y_user_mode') {
        const key = path === '__tilt_x_user_mode' ? 'tilt_x' : 'tilt_y'
        const specByKey = {
          disabled: { enabled: false },
          control_change: { output_type: 'cc', enabled: true },
          notes: { output_type: 'note', enabled: true },
          lfo_rate: { output_type: 'lfo_rate', enabled: true },
          lfo_depth: { output_type: 'lfo_depth', enabled: true },
          pitch_bend: { output_type: 'pitch_bend', enabled: true },
          tempo_nudge: { output_type: 'tempo_nudge', enabled: true }
        }
        const spec = specByKey[val]
        if (!spec) return
        if (!this.editModel[key]) {
          this.editModel[key] = { enabled: false, output_type: 'cc' }
        }
        this.editModel[key].enabled = spec.enabled
        if (spec.output_type) this.editModel[key].output_type = spec.output_type
        if ((val === 'lfo_rate' || val === 'lfo_depth') && !this.editModel[key].lfo_target) {
          this.editModel[key].lfo_target = 'both'
        }
        this.markDirty()
        this.renderEditor()
        return
      }

      if (path === '__sample_hold_user_mode') {
        const specByKey = {
          disabled: { enabled: false },
          continuous: { enabled: true, mode: 'continuous' },
          step: { enabled: true, mode: 'step' }
        }
        const spec = specByKey[val]
        if (!spec) return
        if (!this.editModel.sample_hold_config) {
          this.editModel.sample_hold_config = { enabled: false, mode: 'continuous' }
        }
        if (!this.editModel.sample_hold) {
          this.editModel.sample_hold = { enabled: false, output_type: 'cc' }
        }
        this.editModel.sample_hold_config.enabled = spec.enabled
        this.editModel.sample_hold.enabled = spec.enabled
        if (spec.mode) this.editModel.sample_hold_config.mode = spec.mode
        this.markDirty()
        this.renderEditor()
        return
      }

      if (path === '__rtg_user_mode') {
        const specByKey = {
          disabled: { enabled: false },
          continuous: { enabled: true, mode: 'continuous' },
          step: { enabled: true, mode: 'step' }
        }
        const spec = specByKey[val]
        if (!spec) return
        if (!this.editModel.rtg_config) {
          this.editModel.rtg_config = { enabled: false, mode: 'continuous', generator: 'random' }
        }
        this.editModel.rtg_config.enabled = spec.enabled
        if (spec.mode) this.editModel.rtg_config.mode = spec.mode
        this.markDirty()
        this.renderEditor()
        return
      }

      if (path === '__cv_audio_gain') {
        if (!this.editModel.audio_config) this.editModel.audio_config = {}
        this.editModel.audio_config.sensitivity = ActionCatalog.gainToSensitivity(Number(val))
        this.markDirty()
        this.renderEditor()
        return
      }

      if (path.endsWith('.__boomerang_target')) {
        const aPath = path.slice(0, -'.__boomerang_target'.length)
        const action = this.getAtPath(aPath)
        if (!action) return
        if (val === 'random') {
          action.target_mode = 'random'
        } else {
          const n = Number(val)
          action.target_mode = 'explicit'
          action.target_value = action.output_type === 'pitch_bend' ? n * 128 : n
        }
        this.markDirty()
        this.renderEditor()
        return
      }

      if (path.endsWith('.__boomerang_origin')) {
        const aPath = path.slice(0, -'.__boomerang_origin'.length)
        const action = this.getAtPath(aPath)
        if (!action) return
        if (val === 'current') {
          action.start_mode = 'current'
        } else {
          action.start_mode = 'explicit'
          action.start_value = Number(val)
        }
        this.markDirty()
        this.renderEditor()
        return
      }

      const boomPhaseMode = path.match(/^(.*)\.(attack|sustain|release)_mode$/)
      if (boomPhaseMode) {
        const aPath = boomPhaseMode[1]
        const phase = boomPhaseMode[2]
        const action = this.getAtPath(aPath)
        this.setAtPath(path, val)
        if (action?.type === 'boomerang') {
          const timePath = `${aPath}.${phase}_time_ms`
          if (val === 'time_ms' && !action[`${phase}_time_ms`]) {
            this.setAtPath(timePath, 1000)
          } else if (val === 'instant') {
            this.setAtPath(timePath, 0)
          }
        }
        this.markDirty()
        this.renderEditor()
        return
      }

      if (val === '__original__' && path.endsWith('.release_preset')) {
        const aPath = path.slice(0, -'.release_preset'.length)
        const action = this.getAtPath(aPath)
        if (action) {
          action.release_to_original = true
          delete action.release_preset
        }
        this.markDirty()
        this.renderEditor()
        return
      }

      if (val === '__original__' && path.endsWith('.mode2')) {
        const aPath = path.slice(0, -'.mode2'.length)
        const action = this.getAtPath(aPath)
        if (action?.type === 'touchwheel' && (action.variant || 'hold') === 'hold') {
          action.release_to_original = true
          delete action.mode2
          this.markDirty()
          this.renderEditor()
          return
        }
      }

      if (val === '__original__' && path.endsWith('.param2')) {
        const aPath = path.slice(0, -'.param2'.length)
        const action = this.getAtPath(aPath)
        if (action?.type === 'param' && (action.variant || 'hold') === 'hold') {
          action.release_to_original = true
          delete action.param2
          this.markDirty()
          this.renderEditor()
          return
        }
      }

      if (path.endsWith('.release_preset')) {
        const aPath = path.slice(0, -'.release_preset'.length)
        const action = this.getAtPath(aPath)
        if (action?.type === 'preset' && action?.variant === 'hold') {
          action.release_to_original = false
        }
      }

      if (path.endsWith('.mode2')) {
        const aPath = path.slice(0, -'.mode2'.length)
        const action = this.getAtPath(aPath)
        if (action?.type === 'touchwheel' && (action.variant || 'hold') === 'hold') {
          action.release_to_original = false
        }
      }

      if (path.endsWith('.param2')) {
        const aPath = path.slice(0, -'.param2'.length)
        const action = this.getAtPath(aPath)
        if (action?.type === 'param' && (action.variant || 'hold') === 'hold') {
          action.release_to_original = false
        }
      }

      if (path.endsWith('.num_modes')) {
        const aPath = path.slice(0, -'.num_modes'.length)
        const action = this.getAtPath(aPath)
        if (action?.type === 'touchwheel' && action.variant === 'cycle') {
          const count = Math.max(2, Math.min(8, Number(val)))
          const modes = this.asArray(action.modes)
          while (modes.length < count) modes.push(0)
          action.modes = modes.slice(0, count)
          action.num_modes = count
          this.markDirty()
          this.renderEditor()
          return
        }
      }

      if (path.endsWith('.num_modules')) {
        const aPath = path.slice(0, -'.num_modules'.length)
        const action = this.getAtPath(aPath)
        if (action?.type === 'ui' && action.variant === 'cycle') {
          const count = Math.max(2, Math.min(8, Number(val)))
          const mods = this.asArray(action.modules)
          while (mods.length < count) mods.push(0)
          action.modules = mods.slice(0, count)
          action.num_modules = count
          this.markDirty()
          this.renderEditor()
          return
        }
      }

      if (path.endsWith('.num_params')) {
        const aPath = path.slice(0, -'.num_params'.length)
        const action = this.getAtPath(aPath)
        if (action?.type === 'param' && action.variant === 'cycle') {
          const count = Math.max(2, Math.min(8, Number(val)))
          const cc = DeviceControls.firstParameterCc(this.deviceDefinition)
          const params = this.asArray(action.params)
          while (params.length < count) params.push(cc)
          action.params = params.slice(0, count)
          action.num_params = count
          this.markDirty()
          this.renderEditor()
          return
        }
      }

      if (path.endsWith('.start_enabled')) {
        this.setAtPath(path, val === 'enable' || val === 'true' || val === true)
        this.markDirty()
        this.renderEditor()
        return
      }

      if (path.endsWith('.reset_phase') || path.endsWith('.restore_on_stop')) {
        this.setAtPath(path, val === '1' || val === 1 || val === true || val === 'true')
        this.markDirty()
        this.renderEditor()
        return
      }

      const ccValueSync = path.match(/^(.*)\.(start_cc|finish_cc|flag_up_cc|flag_down_cc)$/)
      if (ccValueSync) {
        const actionPath = ccValueSync[1]
        const ccKey = ccValueSync[2]
        const valKey = ccKey.replace('_cc', '_value')
        const action = this.getAtPath(actionPath)
        if (action && (action.type === 'punch_in' || action.type === 'flag_ceremony')) {
          const cc = Number(val)
          const curVal = action[valKey]
          this.setAtPath(path, cc)
          this.setAtPath(
            `${actionPath}.${valKey}`,
            DeviceControls.resolveParameterValue(this.deviceDefinition, cc, curVal)
          )
          this.markDirty()
          this.renderEditor()
          return
        }
      }

      if (path.endsWith('.release_mode')) {
        const aPath = path.slice(0, -'.release_mode'.length)
        const action = this.getAtPath(aPath)
        if (val === 'always' && action) {
          delete action.release_mode
          delete action.release_threshold_ms
          this.markDirty()
          this.renderEditor()
          return
        }
        if (action && val !== 'always' && !action.release_threshold_ms) {
          this.setAtPath(`${aPath}.release_threshold_ms`, 1000)
        }
      }

      const randomizeCcMatch = path.match(/^(.*)\.cc\.(\d+)$/)
      if (randomizeCcMatch) {
        const action = this.getAtPath(randomizeCcMatch[1])
        if (action?.type === 'randomize') {
          if (this.patchRandomizeCc(randomizeCcMatch[1], Number(randomizeCcMatch[2]), val)) {
            this.markDirty()
            this.renderEditor()
          }
          return
        }
      }

      const twCcMatch = path.match(/^touchwheel\.cc_numbers\.(\d+)$/)
      if (twCcMatch) {
        this.setAtPath(path, Number(val))
        if (this.editModel.touchwheel) this.syncTouchwheelCcNumbers(this.editModel.touchwheel)
        this.markDirty()
        this.renderEditor()
        return
      }

      const mapCcMatch = path.match(/^(expression|proximity|als|tilt_x|tilt_y|cv|lfo1|lfo2|note_track|sample_hold)\.cc_numbers\.(\d+)$/)
      if (mapCcMatch) {
        this.setAtPath(path, Number(val))
        const mapping = this.getAtPath(mapCcMatch[1])
        if (mapping) this.syncLfoCcNumbers(mapping)
        this.markDirty()
        this.renderEditor()
        return
      }

      if (this.isNumericScenePath(path)) {
        const num = Number(val)
        if (!Number.isNaN(num)) val = num
      }
      this.setAtPath(path, val)
      if (path.endsWith('.type')) {
        // Reset the variant to a valid default for the new type so a stale
        // variant (e.g. lfo's "start") can't leak into the next type's list.
        const aPath = path.slice(0, -'.type'.length)
        const def = ActionCatalog.defaultVariant(val)
        const action = this.getAtPath(aPath)
        if (def) this.setAtPath(`${aPath}.variant`, def)
        else if (action) delete action.variant
        if (DeviceControls.isControlAction(val)) DeviceControls.seedControlAction(this, aPath)
        if (DeviceControls.isPresetAction(val)) DeviceControls.seedPresetAction(this, aPath)
        if (val === 'scene') SceneActions.seedSceneSetAction(this, aPath)
        if (val === 'confirm_pending') this.seedConfirmPendingAction(aPath)
        else if (val === 'note') this.seedNoteAction(aPath)
        else if (val === 'randomize') this.seedRandomizeAction(aPath)
        else if (val === 'piano_pedal') this.seedPianoPedalAction(aPath)
        else if (val === 'touchwheel') this.seedTouchwheelAction(aPath)
        else if (val === 'lfo') this.seedLfoAction(aPath)
        else if (val === 'tempo') this.seedTempoAction(aPath)
        else if (['clock', 'cut', 'ui', 'param', 'rtg', 'sample_hold', 'punch_in',
          'flag_ceremony', 'boomerang'].includes(val)) {
          this.seedConsolidatedAction(aPath)
        } else {
          const updated = this.getAtPath(aPath)
          if (updated && !ActionCatalog.supportsRepeat(updated)) {
            ActionCatalog.clearRepeatFields(updated)
          }
        }
      } else if (path.endsWith('.variant')) {
        const aPath = path.slice(0, -'.variant'.length)
        const action = this.getAtPath(aPath)
        if (DeviceControls.isControlAction(action?.type)) {
          DeviceControls.seedControlAction(this, aPath)
          DeviceControls.normalizeControlAction(this.deviceDefinition, this.getAtPath(aPath))
        }
        if (DeviceControls.isPresetAction(action?.type)) {
          DeviceControls.seedPresetAction(this, aPath)
          DeviceControls.normalizePresetAction(this.deviceDefinition, this.getAtPath(aPath))
        }
        if (action?.type === 'scene' && val === 'set') {
          SceneActions.seedSceneSetAction(this, aPath)
        }
        if (action?.type === 'tempo') {
          this.seedTempoAction(aPath)
        }
        if (action?.type === 'touchwheel') {
          this.seedTouchwheelAction(aPath)
        }
        if (action?.type === 'lfo') {
          this.seedLfoAction(aPath)
        }
        if (['clock', 'cut', 'ui', 'param', 'rtg', 'sample_hold'].includes(action?.type)) {
          this.seedConsolidatedAction(aPath)
        }
      } else {
        this.syncControlValueForCc(path, val)
        this.syncCcValueForAction(path, val)
      }
      this.markDirty()
      this.renderEditor()
    }

    // When a control "Parameter" (cc) dropdown changes, re-resolve the matching
    // value slot so discrete-value devices never show a stale/invalid value.
    syncControlValueForCc (path, val) {
      const m = path.match(/^(.*)\.cc(?:\.(\d+))?$/)
      if (!m) return
      const actionPath = m[1]
      const action = this.getAtPath(actionPath)
      if (!DeviceControls.isControlAction(action?.type)) return
      const variant = action.variant || 'set'
      if (variant === 'cycle') {
        const idx = m[2] != null ? Number(m[2]) : 0
        const cc = Number(val)
        const device = this.deviceDefinition
        const steps = DeviceControls.cycleStepCount(action)
        const multi = DeviceControls.cycleIsMultiValues(action.values)
        for (let i = 0; i < steps; i++) {
          const valPath = multi
            ? `${actionPath}.values.${idx}.${i}`
            : `${actionPath}.values.${i}`
          const cur = multi ? action.values?.[idx]?.[i] : action.values?.[i]
          this.setAtPath(valPath, DeviceControls.resolveParameterValue(device, cc, cur))
        }
        return
      }
      if (variant !== 'set' && variant !== 'hold') return
      const idx = m[2] != null ? Number(m[2]) : null
      const cc = Number(val)
      const device = this.deviceDefinition
      const valuePath = idx != null ? `${actionPath}.value.${idx}` : `${actionPath}.value`
      const curVal = idx != null ? action.value?.[idx] : action.value
      this.setAtPath(valuePath, DeviceControls.resolveParameterValue(device, cc, curVal))
      if (variant === 'hold') {
        const rPath = idx != null ? `${actionPath}.value2.${idx}` : `${actionPath}.value2`
        const curRel = idx != null ? action.value2?.[idx] : action.value2
        this.setAtPath(rPath, DeviceControls.resolveParameterValue(device, cc, curRel))
      }
    }

    syncCcValueForAction (path, val) {
      const boomMatch = path.match(/^(.*)\.cc_number$/)
      if (!boomMatch) return
      const action = this.getAtPath(boomMatch[1])
      if (action?.type !== 'boomerang') return
      const cc = Number(val)
      const device = this.deviceDefinition
      if (action.target_value != null) {
        this.setAtPath(
          `${boomMatch[1]}.target_value`,
          DeviceControls.resolveParameterValue(device, cc, action.target_value)
        )
      }
      if (action.start_mode === 'explicit' && action.start_value != null) {
        this.setAtPath(
          `${boomMatch[1]}.start_value`,
          DeviceControls.resolveParameterValue(device, cc, action.start_value)
        )
      }
    }

    patchNumber (e) {
      if (this.deviceProgramming) return
      const path = e.target.dataset.scenePath
      if (!path) return
      let v = e.target.value === '' ? 0 : Number(e.target.value)
      if (path.includes('rate_hz') || path.includes('sync_mult')) {
        v = parseFloat(e.target.value)
      } else {
        const minAttr = e.target.getAttribute('min')
        const maxAttr = e.target.getAttribute('max')
        if (minAttr !== null && minAttr !== '') v = Math.max(Number(minAttr), v)
        if (maxAttr !== null && maxAttr !== '') v = Math.min(Number(maxAttr), v)
      }
      this.setAtPath(path, v)
      if (path === 'touchwheel_tempo_floor' || path === 'touchwheel_tempo_ceiling') {
        let floor = Number(this.editModel.touchwheel_tempo_floor ?? 20)
        let ceiling = Number(this.editModel.touchwheel_tempo_ceiling ?? 300)
        if (floor < 20) floor = 20
        if (ceiling > 300) ceiling = 300
        if (floor > ceiling) {
          if (path === 'touchwheel_tempo_floor') ceiling = floor
          else floor = ceiling
        }
        this.editModel.touchwheel_tempo_floor = floor
        this.editModel.touchwheel_tempo_ceiling = ceiling
      }
      this.markDirty()
    }

    patchCheckbox (e) {
      if (this.deviceProgramming) return
      const path = e.target.dataset.scenePath
      if (!path) return
      this.setAtPath(path, e.target.checked)
      this.markDirty()
      this.renderEditor()
    }

    // Patch a checkbox without a full re-render. Used for reveal triggers
    // (e.g. Repeat) where a `reveal` controller toggles dependent fields, so
    // the model updates but the editor DOM stays put.
    patchCheckboxQuiet (e) {
      if (this.deviceProgramming) return
      const path = e.target.dataset.scenePath
      if (!path) return
      this.setAtPath(path, e.target.checked)
      this.markDirty()
    }

    togglePatternStep (e) {
      if (this.deviceProgramming) return
      const path = e.currentTarget.dataset.scenePath
      const bit = Number(e.currentTarget.dataset.stepBit)
      if (!path || Number.isNaN(bit)) return
      const mask = Number(this.getAtPath(path) ?? 255) ^ (1 << bit)
      this.setAtPath(path, mask & 0xff)
      this.markDirty()
      this.renderEditor()
    }

    slotAdd (e) {
      if (this.deviceProgramming) return
      const { path, kind, max, default: def } = e.detail || {}
      if (!path) return
      if (kind === 'control') this.addControlCcSlot(path)
      else if (kind === 'touchwheel-cc') this.addTouchwheelCcSlot(path)
      else if (kind === 'lfo-cc') this.addLfoCcSlot(path)
      else if (kind === 'cycle-step') this.addCycleStep(path)
      else if (kind === 'param-cycle-step') this.addParamCycleStep(path)
      else if (kind === 'preset-step') this.addPresetCycleStep(path)
      else if (kind === 'tempo-step') this.addTempoCycleStep(path)
      else this.addListItem(path, def ?? 0, max ?? 8)
      this.markDirty()
      this.renderEditor()
    }

    slotRemove (e) {
      if (this.deviceProgramming) return
      const { path, kind, index, min } = e.detail || {}
      if (!path || index == null || index < 0) return
      if (kind === 'control') this.removeControlCcSlot(path, index)
      else if (kind === 'touchwheel-cc') this.removeTouchwheelCcSlot(path, index)
      else if (kind === 'lfo-cc') this.removeLfoCcSlot(path, index)
      else if (kind === 'cycle-step') this.removeCycleStep(path, index)
      else if (kind === 'param-cycle-step') this.removeParamCycleStep(path, index)
      else if (kind === 'preset-step') this.removePresetCycleStep(path, index)
      else if (kind === 'tempo-step') this.removeTempoCycleStep(path, index)
      else if (kind === 'randomize') this.removeRandomizeSlot(path, index)
      else this.removeListItem(path, index, min ?? 0)
      this.markDirty()
      this.renderEditor()
    }

    asArray (field) {
      if (Array.isArray(field)) return field.slice()
      return field == null ? [] : [field]
    }

    normalizeCcSlotList (raw) {
      let list = (raw || []).slice(0, 4)
      if (list.length === 0) return [0]
      while (list.length > 1 && Number(list[list.length - 1]) === 0) list.pop()
      return list
    }

    applyCcSlotFields (mapping, list) {
      mapping.cc_numbers = list
      mapping.num_cc_numbers = list.filter(cc => Number(cc) > 0).length
      const firstActive = list.find(cc => Number(cc) > 0)
      if (firstActive) mapping.cc_number = firstActive
    }

    touchwheelCcList (tw) {
      return this.normalizeCcSlotList(tw?.cc_numbers)
    }

    syncTouchwheelCcNumbers (tw) {
      if (!tw) return
      this.applyCcSlotFields(tw, this.touchwheelCcList(tw))
    }

    addTouchwheelCcSlot (path) {
      const tw = this.getAtPath(path)
      if (!tw) return
      const device = this.deviceDefinition
      const list = this.touchwheelCcList(tw).slice()
      if (list.length >= 4) return
      const used = new Set(list.map(Number).filter(n => n > 0))
      list.push(
        DeviceControls.hasParameters(device)
          ? DeviceControls.firstUnusedParameterCc(device, used)
          : 0
      )
      this.applyCcSlotFields(tw, list)
    }

    removeTouchwheelCcSlot (path, index) {
      const tw = this.getAtPath(path)
      if (!tw) return
      const list = this.touchwheelCcList(tw)
      if (list.length <= 1) {
        list[0] = 0
        tw.cc_numbers = [0]
        tw.num_cc_numbers = 0
        return
      }
      list.splice(index, 1)
      tw.cc_numbers = list
      this.syncTouchwheelCcNumbers(tw)
    }

    lfoCcList (mapping) {
      return this.normalizeCcSlotList(mapping?.cc_numbers)
    }

    syncLfoCcNumbers (mapping) {
      if (!mapping) return
      this.applyCcSlotFields(mapping, this.lfoCcList(mapping))
    }

    addLfoCcSlot (path) {
      const mapping = this.getAtPath(path)
      if (!mapping) return
      const device = this.deviceDefinition
      const list = this.lfoCcList(mapping).slice()
      if (list.length >= 4) return
      const used = new Set(list.map(Number).filter(n => n > 0))
      list.push(
        DeviceControls.hasParameters(device)
          ? DeviceControls.firstUnusedParameterCc(device, used)
          : 0
      )
      this.applyCcSlotFields(mapping, list)
    }

    removeLfoCcSlot (path, index) {
      const mapping = this.getAtPath(path)
      if (!mapping) return
      const list = this.lfoCcList(mapping)
      if (list.length <= 1) {
        list[0] = 0
        mapping.cc_numbers = [0]
        mapping.num_cc_numbers = 0
        return
      }
      list.splice(index, 1)
      mapping.cc_numbers = list
      this.syncLfoCcNumbers(mapping)
    }

    addControlCcSlot (path) {
      const action = this.getAtPath(path)
      if (!action || !DeviceControls.isControlAction(action.type)) return
      const device = this.deviceDefinition
      const hasParams = DeviceControls.hasParameters(device)
      const ccList = this.asArray(action.cc)
      if (ccList.length >= 4) return
      const used = new Set(ccList.map(Number))
      ccList.push(hasParams ? DeviceControls.firstUnusedParameterCc(device, used) : 0)
      action.cc = ccList

      const fill = (cc) => hasParams ? DeviceControls.resolveParameterValue(device, cc, null) : 0
      const grow = (field) => {
        const list = this.asArray(action[field])
        while (list.length < ccList.length) list.push(fill(ccList[list.length]))
        action[field] = list
      }
      const variant = action.variant || 'set'
      if (variant === 'cycle') {
        const stepCount = DeviceControls.cycleStepCount(action)
        const newCc = ccList[ccList.length - 1]
        const def = (cc) => hasParams ? DeviceControls.resolveParameterValue(device, cc, null) : 0
        if (!DeviceControls.cycleIsMultiValues(action.values)) {
          action.values = [this.asArray(action.values)]
        }
        action.values.push(Array.from({ length: stepCount }, () => def(newCc)))
        action.cc = ccList.length <= 1 ? ccList[0] : ccList
        return
      }
      if (variant === 'set' || variant === 'hold') grow('value')
      if (variant === 'hold') grow('value2')
    }

    removeControlCcSlot (path, index) {
      const action = this.getAtPath(path)
      if (!action || !DeviceControls.isControlAction(action.type)) return
      const variant = action.variant || 'set'
      const drop = (field) => {
        if (!Array.isArray(action[field])) return
        action[field].splice(index, 1)
        if (action[field].length === 1) action[field] = action[field][0]
      }
      if (variant === 'cycle') {
        const ccList = this.asArray(action.cc)
        ccList.splice(index, 1)
        action.cc = ccList.length === 1 ? ccList[0] : ccList
        if (DeviceControls.cycleIsMultiValues(action.values)) {
          action.values.splice(index, 1)
          if (action.values.length === 1) action.values = action.values[0]
        }
        return
      }
      drop('cc')
      if (variant === 'set' || variant === 'hold') drop('value')
      if (variant === 'hold') drop('value2')
    }

    addCycleStep (path) {
      const action = this.getAtPath(path)
      if (!action || !DeviceControls.isControlAction(action.type) ||
          (action.variant || 'set') !== 'cycle') return
      if (DeviceControls.cycleStepCount(action) >= 8) return
      const device = this.deviceDefinition
      const hasParams = DeviceControls.hasParameters(device)
      const def = (cc) => hasParams ? DeviceControls.resolveParameterValue(device, cc, null) : 0
      const ccList = this.asArray(action.cc)
      if (DeviceControls.cycleIsMultiValues(action.values)) {
        action.values.forEach((row, i) => {
          row.push(def(ccList[i] ?? ccList[0]))
        })
      } else {
        const cc = ccList[0]
        const steps = this.asArray(action.values)
        steps.push(def(cc))
        action.values = steps
      }
    }

    removeCycleStep (path, stepIndex) {
      const action = this.getAtPath(path)
      if (!action || !DeviceControls.isControlAction(action.type) ||
          (action.variant || 'set') !== 'cycle') return
      if (DeviceControls.cycleStepCount(action) <= 2) return
      if (DeviceControls.cycleIsMultiValues(action.values)) {
        action.values.forEach(row => { if (Array.isArray(row)) row.splice(stepIndex, 1) })
      } else if (Array.isArray(action.values)) {
        action.values.splice(stepIndex, 1)
      }
    }

    addParamCycleStep (path) {
      const action = this.getAtPath(path)
      if (!action || action.type !== 'param' || (action.variant || 'hold') !== 'cycle') return
      if (ActionCatalog.paramStepCount(action) >= 8) return
      const device = this.deviceDefinition
      const steps = this.asArray(action.params)
      const used = new Set(steps.map(Number))
      steps.push(DeviceControls.firstUnusedParameterCc(device, used))
      action.params = steps
      action.num_params = steps.length
    }

    removeParamCycleStep (path, stepIndex) {
      const action = this.getAtPath(path)
      if (!action || action.type !== 'param' || (action.variant || 'hold') !== 'cycle') return
      if (ActionCatalog.paramStepCount(action) <= 2) return
      if (Array.isArray(action.params)) {
        action.params.splice(stepIndex, 1)
        action.num_params = action.params.length
      }
    }

    addPresetCycleStep (path) {
      const action = this.getAtPath(path)
      if (!action || !DeviceControls.isPresetAction(action.type) ||
          (action.variant || 'set') !== 'cycle') return
      if (DeviceControls.presetStepCount(action) >= 8) return
      const device = this.deviceDefinition
      const def = DeviceControls.resolvePresetValue(device, null)
      const steps = this.asArray(action.presets)
      steps.push(def)
      action.presets = steps
      action.num_presets = steps.length
    }

    removePresetCycleStep (path, stepIndex) {
      const action = this.getAtPath(path)
      if (!action || !DeviceControls.isPresetAction(action.type) ||
          (action.variant || 'set') !== 'cycle') return
      if (DeviceControls.presetStepCount(action) <= 2) return
      if (Array.isArray(action.presets)) {
        action.presets.splice(stepIndex, 1)
        action.num_presets = action.presets.length
      }
    }

    addTempoCycleStep (path) {
      const action = this.getAtPath(path)
      if (!action || action.type !== 'tempo' || (action.variant || 'set') !== 'cycle') return
      if (ActionCatalog.tempoStepCount(action) >= 8) return
      const steps = this.asArray(action.tempos)
      steps.push(120)
      action.tempos = steps
      action.num_tempos = steps.length
    }

    removeTempoCycleStep (path, stepIndex) {
      const action = this.getAtPath(path)
      if (!action || action.type !== 'tempo' || (action.variant || 'set') !== 'cycle') return
      if (ActionCatalog.tempoStepCount(action) <= 2) return
      if (Array.isArray(action.tempos)) {
        action.tempos.splice(stepIndex, 1)
        action.num_tempos = action.tempos.length
      }
    }

    patchRandomizeCc (actionPath, slot, val) {
      const action = this.getAtPath(actionPath)
      if (!action || action.type !== 'randomize') return false
      const list = this.asArray(action.cc)

      if (val === '__inactive__') {
        if (slot < list.length) list.splice(slot, 1)
      } else {
        const cc = Number(val)
        if (Number.isNaN(cc)) return false
        if (slot < list.length) list[slot] = cc
        else if (slot === list.length) list.push(cc)
        else return false
      }

      action.cc = list
      return true
    }

    removeRandomizeSlot (path, slotIndex) {
      const action = this.getAtPath(path)
      if (!action || action.type !== 'randomize') return
      const list = this.asArray(action.cc)
      if (slotIndex <= 0 || slotIndex >= list.length) return
      list.splice(slotIndex, 1)
      action.cc = list
    }

    addListItem (path, def, max) {
      const list = this.getAtPath(path)
      if (!Array.isArray(list)) {
        this.setAtPath(path, [def])
        return
      }
      if (list.length >= max) return
      list.push(def)
    }

    removeListItem (path, index, min) {
      const list = this.getAtPath(path)
      if (!Array.isArray(list) || list.length <= min) return
      list.splice(index, 1)
    }

    patchText (e) {
      if (this.deviceProgramming) return
      const path = e.target.dataset.scenePath
      if (!path) return
      this.setAtPath(path, e.target.value)
      this.markDirty()
    }

    async loadSchema () {
      if (this._schema) return this._schema
      if (this._schemaLoad) return this._schemaLoad
      this._schemaLoad = fetch('/schemas/scene.schema.json', { cache: 'no-store' })
        .then(r => {
          if (!r.ok) throw new Error(`HTTP ${r.status}`)
          return r.json()
        })
        .then(s => {
          this._schema = s
          return s
        })
      return this._schemaLoad
    }

    validateScene (model) {
      const errors = []
      if (this.deviceContext.sceneMode !== 0) {
        const name = String(model?.name ?? '').trim()
        if (!name) {
          errors.push({ path: 'name', message: 'Scene name is required' })
        }
      }
      if (model?.name && window.SceneEditorUi?.isReservedSceneName?.(model.name)) {
        errors.push({
          path: 'name',
          message: '"manifest" is reserved (would overwrite manifest.json)'
        })
      }
      const schemaErrors = (this._schema && window.JsonSchemaValidator)
        ? window.JsonSchemaValidator.validate(model, this._schema) : []
      return errors
        .concat(schemaErrors)
        .concat(DeviceControls.validateControlActionsInModel(model))
        .concat(DeviceControls.validatePresetActionsInModel(model, this.deviceDefinition))
        .concat(SceneActions.validateSceneSetActionsInModel(
          this.sceneList, model, SceneActions.currentEditIndex(this)))
    }

    renderValidation () {
      if (!this.hasValidationBoxTarget) return
      if (!this.validationErrors.length) {
        this.validationBoxTarget.classList.add('hidden')
        this.validationBoxTarget.innerHTML = ''
        return
      }
      this.validationBoxTarget.classList.remove('hidden')
      this.validationBoxTarget.innerHTML = this.validationErrors
        .map(e => `<div>${this.escapeHtml(e.path)}: ${this.escapeHtml(e.message)}</div>`)
        .join('')
    }

    renderEditor (openSectionsOverride) {
      if (!this.hasEditorContainerTarget || !this.editModel) return
      let openSections
      if (openSectionsOverride !== undefined) {
        openSections = openSectionsOverride
        this._openEditorSections = openSections
      } else if (this._sectionLayoutPhase === 0) {
        openSections = this.captureOpenEditorSections()
      } else if (this._sectionLayoutPhase === 1) {
        openSections = new Set()
        this._openEditorSections = openSections
      } else {
        openSections = SceneEditorUi.allSectionTitles(this)
        this._openEditorSections = openSections
      }
      this.editorContainerTarget.innerHTML =
        SceneEditorUi.renderEditor(this, openSections)
      this.renderValidation()
      this.updateProgrammingLock()
    }

    async loadSceneForEdit () {
      if (!this.connection.isConnected) return
      if (this.editPosition === null) return
      const gen = ++this._loadGeneration
      if (this.hasEditorContainerTarget) {
        this.editorContainerTarget.innerHTML =
          `<div class="scene-editor-loading" aria-busy="true">
            <wa-spinner></wa-spinner>
            <p>Loading scene…</p>
          </div>`
      }

      try {
        const arg = String(this.editPosition)
        const result = await this.connection.runSerialTask(async () => {
          if (gen !== this._loadGeneration) return null
          if (this.connection.currentMode) {
            await this.connection._exitModeImpl()
            await this.sleep(300)
          }
          await this.ensureDeviceIdleInTask()

          if (!this.sceneList?.length) {
            try {
              await this.fetchSceneListInTask()
            } catch (err) {
              console.warn('Scene editor: scene list skipped:', err.message)
            }
          }

          try {
            await this.fetchConfigContextInTask()
          } catch (err) {
            console.warn('Scene editor: CONFIG context skipped:', err.message)
          }
          try {
            await this.fetchGlobalPedalInTask()
          } catch (err) {
            console.warn('Scene editor: INFO pedal context skipped:', err.message)
          }
          if (gen !== this._loadGeneration) return null
          try {
            await this.loadDeviceDefinitionInTask()
          } catch (err) {
            console.warn('Scene editor: device definition skipped:', err.message)
          }
          await this.ensureDeviceIdleInTask()

          if (gen !== this._loadGeneration) return null
          return this.connection._fetchSizedTransferImpl(`SCENE_GET ${arg}`, {
            lineTimeout: 30000,
            binaryTimeout: 120000
          })
        })

        if (gen !== this._loadGeneration) return
        if (!result?.data?.length) {
          throw new Error('No scene data from device (SCENE_GET failed or was cancelled)')
        }

        const text = new TextDecoder().decode(result.data)
        const model = JSON.parse(text)
        this.normalizeCvGateModel(model)
        if (model.touchpads) {
          model.touchpads.forEach(tp => this.normalizeTouchpadMapping(tp))
        }
        const controlCorrected =
          DeviceControls.normalizeControlActionsInModel(this.deviceDefinition, model)
        const presetCorrected =
          DeviceControls.normalizePresetActionsInModel(this.deviceDefinition, model)
        this.editModel = model

        // Start with assigned/enabled sections expanded; everything else stays
        // collapsed. User toggles during editing are tracked from here on.
        this._sectionLayoutPhase = 0
        this._openEditorSections = SceneEditorUi.sectionsWithContent(this)

        await this.loadSchema()
        this.validationErrors = []
        this.renderEditor()
        this.updateSectionLayoutButton()
        this.baselineJson = JSON.stringify(this.editModel)
        this.dirty = controlCorrected || presetCorrected
        this.markDirty()
        this.updatePanelTitle()
      } catch (err) {
        console.error('Scene load error:', err)
        if (this.hasEditorContainerTarget) {
          const msg = err?.message || 'Failed to load scene'
          this.editorContainerTarget.innerHTML =
            `<p class="scene-editor-error">${this.escapeHtml(msg)}</p>`
        }
      }
    }

    async ensureDeviceIdleInTask () {
      for (let attempt = 0; attempt < 3; attempt++) {
        await this.connection.sendRaw('EXIT\n')
        await this.sleep(100)
        const deadline = Date.now() + 2000
        while (Date.now() < deadline) {
          const line = await this.connection.readLine(400)
          if (!line) break
          if (line === 'SCENES_STOPPED' || line === 'CONFIG_STOPPED' ||
              line === 'SETTINGS_STOPPED' || line === 'PEDALS_STOPPED') break
          if (line.startsWith('EVT:')) {
            this.connection.dispatchCdcNotify(line)
            continue
          }
        }
        await this.connection.drainInput()
        if (this.connection.currentMode === null) break
        await this.connection._exitModeImpl()
        await this.sleep(150)
      }
    }

    async fetchConfigContextInTask () {
      try {
        await this.connection.sendRaw('CONFIG\n')
        const started = await this.connection.readLine(5000)
        if (started !== 'CONFIG_STARTED') {
          throw new Error(started || 'CONFIG did not start')
        }
        await this.connection.sendRaw('VALUES\n')
        const line = await this.connection.readLine(15000)
        if (!line || !line.includes('{')) {
          throw new Error('CONFIG VALUES missing')
        }
        const vals = JSON.parse(line.substring(line.indexOf('{')))
        if (vals['config.scene_mode'] != null) {
          this.deviceContext.sceneMode = Number(vals['config.scene_mode'])
        }
        if (vals['config.device_mode'] != null) {
          this.deviceContext.deviceMode = Number(vals['config.device_mode'])
        }
        if (vals['config.confirm_change'] != null) {
          this.deviceContext.confirmChange = Number(vals['config.confirm_change'])
        }
        if (vals['config.flag_enabled'] != null) {
          this.deviceContext.flagEnabled = Number(vals['config.flag_enabled']) !== 0
        }
        if (vals['midi_control.enabled'] != null) {
          this.deviceContext.midiControl = Number(vals['midi_control.enabled']) !== 0
        }
      } finally {
        await this.ensureDeviceIdleInTask()
      }
    }

    revertEdits () {
      if (!this.baselineJson) return
      this.editModel = JSON.parse(this.baselineJson)
      this.dirty = false
      this.validationErrors = []
      this.renderEditor()
      this.markDirty()
    }

    async saveScene () {
      if (!this.editModel || this.deviceProgramming) return
      await this.loadSchema()
      this.validationErrors = this.validateScene(this.editModel)
      this.renderValidation()
      if (this.validationErrors.length) return

      const pos = Number(this.editPosition)
      if (Number.isNaN(pos)) {
        this.showMessageDialog('Save failed', 'Cannot resolve scene position for save')
        return
      }

      this.normalizeBeforeSave(this.editModel)
      const json = JSON.stringify(this.editModel)
      const bytes = new TextEncoder().encode(json)

      try {
        this.connection.setTabsLocked(true, 'scenes')

        await this.connection.runSerialTask(async () => {
          if (this.connection.currentMode) {
            await this.connection._exitModeImpl()
            await this.sleep(300)
          }
          await this.ensureDeviceIdleInTask()
          await this.connection.sendRaw(`SCENE_PUT ${pos} ${bytes.length}\n`)
          const ready = await this.connection.readLine(10000)
          if (ready !== 'READY') throw new Error(ready || 'No READY')
          await this.connection.sendBinary(bytes)
          const resp = await this.connection.readLine(60000)
          if (resp !== 'OK') throw new Error(resp || 'Save failed')
        })

        this.baselineJson = JSON.stringify(this.editModel)
        this.dirty = false
        this.markDirty()
        const savedName = String(this.editModel.name || '').trim()
        const row = this.sceneList.find(s => s.position === pos)
        if (row) row.name = savedName
        this.updatePanelTitle()
        document.dispatchEvent(new CustomEvent('scenes:scene-renamed', {
          detail: { position: pos, name: savedName }
        }))
        this.flashSaveStatus()
      } catch (err) {
        console.error('Scene save error:', err)
        this.showMessageDialog('Save failed', err.message || 'Save failed')
      } finally {
        this.connection.setTabsLocked(false)
      }
    }

    setCopyable (text, enabled) {
      if (this.hasCopySourceTarget) this.copySourceTarget.value = text || ''
      const disabled = !enabled || !text
      if (this.hasCopyBtnTarget) this.copyBtnTarget.disabled = disabled
    }

    renderDisconnected () {
      this._inspectText = ''
      this.inspectModel = null
      this.inspectTextTarget.textContent = ''
      this.inspectTextTarget.classList.add('empty')
      this.inspectTextTarget.textContent = 'Connect to view scenes'
      this.truncatedBannerTarget.classList.add('hidden')
      this.setCopyable('', false)
    }

    renderLoading () {
      this.inspectTextTarget.classList.remove('empty')
      this.inspectTextTarget.textContent = 'Loading…'
      this.truncatedBannerTarget.classList.add('hidden')
      this.setCopyable('', false)
    }

    renderInspect (text, truncated) {
      this._inspectText = text || ''
      this._inspectTruncated = !!truncated
      this.applyInspectBody()
    }

    applyInspectBody () {
      const body = this._inspectText || '(empty scene)'
      this.inspectTextTarget.classList.remove('empty')
      const boomOpts = this.deviceContext.midiControl === false
        ? { midiControl: false }
        : {}
      const rich = window.LfoWaveformPreview?.renderInspectableDocument?.(
        body,
        this.inspectModel,
        boomOpts
      )
      if (rich != null) {
        this.inspectTextTarget.innerHTML = rich
      } else {
        this.inspectTextTarget.textContent = body
      }
      if (this._inspectTruncated) {
        this.truncatedBannerTarget.classList.remove('hidden')
      } else {
        this.truncatedBannerTarget.classList.add('hidden')
      }
      this.setCopyable(body, true)
    }

    renderError (message) {
      this._inspectText = ''
      this.inspectTextTarget.classList.remove('empty')
      this.inspectTextTarget.textContent = message
      this.truncatedBannerTarget.classList.add('hidden')
      this.setCopyable('', false)
    }

    escapeHtml (text) {
      const div = document.createElement('div')
      div.textContent = text
      return div.innerHTML
    }

    print () {
      const text = this.copySourceTarget?.value || ''
      if (!text) return

      const boomOpts = this.deviceContext.midiControl === false
        ? { midiControl: false }
        : {}
      const htmlBody = window.LfoWaveformPreview?.renderInspectableDocument?.(
        text,
        this.inspectModel,
        boomOpts
      )
      const boomStyles = window.BoomerangEnvelope?.printStyles || ''
      const lfoStyles = window.LfoWaveformPreview?.printStyles || ''

      let frame = this.printFrame
      if (!frame) {
        frame = document.createElement('iframe')
        frame.className = 'scene-print-frame'
        frame.title = 'Scene inspect print'
        frame.setAttribute('aria-hidden', 'true')
        document.body.appendChild(frame)
        this.printFrame = frame
      }

      const doc = frame.contentDocument || frame.contentWindow.document
      doc.open()
      doc.write(`<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>Scene Inspect</title>
<style>
  @page { size: letter portrait; margin: 0.5in; }
  html, body {
    margin: 0;
    padding: 0;
    background: #fff;
    color: #000;
  }
  body {
    width: 7.5in;
    box-sizing: border-box;
  }
  .print-text {
    margin: 0;
    font-family: Consolas, Monaco, "Courier New", monospace;
    line-height: 1.25;
    white-space: pre-wrap;
    word-break: break-word;
  }
  .print-content {
    width: 100%;
  }
  .print-cols {
    display: flex;
    flex-direction: row;
    gap: 0.25in;
    width: 100%;
  }
  .print-col {
    flex: 1;
    min-width: 0;
  }
  .print-inspect-rich {
    font-family: Consolas, Monaco, "Courier New", monospace;
    font-size: 9pt;
    line-height: 1.25;
  }
  ${boomStyles}
  ${lfoStyles}
  @media print {
    html, body {
      width: 7.5in;
      height: 10in;
      max-height: 10in;
      overflow: hidden;
    }
    .print-cols {
      display: flex;
    }
  }
</style>
</head>
<body><div id="print-root"></div></body>
</html>`)
      doc.close()

      const win = frame.contentWindow
      requestAnimationFrame(() => {
        requestAnimationFrame(() => {
          if (htmlBody) {
            const root = doc.getElementById('print-root')
            root.innerHTML = `<div class="print-inspect-rich print-content">${htmlBody}</div>`
            win.print()
            return
          }
          this.fitPrintLayout(doc, text)
          win.print()
        })
      })
    }

    // Letter portrait with 0.5in margins → 10in printable height at 96dpi.
    static PRINTABLE_HEIGHT_PX = 960
    static LAYOUT_REF_PT = 8
    static COMFORT_SINGLE_PT = 9
    static SPLIT_SINGLE_PT = 7.5
    static SHORT_MAX_LINES = 8
    static TWO_COLUMN_MIN_LINES = 18
    static TWO_COLUMN_MIN_CHARS = 1400
    static TWO_COLUMN_HEIGHT_RATIO = 0.58

    countLines (text) {
      if (!text) return 0
      return text.split('\n').length
    }

    measureRefHeight (text, doc) {
      const probe = doc.createElement('div')
      probe.className = 'print-text print-content'
      probe.style.position = 'absolute'
      probe.style.visibility = 'hidden'
      probe.style.left = '0'
      probe.style.top = '0'
      probe.style.width = '100%'
      probe.textContent = text
      probe.style.fontSize = this.constructor.LAYOUT_REF_PT + 'pt'
      doc.body.appendChild(probe)
      const refHeight = probe.scrollHeight
      probe.remove()
      return refHeight
    }

    shouldUseTwoColumns (text, doc, singlePt) {
      if (singlePt >= this.constructor.COMFORT_SINGLE_PT) return false

      const lineCount = this.countLines(text)
      if (lineCount <= this.constructor.SHORT_MAX_LINES) return false
      if (singlePt < this.constructor.SPLIT_SINGLE_PT) return true
      if (lineCount >= this.constructor.TWO_COLUMN_MIN_LINES) return true
      if (text.length >= this.constructor.TWO_COLUMN_MIN_CHARS &&
          singlePt < this.constructor.COMFORT_SINGLE_PT) return true

      return this.measureRefHeight(text, doc) >=
        this.constructor.PRINTABLE_HEIGHT_PX * this.constructor.TWO_COLUMN_HEIGHT_RATIO
    }

    buildPrintRoot (doc, text, twoCol) {
      const root = doc.getElementById('print-root')
      root.replaceChildren()

      if (!twoCol) {
        const el = doc.createElement('div')
        el.className = 'print-text print-content'
        el.textContent = text
        root.appendChild(el)
        return { measureTarget: el, twoCol: false }
      }

      const lines = text.split('\n')
      const mid = Math.ceil(lines.length / 2)
      const row = doc.createElement('div')
      row.className = 'print-cols print-text'
      const left = doc.createElement('div')
      const right = doc.createElement('div')
      left.className = 'print-col'
      right.className = 'print-col'
      left.textContent = lines.slice(0, mid).join('\n')
      right.textContent = lines.slice(mid).join('\n')
      row.append(left, right)
      root.appendChild(row)
      return { measureTarget: row, twoCol: true }
    }

    measurePrintHeight (target, twoCol) {
      if (twoCol) {
        let max = 0
        target.querySelectorAll('.print-col').forEach(col => {
          col.style.height = 'auto'
          col.style.maxHeight = 'none'
          void col.offsetHeight
          max = Math.max(max, col.scrollHeight)
        })
        return max
      }
      target.style.height = 'auto'
      target.style.maxHeight = 'none'
      void target.offsetHeight
      return target.scrollHeight
    }

    setPrintFontSize (target, pt) {
      target.style.fontSize = pt + 'pt'
    }

    searchFontSize (target, printableHeight, minPt, maxPt, twoCol) {
      const fits = (sizePt) => {
        this.setPrintFontSize(target, sizePt)
        return this.measurePrintHeight(target, twoCol) <= printableHeight
      }

      let lo = minPt
      let hi = maxPt
      let best = minPt
      while (hi - lo > 0.1) {
        const mid = (lo + hi) / 2
        if (fits(mid)) {
          best = mid
          lo = mid
        } else {
          hi = mid
        }
      }

      this.setPrintFontSize(target, best)
      while (best > minPt && this.measurePrintHeight(target, twoCol) > printableHeight) {
        best -= 0.25
        this.setPrintFontSize(target, best)
      }
      return best
    }

    fitPrintLayout (doc, text) {
      const printableHeight = this.constructor.PRINTABLE_HEIGHT_PX

      let { measureTarget, twoCol } = this.buildPrintRoot(doc, text, false)
      const singlePt = this.searchFontSize(measureTarget, printableHeight, 4, 72, false)
      if (!this.shouldUseTwoColumns(text, doc, singlePt)) return

      ({ measureTarget, twoCol } = this.buildPrintRoot(doc, text, true))
      this.searchFontSize(measureTarget, printableHeight, 4, 72, twoCol)
    }

    async fetchSceneJsonAtPosition (position) {
      const arg = String(position)
      const result = await this.connection._fetchSizedTransferImpl(`SCENE_GET ${arg}`, {
        lineTimeout: 30000,
        binaryTimeout: 120000
      })
      if (!result?.data?.length) return null
      const model = JSON.parse(new TextDecoder().decode(result.data))
      if (model.touchpads) {
        model.touchpads.forEach(tp => this.normalizeTouchpadMapping(tp))
      }
      return model
    }

    async fetchInspectBoomerangsForPosition (position, gen) {
      try {
        const fetchTask = async () => {
          if (gen !== this._loadGeneration) return null
          if (this.connection.currentMode) {
            await this.connection._exitModeImpl()
            await this.sleep(300)
          }
          await this.ensureDeviceIdleInTask()
          if (gen !== this._loadGeneration) return null
          return this.fetchSceneJsonAtPosition(position)
        }
        const sceneModel = this.connection.isSerialBusy
          ? await fetchTask()
          : await this.connection.runSerialTask(fetchTask)
        if (gen !== this._loadGeneration) return
        this.inspectModel = sceneModel
        this.applyInspectBody()
      } catch (err) {
        if (gen !== this._loadGeneration) return
        console.warn('Scene JSON fetch for inspect skipped:', err)
        this.inspectModel = null
      }
    }

    async fetchInspectForPosition (position) {
      if (!this.connection.isConnected) return
      if (position === null || position === undefined) return

      const gen = ++this._loadGeneration
      this.inspectModel = null
      this.renderLoading()

      try {
        const fetchImpl = async () => {
          if (gen !== this._loadGeneration) return null
          const wasScenes = this.connection.currentMode === 'SCENES'
          if (this.connection.currentMode === 'SCENES') {
            this.connection._stopModeNotifyLoop()
            await this.connection._suspendModeNotify()
            this.connection.mode = null
            this.connection.emit('mode:changed', { mode: null })
          }
          if (gen !== this._loadGeneration) return null
          return this.connection._sendCommandViaPump(
            `SCENE_INSPECT ${position}`,
            60000,
            (data) => typeof data.text === 'string',
            { preludeExit: wasScenes }
          )
        }
        const response = this.connection.isSerialBusy
          ? await fetchImpl()
          : await this.connection.runSerialTask(fetchImpl)

        if (gen !== this._loadGeneration) return
        const inspectOk = this.applyInspectResponse(response)
        if (inspectOk) await this.fetchInspectBoomerangsForPosition(position, gen)
      } catch (err) {
        if (gen !== this._loadGeneration) return
        console.error('Scene inspect fetch error:', err)
        this.renderError('Failed to load scene inspect text')
      } finally {
        if (gen === this._loadGeneration) void this.fetchDeviceProgramming()
      }
    }
  }
)
