/* Live LFO waveform SVG for the scene editor */

application.register(
  'lfo-waveform',
  class extends BaseController {
    static values = { cfgKey: String }

    connect () {
      this._onUpdate = this.onUpdate.bind(this)
      this.element.addEventListener('lfo-waveform:update', this._onUpdate)
      this.refresh()
    }

    disconnect () {
      this.element.removeEventListener('lfo-waveform:update', this._onUpdate)
    }

    onUpdate (e) {
      const cfgKey = e.detail?.cfgKey
      if (cfgKey && cfgKey !== this.cfgKeyValue) return
      this.refresh()
    }

    refresh () {
      const scene = this.application.getControllerForElementAndIdentifier(
        this.element.closest('[data-controller~="scene"]') || document.body,
        'scene'
      )
      const model = scene?.editModel ?? scene?.inspectModel
      const cfg = model?.[this.cfgKeyValue]
      if (!cfg?.enabled) {
        this.element.innerHTML = ''
        return
      }
      const ctx = {
        bpm: model?.bpm ?? 120,
        feltBeats: model?.time_signature?.numerator ?? 4
      }
      this.element.innerHTML = window.LfoWaveformPreview.renderSvg(cfg, ctx)
    }
  }
)
