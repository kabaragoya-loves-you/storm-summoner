/* Storm Summoner - Scene Inspect Controller */

application.register(
  'scene',
  class extends BaseController {
    static targets = [
      'inspectView', 'inspectText', 'editView', 'editBtn', 'truncatedBanner',
      'copySource', 'copyBtn', 'printBtn'
    ]

    connect () {
      this.editing = false
      this._loadGeneration = 0
      this.notifyDebounce = null
      this._onConnectionChanged = this.onConnectionChanged.bind(this)
      this._onTabActivated = this.onTabActivated.bind(this)
      this._onCdcNotify = this.onCdcNotify.bind(this)

      this.connection.on('connection:changed', this._onConnectionChanged)
      document.addEventListener('app:tab-activated', this._onTabActivated)
      document.addEventListener('cdc:notify', this._onCdcNotify)
    }

    disconnect () {
      this.connection.off('connection:changed', this._onConnectionChanged)
      document.removeEventListener('app:tab-activated', this._onTabActivated)
      document.removeEventListener('cdc:notify', this._onCdcNotify)
      if (this.notifyDebounce) clearTimeout(this.notifyDebounce)
      if (this.printFrame) {
        this.printFrame.remove()
        this.printFrame = null
      }
    }

    onConnectionChanged ({ connected }) {
      if (!connected) this.renderDisconnected()
    }

    onTabActivated (e) {
      if (e.detail.tab !== 'scene') return
      this._loadGeneration++
      if (!this.connection.isConnected) {
        this.renderDisconnected()
        return
      }
      if (!this.editing) this.fetchInspect()
    }

    onCdcNotify (e) {
      const kind = e.detail?.kind
      if (kind !== 'scene_changed' && kind !== 'scene_updated') return
      if (!this.connection.isConnected || this.editing) return

      const activeTab = document.querySelector('wa-tab-group wa-tab[active]')
      if (activeTab?.getAttribute('panel') !== 'scene') return

      if (this.notifyDebounce) clearTimeout(this.notifyDebounce)
      this.notifyDebounce = setTimeout(() => {
        this.notifyDebounce = null
        this.fetchInspect()
      }, 100)
    }

    toggleEdit () {
      this.editing = !this.editing
      this.inspectViewTarget.classList.toggle('hidden', this.editing)
      this.editViewTarget.classList.toggle('hidden', !this.editing)
      this.editBtnTarget.textContent = this.editing ? 'View' : 'Edit'

      if (!this.editing && this.connection.isConnected) this.fetchInspect()
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
    static TWO_COLUMN_MIN_LINES = 16
    static TWO_COLUMN_MIN_CHARS = 900
    static TWO_COLUMN_HEIGHT_RATIO = 0.48

    countLines (text) {
      if (!text) return 0
      return text.split('\n').length
    }

    shouldUseTwoColumns (text, doc) {
      const lineCount = this.countLines(text)
      if (lineCount <= 10) return false
      if (lineCount >= this.constructor.TWO_COLUMN_MIN_LINES) return true
      if (text.length >= this.constructor.TWO_COLUMN_MIN_CHARS) return true

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

      return refHeight >= this.constructor.PRINTABLE_HEIGHT_PX *
        this.constructor.TWO_COLUMN_HEIGHT_RATIO
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
      const twoCol = this.shouldUseTwoColumns(text, doc)
      const { measureTarget, twoCol: isTwo } = this.buildPrintRoot(doc, text, twoCol)
      this.searchFontSize(measureTarget, printableHeight, 4, 72, isTwo)
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
