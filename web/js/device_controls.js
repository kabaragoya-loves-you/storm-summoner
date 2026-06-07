/* Device CC parameter/value options (scene editor + shared with pedal defs) */

window.DeviceControls = (function () {
  function findControl (device, ccNum) {
    const n = Number(ccNum)
    if (Number.isNaN(n)) return null
    return (device?.controlChangeCommands || [])
      .find(c => Number(c.controlChangeNumber) === n) || null
  }

  function hasParameters (device) {
    return (device?.controlChangeCommands?.length || 0) > 0
  }

  function firstParameterCc (device) {
    const cmds = device?.controlChangeCommands || []
    return cmds.length ? cmds[0].controlChangeNumber : 0
  }

  function isValidParameterCc (device, ccNum) {
    if (!hasParameters(device)) return true
    const n = Number(ccNum)
    if (Number.isNaN(n)) return false
    return (device.controlChangeCommands || [])
      .some(c => Number(c.controlChangeNumber) === n)
  }

  // Options for a Parameter dropdown. Pass { exclude: Set, keep: cc } to hide
  // parameters already used by sibling slots while always keeping this slot's
  // own current value selectable.
  function parameterOptions (device, opts = {}) {
    const exclude = opts.exclude instanceof Set ? opts.exclude : null
    const keep = opts.keep == null ? null : Number(opts.keep)
    return (device?.controlChangeCommands || [])
      .filter(c => {
        const n = Number(c.controlChangeNumber)
        if (!exclude) return true
        return n === keep || !exclude.has(n)
      })
      .map(c => ({
        v: c.controlChangeNumber,
        l: c.name || `CC ${c.controlChangeNumber}`
      }))
  }

  function parameterCount (device) {
    return device?.controlChangeCommands?.length || 0
  }

  function firstUnusedParameterCc (device, used) {
    for (const c of (device?.controlChangeCommands || [])) {
      if (!used || !used.has(Number(c.controlChangeNumber))) return c.controlChangeNumber
    }
    return firstParameterCc(device)
  }

  function resolveParameterCc (device, currentCc) {
    if (!hasParameters(device)) {
      const cur = Number(currentCc)
      return Number.isNaN(cur) ? 0 : cur
    }
    const cur = currentCc == null || currentCc === '' ? NaN : Number(currentCc)
    if (!Number.isNaN(cur) && isValidParameterCc(device, cur)) return cur
    return firstParameterCc(device)
  }

  function defaultValueForParameter (device, ccNum) {
    const cmd = findControl(device, ccNum)
    if (cmd?.valueRange?.discreteValues?.length) {
      return cmd.valueRange.discreteValues[0].value
    }
    return cmd?.valueRange?.min ?? 0
  }

  function valueOptions (device, ccNum) {
    const cmd = findControl(device, ccNum)

    if (cmd?.valueRange?.discreteValues?.length) {
      return cmd.valueRange.discreteValues.map(dv => ({
        v: dv.value,
        l: dv.name || String(dv.value)
      }))
    }

    let min = 0
    let max = 127
    if (cmd?.valueRange) {
      min = cmd.valueRange.min ?? 0
      max = Math.min(cmd.valueRange.max ?? 127, 127)
    }
    if (min > max) [min, max] = [0, 127]

    const opts = []
    for (let i = min; i <= max; i++) {
      opts.push({ v: i, l: String(i) })
    }
    return opts
  }

  function resolveParameterValue (device, ccNum, currentValue) {
    const opts = valueOptions(device, ccNum)
    if (!opts.length) return 0
    const cur = currentValue == null || currentValue === '' ? NaN : Number(currentValue)
    if (!Number.isNaN(cur) && opts.some(o => Number(o.v) === cur)) return cur
    return opts[0].v
  }

  function toList (v) {
    if (Array.isArray(v)) return v.slice()
    if (v == null) return []
    return [v]
  }

  // Number of parameter slots an action occupies (>=1). Driven by the cc field
  // since cc/value/value2 stay index-aligned.
  function controlSlotCount (action) {
    if (!action) return 1
    const n = Array.isArray(action.cc) ? action.cc.length : 1
    return Math.max(1, Math.min(4, n || 1))
  }

  function ccForSlot (action, index) {
    return Array.isArray(action?.cc) ? action.cc[index] : action?.cc
  }

  // Collapse a length-1 array to a scalar so single-slot actions keep the
  // firmware's backward-compatible simple shape; keep arrays when multi-slot.
  function packField (list) {
    return list.length <= 1 ? list[0] : list
  }

  function clampMidi (v) {
    const n = Number(v)
    if (Number.isNaN(n)) return 0
    return Math.max(0, Math.min(127, n))
  }

  function parameterLabel (device, ccNum) {
    const cmd = findControl(device, ccNum)
    return cmd?.name || `CC ${ccNum}`
  }

  function cycleIsMultiValues (values) {
    return Array.isArray(values?.[0])
  }

  function cycleStepCount (action) {
    const v = action?.values
    if (!v || !Array.isArray(v)) return 2
    if (cycleIsMultiValues(v)) {
      const n = v.reduce((m, row) => Math.max(m, Array.isArray(row) ? row.length : 0), 0)
      return Math.max(2, Math.min(8, n || 2))
    }
    return Math.max(2, Math.min(8, v.length || 2))
  }

  function cycleSlotCount (action) {
    if (cycleIsMultiValues(action?.values)) {
      return Math.max(1, Math.min(4, action.values.length))
    }
    return controlSlotCount(action)
  }

  function cycleStepsForSlot (action, slotIndex) {
    const v = action?.values
    if (!v) return []
    if (cycleIsMultiValues(v)) {
      return Array.isArray(v[slotIndex]) ? v[slotIndex].slice() : []
    }
    return slotIndex === 0 ? toList(v) : []
  }

  function normalizeCycleValues (device, action, ccResolved) {
    const slots = ccResolved.length
    const stepCount = Math.max(2, Math.min(8, cycleStepCount(action)))
    const defFor = (cc) => resolveParameterValue(device, cc, null)

    if (slots === 1) {
      let steps = cycleIsMultiValues(action.values)
        ? toList(action.values[0])
        : toList(action.values)
      steps = steps.slice(0, stepCount).map(clampMidi)
      while (steps.length < stepCount) steps.push(defFor(ccResolved[0]))
      action.values = steps.map(v => resolveParameterValue(device, ccResolved[0], v))
      return
    }

    let matrix = cycleIsMultiValues(action.values)
      ? action.values.map(row => toList(row))
      : [toList(action.values)]
    while (matrix.length < slots) matrix.push([])
    matrix = matrix.slice(0, slots).map((row, i) => {
      let steps = row.slice(0, stepCount).map(clampMidi)
      while (steps.length < stepCount) steps.push(defFor(ccResolved[i]))
      return steps.map(v => resolveParameterValue(device, ccResolved[i], v))
    })
    action.values = matrix
  }

  function normalizeControlAction (device, action) {
    if (!action || action.type !== 'control') return false
    if (!hasParameters(device)) return false

    const variant = action.variant || 'set'
    const before = JSON.stringify([action.cc, action.value, action.value2, action.values])

    const ccList = toList(action.cc)
    if (!ccList.length) ccList.push(undefined)
    const slots = Math.min(ccList.length, 4)
    const ccResolved = ccList.slice(0, slots).map(c => resolveParameterCc(device, c))
    action.cc = packField(ccResolved)

    if (variant === 'set' || variant === 'hold') {
      const valList = toList(action.value)
      const valResolved = ccResolved.map((cc, i) =>
        resolveParameterValue(device, cc, valList[i]))
      action.value = packField(valResolved)
    }
    if (variant === 'hold') {
      const rel = toList(action.value2)
      const relResolved = ccResolved.map((cc, i) =>
        resolveParameterValue(device, cc, rel[i]))
      action.value2 = packField(relResolved)
    }
    if (variant === 'cycle') {
      normalizeCycleValues(device, action, ccResolved)
    }

    return JSON.stringify([action.cc, action.value, action.value2, action.values]) !== before
  }

  function normalizeControlActionsInModel (device, model) {
    if (!model || !hasParameters(device)) return false
    let changed = false
    const fix = (action) => {
      if (normalizeControlAction(device, action)) changed = true
    }
    model.touchpads?.forEach(tp => fix(tp.action))
    fix(model.button_left)
    fix(model.button_right)
    fix(model.button_both)
    fix(model.bump)
    model.on_load?.forEach(fix)
    model.on_play?.forEach(fix)
    model.cc_triggers?.forEach(t => fix(t?.action))
    fix(model.cv_trigger_action)
    return changed
  }

  function seedControlAction (ctrl, actionPath) {
    const device = ctrl.deviceDefinition
    if (!hasParameters(device)) return
    const action = ctrl.getAtPath(actionPath) || {}
    const cc = resolveParameterCc(device, ccForSlot(action, 0))
    ctrl.setAtPath(`${actionPath}.cc`, cc)
    if (!['set', 'hold', 'cycle'].includes(action.variant)) {
      ctrl.setAtPath(`${actionPath}.variant`, 'set')
    }
    const variant = ctrl.getAtPath(`${actionPath}.variant`) || 'set'
    // Drop fields that don't belong to the (possibly just-changed) variant so
    // switching set/hold/cycle never leaves stale CC payload behind.
    if (variant !== 'cycle') delete action.values
    if (variant !== 'hold') delete action.value2
    if (variant === 'set') {
      ctrl.setAtPath(`${actionPath}.value`,
        resolveParameterValue(device, cc, slotVal(action.value, 0)))
    } else if (variant === 'hold') {
      ctrl.setAtPath(`${actionPath}.value`,
        resolveParameterValue(device, cc, slotVal(action.value, 0)))
      ctrl.setAtPath(`${actionPath}.value2`,
        resolveParameterValue(device, cc, slotVal(action.value2, 0)))
    } else if (variant === 'cycle') {
      const cur = Array.isArray(action.values) ? action.values : []
      if (cur.length >= 2) {
        ctrl.setAtPath(`${actionPath}.values`, cur)
      } else {
        ctrl.setAtPath(`${actionPath}.values`, [
          resolveParameterValue(device, cc, cur[0]),
          resolveParameterValue(device, cc, defaultValueForParameter(device, cc))
        ])
      }
    }
  }

  function slotVal (field, index) {
    return Array.isArray(field) ? field[index] : (index === 0 ? field : undefined)
  }

  return {
    findControl,
    parameterOptions,
    parameterCount,
    firstUnusedParameterCc,
    valueOptions,
    hasParameters,
    isValidParameterCc,
    resolveParameterCc,
    defaultValueForParameter,
    resolveParameterValue,
    controlSlotCount,
    ccForSlot,
    parameterLabel,
    cycleIsMultiValues,
    cycleStepCount,
    cycleSlotCount,
    cycleStepsForSlot,
    normalizeControlAction,
    normalizeControlActionsInModel,
    seedControlAction
  }
})()
