/* Constrained flag threshold selects for continuous CC mappings. */

application.register(
  'flag-thresholds',
  class extends BaseController {
    static values = { path: String }

    changed () {
      const root = this.element.closest('[data-controller~="scene"]')
      if (!root) return
      const scene = this.application.getControllerForElementAndIdentifier(root, 'scene')
      if (!scene?.editModel) return
      const mapping = scene.getAtPath(this.pathValue)
      scene.sanitizeFlagThresholds(mapping)
      scene.markDirty()
      scene.renderEditor()
    }
  }
)
