/* Copy textarea contents to clipboard */

application.register(
  'copy',
  class extends BaseController {
    static targets = ['source']

    async copy (event) {
      const el = this.sourceTarget
      const text = el?.value ?? el?.textContent ?? ''
      if (!text) return

      const btn = event?.currentTarget
      if (!btn) return

      try {
        await navigator.clipboard.writeText(text)
        const prev = btn.textContent?.trim() || 'Copy'
        btn.textContent = 'Copied!'
        setTimeout(() => { btn.textContent = prev }, 1500)
      } catch (err) {
        console.error('Copy failed:', err)
      }
    }
  }
)
