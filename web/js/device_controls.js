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

  function isControlAction (type) {
    return type === 'control' || type === 'control_change'
  }

  function hasDiscreteValues (device, ccNum) {
    const cmd = findControl(device, ccNum)
    return (cmd?.valueRange?.discreteValues?.length || 0) > 0
  }

  function discreteValueOptions (device, ccNum) {
    const cmd = findControl(device, ccNum)
    if (!cmd?.valueRange?.discreteValues?.length) return []
    return cmd.valueRange.discreteValues.map(dv => ({
      v: dv.value,
      l: dv.name || String(dv.value)
    }))
  }

  function continuousValueRange (device, ccNum) {
    const cmd = findControl(device, ccNum)
    if (!cmd?.valueRange) return { min: 0, max: 127 }
    let min = cmd.valueRange.min ?? 0
    let max = Math.min(cmd.valueRange.max ?? 127, 127)
    if (min > max) [min, max] = [0, 127]
    return { min, max }
  }

  function valueOptions (device, ccNum) {
    const discrete = discreteValueOptions(device, ccNum)
    if (discrete.length) return discrete

    const { min, max } = continuousValueRange(device, ccNum)
    const opts = []
    for (let i = min; i <= max; i++) opts.push({ v: i, l: String(i) })
    return opts
  }

  function resolveCcForValuePath (getAtPath, path) {
    const m = path.match(/^(.*)\.(?:value2|value)(?:\.(\d+))?$/)
    if (!m) return null
    const action = getAtPath(m[1])
    if (!action) return null
    const slot = m[2] != null ? Number(m[2]) : 0
    return ccForSlot(action, slot)
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

  function isRandomizeAction (type) {
    return type === 'randomize'
  }

  function randomizeMaxSlots (device) {
    const n = parameterCount(device)
    return n > 0 ? Math.min(8, n) : 8
  }

  function randomizeCcList (action) {
    return toList(action?.cc)
  }

  function randomizeDisplaySlotCount (action, device) {
    const active = randomizeCcList(action).length
    const max = randomizeMaxSlots(device)
    if (active >= max) return max
    return Math.max(1, active + 1)
  }

  function randomizeSlotOptions (device, ccList, slotIndex) {
    const opts = [{ v: '__inactive__', l: 'Inactive' }]
    const active = slotIndex < ccList.length
    const keep = active ? Number(ccList[slotIndex]) : null
    const exclude = new Set()
    ccList.forEach((c, i) => {
      if (i === slotIndex) return
      const n = Number(c)
      if (!Number.isNaN(n)) exclude.add(n)
    })

    if (hasParameters(device)) {
      opts.push(...parameterOptions(device, {
        exclude,
        keep: keep == null || Number.isNaN(keep) ? null : keep
      }))
    } else if (active && keep != null && !Number.isNaN(keep)) {
      opts.push({ v: keep, l: parameterLabel(device, keep) })
    }

    if (!hasParameters(device)) {
      for (let cc = 0; cc <= 127; cc++) {
        if (exclude.has(cc) || cc === keep) continue
        opts.push({ v: cc, l: `CC ${cc}` })
      }
    } else if (active && keep != null && !Number.isNaN(keep) &&
        !opts.some(o => Number(o.v) === keep)) {
      opts.push({ v: keep, l: parameterLabel(device, keep) })
    }

    return opts
  }

  function normalizeRandomizeAction (device, action) {
    if (!action || !isRandomizeAction(action.type)) return false
    const before = JSON.stringify(action.cc)
    const seen = new Set()
    let list = randomizeCcList(action)
      .map(cc => Number(cc))
      .filter(cc => {
        if (Number.isNaN(cc)) return false
        if (seen.has(cc)) return false
        seen.add(cc)
        if (hasParameters(device)) return isValidParameterCc(device, cc)
        return cc >= 0 && cc <= 127
      })
    list = list.slice(0, randomizeMaxSlots(device))
    if (list.length === 0) {
      list = [hasParameters(device) ? firstParameterCc(device) : 0]
    }
    action.cc = list
    return JSON.stringify(action.cc) !== before
  }

  function normalizeRandomizeActionsInModel (device, model) {
    if (!model) return false
    let changed = false
    const fix = (action) => {
      if (normalizeRandomizeAction(device, action)) changed = true
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

  function seedRandomizeAction (ctrl, actionPath) {
    const device = ctrl.deviceDefinition
    const action = ctrl.getAtPath(actionPath)
    if (!action || !isRandomizeAction(action.type)) return
    if (randomizeCcList(action).length === 0) {
      const cc = hasParameters(device) ? firstParameterCc(device) : 0
      ctrl.setAtPath(`${actionPath}.cc`, [cc])
    }
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
    if (!action || !isControlAction(action.type)) return false
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

  function seedSlotValues (device, action, field) {
    const slots = Math.max(1, controlSlotCount(action))
    const resolved = []
    for (let i = 0; i < slots; i++) {
      const cc = resolveParameterCc(device, ccForSlot(action, i))
      resolved.push(resolveParameterValue(device, cc, slotVal(action[field], i)))
    }
    return packField(resolved)
  }

  function seedControlAction (ctrl, actionPath) {
    const device = ctrl.deviceDefinition
    if (!hasParameters(device)) return
    const action = ctrl.getAtPath(actionPath) || {}
    const slots = Math.max(1, controlSlotCount(action))
    const ccResolved = []
    for (let i = 0; i < slots; i++) {
      ccResolved.push(resolveParameterCc(device, ccForSlot(action, i)))
    }
    ctrl.setAtPath(`${actionPath}.cc`, packField(ccResolved))
    if (!['set', 'hold', 'cycle'].includes(action.variant)) {
      ctrl.setAtPath(`${actionPath}.variant`, 'set')
    }
    const variant = ctrl.getAtPath(`${actionPath}.variant`) || 'set'
    // Drop fields that don't belong to the (possibly just-changed) variant so
    // switching set/hold/cycle never leaves stale CC payload behind.
    if (variant !== 'cycle') delete action.values
    if (variant !== 'hold') delete action.value2
    if (variant === 'set') {
      ctrl.setAtPath(`${actionPath}.value`, seedSlotValues(device, action, 'value'))
    } else if (variant === 'hold') {
      ctrl.setAtPath(`${actionPath}.value`, seedSlotValues(device, action, 'value'))
      ctrl.setAtPath(`${actionPath}.value2`, seedSlotValues(device, action, 'value2'))
    } else if (variant === 'cycle') {
      const cc = ccResolved[0]
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

  function isPresetAction (type) {
    return type === 'preset'
  }

  function presetRange (device) {
    const pc = device?.x_pc || {}
    const indexBase = Number(pc.indexBase ?? 0)
    const count = Math.max(1, Math.min(16384, Number(pc.count ?? 128)))
    return { indexBase, count, min: indexBase, max: indexBase + count - 1 }
  }

  function hasPresetCatalog (device) {
    return (device?.x_pc?.count ?? 0) > 0 || !!device?.x_pc
  }

  function presetDisplayNumber (device, stored) {
    const { indexBase } = presetRange(device)
    return Number(stored) - indexBase + 1
  }

  function presetOptions (device, currentStored) {
    const { indexBase, count, min, max } = presetRange(device)
    const opts = []
    for (let i = 0; i < count; i++) {
      const stored = indexBase + i
      opts.push({ v: stored, l: String(i + 1) })
    }
    const cur = currentStored == null || currentStored === '' ? NaN : Number(currentStored)
    if (!Number.isNaN(cur) && (cur < min || cur > max) &&
        !opts.some(o => Number(o.v) === cur)) {
      opts.unshift({ v: cur, l: `${presetDisplayNumber(device, cur)} (out of range)` })
    }
    return opts
  }

  function resolvePresetValue (device, current) {
    const { min, max } = presetRange(device)
    const cur = current == null || current === '' ? NaN : Number(current)
    if (!Number.isNaN(cur) && cur >= min && cur <= max) return cur
    return min
  }

  function presetStepCount (action) {
    const presets = action?.presets
    const n = Array.isArray(presets) ? presets.length : 0
    const declared = Number(action?.num_presets ?? 0)
    return Math.max(2, Math.min(8, Math.max(n, declared || 2)))
  }

  function normalizePresetAction (device, action) {
    if (!action || !isPresetAction(action.type)) return false
    const variant = action.variant || 'set'
    const before = JSON.stringify(action)
    const { min, max } = presetRange(device)

    const clampPreset = (v) => {
      const n = Number(v)
      if (Number.isNaN(n)) return min
      return Math.max(min, Math.min(max, Math.round(n)))
    }

    if (variant === 'set') {
      action.number = clampPreset(action.number ?? min)
    } else if (variant === 'hold') {
      action.press_preset = clampPreset(action.press_preset ?? min)
      if (action.release_to_original) {
        delete action.release_preset
      } else {
        action.release_preset = clampPreset(action.release_preset ?? min)
      }
    } else if (variant === 'cycle') {
      let steps = Array.isArray(action.presets) ? action.presets.slice() : []
      const stepCount = Math.max(2, Math.min(8, presetStepCount(action)))
      while (steps.length < stepCount) steps.push(min)
      steps = steps.slice(0, stepCount).map(clampPreset)
      action.presets = steps
      action.num_presets = steps.length
    }

    return JSON.stringify(action) !== before
  }

  function normalizePresetActionsInModel (device, model) {
    if (!model) return false
    let changed = false
    const fix = (action) => {
      if (normalizePresetAction(device, action)) changed = true
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

  function seedPresetAction (ctrl, actionPath) {
    const device = ctrl.deviceDefinition
    const action = ctrl.getAtPath(actionPath) || {}
    if (!isPresetAction(action.type)) return
    if (!['set', 'hold', 'cycle', 'increment', 'decrement'].includes(action.variant)) {
      ctrl.setAtPath(`${actionPath}.variant`, 'set')
    }
    const variant = ctrl.getAtPath(`${actionPath}.variant`) || 'set'
    const { min } = presetRange(device)

    if (variant === 'set') {
      delete action.press_preset
      delete action.release_preset
      delete action.release_to_original
      delete action.presets
      delete action.num_presets
      ctrl.setAtPath(`${actionPath}.number`, resolvePresetValue(device, action.number))
    } else if (variant === 'hold') {
      delete action.number
      delete action.presets
      delete action.num_presets
      ctrl.setAtPath(`${actionPath}.press_preset`,
        resolvePresetValue(device, action.press_preset))
      if (action.release_to_original ||
          (action.release_preset === undefined && action.release_to_original !== false)) {
        action.release_to_original = true
        delete action.release_preset
      } else {
        delete action.release_to_original
        ctrl.setAtPath(`${actionPath}.release_preset`,
          resolvePresetValue(device, action.release_preset))
      }
    } else if (variant === 'cycle') {
      delete action.number
      delete action.press_preset
      delete action.release_preset
      delete action.release_to_original
      const cur = Array.isArray(action.presets) ? action.presets : []
      const stepCount = Math.max(2, Math.min(8, cur.length || 2))
      const steps = []
      for (let i = 0; i < stepCount; i++) {
        steps.push(resolvePresetValue(device, cur[i]))
      }
      ctrl.setAtPath(`${actionPath}.presets`, steps)
      ctrl.setAtPath(`${actionPath}.num_presets`, steps.length)
    } else {
      delete action.number
      delete action.press_preset
      delete action.release_preset
      delete action.release_to_original
      delete action.presets
      delete action.num_presets
    }
  }

  function pushPresetError (errors, path, val, device, label) {
    const { min, max, count } = presetRange(device)
    const n = Number(val)
    if (Number.isNaN(n) || !Number.isInteger(n) || n < min || n > max) {
      errors.push({
        path,
        message: `${label} must be between 1 and ${count} for this device`
      })
    }
  }

  function validatePresetAction (action, path, errors, device) {
    if (!action || !isPresetAction(action.type)) return
    const variant = action.variant || 'set'
    const { min, max } = presetRange(device)

    if (variant === 'set') {
      pushPresetError(errors, `${path}.number`, action.number, device, 'Preset')
    } else if (variant === 'hold') {
      pushPresetError(errors, `${path}.press_preset`, action.press_preset, device, 'Press preset')
      if (!action.release_to_original) {
        pushPresetError(errors, `${path}.release_preset`, action.release_preset, device,
          'Release preset')
      }
    } else if (variant === 'cycle') {
      const steps = action.presets
      if (!Array.isArray(steps) || steps.length < 2 || steps.length > 8) {
        errors.push({
          path: `${path}.presets`,
          message: 'Preset cycle must have 2 to 8 steps'
        })
        return
      }
      steps.forEach((v, i) => {
        pushPresetError(errors, `${path}.presets.${i}`, v, device, 'Cycle preset')
      })
    }
  }

  function validatePresetActionsInModel (model, device) {
    const errors = []
    if (!model) return errors
    const check = (action, p) => validatePresetAction(action, p, errors, device)
    model.touchpads?.forEach((tp, i) => check(tp?.action, `touchpads.${i}.action`))
    check(model.button_left, 'button_left')
    check(model.button_right, 'button_right')
    check(model.button_both, 'button_both')
    check(model.bump, 'bump')
    model.on_load?.forEach((a, i) => check(a, `on_load.${i}`))
    model.on_play?.forEach((a, i) => check(a, `on_play.${i}`))
    model.cc_triggers?.forEach((t, i) => check(t?.action, `cc_triggers.${i}.action`))
    check(model.cv_trigger_action, 'cv_trigger_action')
    return errors
  }

  function pushMidiIntError (errors, path, val, label) {
    const n = Number(val)
    if (Number.isNaN(n) || !Number.isInteger(n) || n < 0 || n > 127) {
      errors.push({
        path,
        message: `${label} must be a whole number from 0 to 127 (got ${val})`
      })
    }
  }

  function validateMidiIntList (errors, path, field, label) {
    if (field == null) return
    if (Array.isArray(field)) {
      field.forEach((v, i) => pushMidiIntError(errors, `${path}.${i}`, v, label))
    } else {
      pushMidiIntError(errors, path, field, label)
    }
  }

  function validateControlAction (action, path, errors) {
    if (!action || !isControlAction(action.type)) return
    const variant = action.variant || 'set'
    validateMidiIntList(errors, `${path}.cc`, action.cc, 'CC number')
    if (variant === 'set' || variant === 'hold') {
      validateMidiIntList(errors, `${path}.value`, action.value, 'Value')
    }
    if (variant === 'hold') {
      validateMidiIntList(errors, `${path}.value2`, action.value2, 'Release value')
    }
    if (variant === 'cycle') {
      const v = action.values
      if (!v || !Array.isArray(v)) return
      if (cycleIsMultiValues(v)) {
        v.forEach((row, ri) => {
          if (!Array.isArray(row)) return
          row.forEach((cell, ci) => {
            pushMidiIntError(errors, `${path}.values.${ri}.${ci}`, cell, 'Cycle value')
          })
        })
      } else {
        v.forEach((cell, i) => {
          pushMidiIntError(errors, `${path}.values.${i}`, cell, 'Cycle value')
        })
      }
    }
  }

  function validateControlActionsInModel (model) {
    const errors = []
    if (!model) return errors
    const check = (action, p) => validateControlAction(action, p, errors)
    model.touchpads?.forEach((tp, i) => check(tp?.action, `touchpads.${i}.action`))
    check(model.button_left, 'button_left')
    check(model.button_right, 'button_right')
    check(model.button_both, 'button_both')
    check(model.bump, 'bump')
    model.on_load?.forEach((a, i) => check(a, `on_load.${i}`))
    model.on_play?.forEach((a, i) => check(a, `on_play.${i}`))
    model.cc_triggers?.forEach((t, i) => check(t?.action, `cc_triggers.${i}.action`))
    check(model.cv_trigger_action, 'cv_trigger_action')
    return errors
  }

  return {
    findControl,
    parameterOptions,
    parameterCount,
    firstUnusedParameterCc,
    valueOptions,
    discreteValueOptions,
    hasDiscreteValues,
    continuousValueRange,
    isControlAction,
    resolveCcForValuePath,
    hasParameters,
    firstParameterCc,
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
    validateControlActionsInModel,
    isPresetAction,
    presetRange,
    hasPresetCatalog,
    presetOptions,
    resolvePresetValue,
    presetStepCount,
    normalizePresetAction,
    normalizePresetActionsInModel,
    seedPresetAction,
    isRandomizeAction,
    randomizeMaxSlots,
    randomizeCcList,
    randomizeDisplaySlotCount,
    randomizeSlotOptions,
    normalizeRandomizeAction,
    normalizeRandomizeActionsInModel,
    seedRandomizeAction,
    validatePresetActionsInModel,
    seedControlAction
  }
})()
