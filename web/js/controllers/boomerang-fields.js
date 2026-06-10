/* Broadcast Boomerang field edits to the envelope SVG controller */

application.register(
  'boomerang-fields',
  class extends BaseController {
    static values = { path: String }
    static targets = ['envelope']

    notify (e) {
      const fieldPath = e?.target?.dataset?.scenePath
      if (fieldPath && !fieldPath.startsWith(this.pathValue)) return
      this.broadcastUpdate()
    }

    broadcastUpdate () {
      const detail = { path: this.pathValue }
      if (this.hasEnvelopeTarget) {
        this.envelopeTarget.dispatchEvent(new CustomEvent('boomerang-envelope:update', {
          bubbles: true,
          detail
        }))
        return
      }
      const host = this.element.querySelector('[data-controller~="boomerang-envelope"]')
      host?.dispatchEvent(new CustomEvent('boomerang-envelope:update', {
        bubbles: true,
        detail
      }))
    }
  }
)
