/* Add/remove repeatable input slots (CC parameter pairs, cycle steps, etc).

   Single responsibility: own the +/x affordances for a list of slots and
   emit intent events. The actual model mutation + re-render is handled by the
   central scene controller, keeping one source of truth. The same controller
   serves every place actions are assigned (pads, buttons, bump, chains, CC
   triggers) because it is parameterised entirely through data-* values. */

application.register(
  'slots',
  class extends BaseController {
    static values = {
      path: String,
      kind: { type: String, default: 'control' },
      min: { type: Number, default: 1 },
      max: { type: Number, default: 4 },
      default: { type: Number, default: 0 }
    }

    add (e) {
      e?.preventDefault()
      this.dispatch('add', {
        detail: {
          path: this.pathValue,
          kind: this.kindValue,
          max: this.maxValue,
          default: this.defaultValue
        }
      })
    }

    remove (e) {
      e?.preventDefault()
      const index = Number(e?.params?.index ??
        e?.currentTarget?.dataset?.slotIndex ?? -1)
      if (index < 0) return
      this.dispatch('remove', {
        detail: { path: this.pathValue, kind: this.kindValue, index, min: this.minValue }
      })
    }
  }
)
