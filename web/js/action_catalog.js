/* Action type/variant/timing catalog (mirrors device action_config + validation) */

window.ActionCatalog = (function () {
  const NOTE_RANDOM = 254
  const NOTE_VEL_RANDOM = 254
  const NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']

  function clampBpm (n) {
    const x = Number(n)
    if (Number.isNaN(x)) return 120
    return Math.max(20, Math.min(300, Math.round(x * 10) / 10))
  }

  function clampBpmForDevice (n, allowFractional) {
    const x = clampBpm(n)
    return allowFractional ? x : Math.round(x)
  }

  function clampIncAmount (n) {
    const x = Number(n)
    if (Number.isNaN(x)) return 1
    return Math.max(0.1, Math.min(20, Math.round(x * 10) / 10))
  }

  function clampIncAmountForDevice (n, allowFractional) {
    const x = clampIncAmount(n)
    return allowFractional ? x : Math.round(x)
  }

  function formatBpmDisplay (n) {
    const x = Number(n)
    if (Number.isNaN(x)) return '--'
    return Number.isInteger(x) ? String(x) : x.toFixed(1)
  }

  const NOTE_VELOCITY_PRESETS = [
    { v: NOTE_VEL_RANDOM, l: 'Random' },
    { v: 127, l: 'Forte' },
    { v: 100, l: 'Strong' },
    { v: 80, l: 'Medium' },
    { v: 60, l: 'Soft' },
    { v: 40, l: 'Piano' }
  ]

  const PIANO_PEDAL_OPTIONS = [
    { v: 64, l: 'Damper' },
    { v: 66, l: 'Sostenuto' },
    { v: 67, l: 'Soft' },
    { v: 68, l: 'Legato' },
    { v: 69, l: 'Hold 2' }
  ]

  const LFO_ORIG_U8 = 255
  const LFO_RAND_U8 = 254
  const LFO_ORIG_U16 = 65535
  const LFO_RAND_U16 = 65534
  const LFO_ORIG_STEPS = 0
  const LFO_RAND_STEPS = 254
  const LFO_RESOLUTION_MANUAL = 4

  const LFO_TARGET_OPTIONS = [
    { v: 1, l: 'LFO 1' },
    { v: 2, l: 'LFO 2' },
    { v: 3, l: 'Both' }
  ]

  const TOUCHWHEEL_MODES = [
    { v: 0, l: 'Pads' },
    { v: 1, l: 'Control Change' },
    { v: 2, l: 'Program Change' },
    { v: 3, l: 'Tempo' },
    { v: 4, l: 'Pitch Bend' },
    { v: 5, l: 'After Touch' },
    { v: 6, l: 'Notes' },
    { v: 7, l: 'Double CC' },
    { v: 8, l: 'Velocity' },
    { v: 9, l: 'LFO Rate' },
    { v: 10, l: 'LFO Depth' },
    { v: 11, l: 'RTG Rate' },
    { v: 12, l: 'Tempo Nudge' }
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

  function noteNameOptions (current) {
    const opts = []
    for (let midi = 36; midi <= 96; midi++) {
      opts.push({ v: midi, l: midiNoteLabel(midi) })
    }
    const cur = Number(current)
    if (!Number.isNaN(cur) && (cur < 36 || cur > 96)) {
      opts.push({ v: cur, l: midiNoteLabel(cur) })
    }
    return opts
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

  function pianoPedalOptions (current) {
    const opts = PIANO_PEDAL_OPTIONS.slice()
    const cur = Number(current)
    if (!Number.isNaN(cur) && !opts.some(o => o.v === cur)) {
      opts.push({ v: cur, l: `CC ${cur}` })
    }
    return opts
  }

  function resolvePianoPedalCc (current) {
    const cur = Number(current)
    if (!Number.isNaN(cur) && PIANO_PEDAL_OPTIONS.some(o => o.v === cur)) return cur
    return 64
  }

  function withStaleOption (opts, current, label) {
    const cur = Number(current)
    if (Number.isNaN(cur) || opts.some(o => Number(o.v) === cur)) return opts
    return opts.concat([{ v: cur, l: label ?? String(cur) }])
  }

  function lfoTargetOptions (current) {
    const cur = Number(current)
    if (!Number.isNaN(cur) && !LFO_TARGET_OPTIONS.some(o => o.v === cur)) {
      return LFO_TARGET_OPTIONS.concat([{ v: cur, l: `Target ${cur}` }])
    }
    return LFO_TARGET_OPTIONS.slice()
  }

  function lfoModifyU8Options (presets, current) {
    return withStaleOption(
      [{ v: LFO_ORIG_U8, l: 'Original' }, { v: LFO_RAND_U8, l: 'Random' }, ...presets],
      current
    )
  }

  function lfoModifyU16Options (presets, current) {
    return withStaleOption(
      [{ v: LFO_ORIG_U16, l: 'Original' }, { v: LFO_RAND_U16, l: 'Random' }, ...presets],
      current
    )
  }

  function lfoModifyWaveformOptions (current) {
    return lfoModifyU8Options([
      { v: 0, l: 'Sine' },
      { v: 1, l: 'Triangle' },
      { v: 2, l: 'Square' },
      { v: 3, l: 'Saw Up' },
      { v: 4, l: 'Saw Down' },
      { v: 5, l: 'Sample & Hold' },
      { v: 6, l: 'Bin' },
      { v: 7, l: 'Glider' },
      { v: 8, l: 'Stray' }
    ], current)
  }

  function lfoModifyRateModeOptions (current) {
    return lfoModifyU8Options([
      { v: 0, l: 'Time' },
      { v: 1, l: 'Division' }
    ], current)
  }

  function lfoModifyRateHzOptions (current) {
    const rates = [5, 10, 25, 50, 100, 200, 300, 500, 800, 1000, 1500, 2000]
    const presets = rates.map(v => ({
      v,
      l: `${Math.floor(v / 100)}.${String(v % 100).padStart(2, '0')} Hz`
    }))
    return lfoModifyU16Options(presets, current)
  }

  function lfoModifyDivisionOptions (current) {
    return lfoModifyU8Options([
      { v: 0, l: '16 Bars' },
      { v: 1, l: '12 Bars' },
      { v: 2, l: '8 Bars' },
      { v: 3, l: '4 Bars' },
      { v: 4, l: '2 Bars' },
      { v: 5, l: '1 Bar' },
      { v: 6, l: '1/2' },
      { v: 7, l: '1/4' },
      { v: 8, l: '1/8' },
      { v: 9, l: '1/16' },
      { v: 10, l: '1/32' }
    ], current)
  }

  const LFO_SCENE_DIVISION_VALUES = [
    '16_bars', '12_bars', '8_bars', '4_bars', '2_bars', '1_bar',
    'half', 'quarter', 'eighth', 'sixteenth', '32nd'
  ]
  const LFO_SCENE_DIVISION_LABELS = [
    '16 Bars', '12 Bars', '8 Bars', '4 Bars', '2 Bars', '1 Bar',
    '1/2', '1/4', '1/8', '1/16', '1/32'
  ]

  function withStaleStringOption (opts, current, label) {
    const cur = String(current ?? '')
    if (!cur || opts.some(o => o.v === cur)) return opts
    return opts.concat([{ v: cur, l: label ?? cur }])
  }

  function lfoSceneDivisionOptions (current) {
    const presets = LFO_SCENE_DIVISION_VALUES.map((v, i) => ({
      v,
      l: LFO_SCENE_DIVISION_LABELS[i]
    }))
    return withStaleStringOption(presets, current || 'quarter')
  }

  function lfoSceneRateHzOptions (currentHz) {
    const rates = [5, 10, 25, 50, 100, 200, 300, 500, 800, 1000, 1500, 2000]
    const presets = rates.map(v => ({
      v: v / 100,
      l: `${Math.floor(v / 100)}.${String(v % 100).padStart(2, '0')} Hz`
    }))
    const cur = Number(currentHz)
    if (!Number.isNaN(cur) && !presets.some(o => o.v === cur)) {
      return presets.concat([{ v: cur, l: `${cur} Hz` }])
    }
    return presets
  }

  function tempoNudgeAmountOptions (current) {
    const presets = []
    for (let p = 5; p <= 100; p += 5) presets.push({ v: p, l: `${p}%` })
    return withStaleOption(presets, current, `${current}%`)
  }

  function tempoNudgeDirectionOptions (current) {
    const presets = [
      { v: 0, l: 'Both' },
      { v: 1, l: 'Faster' },
      { v: 2, l: 'Slower' }
    ]
    const hit = presets.find(p => p.v === current)
    return withStaleOption(presets, current, hit ? hit.l : String(current))
  }

  function touchwheelTempoNudgeAmountOptions (current) {
    const presets = []
    for (let p = 0; p <= 100; p += 5) presets.push({ v: p, l: `${p}%` })
    return withStaleOption(presets, current, `${current}%`)
  }

  function touchwheelNudgeReturnOptions (current) {
    const presets = [
      { v: 0, l: 'Instant' },
      { v: 1, l: 'Fast (200ms)' },
      { v: 2, l: 'Medium (500ms)' },
      { v: 3, l: 'Slow (1s)' }
    ]
    const hit = presets.find(p => p.v === current)
    return withStaleOption(presets, current, hit ? hit.l : String(current))
  }

  function gainToSensitivity (gain) {
    const g = Math.max(1, Math.min(64, Number(gain) || 1))
    return Math.round(255 * Math.log(g * 4) / Math.log(256))
  }

  function sensitivityToGain (sens) {
    return 0.25 * Math.pow(256, (Number(sens) || 0) / 255)
  }

  function closestAudioGain (sens) {
    let best = 1
    let bestDiff = Infinity
    for (let g = 1; g <= 64; g++) {
      const diff = Math.abs(gainToSensitivity(g) - (Number(sens) || 0))
      if (diff < bestDiff) {
        bestDiff = diff
        best = g
      }
    }
    return best
  }

  function cvAudioGainOptions (currentSens) {
    const presets = []
    for (let g = 1; g <= 64; g++) presets.push({ v: g, l: `${g}x` })
    const curGain = closestAudioGain(currentSens)
    return withStaleOption(presets, curGain, `${curGain}x`)
  }

  function cvAudioThresholdOptions (current) {
    const presets = [{ v: 0, l: 'Off' }]
    for (let t = 1; t <= 127; t++) presets.push({ v: t, l: String(t) })
    const hit = presets.find(p => p.v === current)
    return withStaleOption(presets, current, hit ? hit.l : String(current))
  }

  const CV_TRIGGER_DEBOUNCE_MS = [0, 50, 100, 200, 300, 500, 750, 1000, 1500, 2000]

  function cvTriggerDebounceLabel (ms) {
    if (ms === 0) return 'Immediate'
    if (ms === 1000) return '1s'
    if (ms === 1500) return '1.5s'
    if (ms === 2000) return '2s'
    return `${ms}ms`
  }

  function closestCvTriggerDebounce (ms) {
    let best = CV_TRIGGER_DEBOUNCE_MS[0]
    let bestDiff = Infinity
    for (const v of CV_TRIGGER_DEBOUNCE_MS) {
      const diff = Math.abs(v - (Number(ms) || 0))
      if (diff < bestDiff) {
        bestDiff = diff
        best = v
      }
    }
    return best
  }

  function cvTriggerDebounceOptions (currentMs) {
    const presets = CV_TRIGGER_DEBOUNCE_MS.map(v => ({ v, l: cvTriggerDebounceLabel(v) }))
    const cur = closestCvTriggerDebounce(currentMs)
    return withStaleOption(presets, cur, cvTriggerDebounceLabel(cur))
  }

  function closestCvTriggerThreshold (pct) {
    const n = Math.max(0, Math.min(100, Number(pct) || 0))
    return Math.round(n / 10) * 10
  }

  function cvTriggerThresholdOptions (currentPct) {
    const presets = []
    for (let p = 0; p <= 100; p += 10) presets.push({ v: p, l: `${p}%` })
    const cur = closestCvTriggerThreshold(currentPct)
    return withStaleOption(presets, cur, `${cur}%`)
  }

  function lfoModifyFloorCeilingOptions (current) {
    const vals = [0, 10, 20, 30, 40, 50, 60, 64, 70, 80, 90, 100, 110, 120, 127]
    return lfoModifyU8Options(vals.map(v => ({ v, l: String(v) })), current)
  }

  function lfoModifyResolutionOptions (current) {
    return lfoModifyU8Options([
      { v: 0, l: 'Auto' },
      { v: 1, l: 'Coarse' },
      { v: 2, l: 'Medium' },
      { v: 3, l: 'Fine' },
      { v: 4, l: 'Manual' }
    ], current)
  }

  function lfoModifyManualStepsOptions (current) {
    const opts = [
      { v: LFO_ORIG_STEPS, l: 'Original' },
      { v: LFO_RAND_STEPS, l: 'Random' },
      { v: 16, l: '16' },
      { v: 32, l: '32' },
      { v: 64, l: '64' },
      { v: 128, l: '128' }
    ]
    return withStaleOption(opts, current)
  }

  function clearLfoModifyFields (action) {
    delete action.waveform
    delete action.rate_mode
    delete action.rate_hz_x100
    delete action.division
    delete action.floor
    delete action.ceiling
    delete action.resolution_mode
    delete action.manual_steps
  }

  function seedLfoModifyFields (action) {
    if (action.waveform == null) action.waveform = LFO_ORIG_U8
    if (action.rate_mode == null) action.rate_mode = LFO_ORIG_U8
    if (action.rate_hz_x100 == null) action.rate_hz_x100 = LFO_ORIG_U16
    if (action.division == null) action.division = LFO_ORIG_U8
    if (action.floor == null) action.floor = LFO_ORIG_U8
    if (action.ceiling == null) action.ceiling = LFO_ORIG_U8
    if (action.resolution_mode == null) action.resolution_mode = LFO_ORIG_U8
    if (action.manual_steps == null) action.manual_steps = LFO_ORIG_STEPS
  }

  function normalizeLfoAction (action) {
    if (!action || action.type !== 'lfo') return false
    const before = JSON.stringify(action)
    const v = action.variant || defaultVariant('lfo')
    const slot = Number(action.slot)
    action.slot = (slot === 2 || slot === 3) ? slot : 1

    if (v === 'modify') {
      seedLfoModifyFields(action)
    } else {
      clearLfoModifyFields(action)
      if (v === 'start' || v === 'stop') clearRepeatFields(action)
    }
    return JSON.stringify(action) !== before
  }

  function normalizeLfoActionsInModel (model) {
    let changed = false
    forEachAction(model, action => {
      if (normalizeLfoAction(action)) changed = true
    })
    return changed
  }

  const ENGINE_MODIFY_RATES_X100 = [
    50, 75, 100, 125, 150, 175, 200, 250, 300, 350, 400, 500,
    600, 700, 800, 900, 1000, 1250, 1500, 1750, 2000, 2500
  ]

  const ENGINE_MODIFY_SYNC_MULTS = [
    { v: 125, l: '1/8 (0.125x)' },
    { v: 167, l: '1/6 (0.167x)' },
    { v: 250, l: '1/4 (0.25x)' },
    { v: 333, l: '1/3 (0.333x)' },
    { v: 500, l: '1/2 (0.5x)' },
    { v: 667, l: '2/3 (0.667x)' },
    { v: 750, l: '3/4 (0.75x)' },
    { v: 1000, l: '1x' },
    { v: 1500, l: '3/2 (1.5x)' },
    { v: 2000, l: '2x' },
    { v: 3000, l: '3x' },
    { v: 4000, l: '4x' },
    { v: 6000, l: '6x' },
    { v: 8000, l: '8x' }
  ]

  const CLOCK_BURST_SPEEDS = [25, 50, 75, 100, 125, 150, 175, 200, 225, 250, 275, 300]

  const UI_MODULE_OPTIONS = [
    { v: 0, l: 'Beat Grid' },
    { v: 1, l: 'Khyron' },
    { v: 2, l: 'Space' },
    { v: 3, l: 'Summoner' },
    { v: 4, l: 'Pixels' }
  ]

  const CUT_MODE_OPTIONS = [
    { v: 'local', l: 'Local Only' },
    { v: 'passthrough', l: 'Passthrough' },
    { v: 'both', l: 'Both' }
  ]

  const STEP_TARGET_OPTIONS = [
    { v: 'rtg', l: 'RTG' },
    { v: 'sh', l: 'S+H' }
  ]

  function engineModifyRateModeOptions (current) {
    return lfoModifyU8Options([
      { v: 0, l: 'Time' },
      { v: 1, l: 'Division' }
    ], current)
  }

  function engineModifyDivisionOptions (current) {
    return lfoModifyDivisionOptions(current)
  }

  function engineModifyRateHzOptions (current) {
    const presets = ENGINE_MODIFY_RATES_X100.map(v => ({
      v,
      l: v < 1000
        ? (v < 100 ? `${(v / 100).toFixed(2)} Hz` : `${(v / 100).toFixed(1)} Hz`)
        : `${Math.round(v / 100)} Hz`
    }))
    return lfoModifyU16Options(presets, current)
  }

  function engineModifySyncMultOptions (current) {
    return lfoModifyU16Options(ENGINE_MODIFY_SYNC_MULTS, current)
  }

  function engineModifyGlideOptions (current) {
    return lfoModifyU8Options([
      { v: 0, l: 'Off' },
      { v: 1, l: 'On' }
    ], current)
  }

  function engineModifyProbOptions (current) {
    const presets = [10, 20, 30, 40, 50, 60, 70, 80, 90, 100]
      .map(v => ({ v, l: `${v}%` }))
    return lfoModifyU8Options(presets, current)
  }

  function clockBurstSpeedOptions (current) {
    const opts = CLOCK_BURST_SPEEDS.map(v => ({ v, l: `${v}%` }))
    return withStaleOption(opts, current)
  }

  function uiModuleOptions (current) {
    return withStaleOption(UI_MODULE_OPTIONS.slice(), current)
  }

  function uiStepCount (action) {
    const mods = action?.modules
    const n = Array.isArray(mods) ? mods.length : 0
    const declared = Number(action?.num_modules ?? 0)
    return Math.max(2, Math.min(8, Math.max(n, declared || 2)))
  }

  function paramStepCount (action) {
    const params = action?.params
    const n = Array.isArray(params) ? params.length : 0
    const declared = Number(action?.num_params ?? 0)
    return Math.max(2, Math.min(8, Math.max(n, declared || 2)))
  }

  function punchInDurationOptions (numerator = 4) {
    const beats = Math.max(1, Math.min(7, Number(numerator) || 4))
    const opts = []
    for (let i = 1; i < beats && i <= 7; i++) {
      opts.push({ v: `${i}_beat${i === 1 ? '' : 's'}`, l: `${i} beat${i === 1 ? '' : 's'}` })
    }
    opts.push(
      { v: '1_bar', l: '1 bar' },
      { v: '2_bars', l: '2 bars' },
      { v: '4_bars', l: '4 bars' },
      { v: '8_bars', l: '8 bars' },
      { v: '16_bars', l: '16 bars' }
    )
    return opts
  }

  function clearEngineModifyFields (action) {
    delete action.rate_mode
    delete action.rate_hz_x100
    delete action.division
    delete action.sync_mult_x1000
    delete action.glide
    delete action.probability
  }

  function seedEngineModifyFields (action) {
    if (action.rate_mode == null) action.rate_mode = LFO_ORIG_U8
    if (action.rate_hz_x100 == null) action.rate_hz_x100 = LFO_ORIG_U16
    if (action.division == null) action.division = LFO_ORIG_U8
    if (action.glide == null) action.glide = LFO_ORIG_U8
    if (action.probability == null) action.probability = LFO_ORIG_U8
  }

  function normalizeClockAction (action) {
    if (!action || action.type !== 'clock') return false
    const before = JSON.stringify(action)
    const v = action.variant || defaultVariant('clock')
    clearRepeatFields(action)
    delete action.timing
    delete action.timing_beat
    delete action.raise_flag
    if (v === 'burst') {
      delete action.start_enabled
      let sp = Number(action.speed_percent)
      if (!CLOCK_BURST_SPEEDS.includes(sp)) sp = 100
      action.speed_percent = sp
    } else {
      delete action.speed_percent
      if (action.start_enabled == null) action.start_enabled = false
    }
    return JSON.stringify(action) !== before
  }

  function normalizeCutAction (action) {
    if (!action || action.type !== 'cut') return false
    const before = JSON.stringify(action)
    clearRepeatFields(action)
    delete action.timing
    delete action.timing_beat
    delete action.raise_flag
    const modes = CUT_MODE_OPTIONS.map(o => o.v)
    if (!modes.includes(action.cut_mode)) action.cut_mode = 'both'
    return JSON.stringify(action) !== before
  }

  function normalizeUiAction (action) {
    if (!action || action.type !== 'ui') return false
    const before = JSON.stringify(action)
    const v = action.variant || defaultVariant('ui')
    const clampMod = (n) => {
      const x = Number(n)
      if (Number.isNaN(x) || x < 0 || x > 4) return 0
      return Math.round(x)
    }
    if (v === 'set') {
      delete action.module2
      delete action.num_modules
      delete action.modules
      action.module = clampMod(action.module)
    } else if (v === 'hold') {
      delete action.num_modules
      delete action.modules
      action.module = clampMod(action.module)
      action.module2 = clampMod(action.module2)
    } else if (v === 'cycle') {
      delete action.module
      delete action.module2
      let steps = Array.isArray(action.modules) ? action.modules.slice() : []
      const stepCount = uiStepCount(action)
      while (steps.length < stepCount) steps.push(0)
      steps = steps.slice(0, stepCount).map(clampMod)
      action.modules = steps
      action.num_modules = steps.length
    }
    return JSON.stringify(action) !== before
  }

  const PARAM_STREAM_TARGETS = [
    { v: 'touchwheel', l: 'Touchwheel' },
    { v: 'expression', l: 'Expression' },
    { v: 'cv', l: 'CV' },
    { v: 'proximity', l: 'Proximity' },
    { v: 'als', l: 'ALS' },
    { v: 'tilt_x', l: 'Tilt X' },
    { v: 'tilt_y', l: 'Tilt Y' },
    { v: 'note_track', l: 'Note Track' },
    { v: 'lfo1', l: 'LFO 1' },
    { v: 'lfo2', l: 'LFO 2' }
  ]

  function paramStreamMappingIsCc (mapping) {
    if (!mapping) return false
    if (mapping.enabled === false) return false
    return (mapping.output_type || 'cc') === 'cc'
  }

  function paramStreamTargetIsActive (model, target) {
    if (!model || !target) return false
    const m = model
    switch (target) {
      case 'touchwheel':
        return (m.touchwheel_mode || 'pads') === 'continuous' &&
          paramStreamMappingIsCc(m.touchwheel)
      case 'expression':
        return (m.expression_mode || 'expression') === 'expression' &&
          paramStreamMappingIsCc(m.expression)
      case 'cv':
        return (m.cv_input_mode || 'none') === 'cv' &&
          paramStreamMappingIsCc(m.cv)
      case 'proximity':
        return paramStreamMappingIsCc(m.proximity)
      case 'als':
        return paramStreamMappingIsCc(m.als)
      case 'tilt_x':
        return paramStreamMappingIsCc(m.tilt_x)
      case 'tilt_y':
        return paramStreamMappingIsCc(m.tilt_y)
      case 'note_track':
        return paramStreamMappingIsCc(m.note_track)
      case 'lfo1':
        return !!m.lfo1_config?.enabled && paramStreamMappingIsCc(m.lfo1)
      case 'lfo2':
        return !!m.lfo2_config?.enabled && paramStreamMappingIsCc(m.lfo2)
      default:
        return false
    }
  }

  function paramStreamTargetOptions (model, currentTarget) {
    const cur = currentTarget || 'touchwheel'
    const opts = PARAM_STREAM_TARGETS
      .filter(t => paramStreamTargetIsActive(model, t.v))
      .map(t => ({ v: t.v, l: t.l }))
    if (cur && !opts.some(o => o.v === cur)) {
      const base = PARAM_STREAM_TARGETS.find(t => t.v === cur)
      opts.unshift({
        v: cur,
        l: base ? `${base.l} (unavailable)` : `${cur} (unavailable)`
      })
    }
    return opts
  }

  function resolveParamTarget (action, model) {
    const cur = action.target || 'touchwheel'
    if (!model) {
      if (!action.target) action.target = 'touchwheel'
      return
    }
    const active = PARAM_STREAM_TARGETS
      .filter(t => paramStreamTargetIsActive(model, t.v))
      .map(t => t.v)
    if (active.includes(cur)) {
      action.target = cur
      return
    }
    action.target = active.length ? active[0] : 'touchwheel'
  }

  function normalizeParamAction (action, model) {
    if (!action || action.type !== 'param') return false
    const before = JSON.stringify(action)
    const v = action.variant || defaultVariant('param')
    resolveParamTarget(action, model)
    const clampCc = (n) => {
      const x = Number(n)
      if (Number.isNaN(x) || x < 0 || x > 127) return 0
      return Math.round(x)
    }
    if (v === 'hold') {
      delete action.num_params
      delete action.params
      action.param = clampCc(action.param)
      if (action.release_to_original ||
          (action.param2 === undefined && action.release_to_original !== false)) {
        action.release_to_original = true
        delete action.param2
      } else {
        action.param2 = clampCc(action.param2)
        delete action.release_to_original
      }
    } else if (v === 'cycle') {
      delete action.param
      delete action.param2
      delete action.release_to_original
      clearRepeatFields(action)
      delete action.timing
      delete action.timing_beat
      let steps = Array.isArray(action.params) ? action.params.slice() : []
      const stepCount = paramStepCount(action)
      while (steps.length < stepCount) steps.push(0)
      steps = steps.slice(0, stepCount).map(clampCc)
      action.params = steps
      action.num_params = steps.length
    }
    return JSON.stringify(action) !== before
  }

  function normalizeEngineAction (action) {
    if (!action || (action.type !== 'rtg' && action.type !== 'sample_hold')) return false
    const before = JSON.stringify(action)
    const v = action.variant || defaultVariant(action.type)
    if (v !== 'modify') clearRepeatFields(action)
    if (v === 'hold') {
      delete action.release_mode
      delete action.release_threshold_ms
    }
    if (v === 'step') {
      delete action.step_target
      clearEngineModifyFields(action)
    } else if (v === 'modify') {
      delete action.step_target
      delete action.release_mode
      delete action.release_threshold_ms
      seedEngineModifyFields(action)
    } else {
      delete action.step_target
      clearEngineModifyFields(action)
    }
    return JSON.stringify(action) !== before
  }

  function normalizeSimpleActionsInModel (model) {
    let changed = false
    forEachAction(model, action => {
      if (!action?.type) return
      if (action.type === 'clock' && normalizeClockAction(action)) changed = true
      if (action.type === 'cut' && normalizeCutAction(action)) changed = true
      if (action.type === 'ui' && normalizeUiAction(action)) changed = true
      if (action.type === 'param' && normalizeParamAction(action, model)) changed = true
      if ((action.type === 'rtg' || action.type === 'sample_hold') &&
          normalizeEngineAction(action)) changed = true
      if (action.type === 'inspect_scene') {
        const before = JSON.stringify(action)
        clearRepeatFields(action)
        delete action.timing
        delete action.timing_beat
        delete action.raise_flag
        if (JSON.stringify(action) !== before) changed = true
      }
      if (action.type === 'punch_in' || action.type === 'flag_ceremony') {
        const before = JSON.stringify(action)
        clearRepeatFields(action)
        if (JSON.stringify(action) !== before) changed = true
      }
    })
    return changed
  }

  function touchwheelModeOptions (current) {
    const opts = TOUCHWHEEL_MODES.slice()
    const cur = Number(current)
    if (!Number.isNaN(cur) && !opts.some(o => o.v === cur)) {
      opts.push({ v: cur, l: `Mode ${cur}` })
    }
    return opts
  }

  function touchwheelStepCount (action) {
    const modes = action?.modes
    const n = Array.isArray(modes) ? modes.length : 0
    const declared = Number(action?.num_modes ?? 0)
    return Math.max(2, Math.min(8, Math.max(n, declared || 2)))
  }

  function normalizeTouchwheelAction (action) {
    if (!action || action.type !== 'touchwheel') return false
    const before = JSON.stringify(action)
    clearRepeatFields(action)
    delete action.timing
    delete action.timing_beat
    delete action.raise_flag

    const clampMode = (n) => {
      const x = Number(n)
      if (Number.isNaN(x) || x < 0 || x > 12) return 0
      return Math.round(x)
    }

    const v = action.variant || 'hold'
    if (v === 'hold') {
      delete action.num_modes
      delete action.modes
      action.mode = clampMode(action.mode)
      if (action.release_to_original) {
        delete action.mode2
      } else {
        action.mode2 = clampMode(action.mode2)
        delete action.release_to_original
      }
    } else if (v === 'cycle') {
      delete action.mode
      delete action.mode2
      delete action.release_to_original
      let steps = Array.isArray(action.modes) ? action.modes.slice() : []
      const stepCount = touchwheelStepCount(action)
      while (steps.length < stepCount) steps.push(0)
      steps = steps.slice(0, stepCount).map(clampMode)
      action.modes = steps
      action.num_modes = steps.length
    }
    return JSON.stringify(action) !== before
  }

  function normalizeTouchwheelActionsInModel (model) {
    let changed = false
    forEachAction(model, action => {
      if (normalizeTouchwheelAction(action)) changed = true
    })
    return changed
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
    opts.push(
      { v: 'bar_1', l: 'Bar 1' },
      { v: 'bar_2', l: 'Bar 2' },
      { v: 'bar_4', l: 'Bar 4' },
      { v: 'bar_8', l: 'Bar 8' },
      { v: 'bar_16', l: 'Bar 16' },
      { v: 'bar_32', l: 'Bar 32' },
      { v: 'bar_custom', l: 'Specify Bar...' }
    )
    if (useTransport)
      opts.push({ v: 'transport', l: 'On Transport' })
    return opts
  }

  const BAR_TIMING_PRESETS = new Set([
    'bar_1', 'bar_2', 'bar_4', 'bar_8', 'bar_16', 'bar_32'
  ])
  const BAR_TIMING_CUSTOM_DEFAULT = 3

  function parseBarTiming (timing) {
    const m = /^bar_(\d+)$/.exec(timing || '')
    if (!m) return { select: timing || 'immediate', count: null }
    if (BAR_TIMING_PRESETS.has(timing))
      return { select: timing, count: null }
    return { select: 'bar_custom', count: Number(m[1]) }
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
    if (t === 'control' || t === 'preset' || t === 'ui' ||
        t === 'rtg' || t === 'sample_hold') {
      return v !== 'hold'
    }
    if (t === 'param') return false
    if (t === 'touchwheel') return false
    if (t === 'lfo') {
      return v === 'start' || v === 'stop' || v === 'modify' || v === 'toggle'
    }
    if (t === 'clock' || t === 'cut') return false
    return !HOLD_TYPES.has(t)
  }

  function supportsRepeat (action) {
    if (!action?.type || action.type === 'none' || requiresHold(action)) return false
    const t = action.type
    const v = action.variant || defaultVariant(t)
    if (t === 'scene' || t === 'punch_in' || t === 'confirm_pending' || t === 'transport' ||
        t === 'reset') {
      return false
    }
    if (t === 'tempo') {
      return v === 'increment' || v === 'decrement' || v === 'cycle'
    }
    if (t === 'control') return v === 'set' || v === 'cycle'
    if (t === 'preset') return v !== 'hold'
    if (t === 'touchwheel') return false
    if (t === 'lfo') return v === 'toggle' || v === 'modify'
    if (t === 'rtg' || t === 'sample_hold') return v === 'modify'
    if (t === 'clock' || t === 'cut' || t === 'param') return false
    if (t === 'ui') return v === 'cycle'
    return true
  }

  const FOLLOWUP_HOLD_TYPES = new Set([
    'tempo', 'control', 'preset', 'touchwheel', 'clock'
  ])

  function supportsFollowUp (action) {
    if (!action?.type || action.type === 'none') return false
    return action.variant === 'hold' && FOLLOWUP_HOLD_TYPES.has(action.type)
  }

  const RAISE_FLAG_TYPES = new Set([
    'transport', 'control', 'preset', 'tempo', 'note', 'randomize', 'lfo',
    'punch_in', 'flag_ceremony', 'boomerang'
  ])

  function supportsRaiseFlag (action) {
    if (!action?.type || action.type === 'none') return false
    if (requiresHold(action)) return false
    return RAISE_FLAG_TYPES.has(action.type)
  }

  function supportsMorph (action) {
    if (!action?.type || action.type === 'none') return false
    const t = action.type
    const v = action.variant || defaultVariant(t)
    if (t === 'control') return v === 'hold' || v === 'cycle'
    if (t === 'tempo') {
      return v === 'set' || v === 'hold' || v === 'increment' ||
        v === 'decrement' || v === 'cycle'
    }
    if (t === 'randomize') return true
    return false
  }

  function tempoStepCount (action) {
    const tempos = action?.tempos
    const n = Array.isArray(tempos) ? tempos.length : 0
    const declared = Number(action?.num_tempos ?? 0)
    return Math.max(2, Math.min(8, Math.max(n, declared || 2)))
  }

  function normalizeTempoActionsInModel (model, allowFractional = true) {
    let changed = false
    forEachAction(model, action => {
      if (action?.type !== 'tempo') return
      const before = JSON.stringify(action)
      const v = action.variant || defaultVariant('tempo')
      const clampBpmLocal = n => clampBpmForDevice(n, allowFractional)
      if (v === 'set') {
        if (action.bpm != null) action.bpm = clampBpmLocal(action.bpm)
      } else if (v === 'hold') {
        action.press_bpm = clampBpmLocal(action.press_bpm)
        action.release_bpm = clampBpmLocal(action.release_bpm)
      } else if (v === 'cycle') {
        let steps = Array.isArray(action.tempos) ? action.tempos.slice() : []
        const stepCount = tempoStepCount(action)
        while (steps.length < stepCount) steps.push(120)
        steps = steps.slice(0, stepCount).map(clampBpmLocal)
        action.tempos = steps
        action.num_tempos = steps.length
      } else if (v === 'increment' || v === 'decrement') {
        if (action.inc_amount != null) {
          action.inc_amount = clampIncAmountForDevice(action.inc_amount, allowFractional)
        }
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

  const TEMPO_KEEP_FIELDS = {
    tap: new Set(),
    downbeat: new Set(),
    increment: new Set(['inc_amount']),
    decrement: new Set(['inc_amount']),
    set: new Set(['bpm', 'random_floor', 'random_ceiling']),
    hold: new Set(['press_bpm', 'release_bpm']),
    cycle: new Set(['tempos', 'num_tempos'])
  }

  const ACTION_STRIP_KEYS = [
    'cc', 'value', 'value2', 'values', 'bpm', 'press_bpm', 'release_bpm',
    'tempos', 'num_tempos', 'random_floor', 'random_ceiling', 'inc_amount',
    'note', 'velocity'
  ]

  function stripActionFields (action) {
    if (!action?.type || action.type === 'none') return false
    let changed = false
    if (action.type === 'tempo') {
      const v = action.variant || defaultVariant('tempo')
      const keep = TEMPO_KEEP_FIELDS[v] || new Set()
      for (const key of ACTION_STRIP_KEYS) {
        if (key in action && !keep.has(key)) {
          delete action[key]
          changed = true
        }
      }
    } else if (action.type === 'control' || action.type === 'control_change') {
      for (const key of ['bpm', 'press_bpm', 'release_bpm', 'tempos', 'num_tempos',
        'note', 'velocity']) {
        if (key in action) {
          delete action[key]
          changed = true
        }
      }
    }
    return changed
  }

  function stripActionFieldsInModel (model) {
    if (!model) return false
    let changed = false
    const visit = (action) => {
      if (stripActionFields(action)) changed = true
    }
    forEachAction(model, visit)
    visit(model.expr_switch)
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
    noteNameOptions,
    noteRandomBoundOptions,
    noteVelocityOptions,
    pianoPedalOptions,
    resolvePianoPedalCc,
    LFO_RESOLUTION_MANUAL,
    lfoTargetOptions,
    lfoModifyWaveformOptions,
    lfoModifyRateModeOptions,
    lfoModifyRateHzOptions,
    lfoModifyDivisionOptions,
    lfoSceneRateHzOptions,
    lfoSceneDivisionOptions,
    tempoNudgeAmountOptions,
    tempoNudgeDirectionOptions,
    touchwheelTempoNudgeAmountOptions,
    touchwheelNudgeReturnOptions,
    gainToSensitivity,
    sensitivityToGain,
    closestAudioGain,
    cvAudioGainOptions,
    cvAudioThresholdOptions,
    cvTriggerThresholdOptions,
    cvTriggerDebounceOptions,
    closestCvTriggerThreshold,
    closestCvTriggerDebounce,
    lfoModifyFloorCeilingOptions,
    lfoModifyResolutionOptions,
    lfoModifyManualStepsOptions,
    clearLfoModifyFields,
    seedLfoModifyFields,
    normalizeLfoActionsInModel,
    engineModifyRateModeOptions,
    engineModifyRateHzOptions,
    engineModifyDivisionOptions,
    engineModifyGlideOptions,
    engineModifyProbOptions,
    clockBurstSpeedOptions,
    uiModuleOptions,
    uiStepCount,
    paramStepCount,
    paramStreamTargetOptions,
    paramStreamTargetIsActive,
    punchInDurationOptions,
    CUT_MODE_OPTIONS,
    STEP_TARGET_OPTIONS,
    clearEngineModifyFields,
    seedEngineModifyFields,
    normalizeEngineAction,
    normalizeSimpleActionsInModel,
    touchwheelModeOptions,
    touchwheelStepCount,
    normalizeTouchwheelActionsInModel,
    timingOptions,
    BAR_TIMING_PRESETS,
    BAR_TIMING_CUSTOM_DEFAULT,
    parseBarTiming,
    typesForTrigger,
    variantsForType,
    defaultVariant,
    supportsTiming,
    supportsRepeat,
    supportsFollowUp,
    supportsRaiseFlag,
    supportsMorph,
    clearRepeatFields,
    normalizeRepeatActionsInModel,
    tempoStepCount,
    normalizeTempoActionsInModel,
    clampBpm,
    clampBpmForDevice,
    clampIncAmountForDevice,
    formatBpmDisplay,
    stripActionFieldsInModel
  }
})()
