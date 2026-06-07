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

  const SCREEN_MODULES = [
    { v: 'beat', l: 'Beat Grid' },
    { v: 'khyron', l: 'Khyron' },
    { v: 'space', l: 'Space' },
    { v: 'summoner', l: 'Summoner' },
    { v: 'pixels', l: 'Pixels' }
  ]

  const REPEAT_DIVISIONS = [
    { v: '16_bars', l: '16 Bars' }, { v: '12_bars', l: '12 Bars' },
    { v: '8_bars', l: '8 Bars' }, { v: '4_bars', l: '4 Bars' },
    { v: '2_bars', l: '2 Bars' }, { v: '1_bar', l: '1 Bar' },
    { v: 'half', l: 'Half' }, { v: 'quarter', l: 'Quarter' },
    { v: 'eighth', l: 'Eighth' }, { v: 'sixteenth', l: 'Sixteenth' },
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

  function slotValue (field, index) {
    return Array.isArray(field) ? field[index] : (index === 0 ? field : undefined)
  }

  function paramField (ctrl, path, cc, optionOpts) {
    const device = ctrl.deviceDefinition
    if (DeviceControls.hasParameters(device)) {
      return selectField(path, cc, DeviceControls.parameterOptions(device, optionOpts))
    }
    return numberField(path, cc, 0, 127)
  }

  function valueField (ctrl, path, cc, val) {
    const device = ctrl.deviceDefinition
    if (DeviceControls.hasParameters(device)) {
      return selectField(path, val, DeviceControls.valueOptions(device, cc))
    }
    return numberField(path, val, 0, 127)
  }

  function slotAddButton (path, kind, label, max = 4) {
    const min = kind === 'cycle-step' ? 2 : 1
    return `<wa-button class="scene-slot-add" size="small" appearance="text" variant="brand"
      data-controller="slots"
      data-slots-path-value="${esc(path)}" data-slots-kind-value="${esc(kind)}"
      data-slots-min-value="${min}" data-slots-max-value="${max}" data-slots-default-value="0"
      data-action="click->slots#add" aria-label="${esc(label)}"><wa-icon name="plus"></wa-icon></wa-button>`
  }

  function addSlotButton (label) {
    return `<wa-button class="scene-slot-add" size="small" appearance="text" variant="brand"
      data-action="click->slots#add" aria-label="${esc(label)}"><wa-icon name="plus"></wa-icon></wa-button>`
  }

  function slotBlock (title, body, removeIndex, addHtml) {
    const remove = removeIndex >= 0
      ? `<wa-button class="scene-slot-remove" size="small" appearance="text" variant="neutral"
          data-action="click->slots#remove" data-slot-index="${removeIndex}"
          aria-label="Remove ${esc(title)}"><wa-icon name="xmark"></wa-icon></wa-button>`
      : ''
    return `<div class="scene-slot">
      <div class="scene-slot-head"><span class="scene-slot-title">${esc(title)}</span>
        <span class="scene-slot-actions">${remove}${addHtml || ''}</span></div>
      ${body}</div>`
  }

  function slotGroup (opts) {
    return `<div class="scene-slots" data-controller="slots"
      data-slots-path-value="${esc(opts.path)}" data-slots-kind-value="${esc(opts.kind)}"
      data-slots-min-value="${opts.min}" data-slots-max-value="${opts.max}"
      data-slots-default-value="${opts.def}">${opts.rows}</div>`
  }

  function renderCycleSlots (ctrl, path, a) {
    const device = ctrl.deviceDefinition
    const slotCount = DeviceControls.cycleSlotCount(a)
    const stepCount = DeviceControls.cycleStepCount(a)
    const ccBySlot = []
    for (let s = 0; s < slotCount; s++) {
      ccBySlot.push(DeviceControls.resolveParameterCc(device, DeviceControls.ccForSlot(a, s)))
    }
    const usedCcs = new Set(ccBySlot.map(Number))
    const hasParams = DeviceControls.hasParameters(device)
    const maxSlots = hasParams ? Math.min(4, DeviceControls.parameterCount(device)) : 4
    const multiSlot = slotCount > 1

    let html = `<div class="scene-cycle" style="--cycle-steps: ${stepCount}">`

    // Column headers: Parameter | Step 1 | Step 2 | …
    html += '<div class="scene-cycle-row scene-cycle-row-head">'
    html += '<div class="scene-cycle-param-col">Parameter</div>'
    for (let i = 0; i < stepCount; i++) {
      const add = (i === stepCount - 1 && stepCount < 8)
        ? slotAddButton(path, 'cycle-step', 'Add step', 8) : ''
      const remove = i > 1
        ? `<wa-button class="scene-slot-remove scene-cycle-step-remove" size="small" appearance="text" variant="neutral"
            data-controller="slots" data-slots-path-value="${esc(path)}" data-slots-kind-value="cycle-step"
            data-slots-min-value="2" data-slots-max-value="8" data-slots-default-value="0"
            data-action="click->slots#remove" data-slot-index="${i}"
            aria-label="Remove step ${i + 1}"><wa-icon name="xmark"></wa-icon></wa-button>`
        : ''
      html += `<div class="scene-cycle-step-col"><span class="scene-cycle-step-label">Step ${i + 1}</span>${remove}${add}</div>`
    }
    html += '</div>'

    // One row per parameter slot
    for (let s = 0; s < slotCount; s++) {
      const ccPath = multiSlot ? `${path}.cc.${s}` : `${path}.cc`
      const cc = ccBySlot[s]
      const steps = DeviceControls.cycleStepsForSlot(a, s)
      const removeSlot = s > 0
        ? `<wa-button class="scene-slot-remove" size="small" appearance="text" variant="neutral"
            data-controller="slots" data-slots-path-value="${esc(path)}" data-slots-kind-value="control"
            data-slots-min-value="1" data-slots-max-value="4" data-slots-default-value="0"
            data-action="click->slots#remove" data-slot-index="${s}"
            aria-label="Remove ${esc(DeviceControls.parameterLabel(device, cc))}"><wa-icon name="xmark"></wa-icon></wa-button>`
        : ''
      const addParam = (s === slotCount - 1 && slotCount < maxSlots)
        ? slotAddButton(path, 'control', 'Add parameter', maxSlots) : ''

      html += '<div class="scene-cycle-row">'
      html += `<div class="scene-cycle-param-col">
        <div class="scene-cycle-param-head">
          <span class="scene-cycle-param-name">${esc(DeviceControls.parameterLabel(device, cc))}</span>
          <span class="scene-cycle-param-actions">${removeSlot}${addParam}</span>
        </div>
        ${paramField(ctrl, ccPath, cc, { exclude: usedCcs, keep: cc })}
      </div>`
      for (let i = 0; i < stepCount; i++) {
        const valPath = multiSlot ? `${path}.values.${s}.${i}` : `${path}.values.${i}`
        const val = DeviceControls.resolveParameterValue(device, cc, steps[i])
        html += `<div class="scene-cycle-step-col">${valueField(ctrl, valPath, cc, val)}</div>`
      }
      html += '</div>'
    }

    html += '</div>'
    return html
  }

  function renderControlFields (ctrl, path, a) {
    const device = ctrl.deviceDefinition
    const variant = a.variant || 'set'
    if (variant === 'cycle') return renderCycleSlots(ctrl, path, a)

    const count = DeviceControls.controlSlotCount(a)
    const multi = count > 1
    const ccBySlot = []
    for (let i = 0; i < count; i++) {
      ccBySlot.push(DeviceControls.resolveParameterCc(device, DeviceControls.ccForSlot(a, i)))
    }
    // A control action may not target the same parameter twice, so each slot's
    // dropdown hides parameters already taken by the other slots.
    const usedCcs = new Set(ccBySlot.map(Number))
    const hasParams = DeviceControls.hasParameters(device)
    const maxSlots = hasParams ? Math.min(4, DeviceControls.parameterCount(device)) : 4
    let rows = ''
    for (let i = 0; i < count; i++) {
      const ccPath = multi ? `${path}.cc.${i}` : `${path}.cc`
      const cc = ccBySlot[i]
      let body = fieldRow('Parameter',
        paramField(ctrl, ccPath, cc, { exclude: usedCcs, keep: cc }))
      if (variant === 'set') {
        const valPath = multi ? `${path}.value.${i}` : `${path}.value`
        const val = DeviceControls.resolveParameterValue(device, cc, slotValue(a.value, i))
        body += fieldRow('Value', valueField(ctrl, valPath, cc, val))
      } else if (variant === 'hold') {
        const pressPath = multi ? `${path}.value.${i}` : `${path}.value`
        const relPath = multi ? `${path}.value2.${i}` : `${path}.value2`
        const press = DeviceControls.resolveParameterValue(device, cc, slotValue(a.value, i))
        const rel = DeviceControls.resolveParameterValue(device, cc, slotValue(a.value2, i))
        body += fieldRow('Press value', valueField(ctrl, pressPath, cc, press))
        body += fieldRow('Release value', valueField(ctrl, relPath, cc, rel))
      }
      const add = (count < maxSlots && i === count - 1) ? addSlotButton('Add parameter') : ''
      rows += slotBlock(`Slot ${i + 1}`, body, i > 0 ? i : -1, add)
    }
    return slotGroup({ path, kind: 'control', min: 1, max: 4, def: 0, rows })
  }

  function renderRepeatBlock (ctrl, path, a) {
    if (!ActionCatalog.supportsRepeat(a)) return ''
    const on = !!a.repeat
    const repeatCb = `<input type="checkbox" class="scene-checkbox" ${on ? 'checked' : ''}
      data-scene-path="${esc(path)}.repeat" data-reveal-target="trigger"
      data-action="change->scene#patchCheckboxQuiet change->reveal#toggle">`
    let content = fieldRow('Division',
      selectField(`${path}.repeat_division`, a.repeat_division || 'quarter', REPEAT_DIVISIONS))
    content += fieldRow('Probability',
      selectField(`${path}.probability`, a.probability ?? 100, PROBABILITY))
    content += fieldRow('Pattern length',
      selectField(`${path}.pattern_length`, a.pattern_length ?? 0, PATTERN_LENGTHS))
    const plen = Number(a.pattern_length ?? 0)
    if (plen >= 2) content += renderPatternMask(path, plen, a.pattern_mask ?? 255)
    content += fieldRow('Transport trigger',
      checkboxField(`${path}.transport_trigger`, !!a.transport_trigger))
    return `<div class="scene-reveal" data-controller="reveal">
      ${fieldRow('Repeat', repeatCb)}
      <div class="scene-reveal-content" data-reveal-target="content" ${on ? '' : 'hidden'}>${content}</div>
    </div>`
  }

  function renderPatternMask (path, len, mask) {
    let steps = ''
    for (let i = 0; i < len; i++) {
      const active = (mask >> i) & 1
      steps += `<button type="button" class="scene-step${active ? ' is-on' : ''}"
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
    let html = fieldRow('Type', selectField(`${path}.type`, a.type || 'none', typeOpts))
    if (a.type && a.type !== 'none') {
      const variantOpts = ActionCatalog.variantsForType(a.type, trigger, ctx)
      if (variantOpts.length) {
        const curVariant = a.variant || variantOpts[0].v
        if (curVariant && !variantOpts.some(o => o.v === curVariant)) {
          variantOpts.unshift({ v: curVariant, l: ActionCatalog.variantLabel(curVariant) })
        }
        html += fieldRow('Variant', selectField(`${path}.variant`, curVariant, variantOpts))
      }
      if (a.type === 'note') {
        const noteVal = a.note ?? ActionCatalog.NOTE_RANDOM
        html += fieldRow('Note',
          selectField(`${path}.note`, noteVal, ActionCatalog.noteOptions(noteVal)))
        html += fieldRow('Velocity', numberField(`${path}.velocity`, a.velocity ?? 100, 1, 127))
      }
      if (a.type === 'control' || a.type === 'control_change') {
        html += renderControlFields(ctrl, path, a)
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
      if (ActionCatalog.supportsTiming(a)) {
        const beats = ctrl.editModel?.time_signature?.numerator ?? 4
        const timingVal = a.timing || 'immediate'
        const timingOpts = ActionCatalog.timingOptions(beats)
        if (!timingOpts.some(o => o.v === timingVal)) {
          timingOpts.unshift({ v: timingVal, l: timingVal })
        }
        html += fieldRow('Timing', selectField(`${path}.timing`, timingVal, timingOpts))
      }
      html += renderRepeatBlock(ctrl, path, a)
      if (ctrl.deviceContext?.flagEnabled) {
        html += fieldRow('Raise the Flag', checkboxField(`${path}.raise_flag`, !!a.raise_flag))
      }
    }
    return html
  }

  function renderActionChain (ctrl, path, chain, maxItems, trigger) {
    const arr = Array.isArray(chain) ? chain : []
    let html = ''
    for (let i = 0; i < maxItems; i++) {
      const itemPath = `${path}.${i}`
      html += `<div class="scene-action-slot"><h4>Action ${i + 1}</h4>`
      html += renderAction(ctrl, itemPath, arr[i] || { type: 'none' }, { trigger })
      html += '</div>'
    }
    return html
  }

  function section (title, body, open = false) {
    const isOpen = _openSections.has(title) || open
    return `<details class="scene-editor-section" data-section="${esc(title)}" ${isOpen ? 'open' : ''}>
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
      m.device_id || '', ctrl.pedalCatalog, global)
    let html = fieldRow('Pedal',
      `<div class="scene-pedal-display">
        <span class="scene-pedal-name">${esc(pedalName)}</span>
        <wa-button size="small" variant="neutral" appearance="outlined"
                   data-action="click->scene#openPedalPicker">Change</wa-button>
      </div>`)
    html += fieldRow('MIDI channel',
      selectField('midi_channel', m.midi_channel ?? 0, [
        { v: 0, l: `Inherited (${inheritedCh})` },
        ...Array.from({ length: 16 }, (_, i) => ({ v: i + 1, l: String(i + 1) }))
      ]))
    html += fieldRow('TRS type',
      selectField('trs_type', m.trs_type ?? 0, [
        { v: 0, l: `Inherited (${inheritedTrs})` },
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
    html += fieldRow('Screen',
      selectField('ui_module', normalizeUiModule(m.ui_module), screenModuleOptions(m.ui_module)))

    if (ctx.sceneMode !== 1) {
      html += fieldRow('Pedal Preset',
        numberField('program_number', m.program_number ?? 0, 0, 127))
      html += fieldRow('Load preset when loading scene',
        checkboxField('send_pc_on_load', m.send_pc_on_load !== false))
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
    html += fieldRow('Send clock', checkboxField('send_clock', m.send_clock !== false))
    html += fieldRow('Use transport', checkboxField('use_transport', !!m.use_transport))

    const inheritedNoteCh = typeof ctrl.getEffectiveMidiChannel === 'function'
      ? ctrl.getEffectiveMidiChannel()
      : (ctx.globalPedal?.midi_channel || 1)
    html += fieldRow('Send notes on channel',
      selectField('note_channel', m.note_channel ?? 0, [
        { v: 0, l: `Inherited (${inheritedNoteCh})` },
        ...Array.from({ length: 16 }, (_, i) => ({ v: i + 1, l: String(i + 1) }))
      ]))

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
      html += renderAction(ctrl, 'cv_trigger_action', m.cv_trigger_action || { type: 'none' },
        { trigger: ActionCatalog.TRIGGERS.BUTTON })
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
      const trigger = i < 8
        ? ActionCatalog.TRIGGERS.TOUCHPAD_0_7
        : ActionCatalog.TRIGGERS.TOUCHPAD_8_11
      html += `<div class="scene-pad-row"><h4>${esc(ActionCatalog.padDisplayName(i))}</h4>`
      html += renderAction(ctrl, `touchpads.${i}.action`, act, { trigger })
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
      html += renderAction(ctrl, key, act, { trigger: ActionCatalog.TRIGGERS.BUTTON })
      html += '</div>'
    })
    return section('Buttons', html)
  }

  function renderBump (ctrl) {
    return section('Bump', renderAction(ctrl, 'bump', ctrl.editModel.bump || { type: 'none' },
      { trigger: ActionCatalog.TRIGGERS.BUMP }))
  }

  function renderOnLoad (ctrl) {
    if (!ctrl.editModel.on_load) ctrl.editModel.on_load = []
    return section('On-Load', renderActionChain(ctrl, 'on_load', ctrl.editModel.on_load, 4))
  }

  function renderOnPlay (ctrl) {
    if (!ctrl.editModel.use_transport) return ''
    if (!ctrl.editModel.on_play) ctrl.editModel.on_play = []
    return section('On-Play', renderActionChain(ctrl, 'on_play', ctrl.editModel.on_play, 4,
      ActionCatalog.TRIGGERS.ON_PLAY))
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
      html += renderAction(ctrl, `cc_triggers.${i}.action`, slot.action || { type: 'none' },
        { trigger: ActionCatalog.TRIGGERS.CC })
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
    const hasAction = (a) => !!(a && a.type && a.type !== 'none')
    const padAction = (tp) => hasAction(tp?.action) || hasAction(tp?.actions?.[0])

    if ((m.touchpads || []).some(padAction)) open.add('Pads')
    if (m.touchwheel_mode && m.touchwheel_mode !== 'pads') open.add('Touchwheel')
    if (m.expression?.enabled) open.add('Expression')
    if (m.cv_input_mode && m.cv_input_mode !== 'none') open.add('Control Voltage')
    if (m.proximity?.enabled) open.add('Proximity')
    if (m.als?.enabled) open.add('Ambient Light')
    if (hasAction(m.button_left) || hasAction(m.button_right) || hasAction(m.button_both)) {
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
    if ((m.cc_triggers || []).some(t => hasAction(t?.action))) open.add('CC Triggers')
    if (m.note_track?.enabled) open.add('Note Track')
    return open
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
    isReservedSceneName,
    sceneNameToSlug
  }
})()
