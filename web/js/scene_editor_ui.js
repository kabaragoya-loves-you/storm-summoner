/* Scene editor rendering helpers (used by scene.js) */

window.SceneEditorUi = (function () {
  const OUTPUT_TYPES = [
    { v: 'cc', l: 'Control Change' },
    { v: 'note', l: 'Notes' },
    { v: 'lfo_rate', l: 'LFO Rate' },
    { v: 'lfo_depth', l: 'LFO Depth' },
    { v: 'lfo1_rate', l: 'LFO1 Rate' },
    { v: 'lfo1_depth', l: 'LFO1 Depth' },
    { v: 'lfo2_rate', l: 'LFO2 Rate' },
    { v: 'lfo2_depth', l: 'LFO2 Depth' },
    { v: 'rtg_rate', l: 'RTG Rate' },
    { v: 'sh_rate', l: 'S+H Rate' },
    { v: 'pitch_bend', l: 'Pitch Bend' },
    { v: 'tempo_nudge', l: 'Tempo Nudge' }
  ]

  const POLARITY = [
    { v: 0, l: 'Unipolar' },
    { v: 1, l: 'Bipolar' },
    { v: 2, l: 'Inverted' }
  ]

  const TILT_POLARITY = [
    { v: 0, l: 'Unipolar' },
    { v: 2, l: 'Inverted' }
  ]

  function tiltPolaritySelectValue (polarity) {
    return polarity === 2 ? 2 : 0
  }

  const CURVE = [
    { v: 0, l: 'Linear' },
    { v: 1, l: 'Exponential' },
    { v: 2, l: 'Logarithmic' },
    { v: 3, l: 'S-Curve' }
  ]

  const LFO_TARGET = [
    { v: 'lfo1', l: 'LFO 1' },
    { v: 'lfo2', l: 'LFO 2' },
    { v: 'both', l: 'Both' }
  ]

  const VELOCITY_MODE = [
    { v: 'fixed', l: 'Fixed' },
    { v: 'gate_voltage', l: 'Gate Voltage' },
    { v: 'touchwheel', l: 'Touchwheel' }
  ]

  const CV_GATE_VELOCITY_MODE = [
    { v: 'fixed', l: 'Fixed' },
    { v: 'gate_voltage', l: 'Gate Voltage' },
    { v: 'touchwheel', l: 'Touchwheel' },
    { v: 'proximity', l: 'Proximity' },
    { v: 'als', l: 'ALS' },
    { v: 'tilt_x', l: 'Tilt X' },
    { v: 'tilt_y', l: 'Tilt Y' },
    { v: 'lfo1', l: 'LFO 1' },
    { v: 'lfo2', l: 'LFO 2' },
    { v: 'sample_hold', l: 'S+H' }
  ]

  const CV_GATE_CONTROLLED_CALLOUT =
    `<wa-callout variant="neutral">Controlled by CV/Gate.</wa-callout>`

  function cvGateVelocityModeValue (m) {
    if (m.cv_input_mode !== 'note') return m.cv_velocity_mode || 'fixed'
    if (m.cv_velocity_mode && m.cv_velocity_mode !== 'fixed') return m.cv_velocity_mode
    // Legacy: touchwheel_mode velocity supplied CV/Gate velocity before cv_velocity_mode.
    if (m.touchwheel_mode === 'velocity') return 'touchwheel'
    return m.cv_velocity_mode || 'fixed'
  }

  function cvClaimsSource (m, source) {
    return m.cv_input_mode === 'note' && cvGateVelocityModeValue(m) === source
  }

  // Flattened expression mode list (matches device g_expression_mode_mappings order).
  // Continuous routings map to expression_mode 'expression' + a specific output_type.
  const EXPRESSION_USER_MODES = [
    { v: 'disabled', l: 'Disabled' },
    { v: 'control_change', l: 'Control Change', expression_mode: 'expression', output_type: 'cc' },
    { v: 'sustain', l: 'Sustain', expression_mode: 'sustain' },
    { v: 'sostenuto', l: 'Sostenuto', expression_mode: 'sostenuto' },
    { v: 'switch', l: 'Switch', expression_mode: 'switch' },
    { v: 'lfo_rate', l: 'LFO Rate', expression_mode: 'expression', output_type: 'lfo_rate' },
    { v: 'lfo_depth', l: 'LFO Depth', expression_mode: 'expression', output_type: 'lfo_depth' },
    { v: 'notes', l: 'Notes', expression_mode: 'expression', output_type: 'note' },
    { v: 'tempo_nudge', l: 'Tempo Nudge', expression_mode: 'expression', output_type: 'tempo_nudge' }
  ]

  const CV_USER_MODES = [
    { v: 'disabled', l: 'Disabled', cv_input_mode: 'none' },
    { v: 'control_change', l: 'Control Change', cv_input_mode: 'cv', output_type: 'cc' },
    { v: 'cv_gate', l: 'CV/Gate', cv_input_mode: 'note' },
    { v: 'audio', l: 'Audio', cv_input_mode: 'audio' },
    { v: 'trigger', l: 'Trigger', cv_input_mode: 'trigger' },
    { v: 'notes', l: 'Notes', cv_input_mode: 'cv', output_type: 'note' },
    { v: 'lfo_rate', l: 'LFO Rate', cv_input_mode: 'cv', output_type: 'lfo_rate' },
    { v: 'lfo_depth', l: 'LFO Depth', cv_input_mode: 'cv', output_type: 'lfo_depth' },
    { v: 'tempo_nudge', l: 'Tempo Nudge', cv_input_mode: 'cv', output_type: 'tempo_nudge' }
  ]

  // Flattened proximity mode list (matches device g_proximity_mode_mappings order).
  const PROXIMITY_USER_MODES = [
    { v: 'disabled', l: 'Disabled' },
    { v: 'control_change', l: 'Control Change', output_type: 'cc', enabled: true },
    { v: 'notes_theremin', l: 'Notes (Theremin)', output_type: 'note', enabled: true },
    { v: 'lfo_rate', l: 'LFO Rate', output_type: 'lfo_rate', enabled: true },
    { v: 'lfo_depth', l: 'LFO Depth', output_type: 'lfo_depth', enabled: true },
    { v: 'tempo_nudge', l: 'Tempo Nudge', output_type: 'tempo_nudge', enabled: true }
  ]

  // Flattened ALS mode list (matches device g_als_mode_mappings order).
  const ALS_USER_MODES = [
    { v: 'disabled', l: 'Disabled' },
    { v: 'control_change', l: 'Control Change', output_type: 'cc', enabled: true },
    { v: 'notes', l: 'Notes', output_type: 'note', enabled: true },
    { v: 'lfo_rate', l: 'LFO Rate', output_type: 'lfo_rate', enabled: true },
    { v: 'lfo_depth', l: 'LFO Depth', output_type: 'lfo_depth', enabled: true },
    { v: 'tempo_nudge', l: 'Tempo Nudge', output_type: 'tempo_nudge', enabled: true }
  ]

  const LFO1_USER_MODES = [
    { v: 'disabled', l: 'Disabled' },
    { v: 'control_change', l: 'Control Change', output_type: 'cc', enabled: true },
    { v: 'notes', l: 'Notes', output_type: 'note', enabled: true },
    { v: 'lfo2_rate', l: 'LFO 2 Rate', output_type: 'lfo2_rate', enabled: true },
    { v: 'lfo2_depth', l: 'LFO 2 Depth', output_type: 'lfo2_depth', enabled: true },
    { v: 'rtg_rate', l: 'RTG Rate', output_type: 'rtg_rate', enabled: true },
    { v: 'sh_rate', l: 'S+H Rate', output_type: 'sh_rate', enabled: true },
    { v: 'pitch_bend', l: 'Pitch Bend', output_type: 'pitch_bend', enabled: true }
  ]

  const LFO2_USER_MODES = [
    { v: 'disabled', l: 'Disabled' },
    { v: 'control_change', l: 'Control Change', output_type: 'cc', enabled: true },
    { v: 'notes', l: 'Notes', output_type: 'note', enabled: true },
    { v: 'lfo1_rate', l: 'LFO 1 Rate', output_type: 'lfo1_rate', enabled: true },
    { v: 'lfo1_depth', l: 'LFO 1 Depth', output_type: 'lfo1_depth', enabled: true },
    { v: 'rtg_rate', l: 'RTG Rate', output_type: 'rtg_rate', enabled: true },
    { v: 'sh_rate', l: 'S+H Rate', output_type: 'sh_rate', enabled: true },
    { v: 'pitch_bend', l: 'Pitch Bend', output_type: 'pitch_bend', enabled: true }
  ]

  const NOTE_TRACK_USER_MODES = [
    { v: 'disabled', l: 'Disabled' },
    { v: 'control_change', l: 'Control Change', output_type: 'cc', enabled: true },
    { v: 'lfo_rate', l: 'LFO Rate', output_type: 'lfo_rate', enabled: true },
    { v: 'lfo_depth', l: 'LFO Depth', output_type: 'lfo_depth', enabled: true },
    { v: 'pitch_bend', l: 'Pitch Bend', output_type: 'pitch_bend', enabled: true },
    { v: 'tempo_nudge', l: 'Tempo Nudge', output_type: 'tempo_nudge', enabled: true }
  ]

  const TILT_USER_MODES = [
    { v: 'disabled', l: 'Disabled' },
    { v: 'control_change', l: 'Control Change', output_type: 'cc', enabled: true },
    { v: 'notes', l: 'Notes', output_type: 'note', enabled: true },
    { v: 'lfo_rate', l: 'LFO Rate', output_type: 'lfo_rate', enabled: true },
    { v: 'lfo_depth', l: 'LFO Depth', output_type: 'lfo_depth', enabled: true },
    { v: 'pitch_bend', l: 'Pitch Bend', output_type: 'pitch_bend', enabled: true },
    { v: 'tempo_nudge', l: 'Tempo Nudge', output_type: 'tempo_nudge', enabled: true }
  ]

  const SAMPLE_HOLD_USER_MODES = [
    { v: 'disabled', l: 'Disabled' },
    { v: 'continuous', l: 'Continuous', mode: 'continuous', enabled: true },
    { v: 'step', l: 'Step', mode: 'step', enabled: true }
  ]

  const RTG_USER_MODES = [
    { v: 'disabled', l: 'Disabled' },
    { v: 'continuous', l: 'Continuous', mode: 'continuous', enabled: true },
    { v: 'step', l: 'Step', mode: 'step', enabled: true }
  ]

  const ENGINE_START_MODE = [
    { v: 'running', l: 'Running' },
    { v: 'paused', l: 'Paused' },
    { v: 'transport', l: 'Follow Transport' }
  ]

  const LFO_TRIGGER_TIMING = [
    { v: 'immediate', l: 'Immediate' },
    { v: 'beat', l: 'Next Beat' },
    { v: 'bar', l: 'Next Bar' }
  ]

  const LFO_RESOLUTION = [
    { v: 'auto', l: 'Auto' },
    { v: 'coarse', l: 'Coarse' },
    { v: 'medium', l: 'Medium' },
    { v: 'fine', l: 'Fine' },
    { v: 'manual', l: 'Manual' }
  ]

  const SHEPARD_DIRECTION = [
    { v: 'rising', l: 'Rising' },
    { v: 'falling', l: 'Falling' }
  ]

  const SHEPARD_STYLE = [
    { v: 'stream', l: 'Stream' },
    { v: 'wide', l: 'Wide' },
    { v: 'crossfade', l: 'Crossfade' }
  ]

  const SHEPARD_LAYOUT = [
    { v: 'single', l: 'Single' },
    { v: 'multi', l: 'Multi-Ch' }
  ]

  const SHEPARD_FADE = [
    { v: 'none', l: 'None' },
    { v: 'cc11', l: 'CC11' },
    { v: 'poly_at', l: 'Poly AT' }
  ]

  const AUDIO_ATTACK_OPTIONS = [5, 10, 20, 30, 50, 75, 100].map(v => ({
    v,
    l: `${v} ms`
  }))

  const AUDIO_RELEASE_OPTIONS = [50, 100, 200, 300, 500, 750, 1000, 1500, 2000].map(v => ({
    v,
    l: `${v} ms`
  }))

  const AUDIO_RANGE_OPTIONS = [
    { v: 'bi5v', l: '±5V' },
    { v: 'bi10v', l: '±10V' }
  ]

  const AUDIO_POLARITY_OPTIONS = [
    { v: 'attract', l: 'Attract' },
    { v: 'repel', l: 'Repel (Duck)' }
  ]

  const TOUCHWHEEL_USER_MODES = [
    { v: 'disabled', l: 'Disabled' },
    { v: 'pads', l: 'Pads', touchwheel_mode: 'pads' },
    { v: 'control_change', l: 'Control Change', touchwheel_mode: 'continuous', output_type: 'cc', touchwheel_style: 'endless', supports_style: true },
    { v: 'program_change', l: 'Program Change', touchwheel_mode: 'program_change' },
    { v: 'tempo', l: 'Tempo', touchwheel_mode: 'set_tempo', touchwheel_style: 'endless', supports_style: true },
    { v: 'pitch_bend', l: 'Pitch Bend', touchwheel_mode: 'pitch_bend' },
    { v: 'aftertouch', l: 'After Touch', touchwheel_mode: 'aftertouch', touchwheel_style: 'odometer', supports_style: true },
    { v: 'notes', l: 'Notes', touchwheel_mode: 'continuous', output_type: 'note', touchwheel_style: 'odometer', supports_style: true },
    { v: 'double_cc', l: 'Double CC', touchwheel_mode: 'double_cc', touchwheel_style: 'endless', supports_style: true },
    { v: 'velocity', l: 'Velocity', touchwheel_mode: 'velocity', supports_style: true },
    { v: 'lfo_rate', l: 'LFO Rate', touchwheel_mode: 'lfo_rate', touchwheel_style: 'odometer', supports_style: true, lfo_target: true },
    { v: 'lfo_depth', l: 'LFO Depth', touchwheel_mode: 'lfo_depth', touchwheel_style: 'odometer', supports_style: true, lfo_target: true },
    { v: 'rtg_rate', l: 'RTG Rate', touchwheel_mode: 'rtg_rate', touchwheel_style: 'odometer', supports_style: true },
    { v: 'tempo_nudge', l: 'Tempo Nudge', touchwheel_mode: 'continuous', output_type: 'tempo_nudge', touchwheel_style: 'bipolar', supports_style: true }
  ]

  const TOUCHWHEEL_STYLE_OPTIONS = [
    { v: 'odometer', l: 'Odometer' },
    { v: 'endless', l: 'Endless' }
  ]

  const NOTE_OCTAVE_OPTIONS = Array.from({ length: 10 }, (_, i) => {
    const n = i + 1
    return { v: n * 12, l: `${n} Octave${n > 1 ? 's' : ''}` }
  })

  const SENSOR_NOTE_OCTAVE_OPTIONS = NOTE_OCTAVE_OPTIONS.slice(0, 5)

  const SCREEN_MODULES = [
    { v: 'beat', l: 'Beat Grid' },
    { v: 'khyron', l: 'Khyron' },
    { v: 'space', l: 'Space' },
    { v: 'summoner', l: 'Summoner' },
    { v: 'pixels', l: 'Pixels' }
  ]

  const REPEAT_DIVISIONS = [
    { v: '16_bars', l: '16 Bars' },
    { v: '12_bars', l: '12 Bars' },
    { v: '8_bars', l: '8 Bars' },
    { v: '4_bars', l: '4 Bars' },
    { v: '2_bars', l: '2 Bars' },
    { v: '1_bar', l: '1 Bar' },
    { v: 'half', l: 'Half' },
    { v: 'quarter', l: 'Quarter' },
    { v: 'eighth', l: 'Eighth' },
    { v: 'sixteenth', l: 'Sixteenth' },
    { v: '32nd', l: '32nd' }
  ]

  const PROBABILITY = Array.from({ length: 10 }, (_, i) => {
    const v = (i + 1) * 10
    return { v, l: `${v}%` }
  })

  const PATTERN_LENGTHS = [
    { v: 0, l: 'Off' },
    ...Array.from({ length: 7 }, (_, i) => ({ v: i + 2, l: String(i + 2) }))
  ]

  let _openSections = new Set()

  function actionContext (ctrl) {
    return {
      sceneMode: ctrl.deviceContext?.sceneMode ?? 2,
      confirmChange: ctrl.deviceContext?.confirmChange ?? 0,
      clockSource: ctrl.editModel?.clock_source || 'internal'
    }
  }

  function normalizeUiModule (name) {
    if (!name || name === 'scene') return 'beat'
    if (name === 'buttons') return 'space'
    return name
  }

  function screenModuleOptions (value) {
    const current = normalizeUiModule(value || 'beat')
    if (SCREEN_MODULES.some(o => o.v === current)) return SCREEN_MODULES
    return [{ v: current, l: current }, ...SCREEN_MODULES]
  }

  function esc (s) {
    return String(s ?? '')
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/"/g, '&quot;')
  }

  function fieldRow (label, inner) {
    return `<div class="scene-field-row"><label class="scene-field-label">${esc(
      label
    )}</label>
      <div class="scene-field-control">${inner}</div></div>`
  }

  function selectField (path, value, options, action) {
    const opts = options
      .map(o => {
        const sel = String(o.v) === String(value) ? ' selected' : ''
        return `<option value="${esc(o.v)}"${sel}>${esc(o.l)}</option>`
      })
      .join('')
    return `<select class="scene-select" data-scene-path="${esc(path)}"
      data-action="change->scene#patchSelect">${opts}</select>`
  }

  function numberField (path, value, min, max, action) {
    return `<input type="number" class="scene-input" min="${min}" max="${max}"
      value="${esc(value)}" data-scene-path="${esc(path)}"
      data-action="input->scene#patchNumber">`
  }

  function checkboxField (path, checked, action) {
    return `<input type="checkbox" class="scene-checkbox"
      ${checked ? 'checked' : ''} data-scene-path="${esc(path)}"
      data-action="change->scene#patchCheckbox">`
  }

  function textField (path, value, maxLen) {
    return `<input type="text" class="scene-input" maxlength="${maxLen}"
      value="${esc(value)}" data-scene-path="${esc(path)}"
      data-action="input->scene#patchText">`
  }

  function ensureCcNumbers (m) {
    if (!m.cc_numbers) m.cc_numbers = []
    while (m.cc_numbers.length < 4) m.cc_numbers.push(0)
    if (!m.cc_number && m.cc_numbers[0]) m.cc_number = m.cc_numbers[0]
  }

  // Preserve explicit slot count; trim only trailing inactive slots (min 1).
  function normalizeCcSlotList (raw) {
    let list = (raw || []).slice(0, 4)
    if (list.length === 0) return [0]
    while (list.length > 1 && Number(list[list.length - 1]) === 0) list.pop()
    return list
  }

  function applyCcSlotFields (mapping, list) {
    mapping.cc_numbers = list
    mapping.num_cc_numbers = list.filter(cc => Number(cc) > 0).length
    const firstActive = list.find(cc => Number(cc) > 0)
    if (firstActive) mapping.cc_number = firstActive
  }

  function syncTouchwheelCcNumbers (tw) {
    applyCcSlotFields(tw, normalizeCcSlotList(tw?.cc_numbers))
  }

  function lfoRoutingOptions (n) {
    const opts = [
      { v: 'cc', l: 'Control Change' },
      { v: 'note', l: 'Notes' }
    ]
    if (n === 1) {
      opts.push({ v: 'lfo2_rate', l: 'LFO 2 Rate' })
      opts.push({ v: 'lfo2_depth', l: 'LFO 2 Depth' })
    } else {
      opts.push({ v: 'lfo1_rate', l: 'LFO 1 Rate' })
      opts.push({ v: 'lfo1_depth', l: 'LFO 1 Depth' })
    }
    opts.push(
      { v: 'rtg_rate', l: 'RTG Rate' },
      { v: 'sh_rate', l: 'S+H Rate' },
      { v: 'pitch_bend', l: 'Pitch Bend' }
    )
    return opts
  }

  function syncLfoCcNumbers (mapping) {
    applyCcSlotFields(mapping, normalizeCcSlotList(mapping?.cc_numbers))
  }

  function renderLfoCcSlots (ctrl, mappingPath, mapping, opts = {}) {
    syncLfoCcNumbers(mapping)
    const device = ctrl.deviceDefinition
    const slotCap = opts.ccSlotMax ?? 4
    const ccList = mapping.cc_numbers.slice(0, slotCap)
    const maxSlots = opts.ccSlotMax ?? (DeviceControls.hasParameters(device)
      ? Math.min(4, DeviceControls.parameterCount(device))
      : 4)
    const ccBySlot = ccList.map(cc =>
      DeviceControls.resolveParameterCc(device, cc)
    )
    const usedCcs = new Set(ccBySlot.map(Number).filter(n => n > 0))
    let rows = ''
    for (let i = 0; i < ccList.length; i++) {
      const cc = ccBySlot[i]
      const exclude = new Set(usedCcs)
      if (Number(cc) > 0) exclude.delete(Number(cc))
      let body = fieldRow(
        'Parameter',
        paramField(ctrl, `${mappingPath}.cc_numbers.${i}`, cc, { exclude, keep: cc })
      )
      const add =
        ccList.length < maxSlots && i === ccList.length - 1
          ? slotAddButton(mappingPath, 'lfo-cc', 'Add parameter', maxSlots)
          : ''
      rows += slotBlock(`Slot ${i + 1}`, body, i > 0 ? i : -1, add)
    }
    return slotGroup({
      path: mappingPath,
      kind: 'lfo-cc',
      min: 1,
      max: maxSlots,
      def: 0,
      rows
    })
  }

  function renderTouchwheelCcSlots (ctrl) {
    const m = ctrl.editModel
    if (!m.touchwheel) m.touchwheel = { enabled: true }
    const tw = m.touchwheel
    syncTouchwheelCcNumbers(tw)
    const device = ctrl.deviceDefinition
    const ccList = tw.cc_numbers
    const maxSlots = DeviceControls.hasParameters(device)
      ? Math.min(4, DeviceControls.parameterCount(device))
      : 4
    const ccBySlot = ccList.map(cc =>
      DeviceControls.resolveParameterCc(device, cc)
    )
    const usedCcs = new Set(ccBySlot.map(Number).filter(n => n > 0))
    let rows = ''
    for (let i = 0; i < ccList.length; i++) {
      const cc = ccBySlot[i]
      const exclude = new Set(usedCcs)
      if (Number(cc) > 0) exclude.delete(Number(cc))
      let body = fieldRow(
        'Parameter',
        paramField(ctrl, `touchwheel.cc_numbers.${i}`, cc, { exclude, keep: cc })
      )
      const add =
        ccList.length < maxSlots && i === ccList.length - 1
          ? slotAddButton('touchwheel', 'touchwheel-cc', 'Add parameter', maxSlots)
          : ''
      rows += slotBlock(`Slot ${i + 1}`, body, i > 0 ? i : -1, add)
    }
    return slotGroup({
      path: 'touchwheel',
      kind: 'touchwheel-cc',
      min: 1,
      max: maxSlots,
      def: 0,
      rows
    })
  }

  function renderContinuousMapping (ctrl, mappingPath, mapping, opts = {}) {
    const m = mapping || {}
    const enabledPath = `${mappingPath}.enabled`
    const otPath = `${mappingPath}.output_type`
    const ot = m.output_type || 'cc'
    const routingTypes = opts.routingTypes || OUTPUT_TYPES
    const routingLabel = opts.routingLabel || 'Output'
    let html = ''
    if (!opts.hideEnabled) {
      html += fieldRow(
        'Enabled',
        checkboxField(enabledPath, m.enabled !== false)
      )
      if (m.enabled === false) return html
    }

    if (!opts.hideRouting) {
      html += fieldRow(routingLabel, selectField(otPath, ot, routingTypes))
    }
    ensureCcNumbers(m)

    if (ot === 'cc') {
      if (opts.ccSlots) {
        html += renderLfoCcSlots(ctrl, mappingPath, m, opts)
      } else {
        for (let i = 0; i < 4; i++) {
          html += fieldRow(
            `CC slot ${i + 1}`,
            numberField(
              `${mappingPath}.cc_numbers.${i}`,
              m.cc_numbers[i] ?? 0,
              0,
              127
            )
          )
        }
      }
      const polarityOpts = opts.polarityOptions || POLARITY
      const polarityVal = opts.polaritySelectValue
        ? opts.polaritySelectValue(m.polarity ?? 0)
        : (m.polarity ?? 0)
      if (!opts.hidePolarity) {
        html += fieldRow(
          'Polarity',
          selectField(`${mappingPath}.polarity`, polarityVal, polarityOpts)
        )
      }
      if (!opts.hideCurve) {
        html += fieldRow(
          'Curve',
          selectField(`${mappingPath}.curve_type`, m.curve_type ?? 0, CURVE)
        )
      }
    } else if (ot === 'note') {
      if (opts.noteSelectors) {
        html += fieldRow(
          'Base note',
          selectField(`${mappingPath}.base_note`, m.base_note ?? 60,
            ActionCatalog.noteNameOptions(m.base_note ?? 60))
        )
        html += fieldRow(
          'Range',
          selectField(
            `${mappingPath}.note_range`,
            m.note_range ?? 24,
            opts.noteOctaveOptions || NOTE_OCTAVE_OPTIONS
          )
        )
      } else {
        html += fieldRow(
          'Base note',
          numberField(`${mappingPath}.base_note`, m.base_note ?? 60, 0, 127)
        )
        html += fieldRow(
          'Range (semitones)',
          numberField(`${mappingPath}.note_range`, m.note_range ?? 24, 1, 127)
        )
      }
      const velMin = opts.velocityMin ?? 1
      if (opts.velocityModePath) {
        const vm = ctrl.getAtPath(opts.velocityModePath) || 'fixed'
        html += fieldRow(
          'Velocity mode',
          selectField(opts.velocityModePath, vm, VELOCITY_MODE)
        )
        if (vm === 'fixed') {
          html += fieldRow(
            'Velocity',
            numberField(`${mappingPath}.velocity`, m.velocity ?? 100, velMin, 127)
          )
        }
      } else {
        html += fieldRow(
          'Velocity',
          numberField(`${mappingPath}.velocity`, m.velocity ?? 100, velMin, 127)
        )
      }
      const polarityOpts = opts.polarityOptions || POLARITY
      const polarityVal = opts.polaritySelectValue
        ? opts.polaritySelectValue(m.polarity ?? 0)
        : (m.polarity ?? 0)
      if (!opts.hidePolarity) {
        html += fieldRow(
          'Polarity',
          selectField(`${mappingPath}.polarity`, polarityVal, polarityOpts)
        )
      }
      if (!opts.hideCurve) {
        html += fieldRow(
          'Curve',
          selectField(`${mappingPath}.curve_type`, m.curve_type ?? 0, CURVE)
        )
      }
    } else if (ot === 'lfo_rate' || ot === 'lfo_depth') {
      html += fieldRow(
        'LFO target',
        selectField(
          `${mappingPath}.lfo_target`,
          m.lfo_target || 'both',
          LFO_TARGET
        )
      )
    } else if (ot === 'tempo_nudge' && opts.tempoNudgePath) {
      const nudgePct = ctrl.getAtPath(opts.tempoNudgePath) ?? 10
      html += fieldRow(
        'Amount',
        opts.tempoNudgeSelect
          ? selectField(
            opts.tempoNudgePath,
            nudgePct,
            ActionCatalog.tempoNudgeAmountOptions(nudgePct)
          )
          : numberField(opts.tempoNudgePath, nudgePct, 0, 100)
      )
      if (opts.tempoNudgeDirectionPath) {
        const dir = ctrl.getAtPath(opts.tempoNudgeDirectionPath) ?? 0
        html += fieldRow(
          'Direction',
          selectField(
            opts.tempoNudgeDirectionPath,
            dir,
            ActionCatalog.tempoNudgeDirectionOptions(dir)
          )
        )
      }
    } else if (ot === 'pitch_bend') {
      const polarityOpts = opts.polarityOptions || POLARITY
      const polarityVal = opts.polaritySelectValue
        ? opts.polaritySelectValue(m.polarity ?? 0)
        : (m.polarity ?? 0)
      if (!opts.hidePolarity) {
        html += fieldRow(
          'Polarity',
          selectField(`${mappingPath}.polarity`, polarityVal, polarityOpts)
        )
      }
      if (!opts.hideCurve) {
        html += fieldRow(
          'Curve',
          selectField(`${mappingPath}.curve_type`, m.curve_type ?? 0, CURVE)
        )
      }
    }

    if (opts.showMinMidMax && ot === 'cc') {
      html += fieldRow(
        'Min',
        numberField(`${mappingPath}.min_value`, m.min_value ?? 0, 0, 127)
      )
      html += fieldRow(
        'Middle',
        numberField(`${mappingPath}.middle_value`, m.middle_value ?? 64, 0, 127)
      )
      html += fieldRow(
        'Max',
        numberField(`${mappingPath}.max_value`, m.max_value ?? 127, 0, 127)
      )
    }

    return html
  }

  function slotValue (field, index) {
    return Array.isArray(field) ? field[index] : index === 0 ? field : undefined
  }

  function paramField (ctrl, path, cc, optionOpts) {
    const device = ctrl.deviceDefinition
    if (DeviceControls.hasParameters(device)) {
      return selectField(
        path,
        cc,
        DeviceControls.parameterOptions(device, optionOpts)
      )
    }
    return numberField(path, cc, 0, 127)
  }

  function valueField (ctrl, path, cc, val) {
    const device = ctrl.deviceDefinition
    const rawCc =
      DeviceControls.resolveCcForValuePath(p => ctrl.getAtPath(p), path) ?? cc
    const ccNum = DeviceControls.resolveParameterCc(device, rawCc)
    if (DeviceControls.hasDiscreteValues(device, ccNum)) {
      return selectField(
        path,
        val,
        DeviceControls.discreteValueOptions(device, ccNum)
      )
    }
    const range = DeviceControls.continuousValueRange(device, ccNum)
    return numberField(path, val, range.min, range.max)
  }

  function presetField (ctrl, path, val) {
    const device = ctrl.deviceDefinition
    const resolved = DeviceControls.resolvePresetValue(device, val)
    if (DeviceControls.hasPresetCatalog(device)) {
      return selectField(path, resolved, DeviceControls.presetOptions(device, resolved))
    }
    const { min, max } = DeviceControls.presetRange(device)
    return numberField(path, resolved, min, max)
  }

  function sceneSetField (ctrl, path, a) {
    const exclude = SceneActions.currentEditIndex(ctrl)
    const stored = SceneActions.resolveStoredIndex(ctrl.sceneList, a.number, exclude)
    return selectField(path, stored,
      SceneActions.setOptions(ctrl.sceneList, stored, exclude))
  }

  function presetReleaseField (ctrl, path, a) {
    const device = ctrl.deviceDefinition
    const opts = [{ v: '__original__', l: 'Original' }]
    if (DeviceControls.hasPresetCatalog(device)) {
      opts.push(...DeviceControls.presetOptions(device, a.release_preset))
    } else {
      const { count, min } = DeviceControls.presetRange(device)
      for (let i = 1; i <= count; i++) {
        opts.push({ v: min + (i - 1), l: String(i) })
      }
    }
    const cur = a.release_to_original
      ? '__original__'
      : DeviceControls.resolvePresetValue(device, a.release_preset)
    return selectField(path, cur, opts)
  }

  function touchwheelReleaseField (path, a) {
    const opts = [{ v: '__original__', l: 'Original' },
      ...ActionCatalog.touchwheelModeOptions(a.mode2)]
    const cur = a.release_to_original ? '__original__' : (a.mode2 ?? 0)
    return selectField(`${path}.mode2`, cur, opts)
  }

  function paramReleaseField (ctrl, path, a) {
    const device = ctrl.deviceDefinition
    const pressCc = DeviceControls.resolveParameterCc(device, a.param)
    const opts = [{ v: '__original__', l: 'Original' },
      ...DeviceControls.parameterOptions(device, { exclude: new Set([pressCc]) })]
    const cur = a.release_to_original
      ? '__original__'
      : DeviceControls.resolveParameterCc(device, a.param2)
    return selectField(`${path}.param2`, cur, opts)
  }

  function renderLfoFields (ctrl, path, a) {
    const v = a.variant || 'modify'
    let html = ''
    if (v === 'start' || v === 'stop' || v === 'toggle' || v === 'modify') {
      html += fieldRow(
        'Target',
        selectField(
          `${path}.slot`,
          a.slot ?? 1,
          ActionCatalog.lfoTargetOptions(a.slot)
        )
      )
    }
    if (v === 'modify') {
      html += fieldRow(
        'Waveform',
        selectField(
          `${path}.waveform`,
          a.waveform ?? 255,
          ActionCatalog.lfoModifyWaveformOptions(a.waveform)
        )
      )
      html += fieldRow(
        'Rate mode',
        selectField(
          `${path}.rate_mode`,
          a.rate_mode ?? 255,
          ActionCatalog.lfoModifyRateModeOptions(a.rate_mode)
        )
      )
      html += fieldRow(
        'Rate',
        selectField(
          `${path}.rate_hz_x100`,
          a.rate_hz_x100 ?? 65535,
          ActionCatalog.lfoModifyRateHzOptions(a.rate_hz_x100)
        )
      )
      html += fieldRow(
        'Division',
        selectField(
          `${path}.division`,
          a.division ?? 255,
          ActionCatalog.lfoModifyDivisionOptions(a.division)
        )
      )
      html += fieldRow(
        'Floor',
        selectField(
          `${path}.floor`,
          a.floor ?? 255,
          ActionCatalog.lfoModifyFloorCeilingOptions(a.floor)
        )
      )
      html += fieldRow(
        'Ceiling',
        selectField(
          `${path}.ceiling`,
          a.ceiling ?? 255,
          ActionCatalog.lfoModifyFloorCeilingOptions(a.ceiling)
        )
      )
      html += fieldRow(
        'Resolution',
        selectField(
          `${path}.resolution_mode`,
          a.resolution_mode ?? 255,
          ActionCatalog.lfoModifyResolutionOptions(a.resolution_mode)
        )
      )
      if (Number(a.resolution_mode) === ActionCatalog.LFO_RESOLUTION_MANUAL) {
        html += fieldRow(
          'Steps',
          selectField(
            `${path}.manual_steps`,
            a.manual_steps ?? 0,
            ActionCatalog.lfoModifyManualStepsOptions(a.manual_steps)
          )
        )
      }
    }
    return html
  }

  const CUT_MODES = ActionCatalog.CUT_MODE_OPTIONS

  const BOOMERANG_PHASE_MODES = [
    { v: 'instant', l: 'Immediate' },
    { v: 'division', l: 'Division' },
    { v: 'time_ms', l: 'Time' }
  ]

  const BOOMERANG_TIME_PRESETS_MS = [
    25, 50, 100, 150, 200, 300, 500, 750, 1000,
    1500, 2000, 3000, 5000, 7500, 10000, 15000, 20000, 30000, 45000, 60000
  ]

  const BOOMERANG_CURVES = [
    { v: 0, l: 'Linear' },
    { v: 1, l: 'Exponential' },
    { v: 2, l: 'Logarithmic' },
    { v: 3, l: 'S-Curve' },
    { v: 4, l: 'Inverse S' },
    { v: 5, l: 'Quadratic' },
    { v: 6, l: 'Square Root' },
    { v: 7, l: 'Sine' },
    { v: 8, l: 'Custom' }
  ]

  const BOOMERANG_SLOPE = [
    { v: 0, l: 'Gentle' },
    { v: 1, l: 'Medium' },
    { v: 2, l: 'Steep' }
  ]

  const BOOMERANG_PHASE_DIVISIONS = [
    { v: '1_beat', l: '1 Beat' },
    { v: '2_beats', l: '2 Beats' },
    { v: '3_beats', l: '3 Beats' },
    { v: '1_bar', l: '1 Bar' },
    { v: '2_bars', l: '2 Bars' },
    { v: '3_bars', l: '3 Bars' },
    { v: '4_bars', l: '4 Bars' },
    { v: 'beat_2', l: 'Beat 2' },
    { v: 'beat_3', l: 'Beat 3' },
    { v: 'beat_4', l: 'Beat 4' }
  ]

  function boomerangDivisionValue (stored) {
    const legacy = { beat: '1_beat', bar: '1_bar' }
    return legacy[stored] || stored || '1_beat'
  }

  function boomerangDivisionOptions (stored) {
    const cur = boomerangDivisionValue(stored)
    const opts = BOOMERANG_PHASE_DIVISIONS.slice()
    if (cur && !opts.some(o => o.v === cur)) {
      opts.unshift({ v: cur, l: cur.replace(/_/g, ' ') })
    }
    return opts
  }

  function boomerangTimeLabel (ms) {
    if (ms < 1000) return `${ms} ms`
    const s = ms / 1000
    return Number.isInteger(s) ? `${s} s` : `${s.toFixed(1)} s`
  }

  function boomerangTimeOptions (stored) {
    const cur = Number(stored ?? 1000)
    const opts = BOOMERANG_TIME_PRESETS_MS.map(v => ({ v, l: boomerangTimeLabel(v) }))
    if (!opts.some(o => o.v === cur)) {
      opts.unshift({ v: cur, l: boomerangTimeLabel(cur) })
    }
    return opts
  }

  function boomerangOriginOptions () {
    const opts = [{ v: 'current', l: 'Current' }]
    for (let i = 0; i <= 127; i++) opts.push({ v: i, l: String(i) })
    return opts
  }

  function boomerangOriginSelection (a) {
    return (a.start_mode || 'current') === 'current' ? 'current' : (a.start_value ?? 0)
  }

  function boomerangTargetOptions () {
    const opts = [{ v: 'random', l: 'Random' }]
    for (let i = 0; i <= 127; i++) opts.push({ v: i, l: String(i) })
    return opts
  }

  function boomerangTargetSelection (a, ot) {
    if ((a.target_mode || 'explicit') === 'random') return 'random'
    let v = a.target_value ?? 127
    if (ot === 'pitch_bend') {
      v = Math.min(127, Math.round(v / 128))
    }
    return v
  }

  const BOOM_NOTIFY = ' change->boomerang-fields#notify'

  function boomerangSelectField (path, value, options) {
    const opts = options
      .map(o => {
        const sel = String(o.v) === String(value) ? ' selected' : ''
        return `<option value="${esc(o.v)}"${sel}>${esc(o.l)}</option>`
      })
      .join('')
    return `<select class="scene-select" data-scene-path="${esc(path)}"
      data-action="change->scene#patchSelect${BOOM_NOTIFY}">${opts}</select>`
  }

  function boomerangNumberField (path, value, min, max) {
    return `<input type="number" class="scene-input" min="${min}" max="${max}"
      value="${esc(value)}" data-scene-path="${esc(path)}"
      data-action="input->scene#patchNumber input->boomerang-fields#notify">`
  }

  function boomerangValueField (ctrl, path, cc, val) {
    const device = ctrl.deviceDefinition
    const rawCc =
      DeviceControls.resolveCcForValuePath(p => ctrl.getAtPath(p), path) ?? cc
    const ccNum = DeviceControls.resolveParameterCc(device, rawCc)
    if (DeviceControls.hasDiscreteValues(device, ccNum)) {
      const opts = DeviceControls.discreteValueOptions(device, ccNum)
      const inner = opts
        .map(o => {
          const sel = String(o.v) === String(val) ? ' selected' : ''
          return `<option value="${esc(o.v)}"${sel}>${esc(o.l)}</option>`
        })
        .join('')
      return `<select class="scene-select" data-scene-path="${esc(path)}"
        data-action="change->scene#patchSelect${BOOM_NOTIFY}">${inner}</select>`
    }
    const range = DeviceControls.continuousValueRange(device, ccNum)
    return boomerangNumberField(path, val, range.min, range.max)
  }

  function renderCcValuePair (ctrl, path, ccKey, valKey, cc, val) {
    const ccPath = `${path}.${ccKey}`
    const valPath = `${path}.${valKey}`
    const ccNum = DeviceControls.resolveParameterCc(ctrl.deviceDefinition, cc)
    let html = fieldRow(
      'CC',
      paramField(ctrl, ccPath, ccNum)
    )
    html += fieldRow(
      'Value',
      valueField(ctrl, valPath, ccNum, val)
    )
    return html
  }

  function renderClockFields (ctrl, path, a) {
    const v = a.variant || 'toggle'
    if (v === 'burst') {
      return fieldRow(
        'Speed',
        selectField(
          `${path}.speed_percent`,
          a.speed_percent ?? 100,
          ActionCatalog.clockBurstSpeedOptions(a.speed_percent)
        )
      )
    }
    const isHold = v === 'hold'
    return fieldRow(
      isHold ? 'Press' : 'First action',
      selectField(
        `${path}.start_enabled`,
        a.start_enabled ? 'enable' : 'disable',
        [
          { v: 'enable', l: isHold ? 'Enable' : 'Enable first' },
          { v: 'disable', l: isHold ? 'Disable' : 'Disable first' }
        ]
      )
    )
  }

  function renderCutFields (ctrl, path, a) {
    return fieldRow(
      'Cut target',
      selectField(
        `${path}.cut_mode`,
        a.cut_mode || 'both',
        CUT_MODES
      )
    )
  }

  function renderUiFields (ctrl, path, a) {
    const v = a.variant || 'set'
    if (v === 'set') {
      return fieldRow(
        'Module',
        selectField(
          `${path}.module`,
          a.module ?? 0,
          ActionCatalog.uiModuleOptions(a.module)
        )
      )
    }
    if (v === 'hold') {
      let html = fieldRow(
        'Press',
        selectField(
          `${path}.module`,
          a.module ?? 0,
          ActionCatalog.uiModuleOptions(a.module)
        )
      )
      html += fieldRow(
        'Release',
        selectField(
          `${path}.module2`,
          a.module2 ?? 0,
          ActionCatalog.uiModuleOptions(a.module2)
        )
      )
      return html
    }
    if (v === 'cycle') return renderUiCycle(ctrl, path, a)
    return ''
  }

  function renderUiCycle (ctrl, path, a) {
    const stepCount = ActionCatalog.uiStepCount(a)
    const steps = Array.isArray(a.modules) ? a.modules.slice() : []
    const stepOpts = []
    for (let i = 2; i <= 8; i++) stepOpts.push({ v: i, l: String(i) })

    let html = fieldRow(
      'Steps',
      selectField(`${path}.num_modules`, stepCount, stepOpts)
    )
    html += `<div class="scene-cycle scene-ui-cycle" style="--cycle-steps: ${stepCount}">`
    html += '<div class="scene-cycle-row scene-cycle-row-head">'
    html += '<div class="scene-cycle-param-col">Module</div>'
    for (let i = 0; i < stepCount; i++) {
      html += `<div class="scene-cycle-step-col"><span class="scene-cycle-step-label">Step ${i + 1}</span></div>`
    }
    html += '</div><div class="scene-cycle-row">'
    html += '<div class="scene-cycle-param-col" aria-hidden="true"></div>'
    for (let i = 0; i < stepCount; i++) {
      html += `<div class="scene-cycle-step-col">${selectField(
        `${path}.modules.${i}`,
        steps[i] ?? 0,
        ActionCatalog.uiModuleOptions(steps[i])
      )}</div>`
    }
    html += '</div></div>'
    return html
  }

  function renderParamTargetField (ctrl, path, a) {
    const cur = a.target || 'touchwheel'
    const opts = ActionCatalog.paramStreamTargetOptions(ctrl.editModel, cur)
    return fieldRow('Target', selectField(`${path}.target`, cur, opts))
  }

  function renderParamFields (ctrl, path, a) {
    const v = a.variant || 'hold'
    const device = ctrl.deviceDefinition
    let html = renderParamTargetField(ctrl, path, a)
    if (v === 'hold') {
      const pressCc = DeviceControls.resolveParameterCc(device, a.param)
      const relCc = a.release_to_original
        ? null
        : DeviceControls.resolveParameterCc(device, a.param2)
      html += fieldRow(
        'Press',
        paramField(ctrl, `${path}.param`, pressCc,
          { exclude: relCc == null ? new Set() : new Set([relCc]), keep: pressCc })
      )
      html += fieldRow('Release', paramReleaseField(ctrl, path, a))
      return html
    }
    if (v === 'cycle') {
      html += renderParamCycle(ctrl, path, a)
      return html
    }
    return html
  }

  function renderParamCycle (ctrl, path, a) {
    const device = ctrl.deviceDefinition
    const stepCount = ActionCatalog.paramStepCount(a)
    const steps = Array.isArray(a.params) ? a.params.slice() : []

    let html = `<div class="scene-cycle scene-param-cycle" style="--cycle-steps: ${stepCount}">`
    html += '<div class="scene-cycle-row scene-cycle-row-head">'
    html += '<div class="scene-cycle-param-col">Parameter</div>'
    for (let i = 0; i < stepCount; i++) {
      const add = (i === stepCount - 1 && stepCount < 8)
        ? slotAddButton(path, 'param-cycle-step', 'Add step', 8) : ''
      const remove = i > 1
        ? `<wa-button class="scene-slot-remove scene-cycle-step-remove" size="small" appearance="text" variant="neutral"
            data-controller="slots" data-slots-path-value="${esc(path)}" data-slots-kind-value="param-cycle-step"
            data-slots-min-value="2" data-slots-max-value="8" data-slots-default-value="0"
            data-action="click->slots#remove" data-slot-index="${i}"
            title="Remove step ${i + 1}" aria-label="Remove step ${i + 1}"
            ><wa-icon name="xmark"></wa-icon></wa-button>`
        : ''
      html += `<div class="scene-cycle-step-col"><span class="scene-cycle-step-label">Step ${
        i + 1
      }</span>${remove}${add}</div>`
    }
    html += '</div>'

    html += '<div class="scene-cycle-row">'
    html += '<div class="scene-cycle-param-col" aria-hidden="true"></div>'
    for (let i = 0; i < stepCount; i++) {
      const cc = DeviceControls.resolveParameterCc(device, steps[i])
      const used = new Set(steps.filter((_, j) => j !== i).map(c => Number(c)))
      html += `<div class="scene-cycle-step-col">${paramField(
        ctrl,
        `${path}.params.${i}`,
        cc,
        { exclude: used, keep: cc }
      )}</div>`
    }
    html += '</div></div>'
    return html
  }

  function renderEngineModifyFields (ctrl, path, a) {
    let html = fieldRow(
      'Rate mode',
      selectField(
        `${path}.rate_mode`,
        a.rate_mode ?? 255,
        ActionCatalog.engineModifyRateModeOptions(a.rate_mode)
      )
    )
    html += fieldRow(
      'Rate',
      selectField(
        `${path}.rate_hz_x100`,
        a.rate_hz_x100 ?? 65535,
        ActionCatalog.engineModifyRateHzOptions(a.rate_hz_x100)
      )
    )
    html += fieldRow(
      'Divider',
      selectField(
        `${path}.division`,
        a.division ?? 255,
        ActionCatalog.engineModifyDivisionOptions(a.division)
      )
    )
    html += fieldRow(
      'Glide',
      selectField(
        `${path}.glide`,
        a.glide ?? 255,
        ActionCatalog.engineModifyGlideOptions(a.glide)
      )
    )
    html += fieldRow(
      'Probability',
      selectField(
        `${path}.probability`,
        a.probability ?? 255,
        ActionCatalog.engineModifyProbOptions(a.probability)
      )
    )
    return html
  }

  function renderEngineFields (ctrl, path, a) {
    const v = a.variant || 'toggle'
    if (v === 'modify') return renderEngineModifyFields(ctrl, path, a)
    return ''
  }

  function renderPunchInFields (ctrl, path, a) {
    const startBody = renderCcValuePair(
      ctrl, path, 'start_cc', 'start_value', a.start_cc, a.start_value
    )
    const finishBody = renderCcValuePair(
      ctrl, path, 'finish_cc', 'finish_value', a.finish_cc, a.finish_value
    )
    let html = `<div class="scene-slots">${
      slotBlock('Start', startBody, -1, '')
    }${slotBlock('Finish', finishBody, -1, '')}</div>`
    const beats = ctrl.editModel?.time_signature?.numerator ?? 4
    const dur = a.duration || '1_bar'
    const durOpts = ActionCatalog.punchInDurationOptions(beats)
    if (!durOpts.some(o => o.v === dur)) durOpts.unshift({ v: dur, l: dur })
    html += fieldRow(
      'Duration',
      selectField(`${path}.duration`, dur, durOpts)
    )
    return html
  }

  function renderFlagCeremonyFields (ctrl, path, a) {
    const upBody = renderCcValuePair(
      ctrl, path, 'flag_up_cc', 'flag_up_value', a.flag_up_cc, a.flag_up_value
    )
    const downBody = renderCcValuePair(
      ctrl, path, 'flag_down_cc', 'flag_down_value', a.flag_down_cc, a.flag_down_value
    )
    const rows = slotBlock('Flag Up', upBody, -1, '')
      + slotBlock('Flag Down', downBody, -1, '')
    return `<div class="scene-slots">${rows}</div>`
  }

  function renderBoomerangPhase (ctrl, path, a, phase) {
    const modeKey = `${phase}_mode`
    const timeKey = `${phase}_time_ms`
    const divKey = `${phase}_division`
    const curveKey = `${phase}_curve`
    const slopeKey = `${phase}_curve_slope`
    const mode = a[modeKey] || 'instant'
    const phaseTitle = `${phase.charAt(0).toUpperCase()}${phase.slice(1)}`
    let html = fieldRow(
      `${phaseTitle} mode`,
      boomerangSelectField(`${path}.${modeKey}`, mode, BOOMERANG_PHASE_MODES)
    )
    if (mode === 'division') {
      html += fieldRow(
        `${phaseTitle} division`,
        boomerangSelectField(
          `${path}.${divKey}`,
          boomerangDivisionValue(a[divKey]),
          boomerangDivisionOptions(a[divKey])
        )
      )
    } else if (mode === 'time_ms') {
      html += fieldRow(
        `${phaseTitle} time`,
        boomerangSelectField(
          `${path}.${timeKey}`,
          a[timeKey] ?? 1000,
          boomerangTimeOptions(a[timeKey])
        )
      )
    }
    if (phase !== 'sustain' && mode !== 'instant') {
      const curve = Number(a[curveKey] ?? 0)
      html += fieldRow(
        `${phase.charAt(0).toUpperCase()}${phase.slice(1)} curve`,
        boomerangSelectField(`${path}.${curveKey}`, curve, BOOMERANG_CURVES)
      )
      if (curve !== 0) {
        html += fieldRow(
          `${phase.charAt(0).toUpperCase()}${phase.slice(1)} slope`,
          boomerangSelectField(`${path}.${slopeKey}`, a[slopeKey] ?? 1, BOOMERANG_SLOPE)
        )
      }
    }
    return html
  }

  function renderBoomerangFields (ctrl, path, a) {
    const ot = a.output_type || 'cc'
    const envSvg = window.BoomerangEnvelope?.renderSvg(a) || ''
    let html = `<div class="scene-boomerang-block" data-controller="boomerang-fields"
      data-boomerang-fields-path-value="${esc(path)}">
      <div class="scene-boomerang-env" data-boomerang-fields-target="envelope"
        data-controller="boomerang-envelope" data-boomerang-envelope-path-value="${esc(path)}">${envSvg}</div>`
    html += fieldRow(
      'Output',
      boomerangSelectField(`${path}.output_type`, ot, OUTPUT_TYPES)
    )
    if (ot === 'cc') {
      html += fieldRow(
        'Parameter',
        paramField(
          ctrl,
          `${path}.cc_number`,
          DeviceControls.resolveParameterCc(ctrl.deviceDefinition, a.cc_number)
        )
      )
    }
    if (ot === 'lfo_rate' || ot === 'lfo_depth') {
      html += fieldRow(
        'LFO target',
        boomerangSelectField(`${path}.lfo_target`, a.lfo_target || 'both', LFO_TARGET)
      )
    }
    if (ot === 'cc') {
      html += fieldRow(
        'Origin',
        boomerangSelectField(
          `${path}.__boomerang_origin`,
          boomerangOriginSelection(a),
          boomerangOriginOptions()
        )
      )
    }
    html += fieldRow(
      'Target',
      boomerangSelectField(
        `${path}.__boomerang_target`,
        boomerangTargetSelection(a, ot),
        boomerangTargetOptions()
      )
    )
    html += renderBoomerangPhase(ctrl, path, a, 'attack')
    html += renderBoomerangPhase(ctrl, path, a, 'sustain')
    html += renderBoomerangPhase(ctrl, path, a, 'release')
    html += '</div>'
    return html
  }

  function renderTouchwheelHold (ctrl, path, a) {
    let html = fieldRow(
      'Press',
      selectField(
        `${path}.mode`,
        a.mode ?? 0,
        ActionCatalog.touchwheelModeOptions(a.mode)
      )
    )
    html += fieldRow('Release', touchwheelReleaseField(path, a))
    return html
  }

  function renderTouchwheelCycle (ctrl, path, a) {
    const stepCount = ActionCatalog.touchwheelStepCount(a)
    const steps = Array.isArray(a.modes) ? a.modes.slice() : []
    const stepOpts = []
    for (let i = 2; i <= 8; i++) stepOpts.push({ v: i, l: String(i) })

    let html = fieldRow(
      'Steps',
      selectField(`${path}.num_modes`, stepCount, stepOpts)
    )

    html += `<div class="scene-cycle scene-touchwheel-cycle" style="--cycle-steps: ${stepCount}">`
    html += '<div class="scene-cycle-row scene-cycle-row-head">'
    html += '<div class="scene-cycle-param-col">Mode</div>'
    for (let i = 0; i < stepCount; i++) {
      html += `<div class="scene-cycle-step-col"><span class="scene-cycle-step-label">Step ${i + 1}</span></div>`
    }
    html += '</div>'

    html += '<div class="scene-cycle-row">'
    html += '<div class="scene-cycle-param-col" aria-hidden="true"></div>'
    for (let i = 0; i < stepCount; i++) {
      html += `<div class="scene-cycle-step-col">${selectField(
        `${path}.modes.${i}`,
        steps[i] ?? 0,
        ActionCatalog.touchwheelModeOptions(steps[i])
      )}</div>`
    }
    html += '</div></div>'
    return html
  }

  function renderTempoCycle (ctrl, path, a) {
    const stepCount = ActionCatalog.tempoStepCount(a)
    const steps = Array.isArray(a.tempos) ? a.tempos.slice() : []

    let html = `<div class="scene-cycle scene-tempo-cycle" style="--cycle-steps: ${stepCount}">`
    html += '<div class="scene-cycle-row scene-cycle-row-head">'
    html += '<div class="scene-cycle-param-col">Tempo</div>'
    for (let i = 0; i < stepCount; i++) {
      const add = (i === stepCount - 1 && stepCount < 8)
        ? slotAddButton(path, 'tempo-step', 'Add column', 8) : ''
      const remove = i > 1
        ? `<wa-button class="scene-slot-remove scene-cycle-step-remove" size="small" appearance="text" variant="neutral"
            data-controller="slots" data-slots-path-value="${esc(path)}" data-slots-kind-value="tempo-step"
            data-slots-min-value="2" data-slots-max-value="8" data-slots-default-value="120"
            data-action="click->slots#remove" data-slot-index="${i}"
            title="Remove column ${i + 1}" aria-label="Remove column ${i + 1}"
            ><wa-icon name="xmark"></wa-icon></wa-button>`
        : ''
      html += `<div class="scene-cycle-step-col"><span class="scene-cycle-step-label">Step ${i + 1}</span>${remove}${add}</div>`
    }
    html += '</div>'

    html += '<div class="scene-cycle-row">'
    html += '<div class="scene-cycle-param-col" aria-hidden="true"></div>'
    for (let i = 0; i < stepCount; i++) {
      html += `<div class="scene-cycle-step-col">${numberField(
        `${path}.tempos.${i}`, steps[i] ?? 120, 20, 300
      )}</div>`
    }
    html += '</div></div>'
    return html
  }

  function renderPresetCycle (ctrl, path, a) {
    const device = ctrl.deviceDefinition
    const stepCount = DeviceControls.presetStepCount(a)
    const steps = Array.isArray(a.presets) ? a.presets.slice() : []

    let html = `<div class="scene-cycle scene-preset-cycle" style="--cycle-steps: ${stepCount}">`
    html += '<div class="scene-cycle-row scene-cycle-row-head">'
    html += '<div class="scene-cycle-param-col">Preset</div>'
    for (let i = 0; i < stepCount; i++) {
      const add = (i === stepCount - 1 && stepCount < 8)
        ? slotAddButton(path, 'preset-step', 'Add column', 8) : ''
      const remove = i > 1
        ? `<wa-button class="scene-slot-remove scene-cycle-step-remove" size="small" appearance="text" variant="neutral"
            data-controller="slots" data-slots-path-value="${esc(path)}" data-slots-kind-value="preset-step"
            data-slots-min-value="2" data-slots-max-value="8" data-slots-default-value="0"
            data-action="click->slots#remove" data-slot-index="${i}"
            title="Remove column ${i + 1}" aria-label="Remove column ${i + 1}"
            ><wa-icon name="xmark"></wa-icon></wa-button>`
        : ''
      html += `<div class="scene-cycle-step-col"><span class="scene-cycle-step-label">Step ${i + 1}</span>${remove}${add}</div>`
    }
    html += '</div>'

    html += '<div class="scene-cycle-row">'
    html += '<div class="scene-cycle-param-col" aria-hidden="true"></div>'
    for (let i = 0; i < stepCount; i++) {
      const val = DeviceControls.resolvePresetValue(device, steps[i])
      html += `<div class="scene-cycle-step-col">${presetField(ctrl, `${path}.presets.${i}`, val)}</div>`
    }
    html += '</div></div>'
    return html
  }

  function renderPresetFields (ctrl, path, a) {
    const variant = a.variant || 'set'
    if (variant === 'cycle') return renderPresetCycle(ctrl, path, a)

    let html = ''
    if (variant === 'set') {
      html += fieldRow('Preset',
        presetField(ctrl, `${path}.number`, a.number))
    } else if (variant === 'hold') {
      html += fieldRow('Press preset',
        presetField(ctrl, `${path}.press_preset`, a.press_preset))
      html += fieldRow('Release preset',
        presetReleaseField(ctrl, `${path}.release_preset`, a))
    }
    return html
  }

  function slotAddButton (path, kind, label, max = 4) {
    const min = (kind === 'cycle-step' || kind === 'preset-step' || kind === 'tempo-step') ? 2 : 1
    return `<wa-button class="scene-slot-add" size="small" appearance="text" variant="brand"
      data-controller="slots"
      data-slots-path-value="${esc(path)}" data-slots-kind-value="${esc(kind)}"
      data-slots-min-value="${min}" data-slots-max-value="${max}" data-slots-default-value="0"
      data-action="click->slots#add" title="${esc(label)}" aria-label="${esc(
      label
    )}"
      ><wa-icon name="plus"></wa-icon></wa-button>`
  }

  function addSlotButton (label) {
    return `<wa-button class="scene-slot-add" size="small" appearance="text" variant="brand"
      data-action="click->slots#add" aria-label="${esc(
        label
      )}"><wa-icon name="plus"></wa-icon></wa-button>`
  }

  function slotBlock (title, body, removeIndex, addHtml) {
    const remove =
      removeIndex >= 0
        ? `<wa-button class="scene-slot-remove" size="small" appearance="text" variant="neutral"
          data-action="click->slots#remove" data-slot-index="${removeIndex}"
          aria-label="Remove ${esc(
            title
          )}"><wa-icon name="xmark"></wa-icon></wa-button>`
        : ''
    return `<div class="scene-slot">
      <div class="scene-slot-head"><span class="scene-slot-title">${esc(
        title
      )}</span>
        <span class="scene-slot-actions">${remove}${addHtml || ''}</span></div>
      ${body}</div>`
  }

  function slotGroup (opts) {
    return `<div class="scene-slots" data-controller="slots"
      data-slots-path-value="${esc(opts.path)}" data-slots-kind-value="${esc(
      opts.kind
    )}"
      data-slots-min-value="${opts.min}" data-slots-max-value="${opts.max}"
      data-slots-default-value="${opts.def}">${opts.rows}</div>`
  }

  function renderCycleSlots (ctrl, path, a) {
    const device = ctrl.deviceDefinition
    const slotCount = DeviceControls.cycleSlotCount(a)
    const stepCount = DeviceControls.cycleStepCount(a)
    const ccBySlot = []
    for (let s = 0; s < slotCount; s++) {
      ccBySlot.push(
        DeviceControls.resolveParameterCc(
          device,
          DeviceControls.ccForSlot(a, s)
        )
      )
    }
    const usedCcs = new Set(ccBySlot.map(Number))
    const hasParams = DeviceControls.hasParameters(device)
    const maxSlots = hasParams
      ? Math.min(4, DeviceControls.parameterCount(device))
      : 4
    const multiSlot = slotCount > 1

    let html = `<div class="scene-cycle" style="--cycle-steps: ${stepCount}">`

    // Column headers: Parameter | Step 1 | Step 2 | …
    html += '<div class="scene-cycle-row scene-cycle-row-head">'
    html += '<div class="scene-cycle-param-col">Parameter</div>'
    for (let i = 0; i < stepCount; i++) {
      const add =
        i === stepCount - 1 && stepCount < 8
          ? slotAddButton(path, 'cycle-step', 'Add step', 8)
          : ''
      const remove =
        i > 1
          ? `<wa-button class="scene-slot-remove scene-cycle-step-remove" size="small" appearance="text" variant="neutral"
            data-controller="slots" data-slots-path-value="${esc(
              path
            )}" data-slots-kind-value="cycle-step"
            data-slots-min-value="2" data-slots-max-value="8" data-slots-default-value="0"
            data-action="click->slots#remove" data-slot-index="${i}"
            title="Remove step ${i + 1}" aria-label="Remove step ${i + 1}"
            ><wa-icon name="xmark"></wa-icon></wa-button>`
          : ''
      html += `<div class="scene-cycle-step-col"><span class="scene-cycle-step-label">Step ${
        i + 1
      }</span>${remove}${add}</div>`
    }
    html += '</div>'

    // One row per parameter slot
    for (let s = 0; s < slotCount; s++) {
      const ccPath = multiSlot ? `${path}.cc.${s}` : `${path}.cc`
      const cc = ccBySlot[s]
      const steps = DeviceControls.cycleStepsForSlot(a, s)
      const paramLabel = DeviceControls.parameterLabel(device, cc)
      const removeSlot =
        s > 0
          ? `<wa-button class="scene-slot-remove" size="small" appearance="text" variant="neutral"
            data-controller="slots" data-slots-path-value="${esc(
              path
            )}" data-slots-kind-value="control"
            data-slots-min-value="1" data-slots-max-value="4" data-slots-default-value="0"
            data-action="click->slots#remove" data-slot-index="${s}"
            title="Remove ${esc(paramLabel)}" aria-label="Remove ${esc(
              paramLabel
            )}"
            ><wa-icon name="xmark"></wa-icon></wa-button>`
          : '<span class="scene-cycle-param-slot" aria-hidden="true"></span>'
      const addParam =
        s === slotCount - 1 && slotCount < maxSlots
          ? slotAddButton(path, 'control', 'Add parameter', maxSlots)
          : ''

      html += '<div class="scene-cycle-row">'
      html += `<div class="scene-cycle-param-col">
        <div class="scene-cycle-param-line">${removeSlot}
          ${paramField(ctrl, ccPath, cc, {
            exclude: usedCcs,
            keep: cc
          })}${addParam}
        </div>
      </div>`
      for (let i = 0; i < stepCount; i++) {
        const valPath = multiSlot
          ? `${path}.values.${s}.${i}`
          : `${path}.values.${i}`
        const val = DeviceControls.resolveParameterValue(device, cc, steps[i])
        html += `<div class="scene-cycle-step-col">${valueField(
          ctrl,
          valPath,
          cc,
          val
        )}</div>`
      }
      html += '</div>'
    }

    html += '</div>'
    return html
  }

  function renderRandomizeFields (ctrl, path, a) {
    const device = ctrl.deviceDefinition
    const ccList = DeviceControls.randomizeCcList(a)
    const slotCount = DeviceControls.randomizeDisplaySlotCount(a, device)
    const maxSlots = DeviceControls.randomizeMaxSlots(device)
    let rows = ''
    for (let i = 0; i < slotCount; i++) {
      const ccPath = `${path}.cc.${i}`
      const active = i < ccList.length
      const cur = active ? DeviceControls.resolveParameterCc(device, ccList[i]) : '__inactive__'
      const body = fieldRow(
        'Parameter',
        selectField(
          ccPath,
          active ? cur : '__inactive__',
          DeviceControls.randomizeSlotOptions(device, ccList, i)
        )
      )
      rows += slotBlock(`Slot ${i + 1}`, body, i > 0 ? i : -1, '')
    }
    return slotGroup({ path, kind: 'randomize', min: 1, max: maxSlots, def: 0, rows })
  }

  function renderControlFields (ctrl, path, a) {
    const device = ctrl.deviceDefinition
    const variant = a.variant || 'set'
    if (variant === 'cycle') return renderCycleSlots(ctrl, path, a)

    const count = DeviceControls.controlSlotCount(a)
    const multi = count > 1
    const ccBySlot = []
    for (let i = 0; i < count; i++) {
      ccBySlot.push(
        DeviceControls.resolveParameterCc(
          device,
          DeviceControls.ccForSlot(a, i)
        )
      )
    }
    // A control action may not target the same parameter twice, so each slot's
    // dropdown hides parameters already taken by the other slots.
    const usedCcs = new Set(ccBySlot.map(Number))
    const hasParams = DeviceControls.hasParameters(device)
    const maxSlots = hasParams
      ? Math.min(4, DeviceControls.parameterCount(device))
      : 4
    let rows = ''
    for (let i = 0; i < count; i++) {
      const ccPath = multi ? `${path}.cc.${i}` : `${path}.cc`
      const cc = ccBySlot[i]
      let body = fieldRow(
        'Parameter',
        paramField(ctrl, ccPath, cc, { exclude: usedCcs, keep: cc })
      )
      if (variant === 'set') {
        const valPath = multi ? `${path}.value.${i}` : `${path}.value`
        const val = DeviceControls.resolveParameterValue(
          device,
          cc,
          slotValue(a.value, i)
        )
        body += fieldRow('Value', valueField(ctrl, valPath, cc, val))
      } else if (variant === 'hold') {
        const pressPath = multi ? `${path}.value.${i}` : `${path}.value`
        const relPath = multi ? `${path}.value2.${i}` : `${path}.value2`
        const press = DeviceControls.resolveParameterValue(
          device,
          cc,
          slotValue(a.value, i)
        )
        const rel = DeviceControls.resolveParameterValue(
          device,
          cc,
          slotValue(a.value2, i)
        )
        body += fieldRow('Press value', valueField(ctrl, pressPath, cc, press))
        body += fieldRow('Release value', valueField(ctrl, relPath, cc, rel))
      }
      const add =
        count < maxSlots && i === count - 1
          ? addSlotButton('Add parameter')
          : ''
      rows += slotBlock(`Slot ${i + 1}`, body, i > 0 ? i : -1, add)
    }
    return slotGroup({ path, kind: 'control', min: 1, max: 4, def: 0, rows })
  }

  function renderRepeatBlock (ctrl, path, a) {
    if (!ActionCatalog.supportsRepeat(a)) return ''
    const on = !!a.repeat
    const repeatCb = `<input type="checkbox" class="scene-checkbox" ${
      on ? 'checked' : ''
    }
      data-scene-path="${esc(path)}.repeat" data-reveal-target="trigger"
      data-action="change->scene#patchCheckboxQuiet change->reveal#toggle">`
    let content = fieldRow(
      'Division',
      selectField(
        `${path}.repeat_division`,
        a.repeat_division || 'quarter',
        REPEAT_DIVISIONS
      )
    )
    content += fieldRow(
      'Probability',
      selectField(`${path}.probability`, a.probability ?? 100, PROBABILITY)
    )
    content += fieldRow(
      'Pattern length',
      selectField(
        `${path}.pattern_length`,
        a.pattern_length ?? 0,
        PATTERN_LENGTHS
      )
    )
    const plen = Number(a.pattern_length ?? 0)
    if (plen >= 2)
      content += renderPatternMask(path, plen, a.pattern_mask ?? 255)
    return `<div class="scene-reveal" data-controller="reveal">
      ${fieldRow('Repeat', repeatCb)}
      <div class="scene-reveal-content" data-reveal-target="content" ${
        on ? '' : 'hidden'
      }>${content}</div>
    </div>`
  }

  const CONFIRM_TARGETS = [
    { v: 'preset', l: 'Preset' },
    { v: 'scene', l: 'Scene' }
  ]

  const FOLLOWUP_MODES = [
    { v: 'always', l: 'Always' },
    { v: 'if_held', l: 'If held' },
    { v: 'if_quick', l: 'If quick' }
  ]

  const FOLLOWUP_DURATIONS = [
    { v: 500, l: '500 ms' },
    { v: 750, l: '750 ms' },
    { v: 1000, l: '1 s' },
    { v: 1500, l: '1.5 s' },
    { v: 2000, l: '2 s' }
  ]

  const MORPH_STEPS = [
    { v: 'auto', l: 'Auto' },
    { v: 'coarse', l: 'Coarse (8)' },
    { v: 'medium', l: 'Medium (16)' },
    { v: 'fine', l: 'Fine (32)' },
    { v: 'manual', l: 'Manual' }
  ]

  const MORPH_TIMING = [
    { v: 'feel', l: 'Feel' },
    { v: 'duration', l: 'Duration' },
    { v: 'sync', l: 'Sync' }
  ]

  const MORPH_FEEL = [
    { v: 'fast', l: 'Fast' },
    { v: 'medium', l: 'Medium' },
    { v: 'slow', l: 'Slow' }
  ]

  const MORPH_DIVISION = [
    { v: 'beat', l: '1 beat' },
    { v: 'bar', l: '1 bar' },
    { v: '2_bars', l: '2 bars' },
    { v: '4_bars', l: '4 bars' },
    { v: 'beat_2', l: 'Beat 2' },
    { v: 'beat_3', l: 'Beat 3' },
    { v: 'beat_4', l: 'Beat 4' }
  ]

  function renderFollowUpBlock (ctrl, path, a) {
    if (!ActionCatalog.supportsFollowUp(a)) return ''
    const mode = a.release_mode || 'always'
    let html = fieldRow('Follow-up',
      selectField(`${path}.release_mode`, mode, FOLLOWUP_MODES))
    if (mode !== 'always') {
      html += fieldRow('Duration',
        selectField(`${path}.release_threshold_ms`, a.release_threshold_ms ?? 1000,
          FOLLOWUP_DURATIONS))
    }
    return html
  }

  function renderMorphBlock (ctrl, path, a) {
    if (!ActionCatalog.supportsMorph(a)) return ''
    const on = !!a.morph
    let html = fieldRow('Morph', checkboxField(`${path}.morph`, on))
    if (!on) return html
    const steps = a.morph_steps || 'auto'
    html += fieldRow('Steps',
      selectField(`${path}.morph_steps`, steps, MORPH_STEPS))
    if (steps === 'manual') {
      html += fieldRow('Manual steps',
        numberField(`${path}.morph_manual_steps`, a.morph_manual_steps ?? 32, 8, 128))
    }
    const timing = a.morph_timing || 'feel'
    html += fieldRow('Timing',
      selectField(`${path}.morph_timing`, timing, MORPH_TIMING))
    if (timing === 'feel') {
      html += fieldRow('Feel',
        selectField(`${path}.morph_feel`, a.morph_feel || 'medium', MORPH_FEEL))
    } else {
      html += fieldRow('Division',
        selectField(`${path}.morph_division`, a.morph_division || 'bar', MORPH_DIVISION))
    }
    return html
  }

  function renderPatternMask (path, len, mask) {
    let steps = ''
    for (let i = 0; i < len; i++) {
      const active = (mask >> i) & 1
      steps += `<button type="button" class="scene-step${
        active ? ' is-on' : ''
      }"
        data-action="click->scene#togglePatternStep"
        data-scene-path="${esc(path)}.pattern_mask" data-step-bit="${i}"
        aria-pressed="${active ? 'true' : 'false'}">${i + 1}</button>`
    }
    return fieldRow('Pattern', `<div class="scene-steps">${steps}</div>`)
  }

  function renderAction (ctrl, path, action, opts = {}) {
    const a = action || { type: 'none' }
    const trigger = opts.trigger || ActionCatalog.TRIGGERS.TOUCHPAD_0_7
    const ctx = actionContext(ctrl)
    const typeOpts = ActionCatalog.typesForTrigger(trigger, ctx)
    if (a.type && a.type !== 'none' && !typeOpts.some(o => o.v === a.type)) {
      typeOpts.unshift({ v: a.type, l: ActionCatalog.typeLabel(a.type) })
    }
    let html = fieldRow(
      opts.typeLabel || 'Action',
      selectField(`${path}.type`, a.type || 'none', typeOpts)
    )
    if (a.type && a.type !== 'none') {
      const variantOpts = ActionCatalog.variantsForType(a.type, trigger, ctx)
      if (variantOpts.length) {
        const curVariant = a.variant || variantOpts[0].v
        if (curVariant && !variantOpts.some(o => o.v === curVariant)) {
          variantOpts.unshift({
            v: curVariant,
            l: ActionCatalog.variantLabel(curVariant)
          })
        }
        html += fieldRow(
          'Variant',
          selectField(`${path}.variant`, curVariant, variantOpts)
        )
      }
      if (a.type === 'note') {
        const noteVal = a.note ?? ActionCatalog.NOTE_RANDOM
        html += fieldRow(
          'Note',
          selectField(
            `${path}.note`,
            noteVal,
            ActionCatalog.noteOptions(noteVal)
          )
        )
        if (Number(noteVal) === ActionCatalog.NOTE_RANDOM) {
          html += fieldRow(
            'Floor',
            selectField(
              `${path}.random_floor`,
              a.random_floor ?? 36,
              ActionCatalog.noteRandomBoundOptions(a.random_floor)
            )
          )
          html += fieldRow(
            'Ceiling',
            selectField(
              `${path}.random_ceiling`,
              a.random_ceiling ?? 96,
              ActionCatalog.noteRandomBoundOptions(a.random_ceiling)
            )
          )
        }
        html += fieldRow(
          'Voices',
          selectField(`${path}.voices`, a.voices ?? 1, [
            { v: 1, l: '1' },
            { v: 2, l: '2' },
            { v: 3, l: '3' },
            { v: 4, l: '4' }
          ])
        )
        html += fieldRow('Bass', checkboxField(`${path}.bass`, !!a.bass))
        html += fieldRow(
          'Velocity',
          selectField(
            `${path}.velocity`,
            a.velocity ?? 100,
            ActionCatalog.noteVelocityOptions(a.velocity)
          )
        )
        html += fieldRow(
          'Aftertouch',
          checkboxField(`${path}.aftertouch`, a.aftertouch !== false)
        )
      }
      if (a.type === 'randomize') {
        html += renderRandomizeFields(ctrl, path, a)
      }
      if (a.type === 'piano_pedal') {
        html += fieldRow(
          'Pedal',
          selectField(
            `${path}.cc`,
            ActionCatalog.resolvePianoPedalCc(a.cc),
            ActionCatalog.pianoPedalOptions(a.cc)
          )
        )
      }
      if (a.type === 'touchwheel' && (a.variant || 'hold') === 'hold') {
        html += renderTouchwheelHold(ctrl, path, a)
      }
      if (a.type === 'touchwheel' && a.variant === 'cycle') {
        html += renderTouchwheelCycle(ctrl, path, a)
      }
      if (a.type === 'control' || a.type === 'control_change') {
        html += renderControlFields(ctrl, path, a)
      }
      if (a.type === 'preset') {
        html += renderPresetFields(ctrl, path, a)
      }
      if (a.type === 'scene' && a.variant === 'set') {
        html += fieldRow(
          'Scene',
          sceneSetField(ctrl, `${path}.number`, a)
        )
      }
      if (a.type === 'tempo' && a.variant === 'set') {
        html += fieldRow(
          'BPM',
          numberField(`${path}.bpm`, a.bpm ?? 120, 20, 300)
        )
      }
      if (a.type === 'tempo' && a.variant === 'hold') {
        html += fieldRow(
          'Press BPM',
          numberField(`${path}.press_bpm`, a.press_bpm ?? 120, 20, 300)
        )
        html += fieldRow(
          'Release BPM',
          numberField(`${path}.release_bpm`, a.release_bpm ?? 120, 20, 300)
        )
      }
      if (a.type === 'tempo' && a.variant === 'cycle') {
        html += renderTempoCycle(ctrl, path, a)
      }
      if (a.type === 'lfo') {
        html += renderLfoFields(ctrl, path, a)
      }
      if (a.type === 'clock') {
        html += renderClockFields(ctrl, path, a)
      }
      if (a.type === 'cut') {
        html += renderCutFields(ctrl, path, a)
      }
      if (a.type === 'ui') {
        html += renderUiFields(ctrl, path, a)
      }
      if (a.type === 'param') {
        html += renderParamFields(ctrl, path, a)
      }
      if (a.type === 'rtg' || a.type === 'sample_hold') {
        html += renderEngineFields(ctrl, path, a)
      }
      if (a.type === 'punch_in') {
        html += renderPunchInFields(ctrl, path, a)
      }
      if (a.type === 'flag_ceremony') {
        html += renderFlagCeremonyFields(ctrl, path, a)
      }
      if (a.type === 'boomerang') {
        html += renderBoomerangFields(ctrl, path, a)
      }
      if (a.type === 'confirm_pending' && (ctrl.deviceContext?.sceneMode ?? 0) === 2) {
        html += fieldRow(
          'Confirms',
          selectField(`${path}.confirm_target`, a.confirm_target || 'preset', CONFIRM_TARGETS)
        )
      }
      if (trigger !== ActionCatalog.TRIGGERS.ON_LOAD && ActionCatalog.supportsTiming(a)) {
        const beats = ctrl.editModel?.time_signature?.numerator ?? 4
        const useTransport = !!ctrl.editModel?.use_transport
        const timingVal = a.timing || 'immediate'
        const timingOpts = ActionCatalog.timingOptions(beats, useTransport)
        if (!timingOpts.some(o => o.v === timingVal)) {
          timingOpts.unshift({ v: timingVal, l: timingVal })
        }
        html += fieldRow(
          'Timing',
          selectField(`${path}.timing`, timingVal, timingOpts)
        )
      }
      if (trigger !== ActionCatalog.TRIGGERS.ON_LOAD) {
        html += renderRepeatBlock(ctrl, path, a)
      }
      html += renderFollowUpBlock(ctrl, path, a)
      html += renderMorphBlock(ctrl, path, a)
      if (ActionCatalog.supportsRaiseFlag(a) && ctrl.deviceContext?.flagEnabled) {
        html += fieldRow(
          'Raise the Flag',
          checkboxField(`${path}.raise_flag`, !!a.raise_flag)
        )
      }
    }
    return html
  }

  function renderActionChain (ctrl, path, chain, maxItems, trigger, opts = {}) {
    const arr = Array.isArray(chain) ? chain : []
    const slotPrefix = opts.slotTitlePrefix || 'Action'
    let html = ''
    for (let i = 0; i < maxItems; i++) {
      const itemPath = `${path}.${i}`
      html += `<div class="scene-action-slot"><h4>${esc(slotPrefix)} ${i + 1}</h4>`
      html += renderAction(ctrl, itemPath, arr[i] || { type: 'none' }, {
        trigger,
        typeLabel: opts.typeLabel || 'Action'
      })
      html += '</div>'
    }
    return html
  }

  function section (title, body, open = false) {
    const isOpen = _openSections.has(title) || open
    return `<details class="scene-editor-section" data-section="${esc(
      title
    )}" ${isOpen ? 'open' : ''}>
      <summary>${esc(title)}</summary>
      <div class="scene-editor-section-body">${body}</div>
    </details>`
  }

  function flatBlock (title, body) {
    return `<div class="scene-editor-flat">
      <h3 class="scene-editor-flat-title">${esc(title)}</h3>
      <div class="scene-editor-flat-body">${body}</div>
    </div>`
  }

  function editorDivider () {
    return '<div class="scene-editor-divider" role="separator"></div>'
  }

  /** Per-scene pedal/MIDI/TRS (device menu, before scene globals divider). */
  function renderPerSceneDevice (ctrl) {
    if (ctrl.deviceContext.deviceMode !== 1) return ''
    const m = ctrl.editModel
    const global = ctrl.deviceContext.globalPedal || {}
    const inheritedCh = global.midi_channel || 1
    const inheritedTrs = PedalCatalog.formatTrsLabel(global.trs_type)

    const pedalName = PedalCatalog.formatPedalDisplayName(
      m.device_id || '',
      ctrl.pedalCatalog,
      global
    )
    let html = fieldRow(
      'Pedal',
      `<div class="scene-pedal-display">
        <span class="scene-pedal-name">${esc(pedalName)}</span>
        <wa-button size="small" variant="neutral" appearance="outlined"
                   data-action="click->scene#openPedalPicker">Change</wa-button>
      </div>`
    )
    html += fieldRow(
      'MIDI channel',
      selectField('midi_channel', m.midi_channel ?? 0, [
        { v: 0, l: `Inherited (${inheritedCh})` },
        ...Array.from({ length: 16 }, (_, i) => ({
          v: i + 1,
          l: String(i + 1)
        }))
      ])
    )
    html += fieldRow(
      'TRS type',
      selectField('trs_type', m.trs_type ?? 0, [
        { v: 0, l: `Inherited (${inheritedTrs})` },
        { v: 1, l: 'Type A' },
        { v: 2, l: 'Type B' },
        { v: 3, l: 'TS' },
        { v: 4, l: 'Both' }
      ])
    )
    return flatBlock('Pedal', html)
  }

  /** Scene globals (device menu after assignment submenus). */
  function renderSceneGlobals (ctrl) {
    const m = ctrl.editModel
    const ctx = ctrl.deviceContext
    let html = ''

    if (ctx.sceneMode !== 0) {
      html += fieldRow('Scene name', textField('name', m.name || '', 16))
    }
    html += fieldRow(
      'Screen',
      selectField(
        'ui_module',
        normalizeUiModule(m.ui_module),
        screenModuleOptions(m.ui_module)
      )
    )

    if (ctx.sceneMode !== 1) {
      html += fieldRow(
        'Pedal Preset',
        numberField('program_number', m.program_number ?? 0, 0, 127)
      )
      html += fieldRow(
        'Load preset when loading scene',
        checkboxField('send_pc_on_load', m.send_pc_on_load !== false)
      )
    }

    html += fieldRow('BPM', numberField('bpm', m.bpm ?? 120, 20, 300))
    html += fieldRow(
      'Time sig (num)',
      numberField(
        'time_signature.numerator',
        m.time_signature?.numerator ?? 4,
        1,
        16
      )
    )
    html += fieldRow(
      'Time sig (den)',
      numberField(
        'time_signature.denominator',
        m.time_signature?.denominator ?? 4,
        1,
        16
      )
    )
    html += fieldRow(
      'Beat divider',
      selectField('beat_divider', m.beat_divider || 'quarter', [
        { v: 'quarter', l: 'Quarter' },
        { v: 'eighth', l: 'Eighth' },
        { v: 'sixteenth', l: 'Sixteenth' }
      ])
    )
    html += fieldRow(
      'Clock source',
      selectField('clock_source', m.clock_source || 'internal', [
        { v: 'internal', l: 'Internal' },
        { v: 'midi', l: 'MIDI' },
        { v: 'sync', l: 'Sync' }
      ])
    )
    html += fieldRow(
      'Send clock',
      checkboxField('send_clock', m.send_clock !== false)
    )
    html += fieldRow(
      'Use transport',
      checkboxField('use_transport', !!m.use_transport)
    )

    const inheritedNoteCh =
      typeof ctrl.getEffectiveMidiChannel === 'function'
        ? ctrl.getEffectiveMidiChannel()
        : ctx.globalPedal?.midi_channel || 1
    html += fieldRow(
      'Send notes on channel',
      selectField('note_channel', m.note_channel ?? 0, [
        { v: 0, l: `Inherited (${inheritedNoteCh})` },
        ...Array.from({ length: 16 }, (_, i) => ({
          v: i + 1,
          l: String(i + 1)
        }))
      ])
    )

    return flatBlock('Scene settings', html)
  }

  function routingUserModeSelection (modes, mapping, enabledKey) {
    if (!mapping?.[enabledKey ?? 'enabled']) return 'disabled'
    const ot = mapping.output_type || 'cc'
    for (const opt of modes) {
      if (opt.output_type === ot) return opt.v
    }
    return modes[1]?.v || 'control_change'
  }

  function lfoUserModeSelection (n, m) {
    const cfgKey = n === 1 ? 'lfo1_config' : 'lfo2_config'
    if (!m[cfgKey]?.enabled) return 'disabled'
    const mapKey = n === 1 ? 'lfo1' : 'lfo2'
    const modes = n === 1 ? LFO1_USER_MODES : LFO2_USER_MODES
    return routingUserModeSelection(modes, m[mapKey], 'enabled')
  }

  function noteTrackUserModeSelection (m) {
    return routingUserModeSelection(NOTE_TRACK_USER_MODES, m.note_track)
  }

  function tiltUserModeSelection (m, key) {
    return routingUserModeSelection(TILT_USER_MODES, m[key])
  }

  function generatorUserModeSelection (modes, cfg) {
    if (!cfg?.enabled) return 'disabled'
    return (cfg.mode === 'step') ? 'step' : 'continuous'
  }

  function renderLfoEngineFields (cfgKey, cfg) {
    let html = fieldRow(
      'Start mode',
      selectField(`${cfgKey}.start_mode`, cfg.start_mode || 'running', ENGINE_START_MODE)
    )
    if ((cfg.start_mode || 'running') === 'paused') {
      html += fieldRow(
        'Trigger timing',
        selectField(
          `${cfgKey}.trigger_timing`,
          cfg.trigger_timing || 'immediate',
          LFO_TRIGGER_TIMING
        )
      )
    }
    html += fieldRow(
      'Repeat',
      checkboxField(`${cfgKey}.repeat`, cfg.repeat !== false)
    )
    html += fieldRow(
      'On restart',
      selectField(`${cfgKey}.reset_phase`, cfg.reset_phase !== false ? 1 : 0, [
        { v: 1, l: 'From Start' },
        { v: 0, l: 'Continue' }
      ])
    )
    html += fieldRow(
      'On stop',
      selectField(`${cfgKey}.restore_on_stop`, cfg.restore_on_stop ? 1 : 0, [
        { v: 0, l: 'Nothing' },
        { v: 1, l: 'Restore' }
      ])
    )
    return html
  }

  function renderLfoOutputRangeFields (cfgKey, cfg) {
    let html = fieldRow(
      'Floor',
      numberField(`${cfgKey}.floor`, cfg.floor ?? 0, 0, 127)
    )
    html += fieldRow(
      'Ceiling',
      numberField(`${cfgKey}.ceiling`, cfg.ceiling ?? 127, 0, 127)
    )
    if (cfg.waveform !== 'glider') {
      html += fieldRow(
        'Resolution',
        selectField(
          `${cfgKey}.resolution_mode`,
          cfg.resolution_mode || 'auto',
          LFO_RESOLUTION
        )
      )
      if ((cfg.resolution_mode || 'auto') === 'manual') {
        html += fieldRow(
          'Manual steps',
          numberField(`${cfgKey}.manual_steps`, cfg.manual_steps ?? 32, 1, 256)
        )
      }
    }
    return html
  }

  function renderGeneratorPatternFields (cfgKey, cfg) {
    let html = fieldRow(
      'Probability',
      selectField(`${cfgKey}.probability`, cfg.probability ?? 100, PROBABILITY)
    )
    html += fieldRow(
      'Pattern length',
      selectField(`${cfgKey}.pattern_length`, cfg.pattern_length ?? 0, PATTERN_LENGTHS)
    )
    const plen = Number(cfg.pattern_length ?? 0)
    if (plen >= 2)
      html += renderPatternMask(cfgKey, plen, cfg.pattern_mask ?? 255)
    return html
  }

  function renderSampleHoldCcSlots (ctrl) {
    const m = ctrl.editModel
    if (!m.sample_hold) m.sample_hold = { enabled: true, output_type: 'cc' }
    return renderLfoCcSlots(ctrl, 'sample_hold', m.sample_hold)
  }

  function proximityUserModeSelection (m) {
    if (!m.proximity?.enabled) return 'disabled'
    const ot = m.proximity.output_type || 'cc'
    const known = new Set(['cc', 'note', 'lfo_rate', 'lfo_depth', 'tempo_nudge'])
    if (!known.has(ot)) return 'control_change'
    if (ot === 'note') return 'notes_theremin'
    for (const opt of PROXIMITY_USER_MODES) {
      if (opt.output_type === ot) return opt.v
    }
    return 'control_change'
  }

  function alsUserModeSelection (m) {
    if (!m.als?.enabled) return 'disabled'
    const ot = m.als.output_type || 'cc'
    const known = new Set(['cc', 'note', 'lfo_rate', 'lfo_depth', 'tempo_nudge'])
    if (!known.has(ot)) return 'control_change'
    for (const opt of ALS_USER_MODES) {
      if (opt.output_type === ot) return opt.v
    }
    return 'control_change'
  }

  function renderProximity (ctrl) {
    const m = ctrl.editModel
    if (!m.proximity) m.proximity = { enabled: false, output_type: 'cc' }

    if (cvClaimsSource(m, 'proximity')) {
      let html = CV_GATE_CONTROLLED_CALLOUT
      const prox = m.proximity
      html += fieldRow(
        'Polarity',
        selectField('proximity.polarity', prox.polarity ?? 0, POLARITY)
      )
      html += fieldRow(
        'Curve',
        selectField('proximity.curve_type', prox.curve_type ?? 0, CURVE)
      )
      return section('Proximity', html)
    }

    const userMode = proximityUserModeSelection(m)
    let html = fieldRow(
      'Mode',
      selectField('__proximity_user_mode', userMode, PROXIMITY_USER_MODES)
    )

    if (userMode === 'disabled') return section('Proximity', html)

    html += renderContinuousMapping(ctrl, 'proximity', m.proximity, {
      hideEnabled: true,
      hideRouting: true,
      ccSlots: true,
      noteSelectors: true,
      noteOctaveOptions: SENSOR_NOTE_OCTAVE_OPTIONS,
      velocityModePath: 'proximity_velocity_mode',
      tempoNudgePath: 'proximity_tempo_nudge_pct',
      tempoNudgeDirectionPath: 'proximity_tempo_nudge_direction',
      tempoNudgeSelect: true
    })
    return section('Proximity', html)
  }

  function renderAls (ctrl) {
    const m = ctrl.editModel
    if (!m.als) m.als = { enabled: false, output_type: 'cc' }

    if (cvClaimsSource(m, 'als')) {
      let html = CV_GATE_CONTROLLED_CALLOUT
      const als = m.als
      html += fieldRow(
        'Polarity',
        selectField('als.polarity', als.polarity ?? 0, POLARITY)
      )
      html += fieldRow(
        'Curve',
        selectField('als.curve_type', als.curve_type ?? 0, CURVE)
      )
      return section('Ambient Light', html)
    }

    const userMode = alsUserModeSelection(m)
    let html = fieldRow(
      'Mode',
      selectField('__als_user_mode', userMode, ALS_USER_MODES)
    )

    if (userMode === 'disabled') return section('Ambient Light', html)

    html += renderContinuousMapping(ctrl, 'als', m.als, {
      hideEnabled: true,
      hideRouting: true,
      ccSlots: true,
      noteSelectors: true,
      noteOctaveOptions: SENSOR_NOTE_OCTAVE_OPTIONS,
      velocityModePath: 'als_velocity_mode',
      tempoNudgePath: 'als_tempo_nudge_pct',
      tempoNudgeDirectionPath: 'als_tempo_nudge_direction',
      tempoNudgeSelect: true
    })
    return section('Ambient Light', html)
  }

  function renderTiltAxis (ctrl, axis) {
    const key = axis === 'x' ? 'tilt_x' : 'tilt_y'
    const modePath = axis === 'x' ? '__tilt_x_user_mode' : '__tilt_y_user_mode'
    const velKey =
      axis === 'x' ? 'tilt_x_velocity_mode' : 'tilt_y_velocity_mode'
    const nudgeKey =
      axis === 'x' ? 'tilt_x_tempo_nudge_pct' : 'tilt_y_tempo_nudge_pct'
    const nudgeDirectionKey =
      axis === 'x' ? 'tilt_x_tempo_nudge_direction' : 'tilt_y_tempo_nudge_direction'
    const claimKey = axis === 'x' ? 'tilt_x' : 'tilt_y'
    if (!ctrl.editModel[key])
      ctrl.editModel[key] = { enabled: false, output_type: 'cc' }
    const m = ctrl.editModel
    let html = ''
    if (cvClaimsSource(m, claimKey)) {
      const tilt = m[key]
      html += CV_GATE_CONTROLLED_CALLOUT
      html += fieldRow(
        'Polarity',
        selectField(
          `${key}.polarity`,
          tiltPolaritySelectValue(tilt.polarity ?? 0),
          TILT_POLARITY
        )
      )
      html += fieldRow(
        'Curve',
        selectField(`${key}.curve_type`, tilt.curve_type ?? 0, CURVE)
      )
      html += fieldRow(
        'Min',
        numberField(`${key}.min_value`, tilt.min_value ?? 0, 0, 127)
      )
      html += fieldRow(
        'Middle',
        numberField(`${key}.middle_value`, tilt.middle_value ?? 64, 0, 127)
      )
      html += fieldRow(
        'Max',
        numberField(`${key}.max_value`, tilt.max_value ?? 127, 0, 127)
      )
      return section(`Tilt ${axis.toUpperCase()}`, html)
    }
    const userMode = tiltUserModeSelection(m, key)
    html += fieldRow(
      'Mode',
      selectField(modePath, userMode, TILT_USER_MODES)
    )
    if (userMode === 'disabled') return section(`Tilt ${axis.toUpperCase()}`, html)
    html += renderContinuousMapping(ctrl, key, m[key], {
      hideEnabled: true,
      hideRouting: true,
      ccSlots: true,
      noteSelectors: true,
      noteOctaveOptions: SENSOR_NOTE_OCTAVE_OPTIONS,
      velocityModePath: velKey,
      tempoNudgePath: nudgeKey,
      tempoNudgeDirectionPath: nudgeDirectionKey,
      tempoNudgeSelect: true,
      showMinMidMax: true,
      polarityOptions: TILT_POLARITY,
      polaritySelectValue: tiltPolaritySelectValue
    })
    return section(`Tilt ${axis.toUpperCase()}`, html)
  }

  function renderNoteTrack (ctrl) {
    if (!ctrl.editModel.note_track) {
      ctrl.editModel.note_track = { enabled: false, output_type: 'cc' }
    }
    const m = ctrl.editModel
    const userMode = noteTrackUserModeSelection(m)
    let html = fieldRow(
      'Mode',
      selectField('__note_track_user_mode', userMode, NOTE_TRACK_USER_MODES)
    )
    if (userMode === 'disabled') return section('Note Track', html)
    const ot = m.note_track.output_type || 'cc'
    html += renderContinuousMapping(ctrl, 'note_track', m.note_track, {
      hideEnabled: true,
      hideRouting: true,
      ccSlots: true,
      ccSlotMax: 1
    })
    if (['lfo_rate', 'lfo_depth', 'tempo_nudge'].includes(ot)) {
      html += fieldRow(
        'Polarity',
        selectField('note_track.polarity', m.note_track.polarity ?? 0, POLARITY)
      )
      html += fieldRow(
        'Curve',
        selectField('note_track.curve_type', m.note_track.curve_type ?? 0, CURVE)
      )
    }
    return section('Note Track', html)
  }

  function expressionUserModeSelection (m) {
    const mode = m.expression_mode || 'expression'
    if (mode === 'none') return 'disabled'
    if (mode === 'sustain') return 'sustain'
    if (mode === 'sostenuto') return 'sostenuto'
    if (mode === 'switch') return 'switch'
    const ot = m.expression?.output_type || 'cc'
    for (const opt of EXPRESSION_USER_MODES) {
      if (opt.expression_mode !== 'expression') continue
      if (opt.output_type !== ot) continue
      return opt.v
    }
    return 'control_change'
  }

  function renderExpression (ctrl) {
    const m = ctrl.editModel
    if (m.cv_input_mode === 'note') {
      return section(
        'Expression',
        `<wa-callout variant="neutral">Expression locked to Gate (CV/Gate mode).</wa-callout>`
      )
    }

    const userMode = expressionUserModeSelection(m)
    let html = fieldRow(
      'Mode',
      selectField('__expression_user_mode', userMode, EXPRESSION_USER_MODES)
    )

    if (userMode === 'disabled' || userMode === 'sustain' || userMode === 'sostenuto') {
      return section('Expression', html)
    }

    if (userMode === 'switch') {
      html += renderAction(
        ctrl,
        'expr_switch',
        m.expr_switch || { type: 'none' },
        { trigger: ActionCatalog.TRIGGERS.EXPR_SWITCH }
      )
      return section('Expression', html)
    }

    // Continuous pedal routings (control_change / notes / lfo_rate / lfo_depth / tempo_nudge)
    if (!m.expression) m.expression = { enabled: true, output_type: 'cc' }
    html += renderContinuousMapping(ctrl, 'expression', m.expression, {
      hideEnabled: true,
      hideRouting: true,
      noteSelectors: true,
      velocityMin: 0,
      ccSlots: true,
      tempoNudgePath: 'expression_tempo_nudge_pct',
      tempoNudgeDirectionPath: 'expression_tempo_nudge_direction',
      tempoNudgeSelect: true
    })
    return section('Expression', html)
  }

  function cvUserModeSelection (m) {
    const mode = m.cv_input_mode || 'none'
    if (mode === 'none') return 'disabled'
    if (mode === 'note') return 'cv_gate'
    if (mode === 'audio') return 'audio'
    if (mode === 'trigger') return 'trigger'
    const ot = m.cv?.output_type || 'cc'
    for (const opt of CV_USER_MODES) {
      if (opt.cv_input_mode !== 'cv') continue
      if (opt.output_type !== ot) continue
      return opt.v
    }
    return 'control_change'
  }

  function renderCvAudio (ctrl) {
    const m = ctrl.editModel
    if (!m.cv) m.cv = { enabled: true, output_type: 'cc' }
    if (!m.audio_config) {
      m.audio_config = {
        range: 'bi5v',
        sensitivity: 128,
        attack_ms: 10,
        release_ms: 200,
        threshold: 5,
        polarity: 'attract'
      }
    }
    const ac = m.audio_config
    let html = ''
    html += fieldRow(
      'Sensitivity',
      selectField(
        '__cv_audio_gain',
        ActionCatalog.closestAudioGain(ac.sensitivity ?? 128),
        ActionCatalog.cvAudioGainOptions(ac.sensitivity ?? 128)
      )
    )
    html += fieldRow(
      'Threshold',
      selectField(
        'audio_config.threshold',
        ac.threshold ?? 0,
        ActionCatalog.cvAudioThresholdOptions(ac.threshold ?? 0)
      )
    )
    html += fieldRow(
      'Range',
      selectField('audio_config.range', ac.range || 'bi5v', AUDIO_RANGE_OPTIONS)
    )
    html += fieldRow(
      'Attack',
      selectField('audio_config.attack_ms', ac.attack_ms ?? 10, AUDIO_ATTACK_OPTIONS)
    )
    html += fieldRow(
      'Release',
      selectField('audio_config.release_ms', ac.release_ms ?? 200, AUDIO_RELEASE_OPTIONS)
    )
    html += fieldRow(
      'Polarity',
      selectField('audio_config.polarity', ac.polarity || 'attract', AUDIO_POLARITY_OPTIONS)
    )
    html += renderLfoCcSlots(ctrl, 'cv', m.cv)
    return html
  }

  function renderCv (ctrl) {
    const m = ctrl.editModel
    const syncClock = m.clock_source === 'sync'
    let html = ''
    if (syncClock) {
      html += `<wa-callout variant="neutral">CV mode is Clock Sync while tempo source is Sync.</wa-callout>`
      return section('Control Voltage', html)
    }

    const userMode = cvUserModeSelection(m)
    html += fieldRow(
      'Mode',
      selectField('__cv_user_mode', userMode, CV_USER_MODES)
    )

    if (userMode === 'disabled') {
      return section('Control Voltage', html)
    }

    if (userMode === 'cv_gate') {
      html += fieldRow(
        'Velocity mode',
        selectField(
          'cv_velocity_mode',
          cvGateVelocityModeValue(m),
          CV_GATE_VELOCITY_MODE
        )
      )
      const cvVelMode = cvGateVelocityModeValue(m)
      if (cvVelMode === 'fixed') {
        html += fieldRow(
          'Fixed velocity',
          numberField('cv_velocity', m.cv_velocity ?? 100, 1, 127)
        )
      }
      return section('Control Voltage', html)
    }

    if (userMode === 'trigger') {
      html += renderAction(
        ctrl,
        'cv_trigger_action',
        m.cv_trigger_action || { type: 'none' },
        { trigger: ActionCatalog.TRIGGERS.BUTTON, typeLabel: 'Action' }
      )
      html += fieldRow(
        'Threshold',
        selectField(
          'cv_trigger_threshold',
          ActionCatalog.closestCvTriggerThreshold(m.cv_trigger_threshold ?? 50),
          ActionCatalog.cvTriggerThresholdOptions(m.cv_trigger_threshold ?? 50)
        )
      )
      html += fieldRow(
        'Debounce',
        selectField(
          'cv_trigger_debounce_ms',
          ActionCatalog.closestCvTriggerDebounce(m.cv_trigger_debounce_ms ?? 0),
          ActionCatalog.cvTriggerDebounceOptions(m.cv_trigger_debounce_ms ?? 0)
        )
      )
      return section('Control Voltage', html)
    }

    if (userMode === 'audio') {
      html += renderCvAudio(ctrl)
      return section('Control Voltage', html)
    }

    // Continuous CV routings (control_change / notes / lfo_rate / lfo_depth / tempo_nudge)
    if (!m.cv) m.cv = { enabled: true, output_type: 'cc' }
    html += renderContinuousMapping(ctrl, 'cv', m.cv, {
      hideEnabled: true,
      hideRouting: true,
      noteSelectors: true,
      velocityMin: 0,
      ccSlots: true,
      tempoNudgePath: 'cv_tempo_nudge_pct',
      tempoNudgeDirectionPath: 'cv_tempo_nudge_direction',
      tempoNudgeSelect: true
    })
    return section('Control Voltage', html)
  }

  function effectiveTouchwheelMode (m) {
    const mode = m.touchwheel_mode || 'pads'
    if (mode !== 'velocity') return mode
    if (cvGateVelocityModeValue(m) === 'touchwheel') return 'velocity'
    return m.touchwheel_mode_prev || 'pads'
  }

  function touchwheelUserModeSelection (m) {
    const mode = effectiveTouchwheelMode(m)
    if (mode === 'pads') return 'pads'
    if (m.touchwheel?.enabled === false) return 'disabled'
    const ot = m.touchwheel?.output_type || 'cc'
    for (const opt of TOUCHWHEEL_USER_MODES) {
      if (!opt.touchwheel_mode) continue
      if (opt.touchwheel_mode !== mode) continue
      if (opt.output_type && opt.output_type !== ot) continue
      return opt.v
    }
    return 'control_change'
  }

  function renderTouchwheelStyle (m) {
    return fieldRow(
      'Style',
      selectField('touchwheel_style', m.touchwheel_style || 'odometer', TOUCHWHEEL_STYLE_OPTIONS)
    )
  }

  function renderTouchwheel (ctrl) {
    const m = ctrl.editModel
    if (cvClaimsSource(m, 'touchwheel')) {
      let html = CV_GATE_CONTROLLED_CALLOUT
      html += renderTouchwheelStyle(m)
      return section('Touchwheel', html)
    }
    const userMode = touchwheelUserModeSelection(m)
    let html = fieldRow(
      'Mode',
      selectField('__touchwheel_user_mode', userMode, TOUCHWHEEL_USER_MODES)
    )
    if (userMode === 'disabled' || userMode === 'pads') {
      return section('Touchwheel', html)
    }

    const spec = TOUCHWHEEL_USER_MODES.find(o => o.v === userMode)
    if (!m.touchwheel) m.touchwheel = { enabled: true, output_type: 'cc' }

    if (userMode === 'control_change') {
      html += renderTouchwheelCcSlots(ctrl)
      if (spec?.supports_style) html += renderTouchwheelStyle(m)
      if (m.touchwheel_style === 'endless') {
        html += fieldRow(
          'Initial value',
          numberField('touchwheel_initial_value', m.touchwheel_initial_value ?? 0, 0, 127)
        )
      }
    } else if (userMode === 'notes') {
      html += fieldRow(
        'Base note',
        selectField('touchwheel.base_note', m.touchwheel.base_note ?? 60,
          ActionCatalog.noteNameOptions(m.touchwheel.base_note ?? 60))
      )
      html += fieldRow(
        'Range',
        selectField('touchwheel.note_range', m.touchwheel.note_range ?? 24, NOTE_OCTAVE_OPTIONS)
      )
      html += fieldRow(
        'Velocity',
        numberField('touchwheel.velocity', m.touchwheel.velocity ?? 100, 1, 127)
      )
      html += fieldRow('Latch', checkboxField('touchwheel.note_latch', !!m.touchwheel.note_latch))
      if (m.touchwheel.note_latch) {
        html += fieldRow(
          'Release',
          numberField('touchwheel.note_release_ms', m.touchwheel.note_release_ms ?? 500, 100, 5000)
        )
      }
      if (m.touchwheel_style === 'odometer') {
        html += fieldRow(
          'Polyphony',
          selectField('touchwheel.polyphony', m.touchwheel.polyphony || 'mono', [
            { v: 'mono', l: 'Mono' },
            { v: 'poly', l: 'Poly' }
          ])
        )
      }
      if (spec?.supports_style) html += renderTouchwheelStyle(m)
    } else if (userMode === 'double_cc') {
      html += renderTouchwheelCcSlots(ctrl)
      if (spec?.supports_style) html += renderTouchwheelStyle(m)
      if (m.touchwheel_style === 'endless') {
        html += fieldRow(
          'Initial value',
          numberField('touchwheel_initial_value', m.touchwheel_initial_value ?? 0, 0, 127)
        )
      }
    } else if (userMode === 'tempo_nudge') {
      html += fieldRow(
        'Direction',
        selectField(
          'touchwheel_tempo_nudge_direction',
          m.touchwheel_tempo_nudge_direction ?? 0,
          ActionCatalog.tempoNudgeDirectionOptions(m.touchwheel_tempo_nudge_direction ?? 0)
        )
      )
      html += fieldRow(
        'Nudge %',
        selectField(
          'touchwheel_tempo_nudge_pct',
          m.touchwheel_tempo_nudge_pct ?? 10,
          ActionCatalog.touchwheelTempoNudgeAmountOptions(m.touchwheel_tempo_nudge_pct ?? 10)
        )
      )
      html += fieldRow(
        'Return Speed',
        selectField(
          'touchwheel_tempo_nudge_return',
          m.touchwheel_tempo_nudge_return ?? 0,
          ActionCatalog.touchwheelNudgeReturnOptions(m.touchwheel_tempo_nudge_return ?? 0)
        )
      )
      if ((m.touchwheel_tempo_nudge_direction ?? 0) !== 0) {
        html += fieldRow(
          'Style',
          selectField('touchwheel_style', m.touchwheel_style || 'odometer', TOUCHWHEEL_STYLE_OPTIONS)
        )
      }
    } else if (userMode === 'tempo') {
      if (spec?.supports_style) html += renderTouchwheelStyle(m)
      html += fieldRow(
        'Floor',
        numberField('touchwheel_tempo_floor', m.touchwheel_tempo_floor ?? 20, 20, 300)
      )
      html += fieldRow(
        'Ceiling',
        numberField('touchwheel_tempo_ceiling', m.touchwheel_tempo_ceiling ?? 300, 20, 300)
      )
    } else if (userMode === 'aftertouch') {
      html += fieldRow(
        'Return Speed',
        selectField(
          'touchwheel_aftertouch_return',
          m.touchwheel_aftertouch_return ?? 1,
          ActionCatalog.touchwheelNudgeReturnOptions(m.touchwheel_aftertouch_return ?? 1)
        )
      )
      if (spec?.supports_style) html += renderTouchwheelStyle(m)
    } else if (userMode === 'lfo_rate' || userMode === 'lfo_depth') {
      html += fieldRow(
        'Target',
        selectField('touchwheel_lfo_target', m.touchwheel_lfo_target || 'both', LFO_TARGET)
      )
      if (spec?.supports_style) html += renderTouchwheelStyle(m)
    } else if (spec?.supports_style) {
      html += renderTouchwheelStyle(m)
    }

    return section('Touchwheel', html)
  }

  function renderLfo (ctrl, n) {
    const cfgKey = n === 1 ? 'lfo1_config' : 'lfo2_config'
    const mapKey = n === 1 ? 'lfo1' : 'lfo2'
    const velKey = n === 1 ? 'lfo1_velocity_mode' : 'lfo2_velocity_mode'
    const modePath = n === 1 ? '__lfo1_user_mode' : '__lfo2_user_mode'
    const userModes = n === 1 ? LFO1_USER_MODES : LFO2_USER_MODES
    const m = ctrl.editModel
    if (!m[cfgKey])
      m[cfgKey] = { enabled: false, waveform: 'sine', rate_mode: 'free' }
    if (!m[mapKey]) m[mapKey] = { enabled: false, output_type: 'cc' }

    const userMode = lfoUserModeSelection(n, m)
    let html = fieldRow(
      'Mode',
      selectField(modePath, userMode, userModes)
    )
    if (userMode === 'disabled') return section(`LFO${n}`, html)

    m[mapKey].enabled = m[cfgKey].enabled
    const claimKey = n === 1 ? 'lfo1' : 'lfo2'
    const waveformOpts = [
      { v: 'sine', l: 'Sine' },
      { v: 'triangle', l: 'Triangle' },
      { v: 'square', l: 'Square' },
      { v: 'saw_up', l: 'Saw Up' },
      { v: 'saw_down', l: 'Saw Down' },
      { v: 'sample_hold', l: 'Sample & Hold' },
      { v: 'bin', l: 'Bin' },
      { v: 'glider', l: 'Glider' },
      { v: 'stray', l: 'Stray' }
    ]

    if (cvClaimsSource(m, claimKey)) {
      html += CV_GATE_CONTROLLED_CALLOUT
    }

    const lfoCtx = {
      bpm: m.bpm ?? 120,
      feltBeats: m.time_signature?.numerator ?? 4
    }
    const lfoPreviewSvg = window.LfoWaveformPreview?.renderSvg(m[cfgKey], lfoCtx) || ''

    html += `<div class="scene-lfo-block" data-controller="lfo-waveform-fields"
      data-lfo-waveform-fields-cfg-key-value="${esc(cfgKey)}">`
    html += renderLfoEngineFields(cfgKey, m[cfgKey])
    html += fieldRow(
      'Waveform',
      selectField(`${cfgKey}.waveform`, m[cfgKey].waveform || 'sine', waveformOpts)
    )
    html += `<div class="scene-lfo-waveform-preview" data-lfo-waveform-fields-target="preview"
      data-controller="lfo-waveform" data-lfo-waveform-cfg-key-value="${esc(cfgKey)}">${lfoPreviewSvg}</div>`
    const lfoRateMode = (m[cfgKey].rate_mode === 'tempo') ? 'tempo' : 'free'
    if (m[cfgKey].rate_mode !== lfoRateMode) m[cfgKey].rate_mode = lfoRateMode
    html += fieldRow(
      'Rate mode',
      selectField(`${cfgKey}.rate_mode`, lfoRateMode, [
        { v: 'free', l: 'Time' },
        { v: 'tempo', l: 'Division' }
      ])
    )
    if (lfoRateMode === 'free') {
      html += fieldRow(
        'Rate',
        selectField(
          `${cfgKey}.rate_hz`,
          m[cfgKey].rate_hz ?? 1,
          ActionCatalog.lfoSceneRateHzOptions(m[cfgKey].rate_hz ?? 1)
        )
      )
    }
    if (lfoRateMode === 'tempo') {
      html += fieldRow(
        'Divider',
        selectField(
          `${cfgKey}.division`,
          m[cfgKey].division || 'quarter',
          ActionCatalog.lfoSceneDivisionOptions(m[cfgKey].division || 'quarter')
        )
      )
    }
    html += renderLfoOutputRangeFields(cfgKey, m[cfgKey])
    html += '</div>'

    if (!cvClaimsSource(m, claimKey)) {
      html += renderContinuousMapping(ctrl, mapKey, m[mapKey], {
        hideEnabled: true,
        hideRouting: true,
        ccSlots: true,
        noteSelectors: true,
        noteOctaveOptions: SENSOR_NOTE_OCTAVE_OPTIONS,
        velocityModePath: velKey,
        hideCurve: true,
        hidePolarity: true
      })
    }
    return section(`LFO${n}`, html)
  }

  function renderRtg (ctrl) {
    const m = ctrl.editModel
    if (!m.rtg_config)
      m.rtg_config = { enabled: false, mode: 'continuous', generator: 'random' }
    const userMode = generatorUserModeSelection(RTG_USER_MODES, m.rtg_config)
    let html = fieldRow(
      'Mode',
      selectField('__rtg_user_mode', userMode, RTG_USER_MODES)
    )
    if (userMode === 'disabled') return section('RTG', html)

    html += fieldRow(
      'Generator',
      selectField('rtg_config.generator', m.rtg_config.generator || 'random', [
        { v: 'random', l: 'Random' },
        { v: 'shepard', l: 'Shepard' }
      ])
    )
    html += fieldRow(
      'Start mode',
      selectField(
        'rtg_config.start_mode',
        m.rtg_config.start_mode || 'running',
        ENGINE_START_MODE
      )
    )
    if (m.rtg_config.mode === 'continuous') {
      html += fieldRow(
        'Rate mode',
        selectField('rtg_config.rate_mode', m.rtg_config.rate_mode || 'free', [
          { v: 'free', l: 'Time' },
          { v: 'sync', l: 'Division' }
        ])
      )
      if (m.rtg_config.rate_mode === 'free') {
        html += fieldRow(
          'Rate',
          selectField('rtg_config.rate_hz', m.rtg_config.rate_hz ?? 2,
            ActionCatalog.engineModifyRateHzOptions(200).map(o => ({
              v: o.v / 100,
              l: o.l
            })))
        )
      } else {
        html += fieldRow(
          'Divider',
          selectField('rtg_config.division', m.rtg_config.division || 'quarter',
            ActionCatalog.lfoModifyDivisionOptions(0).map(o => ({
              v: ['16_bars', '12_bars', '8_bars', '4_bars', '2_bars', '1_bar',
                'half', 'quarter', 'eighth', 'sixteenth', '32nd'][o.v],
              l: o.l
            })))
        )
      }
      html += renderGeneratorPatternFields('rtg_config', m.rtg_config)
    }
    if (m.rtg_config.generator === 'random') {
      html += fieldRow(
        'Glide',
        checkboxField('rtg_config.glide', !!m.rtg_config.glide)
      )
      html += fieldRow(
        'Note min',
        numberField('rtg_config.note_min', m.rtg_config.note_min ?? 36, 0, 127)
      )
      html += fieldRow(
        'Note max',
        numberField('rtg_config.note_max', m.rtg_config.note_max ?? 96, 0, 127)
      )
      html += fieldRow(
        'Velocity',
        numberField('rtg_config.velocity', m.rtg_config.velocity ?? 100, 1, 127)
      )
    }
    if (m.rtg_config.generator === 'shepard') {
      html += fieldRow(
        'Direction',
        selectField(
          'rtg_config.shepard_direction',
          m.rtg_config.shepard_direction || 'rising',
          SHEPARD_DIRECTION
        )
      )
      html += fieldRow(
        'Smooth',
        checkboxField('rtg_config.glide', !!m.rtg_config.glide)
      )
      if (m.rtg_config.glide) {
        html += fieldRow(
          'Style',
          selectField(
            'rtg_config.shepard_style',
            m.rtg_config.shepard_style || 'stream',
            SHEPARD_STYLE
          )
        )
        if (m.rtg_config.shepard_style === 'wide') {
          html += fieldRow(
            'Retrigger (semitones)',
            selectField('rtg_config.shepard_wide_semis', m.rtg_config.shepard_wide_semis ?? 4, [
              { v: 2, l: '2' },
              { v: 3, l: '3' },
              { v: 4, l: '4' }
            ])
          )
        }
        html += fieldRow(
          'Layout',
          selectField(
            'rtg_config.shepard_layout',
            m.rtg_config.shepard_layout || 'single',
            SHEPARD_LAYOUT
          )
        )
        html += fieldRow(
          'Fade',
          selectField(
            'rtg_config.shepard_fade',
            m.rtg_config.shepard_fade || 'none',
            SHEPARD_FADE
          )
        )
      }
    }
    return section('RTG', html)
  }

  function renderSampleHold (ctrl) {
    const m = ctrl.editModel
    if (!m.sample_hold_config) {
      m.sample_hold_config = { enabled: false, mode: 'continuous' }
    }
    if (!m.sample_hold) m.sample_hold = { enabled: false, output_type: 'cc' }
    const userMode = generatorUserModeSelection(SAMPLE_HOLD_USER_MODES, m.sample_hold_config)
    let html = fieldRow(
      'Mode',
      selectField('__sample_hold_user_mode', userMode, SAMPLE_HOLD_USER_MODES)
    )
    if (userMode === 'disabled') return section('S+H', html)

    if (cvClaimsSource(m, 'sample_hold')) {
      html += CV_GATE_CONTROLLED_CALLOUT
    }
    html += fieldRow(
      'Start mode',
      selectField(
        'sample_hold_config.start_mode',
        m.sample_hold_config.start_mode || 'running',
        ENGINE_START_MODE
      )
    )
    html += fieldRow(
      'Glide',
      checkboxField('sample_hold_config.glide', !!m.sample_hold_config.glide)
    )
    if (m.sample_hold_config.mode === 'continuous') {
      html += fieldRow(
        'Rate mode',
        selectField('sample_hold_config.rate_mode', m.sample_hold_config.rate_mode || 'free', [
          { v: 'free', l: 'Time' },
          { v: 'sync', l: 'Division' }
        ])
      )
      if (m.sample_hold_config.rate_mode === 'sync') {
        html += fieldRow(
          'Divider',
          selectField('sample_hold_config.division', m.sample_hold_config.division || 'quarter',
            ActionCatalog.lfoModifyDivisionOptions(0).map(o => ({
              v: ['16_bars', '12_bars', '8_bars', '4_bars', '2_bars', '1_bar',
                'half', 'quarter', 'eighth', 'sixteenth', '32nd'][o.v],
              l: o.l
            })))
        )
      } else {
        html += fieldRow(
          'Rate',
          selectField('sample_hold_config.rate_hz', m.sample_hold_config.rate_hz ?? 2,
            ActionCatalog.engineModifyRateHzOptions(200).map(o => ({
              v: o.v / 100,
              l: o.l
            })))
        )
      }
      html += renderGeneratorPatternFields('sample_hold_config', m.sample_hold_config)
    }
    if (!cvClaimsSource(m, 'sample_hold'))
      html += renderSampleHoldCcSlots(ctrl)
    return section('S+H', html)
  }

  function renderPads (ctrl) {
    if (!ctrl.editModel.touchpads || ctrl.editModel.touchpads.length !== 12) {
      ctrl.editModel.touchpads = Array.from({ length: 12 }, () => ({
        action: { type: 'none' }
      }))
    }
    let html = ''
    const mode = ctrl.editModel.touchwheel_mode || 'pads'
    const start = mode === 'pads' ? 0 : 8
    for (let i = start; i < 12; i++) {
      const tp = ctrl.editModel.touchpads[i]
      if (!tp.action && tp.actions?.length) tp.action = tp.actions[0]
      const act = tp.action || { type: 'none' }
      const trigger =
        i < 8
          ? ActionCatalog.TRIGGERS.TOUCHPAD_0_7
          : ActionCatalog.TRIGGERS.TOUCHPAD_8_11
      html += `<div class="scene-pad-row"><h4>${esc(
        ActionCatalog.padDisplayName(i)
      )}</h4>`
      html += renderAction(ctrl, `touchpads.${i}.action`, act, { trigger, typeLabel: 'Action' })
      html += '</div>'
    }
    return section('Pads', html)
  }

  function renderButtons (ctrl) {
    let html = ''
    ;['button_left', 'button_right', 'button_both'].forEach((key, idx) => {
      const labels = ['Left', 'Right', 'Both']
      const act = ctrl.editModel[key] || { type: 'none' }
      html += `<div class="scene-action-slot"><h4>Button ${labels[idx]}</h4>`
      html += renderAction(ctrl, key, act, {
        trigger: ActionCatalog.TRIGGERS.BUTTON
      })
      html += '</div>'
    })
    return section('Buttons', html)
  }

  function renderBump (ctrl) {
    return section(
      'Bump',
      renderAction(ctrl, 'bump', ctrl.editModel.bump || { type: 'none' }, {
        trigger: ActionCatalog.TRIGGERS.BUMP
      })
    )
  }

  function renderOnLoad (ctrl) {
    if (!ctrl.editModel.on_load) ctrl.editModel.on_load = []
    return section(
      'On-Load',
      renderActionChain(
        ctrl,
        'on_load',
        ctrl.editModel.on_load,
        4,
        ActionCatalog.TRIGGERS.ON_LOAD,
        { slotTitlePrefix: 'Load Action' }
      )
    )
  }

  function renderOnPlay (ctrl) {
    if (!ctrl.editModel.use_transport) return ''
    if (!ctrl.editModel.on_play) ctrl.editModel.on_play = []
    return section(
      'On-Play',
      renderActionChain(
        ctrl,
        'on_play',
        ctrl.editModel.on_play,
        4,
        ActionCatalog.TRIGGERS.ON_PLAY,
        { slotTitlePrefix: 'Play Action' }
      )
    )
  }

  function renderCcTriggers (ctrl) {
    if (!ctrl.deviceContext.midiControl) return ''
    if (!ctrl.editModel.cc_triggers) {
      ctrl.editModel.cc_triggers = Array.from({ length: 4 }, () => ({
        cc_number: 0,
        action: { type: 'none' }
      }))
    }
    let html = ''
    for (let i = 0; i < 4; i++) {
      const slot = ctrl.editModel.cc_triggers[i]
      html += `<div class="scene-action-slot"><h4>CC Trigger ${i + 1}</h4>`
      html += fieldRow(
        'CC',
        numberField(`cc_triggers.${i}.cc_number`, slot.cc_number ?? 0, 0, 127)
      )
      html += renderAction(
        ctrl,
        `cc_triggers.${i}.action`,
        slot.action || { type: 'none' },
        { trigger: ActionCatalog.TRIGGERS.CC }
      )
      html += '</div>'
    }
    return section('CC Triggers', html)
  }

  function renderEditor (ctrl, openSections) {
    _openSections = openSections || new Set()
    // Order matches device Scene menu (current_scene.c assignment list, then globals).
    let html = renderPads(ctrl)
    html += renderTouchwheel(ctrl)
    html += renderExpression(ctrl)
    html += renderCv(ctrl)
    html += renderProximity(ctrl)
    html += renderAls(ctrl)
    html += renderButtons(ctrl)
    html += renderLfo(ctrl, 1)
    html += renderLfo(ctrl, 2)
    html += renderBump(ctrl)
    html += renderOnLoad(ctrl)
    html += renderOnPlay(ctrl)
    html += renderSampleHold(ctrl)
    html += renderTiltAxis(ctrl, 'x')
    html += renderTiltAxis(ctrl, 'y')
    html += renderRtg(ctrl)
    html += renderCcTriggers(ctrl)
    html += renderNoteTrack(ctrl)
    html += editorDivider()
    html += renderPerSceneDevice(ctrl)
    html += renderSceneGlobals(ctrl)
    return html
  }

  // Section titles that should start expanded because they carry an assignment
  // or an enabled feature. Titles must match the section() headers above.
  function sectionsWithContent (ctrl) {
    const m = ctrl.editModel || {}
    const open = new Set()
    const hasAction = a => !!(a && a.type && a.type !== 'none')
    const padAction = tp => hasAction(tp?.action) || hasAction(tp?.actions?.[0])

    if ((m.touchpads || []).some(padAction)) open.add('Pads')
    if (m.touchwheel_mode && m.touchwheel_mode !== 'pads')
      open.add('Touchwheel')
    if (m.expression?.enabled) open.add('Expression')
    if (m.cv_input_mode && m.cv_input_mode !== 'none')
      open.add('Control Voltage')
    if (m.proximity?.enabled) open.add('Proximity')
    if (m.als?.enabled) open.add('Ambient Light')
    if (
      hasAction(m.button_left) ||
      hasAction(m.button_right) ||
      hasAction(m.button_both)
    ) {
      open.add('Buttons')
    }
    if (m.lfo1_config?.enabled) open.add('LFO1')
    if (m.lfo2_config?.enabled) open.add('LFO2')
    if (hasAction(m.bump)) open.add('Bump')
    if ((m.on_load || []).some(hasAction)) open.add('On-Load')
    if ((m.on_play || []).some(hasAction)) open.add('On-Play')
    if (m.sample_hold_config?.enabled) open.add('S+H')
    if (m.tilt_x?.enabled) open.add('Tilt X')
    if (m.tilt_y?.enabled) open.add('Tilt Y')
    if (m.rtg_config?.enabled) open.add('RTG')
    if (ctrl.deviceContext?.midiControl &&
        (m.cc_triggers || []).some(t => hasAction(t?.action))) {
      open.add('CC Triggers')
    }
    if (m.note_track?.enabled) open.add('Note Track')
    return open
  }

  function allSectionTitles (ctrl) {
    const titles = [
      'Pads', 'Touchwheel', 'Expression', 'Control Voltage', 'Proximity',
      'Ambient Light', 'Buttons', 'LFO1', 'LFO2', 'Bump', 'On-Load',
      'S+H', 'Tilt X', 'Tilt Y', 'RTG', 'Note Track'
    ]
    if (ctrl.editModel?.use_transport) titles.push('On-Play')
    if (ctrl.deviceContext?.midiControl) titles.push('CC Triggers')
    return new Set(titles)
  }

  function sceneNameToSlug (name) {
    if (!name) return 's.json'
    let out = ''
    for (let i = 0; i < name.length && out.length < 58; i++) {
      const c = name[i]
      if (c >= 'A' && c <= 'Z') out += c.toLowerCase()
      else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) out += c
      else if (c === ' ' || c === '-') {
        if (out.length > 0 && out[out.length - 1] !== '_') out += '_'
      }
    }
    if (out.endsWith('_')) out = out.slice(0, -1)
    if (!out) out = 's'
    return `${out}.json`
  }

  function isReservedSceneName (name) {
    return sceneNameToSlug(name).toLowerCase() === 'manifest.json'
  }

  return {
    renderEditor,
    section,
    fieldRow,
    sectionsWithContent,
    allSectionTitles,
    isReservedSceneName,
    sceneNameToSlug
  }
})()
