/* Storm Summoner - Scene tab (inspect + editor) */

application.register(
  'scene',
  class extends BaseController {
    static targets = [
      'inspectView', 'inspectText', 'editView', 'editBtn', 'truncatedBanner',
      'copySource', 'copyBtn', 'printBtn',
      'editorContainer', 'editorTitle', 'editorStatus', 'validationBox',
      'programmingBanner', 'prevBtn', 'nextBtn', 'saveBtn', 'revertBtn',
      'downloadBtn'
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
      this._onTabParams = this.onTabParams.bind(this)

      this.connection.on('connection:changed', this._onConnectionChanged)
      document.addEventListener('app:tab-activated', this._onTabActivated)
      document.addEventListener('cdc:notify', this._onCdcNotify)
      document.addEventListener('app:tab-params', this._onTabParams)
    }

    disconnect () {
      this.connection.off('connection:changed', this._onConnectionChanged)
      document.removeEventListener('app:tab-activated', this._onTabActivated)
      document.removeEventListener('cdc:notify', this._onCdcNotify)
      document.removeEventListener('app:tab-params', this._onTabParams)
      if (this.notifyDebounce) clearTimeout(this.notifyDebounce)
      if (this.printFrame) {
        this.printFrame.remove()
        this.printFrame = null
      }
    }

    onTabParams (e) {
      if (e.detail.tab !== 'scene') return
      const params = e.detail.params || {}
      if (params.position === undefined || params.position === null) return
      this.editPosition = Number(params.position)
      if (params.edit && !this.editing) {
        this.editing = true
        this.inspectViewTarget.classList.add('hidden')
        this.editViewTarget.classList.remove('hidden')
        this.editBtnTarget.textContent = 'View'
        if (this.hasDownloadBtnTarget) {
          this.downloadBtnTarget.classList.remove('hidden')
        }
      }
      if (this.editing && this.connection.isConnected) this.loadSceneForEdit()
    }

    onConnectionChanged ({ connected }) {
      if (!connected) {
        this.renderDisconnected()
        this.editModel = null
        this.deviceProgramming = false
      } else {
        this.fetchDeviceProgramming()
      }
    }

    onTabActivated (e) {
      if (e.detail.tab !== 'scene') return
      if (!this.connection.isConnected) {
        this.renderDisconnected()
        return
      }
      if (this.editing) {
        this.loadSceneForEdit()
      } else {
        this._loadGeneration++
        this.fetchInspect()
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
      if (activeTab?.getAttribute('panel') !== 'scene') return

      if (this.notifyDebounce) clearTimeout(this.notifyDebounce)
      this.notifyDebounce = setTimeout(() => {
        this.notifyDebounce = null
        if (this.editing) this.loadSceneForEdit()
        else this.fetchInspect()
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

    toggleEdit () {
      this.editing = !this.editing
      this.inspectViewTarget.classList.toggle('hidden', this.editing)
      this.editViewTarget.classList.toggle('hidden', !this.editing)
      this.editBtnTarget.textContent = this.editing ? 'View' : 'Edit'
      if (this.hasDownloadBtnTarget) {
        this.downloadBtnTarget.classList.toggle('hidden', !this.editing)
        this.downloadBtnTarget.disabled = !this.editModel
      }

      if (this.editing) {
        if (this.editPosition === null) this.editPosition = 'current'
        this.loadSceneForEdit()
      } else if (this.connection.isConnected) {
        this.fetchInspect()
      }
    }

    downloadSceneJson () {
      if (!this.editModel) return
      const json = JSON.stringify(this.editModel, null, 2)
      const blob = new Blob([json], { type: 'application/json' })
      const url = URL.createObjectURL(blob)
      const safeName = String(this.editModel.name || 'scene')
        .replace(/[^\w.-]+/g, '_')
        .replace(/^_+|_+$/g, '')
        .slice(0, 40) || 'scene'
      const pos = this.editPosition === 'current' ? 'current' : String(this.editPosition)
      const a = document.createElement('a')
      a.href = url
      a.download = `${safeName}-${pos}.json`
      a.click()
      URL.revokeObjectURL(url)
    }

    cloneModel (obj) {
      return JSON.parse(JSON.stringify(obj))
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
      if (this.hasEditorStatusTarget) {
        this.editorStatusTarget.textContent = this.dirty ? 'Unsaved changes' : 'Saved'
      }
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
      if (this.hasDownloadBtnTarget) this.downloadBtnTarget.disabled = false
    }


    resolveGetArg () {
      if (this.editPosition === 'current') return 'current'
      return String(this.editPosition)
    }

    async loadSceneForEdit () {
      if (!this.connection.isConnected) return
      const gen = ++this._loadGeneration
      if (this.hasEditorStatusTarget) this.editorStatusTarget.textContent = 'Loading…'
      if (this.hasDownloadBtnTarget) this.downloadBtnTarget.disabled = true
      if (this.hasEditorContainerTarget) {
        this.editorContainerTarget.innerHTML =
          `<div class="scene-editor-loading" aria-busy="true">
            <wa-spinner></wa-spinner>
            <p>Loading scene…</p>
          </div>`
      }

      try {
        const arg = this.resolveGetArg()
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
          try {
            await this.fetchSceneListInTask()
          } catch (err) {
            console.warn('Scene editor: scene list skipped:', err.message)
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
        this.baselineJson = JSON.stringify(model)
        this.dirty = false

        const pos = this.editPosition === 'current'
          ? this.sceneList.find(s => s.current)?.position
          : Number(this.editPosition)
        const row = this.sceneList.find(s => s.position === pos)
        if (this.hasEditorTitleTarget) {
          const label = row
            ? `${row.name}${row.current ? ' (playing)' : ''}`
            : (model.name || 'Scene')
          this.editorTitleTarget.textContent = label
        }

        await this.loadSchema()
        this.validationErrors = []
        this.renderEditor()
        this.markDirty()
        this.updateNavButtons()
      } catch (err) {
        console.error('Scene load error:', err)
        if (this.hasEditorStatusTarget) {
          this.editorStatusTarget.textContent = 'Failed to load scene'
        }
        if (this.hasEditorContainerTarget) {
          const msg = err?.message || 'Failed to load scene'
          this.editorContainerTarget.innerHTML =
            `<p class="scene-editor-error">${this.escapeHtml(msg)}</p>`
        }
      }
    }

    /** Device may be in CONFIG/SCENES/etc. without connection.mode set — always EXIT+drain. */
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

    async fetchSceneListInTask () {
      try {
        await this.connection.sendRaw('SCENES\n')
        const started = await this.connection.readLine(5000)
        if (started !== 'SCENES_STARTED') {
          throw new Error(started || 'SCENES did not start')
        }
        await this.connection.sendRaw('LIST\n')
        const line = await this.connection.readLine(8000)
        if (!line || !line.includes('[')) {
          throw new Error(line?.startsWith('ERROR:') ? line : 'LIST response missing JSON')
        }
        const jsonStart = line.indexOf('[')
        const jsonEnd = line.lastIndexOf(']')
        this.sceneList = JSON.parse(line.substring(jsonStart, jsonEnd + 1))
      } finally {
        await this.ensureDeviceIdleInTask()
      }
    }

    updateNavButtons () {
      if (!this.sceneList.length || this.editPosition === 'current') {
        if (this.hasPrevBtnTarget) this.prevBtnTarget.disabled = true
        if (this.hasNextBtnTarget) this.nextBtnTarget.disabled = true
        return
      }
      const pos = Number(this.editPosition)
      if (this.hasPrevBtnTarget) this.prevBtnTarget.disabled = pos <= 0
      if (this.hasNextBtnTarget) {
        this.nextBtnTarget.disabled = pos >= this.sceneList.length - 1
      }
    }

    async prevScene () {
      if (this.dirty && !confirm('Discard unsaved changes?')) return
      const pos = Number(this.editPosition)
      if (pos <= 0) return
      this.editPosition = pos - 1
      await this.loadSceneForEdit()
    }

    async nextScene () {
      if (this.dirty && !confirm('Discard unsaved changes?')) return
      const pos = Number(this.editPosition)
      if (pos >= this.sceneList.length - 1) return
      this.editPosition = pos + 1
      await this.loadSceneForEdit()
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

      const pos = this.editPosition === 'current'
        ? this.sceneList.find(s => s.current)?.position
        : Number(this.editPosition)
      if (pos === undefined || pos === null) {
        alert('Cannot resolve scene position for save')
        return
      }

      this.normalizeBeforeSave(this.editModel)
      const json = JSON.stringify(this.editModel)
      const bytes = new TextEncoder().encode(json)

      try {
        this.connection.setTabsLocked(true, 'scene')
        if (this.hasEditorStatusTarget) this.editorStatusTarget.textContent = 'Saving…'

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
        if (this.hasEditorStatusTarget) this.editorStatusTarget.textContent = 'Saved'
      } catch (err) {
        console.error('Scene save error:', err)
        alert(err.message || 'Save failed')
        if (this.hasEditorStatusTarget) this.editorStatusTarget.textContent = 'Save failed'
      } finally {
        this.connection.setTabsLocked(false)
      }
    }

    setCopyable (text, enabled) {
      if (this.hasCopySourceTarget) this.copySourceTarget.value = text || ''
      const disabled = !enabled || !text
      if (this.hasCopyBtnTarget) this.copyBtnTarget.disabled = disabled
      if (this.hasPrintBtnTarget) this.printBtnTarget.disabled = disabled
    }

    renderDisconnected () {
      this.inspectTextTarget.textContent = ''
      this.inspectTextTarget.classList.add('empty')
      this.inspectTextTarget.textContent = 'Connect to view scene inspect text'
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

      const doc = frame.contentWindow.document
      doc.open()
      doc.write(`<!DOCTYPE html>
<html lang="en"><head><meta charset="utf-8"><title>Scene Inspect</title>
<style>@page { size: letter portrait; margin: 0.5in; }
body { margin: 0; font-family: Consolas, monospace; white-space: pre-wrap; }</style>
</head><body><pre>${this.escapeHtml(text)}</pre></body></html>`)
      doc.close()
      frame.contentWindow.print()
    }

    async fetchInspect () {
      if (!this.connection.isConnected) return

      const gen = this._loadGeneration
      this.renderLoading()

      try {
        const response = await this.connection.runSerialTask(async () => {
          if (gen !== this._loadGeneration) return null
          if (this.connection.currentMode) {
            await this.connection._exitModeImpl()
            await this.sleep(300)
          }
          if (gen !== this._loadGeneration) return null
          return this.connection._sendCommandImpl('SCENE_INSPECT', 60000, (data) =>
            typeof data.text === 'string')
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
