/* Scene Set action helpers (stable manifest index, not list position) */

window.SceneActions = (function () {
  const MAX_INDEX = 127

  function findByIndex (sceneList, index) {
    const n = Number(index)
    if (Number.isNaN(n)) return null
    return (sceneList || []).find(s => Number(s.index) === n) || null
  }

  function currentEditIndex (ctrl) {
    if (!ctrl || ctrl.editPosition === null || ctrl.editPosition === undefined) return null
    const row = (ctrl.sceneList || []).find(
      s => Number(s.position) === Number(ctrl.editPosition)
    )
    return row != null ? Number(row.index) : null
  }

  function eligibleScenes (sceneList, excludeIndex) {
    return (sceneList || []).filter(s => {
      if (!s.active) return false
      const idx = Number(s.index)
      if (excludeIndex !== null && idx === excludeIndex) return false
      return true
    })
  }

  function defaultSetIndex (sceneList, excludeIndex = null) {
    const active = eligibleScenes(sceneList, excludeIndex)
    if (active.length) return Number(active[0].index)
    return 0
  }

  function resolveStoredIndex (sceneList, stored, excludeIndex = null) {
    const fallback = defaultSetIndex(sceneList, excludeIndex)
    if (stored === undefined || stored === null) return fallback
    const n = Number(stored)
    if (Number.isNaN(n)) return fallback
    if (excludeIndex !== null && n === excludeIndex) return fallback
    return n
  }

  function setOptions (sceneList, storedIndex, excludeIndex = null) {
    const resolved = resolveStoredIndex(sceneList, storedIndex, excludeIndex)
    const opts = []
    const seen = new Set()

    for (const s of eligibleScenes(sceneList, excludeIndex)) {
      const idx = Number(s.index)
      if (seen.has(idx)) continue
      seen.add(idx)
      opts.push({ v: idx, l: String(s.name || 'Untitled') })
    }

    if (!seen.has(resolved)) {
      const row = findByIndex(sceneList, resolved)
      let label
      if (excludeIndex !== null && resolved === excludeIndex) {
        label = row ? `${row.name} (this scene)` : 'This scene'
      } else if (row) {
        label = `${row.name} (inactive)`
      } else {
        label = `Missing scene (index ${resolved})`
      }
      opts.unshift({ v: resolved, l: label })
    }

    if (!opts.length) opts.push({ v: resolved, l: `Scene index ${resolved}` })
    return opts
  }

  function forEachAction (model, cb) {
    if (!model) return
    const visit = (action, path) => {
      if (action) cb(action, path)
    }
    model.touchpads?.forEach((tp, i) => visit(tp?.action, `touchpads.${i}.action`))
    visit(model.button_left, 'button_left')
    visit(model.button_right, 'button_right')
    visit(model.button_both, 'button_both')
    visit(model.bump, 'bump')
    model.on_load?.forEach((a, i) => visit(a, `on_load.${i}`))
    model.on_play?.forEach((a, i) => visit(a, `on_play.${i}`))
    model.cc_triggers?.forEach((t, i) => visit(t?.action, `cc_triggers.${i}.action`))
    visit(model.cv_trigger_action, 'cv_trigger_action')
  }

  function seedSceneSetAction (ctrl, actionPath) {
    const action = ctrl.getAtPath(actionPath)
    if (!action || action.type !== 'scene') return
    const variant = action.variant || 'set'
    if (variant !== 'set') return
    const exclude = currentEditIndex(ctrl)
    ctrl.setAtPath(`${actionPath}.number`,
      resolveStoredIndex(ctrl.sceneList, action.number, exclude))
  }

  function validateSceneSetActionsInModel (sceneList, model, excludeIndex = null) {
    const errors = []
    forEachAction(model, (action, path) => {
      if (action.type !== 'scene') return
      if ((action.variant || 'set') !== 'set') return
      const idx = Number(action.number)
      if (Number.isNaN(idx) || !Number.isInteger(idx) || idx < 0 || idx > MAX_INDEX) {
        errors.push({
          path: `${path}.number`,
          message: 'Scene index must be between 0 and 127'
        })
        return
      }
      if (excludeIndex !== null && idx === excludeIndex) {
        errors.push({
          path: `${path}.number`,
          message: 'Cannot set scene to itself'
        })
        return
      }
      const row = findByIndex(sceneList, idx)
      if (!row) {
        errors.push({
          path: `${path}.number`,
          message: 'Target scene no longer exists on the device'
        })
      } else if (!row.active) {
        errors.push({
          path: `${path}.number`,
          message: `Target scene "${row.name}" is inactive`
        })
      }
    })
    return errors
  }

  return {
    currentEditIndex,
    defaultSetIndex,
    resolveStoredIndex,
    setOptions,
    seedSceneSetAction,
    validateSceneSetActionsInModel
  }
})()
