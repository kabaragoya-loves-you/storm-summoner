/* Mocked live runtime for the scene editor.

   The web editor has no connected device, so there is no real s_last_cc_values
   to tell us which "mode" a gating CC (e.g. a pedal's Mode selector) is in.
   This controller is attached to the value input of any CC that gates another
   control's x_variants. When that input changes, it writes the new value into
   the scene controller's editor-local runtimeCcValues map and asks it to
   re-render, so dependent fields rebuild with the correct effective options.

   Single responsibility: bridge a gating-CC input edit to the central scene
   controller, which owns the runtime map, the clamp, and the re-render. */

application.register(
  'mock-runtime',
  class extends BaseController {
    static values = { cc: Number }

    update (e) {
      const raw = e?.target?.value
      const value = raw === '' || raw == null ? 0 : Number(raw)
      if (Number.isNaN(value)) return
      const scene = this.sceneController
      if (!scene) return
      scene.setRuntimeCcValue(this.ccValue, value)
    }

    get sceneController () {
      const host = this.element.closest('[data-controller~="scene"]') ||
        document.querySelector('[data-controller~="scene"]')
      if (!host) return null
      return this.application.getControllerForElementAndIdentifier(host, 'scene')
    }
  }
)
