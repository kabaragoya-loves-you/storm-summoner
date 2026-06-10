/* Live Boomerang envelope SVG for the scene editor */

application.register(
  'boomerang-envelope',
  class extends BaseController {
    static values = { path: String }

    connect () {
      this._onUpdate = this.onUpdate.bind(this)
      this.element.addEventListener('boomerang-envelope:update', this._onUpdate)
      this.refresh()
    }

    disconnect () {
      this.element.removeEventListener('boomerang-envelope:update', this._onUpdate)
    }

    onUpdate (e) {
      const path = e.detail?.path
      if (path && path !== this.pathValue) return
      this.refresh()
    }

    refresh () {
      const scene = this.application.getControllerForElementAndIdentifier(
        this.element.closest('[data-controller~="scene"]') || document.body,
        'scene'
      )
      const action = scene?.getAtPath?.(this.pathValue)
      if (!action || action.type !== 'boomerang') {
        this.element.innerHTML = ''
        return
      }
      this.element.innerHTML = window.BoomerangEnvelope.renderSvg(action)
    }
  }
)
