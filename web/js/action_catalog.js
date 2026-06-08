/* Action type/variant/timing catalog (mirrors device action_config + validation) */

window.ActionCatalog = (function () {
  const NOTE_RANDOM = 254
  const NOTE_VEL_RANDOM = 254
  const NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']

  const NOTE_VELOCITY_PRESETS = [
    { v: NOTE_VEL_RANDOM, l: 'Random' },
    { v: 127, l: 'Forte' },
    { v: 100, l: 'Strong' },
    { v: 80, l: 'Medium' },
    { v: 60, l: 'Soft' },
    { v: 40, l: 'Piano' }
  ]

  const ALL_TYPES = [
    'none', 'control', 'preset', 'scene', 'confirm_pending', 'transport', 'tempo',
    'note', 'randomize', 'piano_pedal', 'touchwheel', 'lfo', 'clock', 'cut', 'ui',
    'param', 'rtg', 'sample_hold', 'punch_in', 'flag_ceremony', 'boomerang',
    'inspect_scene', 'reset'
  ]

  const TYPE_LABELS = {
    none: 'None',
    control: 'Control Change',
    preset: 'Preset',
    scene: 'Scene',
    confirm_pending: 'Confirm Pending',
    transport: 'Transport',
    tempo: 'Tempo',
    note: 'Note',
    randomize: 'Randomize',
    piano_pedal: 'Piano Pedal',
    touchwheel: 'Touchwheel',
    lfo: 'LFO',
    clock: 'Clock',
    cut: 'Cut',
    ui: 'UI',
    param: 'Param',
    rtg: 'RTG',
    sample_hold: 'S+H',
    punch_in: 'Punch-In',
    flag_ceremony: 'Flag Ceremony',
    boomerang: 'Boomerang',
    inspect_scene: 'Inspect Scene',
    reset: 'Reset'
  }

  const VARIANTS_BY_TYPE = {
    tempo: ['tap', 'set', 'increment', 'decrement', 'hold', 'cycle', 'downbeat'],
    control: ['set', 'hold', 'cycle'],
    scene: ['set', 'increment', 'decrement'],
    preset: ['set', 'hold', 'cycle', 'increment', 'decrement'],
    transport: ['play', 'stop', 'record'],
    touchwheel: ['hold', 'cycle'],
    lfo: ['start', 'stop', 'toggle', 'modify'],
    clock: ['toggle', 'hold', 'burst'],
    cut: ['toggle', 'hold'],
    ui: ['set', 'hold', 'cycle'],
    param: ['hold', 'cycle'],
    rtg: ['toggle', 'hold', 'step', 'modify'],
    sample_hold: ['toggle', 'hold', 'step', 'modify']
  }

  const VARIANT_LABELS = {
    increment: 'Increment',
    decrement: 'Decrement',
    set: 'Set',
    hold: 'Hold',
    cycle: 'Cycle',
    toggle: 'Toggle',
    start: 'Start',
    stop: 'Stop',
    tap: 'Tap',
    burst: 'Burst',
    play: 'Play',
    pause: 'Pause',
    record: 'Record',
    modify: 'Modify',
    step: 'Step',
    downbeat: 'Downbeat'
  }

  const HOLD_TYPES = new Set(['note', 'piano_pedal', 'inspect_scene'])

  const TRIGGERS = {
    TOUCHPAD_0_7: 'touchpad_0_7',
    TOUCHPAD_8_11: 'touchpad_8_11',
    BUTTON: 'button',
    BUMP: 'bump',
    ON_LOAD: 'on_load',
    ON_PLAY: 'on_play',
    CC: 'cc',
    EXPR_SWITCH: 'expr_switch'
  }

  function padDisplayName (index) {
    const names = [
      'Pad 1', 'Pad 2', 'Pad 3', 'Pad 4', 'Pad 5', 'Pad 6', 'Pad 7', 'Pad 8',
      'Omega', 'Alpha', 'Beta', 'Gamma'
    ]
    return names[index] || `Pad ${index + 1}`
  }

  function typeLabel (type) {
    return TYPE_LABELS[type] || type
  }

  function variantLabel (variant) {
    return VARIANT_LABELS[variant] || variant
  }

  function typeHasVariants (type) {
    return Array.isArray(VARIANTS_BY_TYPE[type]) && VARIANTS_BY_TYPE[type].length > 0
  }

  function midiNoteLabel (midi) {
    const n = Number(midi)
    if (Number.isNaN(n)) return String(midi)
    const octave = Math.floor(n / 12) - 1
    return `${NOTE_NAMES[n % 12]}${octave}`
  }

  function noteOptions (current) {
    const opts = [{ v: NOTE_RANDOM, l: 'Random' }]
    for (let midi = 36; midi <= 96; midi++) {
      opts.push({ v: midi, l: `${midiNoteLabel(midi)} (${midi})` })
    }
    const cur = Number(current)
    if (!Number.isNaN(cur) && cur !== NOTE_RANDOM && (cur < 36 || cur > 96)) {
      opts.push({ v: cur, l: `${midiNoteLabel(cur)} (${cur})` })
    }
    return opts
  }

  function noteRandomBoundOptions (current) {
    const opts = []
    for (let midi = 36; midi <= 96; midi++) {
      opts.push({ v: midi, l: `${midiNoteLabel(midi)} (${midi})` })
    }
    const cur = Number(current)
    if (!Number.isNaN(cur) && (cur < 36 || cur > 96)) {
      opts.push({ v: cur, l: `${midiNoteLabel(cur)} (${cur})` })
    }
    return opts
  }

  function noteVelocityOptions (current) {
    const opts = NOTE_VELOCITY_PRESETS.slice()
    const cur = Number(current)
    if (!Number.isNaN(cur) && !opts.some(o => o.v === cur)) {
      opts.push({ v: cur, l: String(cur) })
    }
    return opts
  }

  function timingOptions (numerator, useTransport) {
    let beats = Number(numerator)
    if (!beats || beats < 1) beats = 4
    if (beats > 16) beats = 16
    const opts = [
      { v: 'immediate', l: 'Immediate' },
      { v: 'beat', l: 'Next Beat' }
    ]
    for (let i = 1; i <= beats; i++) {
      opts.push({ v: `beat_${i}`, l: `Beat ${i}` })
    }
    if (useTransport)
      opts.push({ v: 'transport', l: 'On Transport' })
    return opts
  }

  function triggerCaps (trigger) {
    switch (trigger) {
      case TRIGGERS.TOUCHPAD_0_7:
      case TRIGGERS.TOUCHPAD_8_11:
      case TRIGGERS.BUTTON:
      case TRIGGERS.CC:
      case TRIGGERS.EXPR_SWITCH:
        return {
          deliversRelease: true,
          inhibitsTransport: false,
          firesAtLoad: false,
          firesAtPlay: false
        }
      case TRIGGERS.BUMP:
        return {
          deliversRelease: false,
          inhibitsTransport: false,
          firesAtLoad: false,
          firesAtPlay: false
        }
      case TRIGGERS.ON_LOAD:
        return {
          deliversRelease: false,
          inhibitsTransport: false,
          firesAtLoad: true,
          firesAtPlay: false
        }
      case TRIGGERS.ON_PLAY:
        return {
          deliversRelease: false,
          inhibitsTransport: true,
          firesAtLoad: false,
          firesAtPlay: true
        }
      default:
        return {
          deliversRelease: true,
          inhibitsTransport: false,
          firesAtLoad: false,
          firesAtPlay: false
        }
    }
  }

  function requiresHold (action) {
    if (!action?.type) return false
    if (HOLD_TYPES.has(action.type)) return true
    const v = action.variant
    if (v === 'hold') {
      return ['tempo', 'control', 'preset', 'touchwheel', 'clock', 'cut', 'ui', 'param',
        'rtg', 'sample_hold'].includes(action.type)
    }
    if (v === 'burst' && action.type === 'clock') return true
    return false
  }

  function isTransport (type) {
    return type === 'transport'
  }

  function defaultVariant (type) {
    switch (type) {
      case 'tempo':
      case 'control':
      case 'scene':
      case 'preset':
      case 'ui':
        return 'set'
      case 'transport':
        return 'play'
      case 'lfo':
        return 'modify'
      case 'clock':
      case 'cut':
      case 'rtg':
      case 'sample_hold':
        return 'toggle'
      case 'param':
        return 'hold'
      default:
        return ''
    }
  }

  function isFireAndForget (action) {
    if (!action?.type || action.type === 'none') return true
    const t = action.type
    const v = action.variant || defaultVariant(t)
    switch (t) {
      case 'control':
      case 'tempo':
      case 'preset':
      case 'scene':
      case 'ui':
        return v === 'set'
      case 'transport':
      case 'randomize':
      case 'reset':
      case 'boomerang':
      case 'lfo':
        return true
      case 'clock':
      case 'cut':
        return v === 'toggle'
      case 'rtg':
      case 'sample_hold':
        return v === 'toggle' || v === 'step' || v === 'modify'
      default:
        return false
    }
  }

  function inputRestrictionAllows (action, trigger) {
    if (!action?.type) return true
    if (action.type === 'touchwheel') {
      return trigger === TRIGGERS.TOUCHPAD_8_11 ||
        trigger === TRIGGERS.BUTTON ||
        trigger === TRIGGERS.BUMP ||
        trigger === TRIGGERS.EXPR_SWITCH
    }
    if (action.type === 'param') {
      if (trigger === TRIGGERS.BUMP) return action.variant === 'cycle'
      return trigger === TRIGGERS.TOUCHPAD_8_11 ||
        trigger === TRIGGERS.BUTTON ||
        trigger === TRIGGERS.EXPR_SWITCH ||
        trigger === TRIGGERS.CC
    }
    return true
  }

  function isValidForTrigger (action, trigger) {
    if (!action?.type || action.type === 'none') return true
    const caps = triggerCaps(trigger)
    if (requiresHold(action) && !caps.deliversRelease) return false
    if (caps.inhibitsTransport && isTransport(action.type)) return false
    if ((caps.firesAtLoad || caps.firesAtPlay) && !isFireAndForget(action)) return false
    if (caps.firesAtLoad && action.type === 'lfo') {
      const v = action.variant || defaultVariant('lfo')
      if (v === 'start' || v === 'stop' || v === 'toggle') return false
    }
    return inputRestrictionAllows(action, trigger)
  }

  function isTypeVisible (type, trigger, ctx) {
    if (type === 'none') return true
    const probe = { type, variant: defaultVariant(type) }
    if (!isValidForTrigger(probe, trigger)) return false

    const sceneMode = ctx?.sceneMode ?? 2
    if (type === 'preset' && sceneMode !== 0 && sceneMode !== 2) return false
    if (type === 'scene' && sceneMode !== 1 && sceneMode !== 2) return false
    if (type === 'confirm_pending' && (ctx?.confirmChange ?? 0) !== 1) return false

    const clockSource = ctx?.clockSource || 'internal'
    if (type === 'tempo' && clockSource !== 'internal') {
      return isValidForTrigger({ type: 'tempo', variant: 'downbeat' }, trigger)
    }

    return true
  }

  function typesForTrigger (trigger, ctx) {
    return ALL_TYPES
      .filter(t => isTypeVisible(t, trigger, ctx))
      .map(t => ({ v: t, l: typeLabel(t) }))
  }

  function variantsForType (type, trigger, ctx) {
    if (!typeHasVariants(type)) return []
    const variants = VARIANTS_BY_TYPE[type] || []
    return variants
      .filter(v => isValidForTrigger({ type, variant: v }, trigger))
      .map(v => ({ v, l: variantLabel(v) }))
  }

  function supportsTiming (action) {
    if (!action?.type || action.type === 'none' || action.type === 'punch_in') return false
    const t = action.type
    const v = action.variant || defaultVariant(t)
    if (requiresHold(action)) return false
    if (t === 'tempo') {
      return v !== 'tap' && v !== 'hold' && v !== 'downbeat'
    }
    if (t === 'control' || t === 'preset' || t === 'touchwheel' || t === 'ui' ||
        t === 'param' || t === 'rtg' || t === 'sample_hold') {
      return v !== 'hold'
    }
    if (t === 'lfo') return v !== 'toggle'
    if (t === 'clock' || t === 'cut') return false
    return !HOLD_TYPES.has(t)
  }

  function supportsRepeat (action) {
    if (!action?.type || action.type === 'none' || requiresHold(action)) return false
    const t = action.type
    const v = action.variant || defaultVariant(t)
    if (t === 'scene' || t === 'punch_in' || t === 'confirm_pending' || t === 'transport') return false
    if (t === 'tempo') {
      return v === 'increment' || v === 'decrement' || v === 'cycle'
    }
    if (t === 'control') return v === 'set' || v === 'cycle'
    if (t === 'preset') return v !== 'hold'
    if (t === 'touchwheel') return v === 'cycle'
    if (t === 'lfo') return v !== 'toggle'
    if (t === 'clock' || t === 'cut') return false
    return true
  }

  const FOLLOWUP_HOLD_TYPES = new Set([
    'tempo', 'control', 'preset', 'touchwheel', 'clock', 'cut', 'ui', 'param',
    'rtg', 'sample_hold'
  ])

  function supportsFollowUp (action) {
    if (!action?.type || action.type === 'none') return false
    return action.variant === 'hold' && FOLLOWUP_HOLD_TYPES.has(action.type)
  }

  function supportsMorph (action) {
    if (!action?.type || action.type === 'none') return false
    const t = action.type
    const v = action.variant || defaultVariant(t)
    if (t === 'control') return v === 'hold' || v === 'cycle'
    if (t === 'tempo') return v === 'hold'
    if (t === 'randomize') return true
    return false
  }

  function tempoStepCount (action) {
    const tempos = action?.tempos
    const n = Array.isArray(tempos) ? tempos.length : 0
    const declared = Number(action?.num_tempos ?? 0)
    return Math.max(2, Math.min(8, Math.max(n, declared || 2)))
  }

  function normalizeTempoActionsInModel (model) {
    let changed = false
    forEachAction(model, action => {
      if (action?.type !== 'tempo') return
      const before = JSON.stringify(action)
      const v = action.variant || defaultVariant('tempo')
      const clampBpm = (n) => {
        const x = Number(n)
        if (Number.isNaN(x)) return 120
        return Math.max(20, Math.min(300, Math.round(x)))
      }
      if (v === 'set') {
        if (action.bpm != null) action.bpm = clampBpm(action.bpm)
      } else if (v === 'hold') {
        action.press_bpm = clampBpm(action.press_bpm)
        action.release_bpm = clampBpm(action.release_bpm)
      } else if (v === 'cycle') {
        let steps = Array.isArray(action.tempos) ? action.tempos.slice() : []
        const stepCount = tempoStepCount(action)
        while (steps.length < stepCount) steps.push(120)
        steps = steps.slice(0, stepCount).map(clampBpm)
        action.tempos = steps
        action.num_tempos = steps.length
      }
      if (JSON.stringify(action) !== before) changed = true
    })
    return changed
  }

  const REPEAT_KEYS = ['repeat', 'repeat_division', 'probability', 'pattern_length', 'pattern_mask']

  function clearRepeatFields (action) {
    if (!action) return false
    let changed = false
    for (const key of REPEAT_KEYS) {
      if (key in action) {
        delete action[key]
        changed = true
      }
    }
    return changed
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

  function normalizeRepeatActionsInModel (model) {
    let changed = false
    forEachAction(model, action => {
      if (action?.type && action.type !== 'none' && !supportsRepeat(action) &&
          clearRepeatFields(action)) {
        changed = true
      }
    })
    return changed
  }

  return {
    NOTE_RANDOM,
    NOTE_VEL_RANDOM,
    TRIGGERS,
    padDisplayName,
    typeLabel,
    variantLabel,
    typeHasVariants,
    noteOptions,
    noteRandomBoundOptions,
    noteVelocityOptions,
    timingOptions,
    typesForTrigger,
    variantsForType,
    defaultVariant,
    supportsTiming,
    supportsRepeat,
    supportsFollowUp,
    supportsMorph,
    clearRepeatFields,
    normalizeRepeatActionsInModel,
    tempoStepCount,
    normalizeTempoActionsInModel
  }
})()
