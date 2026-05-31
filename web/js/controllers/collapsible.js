/* Toggle collapsible sections (CC rows, discrete value editors) */

application.register(
  'collapsible',
  class extends BaseController {
    static targets = ['panel', 'icon']
    static classes = ['open']

    connect () {
      this.open = this.element.classList.contains('is-open')
        || this.element.hasAttribute('open')
      this.sync()
    }

    toggle (event) {
      event?.preventDefault()
      this.open = !this.open
      this.sync()
    }

    sync () {
      if (this.hasPanelTarget) {
        this.panelTarget.hidden = !this.open
      }
      if (this.hasIconTarget) {
        this.iconTarget.name = this.open ? 'chevron-down' : 'chevron-right'
      }
      this.element.classList.toggle('is-open', this.open)
    }
  }
)
