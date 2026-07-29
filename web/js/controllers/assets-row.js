/* Per-row checkbox for Assets multi-select.

   Owns the checkbox UI for one deletable file/folder row and emits
   assets-row:toggled so the parent assets controller can track selection.
   Click/change on the checkbox cell stopPropagation so folder rows do not
   navigate when selecting. */

application.register(
  'assets-row',
  class extends BaseController {
    static values = {
      path: String,
      type: { type: String, default: 'file' },
      name: String,
      selected: { type: Boolean, default: false }
    }

    static targets = ['checkbox']

    connect () {
      this.syncCheckbox()
      this.syncSelectedClass()
    }

    selectedValueChanged () {
      this.syncCheckbox()
      this.syncSelectedClass()
    }

    syncCheckbox () {
      if (!this.hasCheckboxTarget) return
      this.checkboxTarget.checked = this.selectedValue
    }

    syncSelectedClass () {
      this.element.classList.toggle('is-selected', this.selectedValue)
    }

    stopPropagation (e) {
      e?.stopPropagation()
    }

    toggle (e) {
      e?.stopPropagation()
      const checked = !!this.checkboxTarget?.checked
      this.selectedValue = checked
      this.dispatch('toggled', {
        detail: {
          path: this.pathValue,
          type: this.typeValue,
          name: this.nameValue,
          selected: checked
        }
      })
    }

    setSelected (selected) {
      const next = !!selected
      if (this.selectedValue === next) {
        this.syncCheckbox()
        return
      }
      this.selectedValue = next
      this.dispatch('toggled', {
        detail: {
          path: this.pathValue,
          type: this.typeValue,
          name: this.nameValue,
          selected: next
        }
      })
    }
  }
)
