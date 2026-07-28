/* Storm Summoner - System Update Controller (Phase 8 of partition split)

See docs/SYSTEM_UPDATE.md for the operator runbook, protocol reference,
resume semantics, and why this tab is hidden by default rather than deleted.

Drives the coordinated v(N+1) -> v(N+2) push:
  Step 0: pre-flight
  Step 1: write new shared assets via RAW_ASSETS_WRITE chunks
  Step 2: standard FIRMWARE OTA + COMMIT
  Step 3: PARTITION_TABLE upload + verify (PSRAM-staged on device)
  Step 4: COMMIT_PARTITION_TABLE  <-- the only catastrophic-failure point
  Step 5: wait for reboot, reconnect, verify v(N+2) is live with 8 MB assets

Each step is checkpointed in localStorage so the user can close the tab
and resume after a failure. Every interruption point lands the device in
either "v(N+1) running normally" or "v(N+2) booting in resilience mode" --
no bricked state -- except a power loss inside the ~50 ms Step 4 window,
which is the irreducible risk we surface as a hard confirmation.

Wire protocol assumptions (see web/js/updater.js SYSTEM_UPDATE_COMMANDS
and components/usb_cdc_update/usb_cdc_update.c):
  PARTITION_TABLE <size>      -> READY ; binary upload ; PT_VERIFIED|PT_INVALID:<reason>
  COMMIT_PARTITION_TABLE      -> PT_COMMITTED|PT_COMMIT_FAILED:<reason>
  ABORT_PARTITION_TABLE       -> PT_ABORTED
  RAW_ASSETS_WRITE <off> <sz> -> READY ; binary upload ; RAW_OK <off> <sz>|RAW_ERROR:<reason>
  RAW_ASSETS_FINALIZE         -> RAW_FINALIZED
*/

