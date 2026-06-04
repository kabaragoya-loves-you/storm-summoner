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

  const CURVE = [
    { v: 0, l: 'Linear' },
    { v: 1, l: 'Exponential' },
    { v: 2, l: 'Logarithmic' },
    { v: 3, l: 'S-Curve' }
  ]

  const LFO_TARGET = [
    { v: 'lfo1', l: 'LFO1' },
    { v: 'lfo2', l: 'LFO2' },
    { v: 'both', l: 'Both' }
  ]

  const VELOCITY_MODE = [
    { v: 'fixed', l: 'Fixed' },
    { v: 'gate_voltage', l: 'Gate Voltage' },
    { v: 'touchwheel', l: 'Touchwheel' }
  ]

  const EXPRESSION_MODE = [
    { v: 'expression', l: 'Expression' },
    { v: 'sustain', l: 'Sustain' },
    { v: 'sostenuto', l: 'Sostenuto' },
    { v: 'switch', l: 'Switch' }
  ]

  const CV_INPUT_MODE = [
    { v: 'none', l: 'None' },
    { v: 'cv', l: 'Control Voltage' },
    { v: 'note', l: 'CV/Gate' },
    { v: 'audio', l: 'Audio' },
    { v: 'trigger', l: 'Trigger' }
  ]

  const TOUCHWHEEL_MODE = [
    { v: 'pads', l: 'Pads' },
    { v: 'program_change', l: 'Program Change' },
    { v: 'continuous', l: 'Continuous' },
    { v: 'set_tempo', l: 'Set Tempo' },
    { v: 'pitch_bend', l: 'Pitch Bend' },
    { v: 'aftertouch', l: 'Aftertouch' },
    { v: 'double_cc', l: 'Double CC' },
    { v: 'velocity', l: 'Velocity' },
    { v: 'lfo_rate', l: 'LFO Rate' },
    { v: 'lfo_depth', l: 'LFO Depth' },
    { v: 'lfo1_rate', l: 'LFO1 Rate' },
    { v: 'lfo2_rate', l: 'LFO2 Rate' },
    { v: 'rtg_rate', l: 'RTG Rate' }
  ]

  const ACTION_TYPES = [
    'none', 'note', 'control', 'scene', 'preset', 'transport', 'tempo', 'touchwheel',
    'lfo', 'clock', 'cut', 'ui', 'param', 'rtg', 'sample_hold', 'piano_pedal',
    'randomize', 'punch_in', 'flag_ceremony', 'boomerang', 'inspect_scene'
  ]

  const ACTION_VARIANTS = [
    'set', 'hold', 'cycle', 'toggle', 'increment', 'decrement', 'start', 'stop',
    'tap', 'play', 'pause', 'record', 'modify', 'step', 'downbeat', 'burst'
  ]

  function esc (s) {
    return String(s ?? '')
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/"/g, '&quot;')
  }

  function fieldRow (label, inner) {
    return `<div class="scene-field-row"><label class="scene-field-label">${esc(label)}</label>
      <div class="scene-field-control">${inner}</div></div>`
  }

  function selectField (path, value, options, action) {
    const opts = options.map(o => {
      const sel = String(o.v) === String(value) ? ' selected' : ''
      return `<option value="${esc(o.v)}"${sel}>${esc(o.l)}</option>`
    }).join('')
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

  function renderContinuousMapping (ctrl, mappingPath, mapping, opts = {}) {
    const m = mapping || {}
    const enabledPath = `${mappingPath}.enabled`
    const otPath = `${mappingPath}.output_type`
    const ot = m.output_type || 'cc'
    let html = fieldRow('Enabled', checkboxField(enabledPath, m.enabled !== false))
    if (m.enabled === false) return html

    html += fieldRow('Output', selectField(otPath, ot, OUTPUT_TYPES))
    ensureCcNumbers(m)

    if (ot === 'cc') {
      for (let i = 0; i < 4; i++) {
        html += fieldRow(`CC slot ${i + 1}`,
          numberField(`${mappingPath}.cc_numbers.${i}`, m.cc_numbers[i] ?? 0, 0, 127))
      }
      html += fieldRow('Polarity', selectField(`${mappingPath}.polarity`, m.polarity ?? 0, POLARITY))
      html += fieldRow('Curve', selectField(`${mappingPath}.curve_type`, m.curve_type ?? 0, CURVE))
    } else if (ot === 'note') {
      html += fieldRow('Base note', numberField(`${mappingPath}.base_note`, m.base_note ?? 60, 0, 127))
      html += fieldRow('Range (semitones)',
        numberField(`${mappingPath}.note_range`, m.note_range ?? 24, 1, 127))
      if (opts.velocityModePath) {
        const vm = ctrl.getAtPath(opts.velocityModePath) || 'fixed'
        html += fieldRow('Velocity mode', selectField(opts.velocityModePath, vm, VELOCITY_MODE))
        if (vm === 'fixed') {
          html += fieldRow('Velocity',
            numberField(`${mappingPath}.velocity`, m.velocity ?? 100, 1, 127))
        }
      } else {
        html += fieldRow('Velocity',
          numberField(`${mappingPath}.velocity`, m.velocity ?? 100, 1, 127))
      }
      html += fieldRow('Polarity', selectField(`${mappingPath}.polarity`, m.polarity ?? 0, POLARITY))
      html += fieldRow('Curve', selectField(`${mappingPath}.curve_type`, m.curve_type ?? 0, CURVE))
    } else if (ot === 'lfo_rate' || ot === 'lfo_depth') {
      html += fieldRow('LFO target',
        selectField(`${mappingPath}.lfo_target`, m.lfo_target || 'both', LFO_TARGET))
    } else if (ot === 'tempo_nudge' && opts.tempoNudgePath) {
      html += fieldRow('Nudge %',
        numberField(opts.tempoNudgePath, ctrl.getAtPath(opts.tempoNudgePath) ?? 10, 0, 100))
    }

    if (opts.showMinMidMax && ot === 'cc') {
      html += fieldRow('Min', numberField(`${mappingPath}.min_value`, m.min_value ?? 0, 0, 127))
      html += fieldRow('Middle', numberField(`${mappingPath}.middle_value`, m.middle_value ?? 64, 0, 127))
      html += fieldRow('Max', numberField(`${mappingPath}.max_value`, m.max_value ?? 127, 0, 127))
    }

    return html
  }

  function renderAction (ctrl, path, action) {
    const a = action || { type: 'none' }
    const typeOpts = ACTION_TYPES.map(t => ({ v: t, l: t }))
    let html = fieldRow('Type', selectField(`${path}.type`, a.type || 'none', typeOpts))
    if (a.type && a.type !== 'none') {
      html += fieldRow('Variant', selectField(`${path}.variant`, a.variant || '', [
        { v: '', l: '(none)' },
        ...ACTION_VARIANTS.map(v => ({ v, l: v }))
      ]))
      if (a.type === 'note') {
        html += fieldRow('Note', numberField(`${path}.note`, a.note ?? 60, 0, 127))
        html += fieldRow('Velocity', numberField(`${path}.velocity`, a.velocity ?? 100, 1, 127))
      }
      if (a.type === 'control' || a.type === 'control_change') {
        const cc = Array.isArray(a.cc) ? a.cc[0] : (a.cc ?? 0)
        html += fieldRow('CC', numberField(`${path}.cc`, cc, 0, 127))
        if (a.variant === 'set') {
          html += fieldRow('Value', numberField(`${path}.value`, a.value ?? 0, 0, 127))
        }
      }
      if (a.type === 'scene' && a.variant === 'set') {
        html += fieldRow('Scene #', numberField(`${path}.number`, a.number ?? 1, 1, 128))
      }
      if (a.type === 'tempo' && a.variant === 'set') {
        html += fieldRow('BPM', numberField(`${path}.bpm`, a.bpm ?? 120, 20, 300))
      }
      if (a.type === 'lfo') {
        html += fieldRow('Slot', numberField(`${path}.slot`, a.slot ?? 1, 1, 3))
      }
      html += fieldRow('Timing', textField(`${path}.timing`, a.timing || 'immediate', 32))
      html += fieldRow('Repeat', checkboxField(`${path}.repeat`, !!a.repeat))
    }
    return html
  }

  function renderActionChain (ctrl, path, chain, maxItems) {
    const arr = Array.isArray(chain) ? chain : []
    let html = ''
    for (let i = 0; i < maxItems; i++) {
      const itemPath = `${path}.${i}`
      html += `<div class="scene-action-slot"><h4>Action ${i + 1}</h4>`
      html += renderAction(ctrl, itemPath, arr[i] || { type: 'none' })
      html += '</div>'
    }
    return html
  }

  function section (title, body, open = false) {
    return `<details class="scene-editor-section" ${open ? 'open' : ''}>
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
    let html = fieldRow('Pedal (device ID)', textField('device_id', m.device_id || '', 32))
    html += fieldRow('MIDI channel',
      numberField('midi_channel', m.midi_channel ?? 0, 0, 16))
    html += fieldRow('TRS type',
      selectField('trs_type', m.trs_type ?? 0, [
        { v: 0, l: 'Global' },
        { v: 1, l: 'Type A' },
        { v: 2, l: 'Type B' },
        { v: 3, l: 'TS' },
        { v: 4, l: 'Both' }
      ]))
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
    html += fieldRow('Screen', textField('ui_module', m.ui_module || 'beat', 15))

    if (ctx.sceneMode !== 1) {
      html += fieldRow('Program', numberField('program_number', m.program_number ?? 0, 0, 127))
      html += fieldRow('PC on load', checkboxField('send_pc_on_load', m.send_pc_on_load !== false))
    }

    html += fieldRow('BPM', numberField('bpm', m.bpm ?? 120, 20, 300))
    html += fieldRow('Time sig (num)',
      numberField('time_signature.numerator', m.time_signature?.numerator ?? 4, 1, 16))
    html += fieldRow('Time sig (den)',
      numberField('time_signature.denominator', m.time_signature?.denominator ?? 4, 1, 16))
    html += fieldRow('Beat divider',
      selectField('beat_divider', m.beat_divider || 'quarter', [
        { v: 'quarter', l: 'Quarter' },
        { v: 'eighth', l: 'Eighth' },
        { v: 'sixteenth', l: 'Sixteenth' }
      ]))
    html += fieldRow('Clock source',
      selectField('clock_source', m.clock_source || 'internal', [
        { v: 'internal', l: 'Internal' },
        { v: 'midi', l: 'MIDI' },
        { v: 'sync', l: 'Sync' }
      ]))
    html += fieldRow('Use transport', checkboxField('use_transport', !!m.use_transport))
    html += fieldRow('Send clock', checkboxField('send_clock', m.send_clock !== false))
    html += fieldRow('Note channel', numberField('note_channel', m.note_channel ?? 0, 0, 16))

    return flatBlock('Scene settings', html)
  }

  function renderProximity (ctrl) {
    if (!ctrl.editModel.proximity) ctrl.editModel.proximity = { enabled: false, output_type: 'cc' }
    const body = renderContinuousMapping(ctrl, 'proximity', ctrl.editModel.proximity, {
      velocityModePath: 'proximity_velocity_mode',
      tempoNudgePath: 'proximity_tempo_nudge_pct'
    })
    return section('Proximity', body)
  }

  function renderAls (ctrl) {
    if (!ctrl.editModel.als) ctrl.editModel.als = { enabled: false, output_type: 'cc' }
    const body = renderContinuousMapping(ctrl, 'als', ctrl.editModel.als, {
      velocityModePath: 'als_velocity_mode',
      tempoNudgePath: 'als_tempo_nudge_pct'
    })
    return section('Ambient Light', body)
  }

  function renderTiltAxis (ctrl, axis) {
    const key = axis === 'x' ? 'tilt_x' : 'tilt_y'
    const velKey = axis === 'x' ? 'tilt_x_velocity_mode' : 'tilt_y_velocity_mode'
    const nudgeKey = axis === 'x' ? 'tilt_x_tempo_nudge_pct' : 'tilt_y_tempo_nudge_pct'
    if (!ctrl.editModel[key]) ctrl.editModel[key] = { enabled: false, output_type: 'cc' }
    const body = renderContinuousMapping(ctrl, key, ctrl.editModel[key], {
      velocityModePath: velKey,
      tempoNudgePath: nudgeKey,
      showMinMidMax: true
    })
    return section(`Tilt ${axis.toUpperCase()}`, body)
  }

  function renderNoteTrack (ctrl) {
    if (!ctrl.editModel.note_track) {
      ctrl.editModel.note_track = { enabled: false, output_type: 'cc' }
    }
    const body = renderContinuousMapping(ctrl, 'note_track', ctrl.editModel.note_track, {})
    return section('Note Track', body)
  }

  function renderExpression (ctrl) {
    const m = ctrl.editModel
    const lockedGate = m.cv_input_mode === 'note'
    const lockedLfo = m.lfo1_config?.rate_mode === 'expression' ||
      m.lfo2_config?.rate_mode === 'expression'
    let html = ''
    if (lockedGate) {
      html += `<wa-callout variant="neutral">Expression locked to Gate (CV/Gate mode).</wa-callout>`
    } else if (lockedLfo) {
      html += `<wa-callout variant="neutral">Expression locked to LFO rate source.</wa-callout>`
    } else {
      html += fieldRow('Mode', selectField('expression_mode', m.expression_mode || 'expression',
        EXPRESSION_MODE))
    }
    if (m.expression_mode === 'expression' && !lockedGate && !lockedLfo) {
      if (!m.expression) m.expression = { enabled: true, output_type: 'cc' }
      html += renderContinuousMapping(ctrl, 'expression', m.expression, {
        velocityModePath: 'expression_velocity_mode',
        tempoNudgePath: 'expression_tempo_nudge_pct'
      })
    }
    return section('Expression', html)
  }

  function renderCv (ctrl) {
    const m = ctrl.editModel
    const syncClock = m.clock_source === 'sync'
    let html = ''
    if (syncClock) {
      html += `<wa-callout variant="neutral">CV mode is Clock Sync while tempo source is Sync.</wa-callout>`
    } else {
      html += fieldRow('Input mode',
        selectField('cv_input_mode', m.cv_input_mode || 'none', CV_INPUT_MODE))
    }
    const mode = m.cv_input_mode || 'none'
    if (mode === 'cv' && !syncClock) {
      if (!m.cv) m.cv = { enabled: true, output_type: 'cc' }
      html += renderContinuousMapping(ctrl, 'cv', m.cv, {
        velocityModePath: 'cv_velocity_mode',
        tempoNudgePath: 'cv_tempo_nudge_pct'
      })
    } else if (mode === 'note') {
      html += fieldRow('Velocity mode',
        selectField('cv_velocity_mode', m.cv_velocity_mode || 'fixed', VELOCITY_MODE))
      if (m.cv_velocity_mode === 'fixed') {
        html += fieldRow('Fixed velocity',
          numberField('cv_velocity', m.cv_velocity ?? 100, 1, 127))
      }
    } else if (mode === 'trigger') {
      html += renderAction(ctrl, 'cv_trigger_action', m.cv_trigger_action || { type: 'none' })
      html += fieldRow('Threshold %',
        numberField('cv_trigger_threshold', m.cv_trigger_threshold ?? 50, 0, 100))
      html += fieldRow('Debounce ms',
        numberField('cv_trigger_debounce_ms', m.cv_trigger_debounce_ms ?? 0, 0, 2000))
    }
    return section('Control Voltage', html)
  }

  function renderTouchwheel (ctrl) {
    const m = ctrl.editModel
    let html = fieldRow('Mode',
      selectField('touchwheel_mode', m.touchwheel_mode || 'pads', TOUCHWHEEL_MODE))
    const mode = m.touchwheel_mode || 'pads'
    if (mode === 'continuous') {
      if (!m.touchwheel) m.touchwheel = { enabled: true, output_type: 'cc' }
      html += renderContinuousMapping(ctrl, 'touchwheel', m.touchwheel, {
        tempoNudgePath: 'touchwheel_tempo_nudge_pct'
      })
      html += fieldRow('Style',
        selectField('touchwheel_style', m.touchwheel_style || 'odometer', [
          { v: 'odometer', l: 'Odometer' },
          { v: 'endless', l: 'Endless' },
          { v: 'bipolar', l: 'Bipolar' }
        ]))
      if (m.touchwheel_style === 'endless') {
        html += fieldRow('Initial value',
          numberField('touchwheel_initial_value', m.touchwheel_initial_value ?? 0, 0, 127))
      }
    }
    return section('Touchwheel', html)
  }

  function renderLfo (ctrl, n) {
    const cfgKey = n === 1 ? 'lfo1_config' : 'lfo2_config'
    const mapKey = n === 1 ? 'lfo1' : 'lfo2'
    const velKey = n === 1 ? 'lfo1_velocity_mode' : 'lfo2_velocity_mode'
    const m = ctrl.editModel
    if (!m[cfgKey]) m[cfgKey] = { enabled: false, waveform: 'sine', rate_mode: 'free' }
    if (!m[mapKey]) m[mapKey] = { enabled: false, output_type: 'cc' }
    let html = fieldRow('LFO enabled', checkboxField(`${cfgKey}.enabled`, !!m[cfgKey].enabled))
    if (!m[cfgKey].enabled) return section(`LFO${n}`, html)

    html += fieldRow('Waveform', selectField(`${cfgKey}.waveform`, m[cfgKey].waveform || 'sine', [
      { v: 'sine', l: 'Sine' }, { v: 'triangle', l: 'Triangle' },
      { v: 'square', l: 'Square' }, { v: 'saw_up', l: 'Saw Up' },
      { v: 'saw_down', l: 'Saw Down' }, { v: 'sample_hold', l: 'Sample & Hold' }
    ]))
    html += fieldRow('Rate mode', selectField(`${cfgKey}.rate_mode`, m[cfgKey].rate_mode || 'free', [
      { v: 'free', l: 'Free' }, { v: 'tempo', l: 'Tempo' },
      { v: 'touchwheel', l: 'Touchwheel' }, { v: 'expression', l: 'Expression' },
      { v: 'cv', l: 'CV' }, { v: 'als', l: 'ALS' }, { v: 'proximity', l: 'Proximity' }
    ]))
    if (m[cfgKey].rate_mode === 'free') {
      html += fieldRow('Rate Hz', numberField(`${cfgKey}.rate_hz`, m[cfgKey].rate_hz ?? 1, 0.05, 20))
    }
    if (m[cfgKey].rate_mode === 'tempo') {
      html += fieldRow('Division', textField(`${cfgKey}.division`, m[cfgKey].division || 'quarter', 16))
    }
    html += renderContinuousMapping(ctrl, mapKey, m[mapKey], { velocityModePath: velKey })
    return section(`LFO${n}`, html)
  }

  function renderRtg (ctrl) {
    const m = ctrl.editModel
    if (!m.rtg_config) m.rtg_config = { enabled: false, mode: 'continuous', generator: 'random' }
    let html = fieldRow('Enabled', checkboxField('rtg_config.enabled', !!m.rtg_config.enabled))
    if (!m.rtg_config.enabled) return section('RTG', html)
    html += fieldRow('Generator', selectField('rtg_config.generator', m.rtg_config.generator || 'random', [
      { v: 'random', l: 'Random' }, { v: 'shepard', l: 'Shepard' }
    ]))
    html += fieldRow('Mode', selectField('rtg_config.mode', m.rtg_config.mode || 'continuous', [
      { v: 'continuous', l: 'Continuous' }, { v: 'step', l: 'Step' }
    ]))
    if (m.rtg_config.mode === 'continuous') {
      html += fieldRow('Rate mode', selectField('rtg_config.rate_mode', m.rtg_config.rate_mode || 'free', [
        { v: 'free', l: 'Free' }, { v: 'sync', l: 'Sync' }
      ]))
      if (m.rtg_config.rate_mode === 'free') {
        html += fieldRow('Rate Hz', numberField('rtg_config.rate_hz', m.rtg_config.rate_hz ?? 2, 0.5, 25))
      } else {
        html += fieldRow('Sync mult',
          numberField('rtg_config.sync_mult', m.rtg_config.sync_mult ?? 1, 0.125, 8))
      }
    }
    if (m.rtg_config.generator === 'random') {
      html += fieldRow('Glide', checkboxField('rtg_config.glide', !!m.rtg_config.glide))
    }
    return section('RTG', html)
  }

  function renderSampleHold (ctrl) {
    const m = ctrl.editModel
    if (!m.sample_hold_config) {
      m.sample_hold_config = { enabled: false, mode: 'continuous' }
    }
    if (!m.sample_hold) m.sample_hold = { enabled: false, output_type: 'cc' }
    let html = fieldRow('Enabled', checkboxField('sample_hold_config.enabled',
      !!m.sample_hold_config.enabled))
    if (!m.sample_hold_config.enabled) return section('S+H', html)
    html += fieldRow('Mode', selectField('sample_hold_config.mode',
      m.sample_hold_config.mode || 'continuous', [
        { v: 'continuous', l: 'Continuous' }, { v: 'step', l: 'Step' }
      ]))
    if (m.sample_hold_config.mode === 'continuous') {
      html += fieldRow('Rate Hz',
        numberField('sample_hold_config.rate_hz', m.sample_hold_config.rate_hz ?? 2, 0.5, 25))
    }
    ensureCcNumbers(m.sample_hold)
    for (let i = 0; i < 4; i++) {
      html += fieldRow(`CC slot ${i + 1}`,
        numberField(`sample_hold.cc_numbers.${i}`, m.sample_hold.cc_numbers[i] ?? 0, 0, 127))
    }
    return section('S+H', html)
  }

  function renderPads (ctrl) {
    if (!ctrl.editModel.touchpads || ctrl.editModel.touchpads.length !== 12) {
      ctrl.editModel.touchpads = Array.from({ length: 12 }, (_, i) => ({
        enabled: i < 8,
        actions: [{ type: 'none' }]
      }))
    }
    let html = ''
    const mode = ctrl.editModel.touchwheel_mode || 'pads'
    const start = mode === 'pads' ? 0 : 8
    for (let i = start; i < 12; i++) {
      const tp = ctrl.editModel.touchpads[i]
      if (!tp.actions) tp.actions = [{ type: 'none' }]
      const act = tp.action || tp.actions?.[0] || { type: 'none' }
      html += `<div class="scene-pad-row"><h4>Pad ${i + 1}</h4>`
      html += fieldRow('Enabled', checkboxField(`touchpads.${i}.enabled`, tp.enabled !== false))
      html += renderAction(ctrl, `touchpads.${i}.action`, act)
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
      html += renderAction(ctrl, key, act)
      html += '</div>'
    })
    return section('Buttons', html)
  }

  function renderBump (ctrl) {
    return section('Bump', renderAction(ctrl, 'bump', ctrl.editModel.bump || { type: 'none' }))
  }

  function renderOnLoad (ctrl) {
    if (!ctrl.editModel.on_load) ctrl.editModel.on_load = []
    return section('On-Load', renderActionChain(ctrl, 'on_load', ctrl.editModel.on_load, 4))
  }

  function renderOnPlay (ctrl) {
    if (!ctrl.editModel.use_transport) return ''
    if (!ctrl.editModel.on_play) ctrl.editModel.on_play = []
    return section('On-Play', renderActionChain(ctrl, 'on_play', ctrl.editModel.on_play, 4))
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
      html += fieldRow('CC', numberField(`cc_triggers.${i}.cc_number`, slot.cc_number ?? 0, 0, 127))
      html += renderAction(ctrl, `cc_triggers.${i}.action`, slot.action || { type: 'none' })
      html += '</div>'
    }
    return section('CC Triggers', html)
  }

  function renderEditor (ctrl) {
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
    isReservedSceneName,
    sceneNameToSlug
  }
})()
