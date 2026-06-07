/* Reveal/hide dependent fields based on a trigger checkbox.

   Single responsibility: keep [data-reveal-target="content"] elements visible
   only while [data-reveal-target="trigger"] is checked. The trigger still
   patches the model via its own data-action; this controller only owns
   visibility, so toggling never forces a full editor re-render. */

application.register(
  'reveal',
  class extends BaseController {
    static targets = ['trigger', 'content']

    connect () {
      this.sync()
    }

    toggle () {
      this.sync()
    }

    sync () {
      const on = this.hasTriggerTarget && this.triggerTarget.checked
      this.contentTargets.forEach(el => { el.hidden = !on })
    }
  }
)