application.register(
  'system-update',
  class extends BaseController {
    static targets = [
      'bundleSelect',
      'bundleInfo',
      'startBtn',
      'resumeBtn',
      'cancelBtn',
      'state',
      'stepList',
      'progressBar',
      'progressFill',
      'progressLabel',
      'logContent',
      'commitDialog',
      'commitConfirmInput',
      'commitConfirmBtn',
      'commitCancelBtn'
    ]

    static STORAGE_KEY = 'storm-summoner.system_update.state'
    static RAW_CHUNK_SIZE = 32 * 1024  // 32 KB; sized for CDC throughput, not flash sectors
    static FW_CHUNK_SIZE = 4 * 1024
    static REBOOT_TIMEOUT_MS = 60_000
    static COMMIT_CONFIRMATION_PHRASE = 'COMMIT'

    // Linear state machine. Each value is a stable string; we persist these
    // verbatim so a session resumed from localStorage knows exactly which
    // step to restart. Steps before COMMITTING_PT are idempotent on the
    // device side (re-uploading shared assets / firmware / candidate PT just
    // overwrites previous bytes); WAITING_REBOOT and beyond require physical
    // device state and can't be replayed.
    static STATE = {
      IDLE: 'IDLE',
      CHECKING_VERSION: 'CHECKING_VERSION',
      UPLOADING_ASSETS: 'UPLOADING_ASSETS',
      UPLOADING_FIRMWARE: 'UPLOADING_FIRMWARE',
      UPLOADING_PT: 'UPLOADING_PT',
      AWAITING_COMMIT_CONFIRMATION: 'AWAITING_COMMIT_CONFIRMATION',
      COMMITTING_PT: 'COMMITTING_PT',
      WAITING_REBOOT: 'WAITING_REBOOT',
      VERIFYING: 'VERIFYING',
      DONE: 'DONE',
      FAILED: 'FAILED'
    }

    connect () {
      this.releases = null
      this.bundle = null              // currently selected `system_update` entry
      this.session = this.loadSession() // {bundleKey, state, progress: {assetsBytesSent, fwBytesSent}}
      this.deviceVersion = null
      this.busy = false

      this.fetchReleases()

      this.connection.on('connection:changed', this.onConnectionChanged.bind(this))
      this.connection.on('mode:changed', ({ mode }) => {
        // We keep client mode null and use the rx pump (Chromium-safe). Abort
        // only if another tab steals an exclusive mode out from under us.
        if (this.busy && mode !== null) this.abortInFlight('mode changed')
      })

      document.addEventListener('device:info', (e) => {
        if (e.detail?.version) this.deviceVersion = e.detail.version
        this.refreshUi()
      })
    }

    disconnect () { /* persistent listeners are fine */ }

    // ------------------------------------------------------------------
    // localStorage session
    // ------------------------------------------------------------------

    loadSession () {
      try {
        const raw = localStorage.getItem(this.constructor.STORAGE_KEY)
        if (!raw) return this.freshSession()
        const parsed = JSON.parse(raw)
        if (!parsed.state) return this.freshSession()
        return parsed
      } catch {
        return this.freshSession()
      }
    }

    freshSession () {
      return {
        bundleKey: null,
        state: this.constructor.STATE.IDLE,
        progress: { assetsBytesSent: 0, fwBytesSent: 0 },
        lastError: null,
        failedAt: null
      }
    }

    // A FAILED session is resumable if we know which step it died in.
    // failedAt is recorded at failure time; for sessions saved before that
    // field existed, infer the step from the checkpointed progress.
    resumableFailedState () {
      const S = this.constructor.STATE
      if (this.session.state !== S.FAILED) return null
      if (this.session.failedAt) return this.session.failedAt
      if (this.session.progress?.fwBytesSent > 0) return S.UPLOADING_FIRMWARE
      if (this.session.progress?.assetsBytesSent > 0) return S.UPLOADING_ASSETS
      return null
    }

    saveSession () {
      try {
        localStorage.setItem(
          this.constructor.STORAGE_KEY,
          JSON.stringify(this.session)
        )
      } catch { /* quota errors are non-fatal */ }
    }

    setState (newState, extra = {}) {
      this.session.state = newState
      Object.assign(this.session, extra)
      this.saveSession()
      this.refreshUi()
    }

    clearSession () {
      this.session = this.freshSession()
      this.saveSession()
      this.refreshUi()
    }

    // ------------------------------------------------------------------
    // releases.json + bundle selection
    // ------------------------------------------------------------------

    async fetchReleases () {
      try {
        const response = await fetch('/releases.json')
        if (!response.ok) throw new Error(`HTTP ${response.status}`)
        this.releases = await response.json()
        this.populateBundleSelect()
      } catch (err) {
        this.log(`Failed to load releases: ${err.message}`, 'error')
      }
    }

    bundleKey (bundle) {
      // A bundle is uniquely identified by its three artifact names. Used
      // both as <wa-option value=...> and as the localStorage session key
      // so a partial run can be matched back to a specific bundle.
      return [bundle.firmware, bundle.partition_table, bundle.shared_assets].join('|')
    }

    populateBundleSelect () {
      const list = this.releases?.system_update || []
      if (!list.length) {
        this.bundleSelectTarget.innerHTML =
          '<wa-option disabled>No system_update bundles in releases.json</wa-option>'
        return
      }
      this.bundleSelectTarget.innerHTML = list.map(b => {
        const label = `v${b.firmware_version} (${b.date})`
        return `<wa-option value="${this.bundleKey(b)}">${label}</wa-option>`
      }).join('')

      // Pick a default selection so Start enables without forcing the user
      // to actively click the dropdown. wa-select needs the value attribute
      // set after the slotted options have rendered, so we wait one tick.
      // Priority: an in-flight session > the first bundle in the list.
      const desired = this.session.bundleKey || this.bundleKey(list[0])
      requestAnimationFrame(() => {
        try {
          this.bundleSelectTarget.value = desired
        } catch (e) { /* element may have been replaced */ }
        this.onBundleSelected()
      })
    }

    onBundleSelected () {
      // wa-select.value is the canonical source, but during initial render
      // and in some browsers the property hasn't reflected the picked option
      // yet -- fall back to the selectedOptions collection in that case.
      let key = this.bundleSelectTarget.value
      if (!key && this.bundleSelectTarget.selectedOptions?.length) {
        key = this.bundleSelectTarget.selectedOptions[0].value
      }
      const list = this.releases?.system_update || []
      this.bundle = list.find(b => this.bundleKey(b) === key) || null
      if (this.bundle) {
        this.bundleInfoTarget.innerHTML = `
          <div><strong>Firmware:</strong> ${this.bundle.firmware} (v${this.bundle.firmware_version})</div>
          <div><strong>Partition table:</strong> ${this.bundle.partition_table}</div>
          <div><strong>Shared assets:</strong> ${this.bundle.shared_assets}</div>
        `
      } else {
        this.bundleInfoTarget.innerHTML = ''
      }
      this.refreshUi()
    }

    // ------------------------------------------------------------------
    // UI plumbing
    // ------------------------------------------------------------------

    onConnectionChanged ({ connected }) {
      if (!connected && this.busy) {
        // Don't set FAILED -- a USB disconnect (intentional or otherwise) is
        // recoverable. We just clear busy and leave the saved state alone so
        // the next Start can resume from the same point.
        this.busy = false
        this.log('Device disconnected mid-flight; saved state preserved for resume.', 'warning')
      }
      this.refreshUi()
    }

    refreshUi () {
      const S = this.constructor.STATE
      const haveBundle = !!this.bundle
      const haveDevice = this.connection.isConnected
      const inFlight = this.busy
      const matchesSession = haveBundle
        && this.session.bundleKey === this.bundleKey(this.bundle)

      this.stateTarget.textContent = this.session.state
        + (this.session.lastError ? ` (last error: ${this.session.lastError})` : '')
      // Start: smart -- if a matching in-flight session exists it resumes,
      // otherwise it begins a fresh run. See startUpdate().
      this.startBtnTarget.disabled = inFlight || !haveBundle || !haveDevice
      // Resume: redundant with smart-Start now, but kept as an explicit
      // affordance for users who want to make their intent clear. Only
      // enabled when there IS something to resume.
      this.resumeBtnTarget.disabled = inFlight || !haveDevice
        || !matchesSession
        || this.session.state === S.IDLE
        || this.session.state === S.DONE
        || (this.session.state === S.FAILED && !this.resumableFailedState())
      // Cancel: useful any time there's work to throw away.
      this.cancelBtnTarget.disabled = !inFlight && this.session.state === S.IDLE

      this.renderStepList()
    }

    renderStepList () {
      const S = this.constructor.STATE
      const order = [
        [S.CHECKING_VERSION, 'Pre-flight checks'],
        [S.UPLOADING_ASSETS, 'Upload shared assets via RAW_ASSETS_WRITE'],
        [S.UPLOADING_FIRMWARE, 'Upload + commit firmware (FIRMWARE OTA)'],
        [S.UPLOADING_PT, 'Upload + verify candidate partition table'],
        [S.COMMITTING_PT, 'Commit partition table to flash 0x8000'],
        [S.WAITING_REBOOT, 'Wait for reboot'],
        [S.VERIFYING, 'Verify v(N+2) is live']
      ]
      const stateIdx = order.findIndex(([s]) => s === this.session.state)
      const html = order.map(([s, label], i) => {
        let cls = 'pending'
        if (this.session.state === S.DONE) cls = 'done'
        else if (this.session.state === S.FAILED && i === stateIdx) cls = 'failed'
        else if (i < stateIdx) cls = 'done'
        else if (i === stateIdx) cls = 'active'
        return `<li class="step ${cls}">${label}</li>`
      }).join('')
      this.stepListTarget.innerHTML = html
    }

    setProgress (pct, label) {
      this.progressFillTarget.style.width = `${Math.max(0, Math.min(100, pct))}%`
      if (label) this.progressLabelTarget.textContent = label
    }

    // ------------------------------------------------------------------
    // High-level flow
    // ------------------------------------------------------------------

    async startUpdate () {
      if (!this.bundle) return
      const S = this.constructor.STATE
      const matches = this.session.bundleKey === this.bundleKey(this.bundle)
      const reusable = matches
        && this.session.state !== S.IDLE
        && this.session.state !== S.DONE
        && (this.session.state !== S.FAILED || !!this.resumableFailedState())
      if (reusable) {
        // Carry on from where we left off rather than redoing all the
        // uploads. The user expectation after a refresh is "pick up
        // where you were", not "start over".
        this.log(`Resuming from state ${this.session.state}`, 'info')
        await this.resumeFromState()
        return
      }
      // Fresh run -- either no prior session, or the prior one finished/
      // failed cleanly. Reset and go.
      this.session = {
        ...this.freshSession(),
        bundleKey: this.bundleKey(this.bundle)
      }
      this.saveSession()
      await this.runFromCurrentState()
    }

    async resumeUpdate () {
      if (!this.bundle) return
      if (this.session.bundleKey !== this.bundleKey(this.bundle)) {
        this.log('Cannot resume: selected bundle does not match the in-flight session.', 'error')
        return
      }
      await this.resumeFromState()
    }

    // Pick the right re-entry point for the saved state. Most states just
    // hand off to runFromCurrentState; AWAITING_COMMIT_CONFIRMATION is
    // special because the staged PT lives in PSRAM on the device and may
    // not survive a host or device reboot -- so we rewind one step and
    // re-upload before reopening the dialog.
    async resumeFromState () {
      const S = this.constructor.STATE
      if (this.session.state === S.FAILED) {
        let target = this.resumableFailedState()
        if (!target) {
          this.log('Cannot resume: failed session has no recorded step.', 'error')
          return
        }
        // A PT commit failure can't be replayed directly (the staged PT in
        // PSRAM may be gone); rewind to re-upload + re-verify instead.
        if (target === S.COMMITTING_PT || target === S.AWAITING_COMMIT_CONFIRMATION) {
          target = S.UPLOADING_PT
        }
        this.log(`Resuming failed session from ${target}`)
        this.setState(target, { failedAt: null, lastError: null })
      }
      if (this.session.state === S.AWAITING_COMMIT_CONFIRMATION) {
        this.log('Re-uploading partition table before reopening commit dialog.')
        this.setState(S.UPLOADING_PT)
      }
      await this.runFromCurrentState()
    }

    async cancelUpdate () {
      // Best-effort device-side cleanup. We never block on the response
      // because the device may already be wedged in any of these modes.
      try { await this.connection.sendRaw('ABORT_PARTITION_TABLE\n') } catch {}
      try { await this.connection.sendRaw('RAW_ASSETS_FINALIZE\n') } catch {}
      this.busy = false
      this.clearSession()
      this.log('Cancelled.', 'warning')
    }

    abortInFlight (why) {
      this.busy = false
      const failedAt = this.session.state
      this.session.lastError = why
      this.setState(this.constructor.STATE.FAILED, { failedAt })
      this.log(`Aborted: ${why}`, 'error')
    }

    async runFromCurrentState () {
      const S = this.constructor.STATE
      this.busy = true
      this.refreshUi()

      try {
        // Do NOT requestMode('UPDATE'): that path cancels the rx pump and
        // uses a dedicated reader, which hits a Chromium Web Serial stall
        // where read() never wakes on newly arrived bytes (READY is sent by
        // the device but never observed). Stay in mode=null and use the
        // pump-based helpers, same pattern as scene.js SCENE_PUT.
        this.connection.setTabsLocked(true, 'system-update')
        await this.connection.runSerialTask(async () => {
          await this.connection.ensureDeviceIdle()
        })

        const flow = [
          { from: S.IDLE,                run: () => this.stepCheckingVersion() },
          { from: S.CHECKING_VERSION,    run: () => this.stepCheckingVersion() },
          { from: S.UPLOADING_ASSETS,    run: () => this.stepUploadAssets() },
          { from: S.UPLOADING_FIRMWARE,  run: () => this.stepUploadFirmware() },
          { from: S.UPLOADING_PT,        run: () => this.stepUploadPt() },
          { from: S.COMMITTING_PT,       run: () => this.stepCommitPt() },
          { from: S.WAITING_REBOOT,      run: () => this.stepWaitReboot() },
          { from: S.VERIFYING,           run: () => this.stepVerify() }
        ]

        while (true) {
          const cur = this.session.state
          if (cur === S.DONE || cur === S.FAILED || cur === S.AWAITING_COMMIT_CONFIRMATION) break
          const step = flow.find(s => s.from === cur)
          if (!step) {
            this.log(`Nothing to do for state ${cur}`, 'warning')
            break
          }
          this.session.lastError = null
          this.saveSession()
          await step.run()
          if (this.session.state === cur) break
        }
      } catch (err) {
        if (!this.connection.isConnected) {
          this.log(`Flow interrupted by disconnect: ${err.message}`, 'warning')
        } else {
          // Remember which step was running so Resume can rewind to it;
          // uploads are chunk-checkpointed, so a transient protocol error
          // should not force a from-scratch rerun.
          const failedAt = this.session.state
          this.session.lastError = err.message
          this.setState(this.constructor.STATE.FAILED, { failedAt })
          this.log(`Flow error: ${err.message}`, 'error')
        }
      } finally {
        this.busy = false
        this.connection.setTabsLocked(false)
        this.refreshUi()
      }
    }

    // ------------------------------------------------------------------
    // Steps
    // ------------------------------------------------------------------

    async stepCheckingVersion () {
      // Persist CHECKING_VERSION before any I/O so a disconnect mid-INFO/DF
      // leaves the saved session in a resumable state instead of stranded
      // back in IDLE (which startUpdate treats as "fresh run, restart").
      this.setState(this.constructor.STATE.CHECKING_VERSION)
      this.log('Pre-flight: checking firmware version + DF...')
      this.setProgress(0, 'pre-flight checks')

      const infoRaw = await this.connection.runSerialTask(async () => {
        await this.connection.ensureDeviceIdle()
        return this.connection._sendCommandViaPump(
          'INFO',
          5_000,
          data => typeof data?.version === 'string'
        )
      })
      this.log(`HOST: INFO`)
      this.log(`DEVICE: ${infoRaw || '(empty)'}`)
      if (!infoRaw || infoRaw.startsWith('ERROR')) {
        throw new Error(`INFO failed: ${infoRaw}`)
      }
      let info
      try { info = JSON.parse(infoRaw) } catch (e) {
        throw new Error('INFO did not return JSON; unit may not be v(N+2). '
          + 'Update firmware via the Updater tab first.')
      }
      this.log(`Device firmware: ${info.version} build ${info.build}`)

      const dfRaw = await this.runInAssetsMode(async () => {
        await this.connection.sendRaw('DF\n')
        return this.connection._readJsonLineViaPump(5_000)
      })
      this.log(`HOST: DF`)
      this.log(`DEVICE: ${dfRaw || '(empty)'}`)

      if (!dfRaw || dfRaw.startsWith('ERROR')) {
        throw new Error(`DF failed: ${dfRaw}`)
      }
      let df
      try { df = JSON.parse(dfRaw) } catch (e) {
        throw new Error('DF response not JSON; unit may not be v(N+2). '
          + 'Update firmware via the Updater tab first.')
      }
      if (!df.assets) throw new Error('DF missing assets entry')
      this.log(`Device assets partition: ${this.formatSize(df.assets.total)} total, `
        + `userdata.available=${df.userdata?.available}`)

      if (df.userdata?.available === true) {
        this.log('Device already has /userdata mounted; layout migration already done.')
        this.setState(this.constructor.STATE.DONE)
        return
      }
      this.setState(this.constructor.STATE.UPLOADING_ASSETS,
        { progress: { ...this.session.progress, dfTotalAssets: df.assets.total } })
    }

    // Enter ASSETS mode on the device, run fn (must use private pump helpers,
    // not public sendCommand/readLine), then EXIT. Entire sequence is one
    // serial task so nothing can interleave.
    async runInAssetsMode (fn) {
      return this.connection.runSerialTask(async () => {
        await this.connection.ensureDeviceIdle()
        await this.connection.sendRaw('ASSETS\n')
        const started = await this.readPumpUntil(['ASSETS_STARTED'], 5_000)
        if (started !== 'ASSETS_STARTED') {
          throw new Error(`Could not enter ASSETS mode: ${started}`)
        }
        try {
          return await fn()
        } finally {
          try {
            await this.connection.sendRaw('EXIT\n')
            await this.readPumpLine(2_000)
          } catch (e) { /* best-effort */ }
        }
      })
    }

    async stepUploadAssets () {
      const url = `/binaries/${this.bundle.shared_assets}`
      this.log(`Downloading shared assets blob: ${this.bundle.shared_assets}`)
      const blob = await this.fetchBinary(url)
      this.log(`Shared assets is ${this.formatSize(blob.length)}`)

      const startOffset = this.session.progress.assetsBytesSent || 0
      if (startOffset > 0) {
        this.log(`Resuming shared-assets upload at offset ${startOffset}`)
      }
      const chunkSize = this.constructor.RAW_CHUNK_SIZE

      for (let off = startOffset; off < blob.length; off += chunkSize) {
        const len = Math.min(chunkSize, blob.length - off)
        const chunk = blob.slice(off, off + len)
        const reply = await this.connection.runSerialTask(async () => {
          await this.armPump()
          this.log(`HOST: RAW_ASSETS_WRITE ${off} ${len}`)
          await this.connection.sendRaw(`RAW_ASSETS_WRITE ${off} ${len}\n`)
          // Prefix-filtered wait: stray notify/log lines are skipped instead
          // of being mistaken for the protocol reply (a truncated EVT:clock
          // once aborted a run at 7.1 of 8 MB this way).
          const ready = await this.readPumpUntil(['READY', 'RAW_ERROR'], 10_000)
          if (ready !== 'READY') {
            throw new Error(`RAW_ASSETS_WRITE refused at offset ${off}: ${ready}`)
          }
          await this.connection.sendBinary(chunk)
          return this.readPumpUntil(['RAW_OK', 'RAW_ERROR'], 15_000)
        })
        if (!reply || !reply.startsWith('RAW_OK')) {
          throw new Error(`RAW_ASSETS_WRITE failed at offset ${off}: ${reply}`)
        }
        const newOff = off + len
        this.session.progress.assetsBytesSent = newOff
        this.saveSession()
        this.setProgress((newOff / blob.length) * 100,
          `assets ${this.formatSize(newOff)} / ${this.formatSize(blob.length)}`)
      }

      const csumMatch = this.bundle.shared_assets.match(/-([a-f0-9]{8})\.bin$/i)
      const csum = csumMatch ? csumMatch[1].toLowerCase() : ''
      const finalizeCmd = csum
        ? `RAW_ASSETS_FINALIZE ${csum}`
        : 'RAW_ASSETS_FINALIZE'
      const finalize = await this.sendPlainCommand(finalizeCmd, 5_000, ['RAW_FINALIZED'])
      if (finalize !== 'RAW_FINALIZED') {
        throw new Error(`RAW_ASSETS_FINALIZE failed: ${finalize}`)
      }
      this.log(csum
        ? `Shared assets uploaded (checksum ${csum}).`
        : 'Shared assets uploaded.', 'success')
      this.setState(this.constructor.STATE.UPLOADING_FIRMWARE)
    }

    async stepUploadFirmware () {
      const url = `/binaries/${this.bundle.firmware}`
      this.log(`Downloading firmware: ${this.bundle.firmware}`)
      const blob = await this.fetchBinary(url)
      this.log(`Firmware is ${this.formatSize(blob.length)}`)

      this.session.progress.fwBytesSent = 0
      this.saveSession()
      this.setProgress(0, 'firmware upload')

      await this.connection.runSerialTask(async () => {
        await this.armPump()
        this.log(`HOST: FIRMWARE ${blob.length}`)
        await this.connection.sendRaw(`FIRMWARE ${blob.length}\n`)
        const start = await this.readPumpUntil(['READY'], 10_000)
        if (start !== 'READY') {
          throw new Error(`FIRMWARE refused: ${start}`)
        }

        const chunkSize = this.constructor.FW_CHUNK_SIZE
        for (let off = 0; off < blob.length; off += chunkSize) {
          const len = Math.min(chunkSize, blob.length - off)
          await this.connection.sendBinary(blob.slice(off, off + len))
          this.setProgress((off + len) / blob.length * 100,
            `firmware ${this.formatSize(off + len)} / ${this.formatSize(blob.length)}`)
        }

        const tc = await this.readPumpUntil(['TRANSFER_COMPLETE'], 120_000)
        if (tc !== 'TRANSFER_COMPLETE') {
          throw new Error(`Expected TRANSFER_COMPLETE, got: ${tc}`)
        }
        this.log('Firmware uploaded; committing...')
        await this.connection.sendRaw('COMMIT\n')

        const success = await this.readPumpUntil(['SUCCESS'], 120_000)
        if (success !== 'SUCCESS') {
          throw new Error(`Firmware COMMIT failed: ${success}`)
        }
      })
      this.log('Firmware committed; boot partition switched to v(N+2).', 'success')
      this.setState(this.constructor.STATE.UPLOADING_PT)
    }

    async stepUploadPt () {
      const url = `/binaries/${this.bundle.partition_table}`
      this.log(`Downloading partition table: ${this.bundle.partition_table}`)
      const blob = await this.fetchBinary(url)
      this.log(`Partition table is ${blob.length} bytes`)

      await this.connection.runSerialTask(async () => {
        await this.armPump()
        this.log(`HOST: PARTITION_TABLE ${blob.length}`)
        await this.connection.sendRaw(`PARTITION_TABLE ${blob.length}\n`)
        const ready = await this.readPumpUntil(['READY'], 10_000)
        if (ready !== 'READY') {
          throw new Error(`PARTITION_TABLE refused: ${ready}`)
        }
        await this.connection.sendBinary(blob)
        const verdict = await this.readPumpUntil(['PT_VERIFIED', 'PT_INVALID'], 15_000)
        if (verdict !== 'PT_VERIFIED') {
          throw new Error(`Partition table not verified: ${verdict}`)
        }
      })
      this.log('Candidate partition table verified.', 'success')
      this.setState(this.constructor.STATE.AWAITING_COMMIT_CONFIRMATION)
      this.openCommitDialog()
    }

    openCommitDialog () {
      // Just gate the rest of the flow on user input. The dialog wires a
      // button that calls confirmCommit() directly, which kicks off
      // stepCommitPt() and the rest of the flow.
      this.log('Awaiting hard confirmation before partition-table commit.')
      this.commitConfirmInputTarget.value = ''
      this.commitConfirmBtnTarget.disabled = true
      this.commitDialogTarget.open = true
    }

    onCommitConfirmInput () {
      const phrase = this.constructor.COMMIT_CONFIRMATION_PHRASE
      this.commitConfirmBtnTarget.disabled =
        (this.commitConfirmInputTarget.value || '').trim().toUpperCase() !== phrase
    }

    async cancelCommit () {
      this.commitDialogTarget.open = false
      try { await this.connection.sendRaw('ABORT_PARTITION_TABLE\n') } catch {}
      this.log('User declined commit; staged partition table aborted.', 'warning')
      // We stay in AWAITING_COMMIT_CONFIRMATION so resume re-uploads PT.
      // Reset to UPLOADING_PT so resume re-issues PARTITION_TABLE.
      this.setState(this.constructor.STATE.UPLOADING_PT)
    }

    async confirmCommit () {
      this.commitDialogTarget.open = false
      this.setState(this.constructor.STATE.COMMITTING_PT)
      // Re-enter the flow loop from COMMITTING_PT.
      await this.runFromCurrentState()
    }

    async stepCommitPt () {
      this.log('Committing partition table to flash 0x8000. DO NOT POWER OFF.')
      const reply = await this.sendPlainCommand('COMMIT_PARTITION_TABLE', 30_000,
        ['PT_COMMITTED', 'PT_COMMIT_FAILED'])
      if (reply !== 'PT_COMMITTED') {
        throw new Error(`Partition-table commit failed: ${reply}`)
      }
      this.log('Partition table committed.', 'success')
      this.setState(this.constructor.STATE.WAITING_REBOOT)
    }

    async stepWaitReboot () {
      this.log('Waiting for device reboot. The web-serial port will drop.')
      // The device may or may not RESET on its own depending on firmware.
      // Either way we wait for the port to disappear, then for the user to
      // reconnect manually (which fires connection:changed).
      const start = Date.now()
      while (this.connection.isConnected
        && Date.now() - start < this.constructor.REBOOT_TIMEOUT_MS) {
        await this.sleep(500)
      }
      if (this.connection.isConnected) {
        this.log('Device did not drop within timeout. Issuing RESET as a fallback.', 'warning')
        try { await this.connection.sendRaw('RESET\n') } catch {}
      }
      // We can't auto-reconnect (WebSerial requires a user gesture); ask
      // the user to reconnect, which will resume into VERIFYING.
      this.log('Reconnect the device when ready, then press Resume.', 'warning')
      this.setState(this.constructor.STATE.VERIFYING)
    }

    async stepVerify () {
      if (!this.connection.isConnected) {
        this.log('Not connected yet; waiting for reconnect to verify.', 'warning')
        return
      }
      this.log('Verifying that v(N+2) is live with the new partition layout...')
      const dfRaw = await this.runInAssetsMode(async () => {
        await this.connection.sendRaw('DF\n')
        return this.connection._readJsonLineViaPump(5_000)
      })
      this.log(`HOST: DF`)
      this.log(`DEVICE: ${dfRaw || '(empty)'}`)
      if (!dfRaw || dfRaw.startsWith('ERROR')) {
        throw new Error(`DF failed post-reboot: ${dfRaw}`)
      }
      let df
      try { df = JSON.parse(dfRaw) } catch (e) {
        throw new Error(`DF post-reboot returned non-JSON: ${dfRaw}`)
      }
      if (!df.userdata) {
        throw new Error('DF response missing userdata; firmware did not pick up split.')
      }
      if (df.userdata.available !== true) {
        throw new Error('userdata partition not available after reboot.')
      }
      const expected = 0x800000 // 8 MB shared assets after split
      if (Math.abs(df.assets.total - expected) > 0x1000) {
        this.log(`Warning: assets.total=${df.assets.total}, expected ~${expected}`,
          'warning')
      }
      this.log('System update complete. v(N+2) layout confirmed.', 'success')
      this.setState(this.constructor.STATE.DONE)
    }

    // ------------------------------------------------------------------
    // I/O helpers — pump-based (mode=null). Never use sendCommand /
    // dedicated-reader paths here; they stall under Chromium Web Serial.
    // ------------------------------------------------------------------

    async fetchBinary (url) {
      const response = await fetch(url)
      if (!response.ok) throw new Error(`HTTP ${response.status} fetching ${url}`)
      return new Uint8Array(await response.arrayBuffer())
    }

    async armPump () {
      if (this.connection.mode !== null) {
        await this.connection.ensureDeviceIdle()
      } else {
        await this.connection._armRxPump(500)
      }
      this.connection.clearPendingRx()
    }

    // Must be called from inside a runSerialTask body.
    async readPumpLine (timeoutMs) {
      const deadline = Date.now() + timeoutMs
      while (Date.now() < deadline) {
        const line = await this.connection._readPumpLineBody(
          Math.min(1000, Math.max(1, deadline - Date.now())))
        if (!line) continue
        if (line.startsWith('EVT:')) continue
        if (/^I \(/.test(line)) continue
        return line
      }
      return ''
    }

    // Must be called from inside a runSerialTask body. Waits for a line
    // matching one of wantedPrefixes, logging and skipping anything else
    // (device logs, stray or corrupted EVT: notifies). Throws on ERROR
    // lines and on timeout; unlisted failure verdicts (RAW_ERROR,
    // PT_INVALID, ...) should be included in wantedPrefixes so the caller
    // sees them and produces a step-specific error.
    async readPumpUntil (wantedPrefixes, timeoutMs) {
      const deadline = Date.now() + timeoutMs
      while (Date.now() < deadline) {
        const line = await this.readPumpLine(Math.min(5_000, deadline - Date.now()))
        if (!line) continue
        this.log(`DEVICE: ${line}`)
        if (line.startsWith('ERROR')) throw new Error(line)
        for (const want of wantedPrefixes) {
          if (line === want || line.startsWith(want + ' ') || line.startsWith(want + ':')) {
            return line
          }
        }
        if (line.startsWith('PROGRESS ')) {
          const pct = parseInt(line.split(' ')[1])
          if (!isNaN(pct)) this.setProgress(pct, `firmware commit ${pct}%`)
        }
      }
      throw new Error(`Timeout waiting for ${wantedPrefixes.join('/')}`)
    }

    async sendPlainCommand (cmd, timeout, wantedPrefixes = null) {
      this.log(`HOST: ${cmd}`)
      const reply = await this.connection.runSerialTask(async () => {
        await this.armPump()
        await this.connection.sendRaw(cmd + '\n')
        if (wantedPrefixes) return this.readPumpUntil(wantedPrefixes, timeout)
        const line = await this.readPumpLine(timeout)
        this.log(`DEVICE: ${line || '(empty)'}`)
        return line
      })
      return reply
    }
  }
)
