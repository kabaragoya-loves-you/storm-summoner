/* Storm Summoner - Scene Inspect Controller */

application.register(
  'scene',
  class extends BaseController {
    static targets = [
      'inspectView', 'inspectText', 'editView', 'editBtn', 'truncatedBanner'
    ]

    connect () {
      this.editing = false
      this.fetchPending = false
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
    }

    onConnectionChanged ({ connected }) {
      if (!connected) this.renderDisconnected()
    }

    onTabActivated (e) {
      if (e.detail.tab !== 'scene') return
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

    renderDisconnected () {
      this.inspectTextTarget.textContent = ''
      this.inspectTextTarget.classList.add('empty')
      this.inspectTextTarget.textContent = 'Connect to view scene inspect text'
      this.truncatedBannerTarget.classList.add('hidden')
    }

    renderLoading () {
      this.inspectTextTarget.classList.remove('empty')
      this.inspectTextTarget.textContent = 'Loading…'
      this.truncatedBannerTarget.classList.add('hidden')
    }

    renderInspect (text, truncated) {
      this.inspectTextTarget.classList.remove('empty')
      this.inspectTextTarget.textContent = text || '(empty scene)'
      if (truncated) {
        this.truncatedBannerTarget.classList.remove('hidden')
      } else {
        this.truncatedBannerTarget.classList.add('hidden')
      }
    }

    renderError (message) {
      this.inspectTextTarget.classList.remove('empty')
      this.inspectTextTarget.textContent = message
      this.truncatedBannerTarget.classList.add('hidden')
    }

    async fetchInspect () {
      if (!this.connection.isConnected || this.fetchPending) return

      this.fetchPending = true
      this.renderLoading()

      try {
        if (this.connection.currentMode) {
          await this.connection.exitMode()
          await this.sleep(200)
        }

        const response = await this.connection.sendCommand('SCENE_INSPECT', 60000, (data) =>
          typeof data.text === 'string')

        if (!response || response.startsWith('ERROR:')) {
          this.renderError(response || 'No response from device')
          return
        }

        const data = JSON.parse(response)
        this.renderInspect(data.text || '', !!data.truncated)
      } catch (err) {
        console.error('Scene inspect fetch error:', err)
        this.renderError('Failed to load scene inspect text')
      } finally {
        this.fetchPending = false
      }
    }
  }
)
