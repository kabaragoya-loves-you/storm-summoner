/* Storm Summoner - Scene inspect + editor (Scenes tab right panel) */

application.register(
  'scene',
  class extends BaseController {
    static targets = [
      'inspectView', 'inspectText', 'editView', 'truncatedBanner',
      'copySource', 'copyBtn', 'editorToolbar',
      'editorContainer', 'editorTitle', 'validationBox',
      'programmingBanner', 'saveBtn', 'revertBtn'
    ]

    connect () {
      this.editing = false
      this._loadGeneration = 0
      this.notifyDebounce = null
      this.editModel = null
      this.baselineJson = null
      this.dirty = false
      this.editPosition = null
      this.sceneList = []
      this.deviceProgramming = false
      this.deviceContext = {
        sceneMode: 2,
        deviceMode: 0,
        midiControl: false
      }
      this.validationErrors = []
      this._schema = null
      this._schemaLoad = null

      this._onConnectionChanged = this.onConnectionChanged.bind(this)
      this._onTabActivated = this.onTabActivated.bind(this)
      this._onCdcNotify = this.onCdcNotify.bind(this)
      this._onOpenScene = this.onOpenScene.bind(this)
      this._onSceneListUpdated = this.onSceneListUpdated.bind(this)
      this._onDownloadScene = this.onDownloadScene.bind(this)

      this.connection.on('connection:changed', this._onConnectionChanged)
      document.addEventListener('app:tab-activated', this._onTabActivated)
      document.addEventListener('cdc:notify', this._onCdcNotify)
      document.addEventListener('scenes:open-scene', this._onOpenScene)
      document.addEventListener('scenes:list-updated', this._onSceneListUpdated)
      document.addEventListener('scenes:download-scene', this._onDownloadScene)
    }

    disconnect () {
      this.connection.off('connection:changed', this._onConnectionChanged)
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
    }

    onSceneListUpdated (e) {
      this.sceneList = e.detail?.scenes || []
      this.updatePanelTitle()
    }

    onOpenScene (e) {
      const { position, mode } = e.detail || {}
      if (position === undefined || position === null) return
      if (this.dirty && position !== this.editPosition &&
          !confirm('Discard unsaved changes?')) return

      this.editPosition = position
      if (mode === 'edit') {
        this.showEditMode()
      } else if (mode === 'print') {
        void this.openForPrint()
      } else {
        void this.showViewMode()
      }
    }

    async showViewMode () {
      this.editing = false
      this.inspectViewTarget.classList.remove('hidden')
      this.editViewTarget.classList.add('hidden')
      if (this.hasEditorToolbarTarget) {
        this.editorToolbarTarget.classList.add('hidden')
      }
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
        this.renderDisconnected()
        this.editModel = null
        this.editPosition = null
        this.deviceProgramming = false
        if (this.hasEditorToolbarTarget) {
          this.editorToolbarTarget.classList.add('hidden')
        }
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
      if (this.editPosition !== null) {
        if (this.editing) this.loadSceneForEdit()
        else this.fetchInspectForPosition(this.editPosition)
      }
    }

    onCdcNotify (e) {
      const kind = e.detail?.kind
      if (kind === 'programming') {
        const active = e.detail?.index === 1
        this.deviceProgramming = active
        this.updateProgrammingLock()
        if (!active && this.editing && !this.dirty) this.loadSceneForEdit()
        return
      }
      if (kind !== 'scene_changed' && kind !== 'scene_updated') return
      if (!this.connection.isConnected) return
      if (this.editing && this.dirty) return

      const activeTab = document.querySelector('wa-tab-group wa-tab[active]')
      if (activeTab?.getAttribute('panel') !== 'scenes') return
      if (this.editPosition === null) return

      if (this.notifyDebounce) clearTimeout(this.notifyDebounce)
      this.notifyDebounce = setTimeout(() => {
        this.notifyDebounce = null
        if (this.editing) this.loadSceneForEdit()
        else this.fetchInspectForPosition(this.editPosition)
      }, 100)
    }

    async fetchDeviceProgramming () {
      try {
        const response = await this.connection.runSerialTask(async () => {
          if (this.connection.currentMode) {
            await this.connection._exitModeImpl()
            await this.sleep(300)
          }
          return this.connection._sendCommandImpl('INFO', 15000, (d) =>
            typeof d.programming === 'boolean')
        })
        if (!response || response.startsWith('ERROR:')) return
        const data = JSON.parse(response)
        this.deviceProgramming = !!data.programming
        this.updateProgrammingLock()
      } catch (_) { /* ignore */ }
    }

    updateProgrammingLock () {
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
          alert('Failed to download scene')
          return
        }
        const model = JSON.parse(new TextDecoder().decode(result.data))
        this.saveJsonBlob(model, pos)
      } catch (err) {
        console.error('Scene download error:', err)
        alert(err.message || 'Download failed')
      }
    }

    normalizeBeforeSave (model) {
      if (!model.touchpads) return
      model.touchpads.forEach(tp => {
        if (tp.actions?.length) {
          tp.action = tp.actions[0]
          delete tp.actions
        }
      })
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

    markDirty () {
      this.dirty = JSON.stringify(this.editModel) !== this.baselineJson
      if (this.hasSaveBtnTarget) {
        this.saveBtnTarget.disabled = this.deviceProgramming || !this.dirty
      }
      if (this.hasRevertBtnTarget) this.revertBtnTarget.disabled = !this.dirty
    }

    patchSelect (e) {
      if (this.deviceProgramming) return
      const path = e.target.dataset.scenePath
      if (!path) return
      this.setAtPath(path, e.target.value)
      this.markDirty()
      this.renderEditor()
    }

    patchNumber (e) {
      if (this.deviceProgramming) return
      const path = e.target.dataset.scenePath
      if (!path) return
      let v = e.target.value === '' ? 0 : Number(e.target.value)
      if (path.includes('rate_hz') || path.includes('sync_mult')) v = parseFloat(e.target.value)
      this.setAtPath(path, v)
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
      this._schemaLoad = fetch('/schemas/scene.schema.json')
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
      if (model?.name && window.SceneEditorUi?.isReservedSceneName?.(model.name)) {
        errors.push({
          path: 'name',
          message: '"manifest" is reserved (would overwrite manifest.json)'
        })
      }
      if (!this._schema || !window.JsonSchemaValidator) return errors
      return errors.concat(window.JsonSchemaValidator.validate(model, this._schema))
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

    renderEditor () {
      if (!this.hasEditorContainerTarget || !this.editModel) return
      this.editorContainerTarget.innerHTML =
        SceneEditorUi.renderEditor(this)
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

          try {
            await this.fetchConfigContextInTask()
          } catch (err) {
            console.warn('Scene editor: CONFIG context skipped:', err.message)
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
        this.editModel = model

        await this.loadSchema()
        this.validationErrors = []
        this.renderEditor()
        this.baselineJson = JSON.stringify(this.editModel)
        this.dirty = false
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
      await this.connection.sendRaw('EXIT\n')
      await this.sleep(200)
      let line = await this.connection.readLine(2000)
      while (line && line.startsWith('EVT:')) {
        line = await this.connection.readLine(500)
      }
      await this.connection.drainInput()
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
        alert('Cannot resolve scene position for save')
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
      } catch (err) {
        console.error('Scene save error:', err)
        alert(err.message || 'Save failed')
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
      const body = text || '(empty scene)'
      this.inspectTextTarget.classList.remove('empty')
      this.inspectTextTarget.textContent = body
      if (truncated) {
        this.truncatedBannerTarget.classList.remove('hidden')
      } else {
        this.truncatedBannerTarget.classList.add('hidden')
      }
      this.setCopyable(body, true)
    }

    renderError (message) {
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

    async fetchInspectForPosition (position) {
      if (!this.connection.isConnected) return
      if (position === null || position === undefined) return

      const gen = ++this._loadGeneration
      this.renderLoading()

      try {
        const response = await this.connection.runSerialTask(async () => {
          if (gen !== this._loadGeneration) return null
          if (this.connection.currentMode) {
            await this.connection._exitModeImpl()
            await this.sleep(300)
          }
          await this.ensureDeviceIdleInTask()
          if (gen !== this._loadGeneration) return null
          return this.connection._sendCommandImpl(
            `SCENE_INSPECT ${position}`,
            60000,
            (data) => typeof data.text === 'string'
          )
        })

        if (gen !== this._loadGeneration) return
        if (!response || response.startsWith('ERROR:')) {
          this.renderError(response || 'No response from device')
          return
        }

        const data = JSON.parse(response)
        this.renderInspect(data.text || '', !!data.truncated)
      } catch (err) {
        if (gen !== this._loadGeneration) return
        console.error('Scene inspect fetch error:', err)
        this.renderError('Failed to load scene inspect text')
      }
    }
  }
)
