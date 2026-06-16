/* Broadcast LFO field edits to the waveform SVG controller */

application.register(
  'lfo-waveform-fields',
  class extends BaseController {
    static values = { cfgKey: String }
    static targets = ['preview']

    connect () {
      this._onChange = this.notify.bind(this)
      this.element.addEventListener('change', this._onChange)
      this.element.addEventListener('input', this._onChange)
    }

    disconnect () {
      this.element.removeEventListener('change', this._onChange)
      this.element.removeEventListener('input', this._onChange)
    }

    notify (e) {
      const fieldPath = e?.target?.dataset?.scenePath
      if (fieldPath && !fieldPath.startsWith(this.cfgKeyValue)) return
      queueMicrotask(() => this.broadcastUpdate())
    }

    broadcastUpdate () {
      const detail = { cfgKey: this.cfgKeyValue }
      if (this.hasPreviewTarget) {
        this.previewTarget.dispatchEvent(new CustomEvent('lfo-waveform:update', {
          bubbles: true,
          detail
        }))
        return
      }
      const host = this.element.querySelector('[data-controller~="lfo-waveform"]')
      host?.dispatchEvent(new CustomEvent('lfo-waveform:update', {
        bubbles: true,
        detail
      }))
    }
  }
)
