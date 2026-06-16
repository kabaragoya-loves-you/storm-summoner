/* LFO waveform SVG preview (editor + read-only inspect) */

window.LfoWaveformPreview = (function () {
  const CYCLES = 4
  const VIEW_W = 280
  const VIEW_H = 100
  const PLOT_LEFT = 8
  const PLOT_RIGHT = VIEW_W - 8
  const PLOT_TOP = 10
  const PLOT_BOT = 70
  const LABEL_Y = 84
  const SAMPLES = CYCLES * 256

  const BIN_SWITCH_PCT = 65
  const GLIDER_OUTPUT_COEFF = 0.008
  const GLIDER_TARGET_COEFF = 0.018
  const GLIDER_OUTPUT_COEFF_MAX = 0.045
  const GLIDER_TARGET_COEFF_MAX = 0.07
  const GLIDER_TARGET_DELTA = 16
  const GLIDER_TICK_SEC = 0.01  // match firmware 100 Hz
  const GLIDER_RETARGET_MIN_SEC = 3
  const GLIDER_RETARGET_RANGE_SEC = 5
  const STRAY_DELTA = 72

  const DIVISION_VALUES = [
    '16_bars', '12_bars', '8_bars', '4_bars', '2_bars', '1_bar',
    'half', 'quarter', 'eighth', 'sixteenth', '32nd'
  ]
  const DIVISION_LABELS = [
    '16 Bars', '12 Bars', '8 Bars', '4 Bars', '2 Bars', '1 Bar',
    '1/2', '1/4', '1/8', '1/16', '1/32'
  ]

  function esc (s) {
    return String(s ?? '')
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/"/g, '&quot;')
  }

  function mulberry32 (seed) {
    let s = seed >>> 0
    return function () {
      s = (s + 0x6D2B79F5) >>> 0
      let t = s
      t = Math.imul(t ^ (t >>> 15), t | 1)
      t ^= t + Math.imul(t ^ (t >>> 7), t | 61)
      return ((t ^ (t >>> 14)) >>> 0) / 4294967296
    }
  }

  function clampMidi (v) {
    return Math.max(0, Math.min(127, Math.round(v)))
  }

  function yForMidi (v) {
    const norm = clampMidi(v) / 127
    return PLOT_BOT - norm * (PLOT_BOT - PLOT_TOP)
  }

  function xForCycle (cycle) {
    return PLOT_LEFT + (cycle / CYCLES) * (PLOT_RIGHT - PLOT_LEFT)
  }

  function clampInt (v, lo, hi) {
    const n = Number(v)
    if (!Number.isFinite(n)) return lo
    return Math.max(lo, Math.min(hi, Math.round(n)))
  }

  function applyFloorCeiling (raw255, cfg) {
    const floor = clampInt(cfg?.floor ?? 0, 0, 127)
    const ceiling = clampInt(cfg?.ceiling ?? 127, 0, 127)
    if (floor > ceiling) return Math.floor((floor + ceiling) / 2)
    if (floor === 0 && ceiling === 127) return raw255 >> 1
    const range = ceiling - floor
    return floor + Math.floor((raw255 * range) / 255)
  }

  function getEffectiveSteps (cfg, ctx = {}) {
    const cycleMs = cyclePeriodSec(cfg, ctx) * 1000
    const mode = cfg?.resolution_mode || 'auto'
    switch (mode) {
      case 'coarse': return 16
      case 'medium': return 32
      case 'fine': return 64
      case 'manual': return Math.max(1, clampInt(cfg?.manual_steps ?? 32, 1, 255))
      case 'auto':
      default:
        return cycleMs >= 800 ? 0 : 32
    }
  }

  function quantizeByResolution (midiValues, cfg, ctx = {}) {
    const steps = getEffectiveSteps(cfg, ctx)
    if (!steps) return midiValues

    const samplesPerCycle = 256
    const out = midiValues.slice()
    for (let c = 0; c < CYCLES; c++) {
      const base = c * samplesPerCycle
      for (let s = 0; s < steps; s++) {
        const segStart = base + Math.floor((s / steps) * samplesPerCycle)
        const segEnd = base + Math.floor(((s + 1) / steps) * samplesPerCycle) - 1
        const held = midiValues[Math.min(segEnd, midiValues.length - 1)]
        const end = Math.min(base + samplesPerCycle - 1, segEnd)
        for (let i = segStart; i <= end; i++) out[i] = held
      }
    }
    return out
  }

  function lookupSine (phase) {
    return 128 + Math.round(Math.sin((phase / 256) * Math.PI * 2) * 127)
  }

  function deterministicValue (waveform, phase, cfg) {
    switch (waveform) {
      case 'sine':
        return lookupSine(phase)
      case 'triangle':
        return phase < 128 ? phase * 2 : 255 - ((phase - 128) * 2)
      case 'square': {
        const duty = cfg?.duty_cycle ?? 64
        const threshold = Math.round((duty * 255) / 127)
        return phase < threshold ? 255 : 0
      }
      case 'saw_up':
        return phase
      case 'saw_down':
        return 255 - phase
      default:
        return phase
    }
  }

  function gliderSpeedFactor (rateHz) {
    const hz = Math.max(0.05, Math.min(20, rateHz))
    const logMin = Math.log(0.05)
    const logMax = Math.log(20)
    const t = (Math.log(hz) - logMin) / (logMax - logMin)
    return 1 + t * 9
  }

  function effectiveRateHz (cfg, ctx = {}) {
    if (cfg?.rate_mode === 'tempo') return 1 / cyclePeriodSec(cfg, ctx)
    const hz = Number(cfg?.rate_hz)
    return Number.isFinite(hz) && hz > 0 ? hz : 1
  }

  function strayClamp (v) {
    return Math.max(0, Math.min(255, v))
  }

  function strayRandomDelta (rand) {
    return Math.floor(rand() * (STRAY_DELTA * 2 + 1)) - STRAY_DELTA
  }

  function catmullRom (p0, p1, p2, p3, t) {
    const t2 = t * t
    const t3 = t2 * t
    return 0.5 * ((2 * p1) +
      (-p0 + p2) * t +
      (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2 +
      (-p0 + 3 * p1 - 3 * p2 + p3) * t3)
  }

  function initStraySpline (rand) {
    let p1 = 128
    const p0 = strayClamp(p1 + strayRandomDelta(rand))
    const p2 = strayClamp(p1 + strayRandomDelta(rand))
    const p3 = strayClamp(p2 + strayRandomDelta(rand))
    return { p0, p1, p2, p3 }
  }

  function advanceStraySpline (spline, rand) {
    spline.p0 = spline.p1
    spline.p1 = spline.p2
    spline.p2 = spline.p3
    spline.p3 = strayClamp(spline.p2 + strayRandomDelta(rand))
  }

  function simulateStochastic (waveform, cfg, ctx = {}) {
    const seed = hashWaveformSeed(waveform, cfg)
    const rand = mulberry32(seed)
    const values = new Array(SAMPLES)

    if (waveform === 'sample_hold') {
      let held = Math.floor(rand() * 256)
      for (let i = 0; i < SAMPLES; i++) {
        if (i > 0 && (i % 256) === 0) held = Math.floor(rand() * 256)
        values[i] = held
      }
      return values
    }

    if (waveform === 'bin') {
      let level = 0
      for (let i = 0; i < SAMPLES; i++) {
        if (i % 256 === 0 && i > 0) {
          if (Math.floor(rand() * 100) < BIN_SWITCH_PCT) level = level ? 0 : 255
        }
        if (i === 0) level = rand() < 0.5 ? 0 : 255
        values[i] = level
      }
      return values
    }

    if (waveform === 'glider') {
      const cycleSec = cyclePeriodSec(cfg, ctx)
      const sampleDt = cycleSec / 256
      const speed = gliderSpeedFactor(effectiveRateHz(cfg, ctx))
      const outCoeff = Math.min(GLIDER_OUTPUT_COEFF_MAX, GLIDER_OUTPUT_COEFF * speed)
      const tgtCoeff = Math.min(GLIDER_TARGET_COEFF_MAX, GLIDER_TARGET_COEFF * speed)
      let value = 128
      let target = 128
      let wish = 128
      let nextRetargetSec = (GLIDER_RETARGET_MIN_SEC + rand() * GLIDER_RETARGET_RANGE_SEC) / speed
      let tSec = 0
      for (let i = 0; i < SAMPLES; i++) {
        const sampleEnd = (i + 1) * sampleDt
        while (tSec < sampleEnd) {
          if (tSec >= nextRetargetSec) {
            const span = Math.max(6, Math.round(GLIDER_TARGET_DELTA * (0.5 + speed * 0.1)))
            const delta = Math.floor(rand() * (span * 2 + 1)) - span
            wish = strayClamp(wish + delta)
            nextRetargetSec = tSec + (GLIDER_RETARGET_MIN_SEC + rand() * GLIDER_RETARGET_RANGE_SEC) / speed
          }
          target += (wish - target) * tgtCoeff
          value += (target - value) * outCoeff
          tSec += GLIDER_TICK_SEC
        }
        values[i] = Math.round(value)
      }
      return values
    }

    if (waveform === 'stray') {
      const spline = initStraySpline(rand)
      let triggered = false
      for (let i = 0; i < SAMPLES; i++) {
        const phase = i % 256
        if (phase < 8 && !triggered) {
          advanceStraySpline(spline, rand)
          triggered = true
        } else if (phase >= 128) {
          triggered = false
        }
        const t = phase / 255
        values[i] = Math.round(strayClamp(catmullRom(spline.p0, spline.p1, spline.p2, spline.p3, t)))
      }
      return values
    }

    return null
  }

  function hashWaveformSeed (waveform, cfg) {
    let h = 2166136261
    const s = `${waveform}:${cfg?.duty_cycle ?? 64}:${cfg?.phase_offset ?? 0}`
    for (let i = 0; i < s.length; i++) {
      h ^= s.charCodeAt(i)
      h = Math.imul(h, 16777619)
    }
    return h >>> 0
  }

  function sampleRawWaveform (waveform, cfg, ctx = {}) {
    const wf = (typeof waveform === 'string' && waveform) ? waveform : 'sine'
    const offset = clampInt(cfg?.phase_offset ?? 0, 0, 255)
    const stochastic = simulateStochastic(wf, cfg, ctx)
    if (stochastic) {
      if (!offset) return stochastic
      const shifted = new Array(SAMPLES)
      for (let i = 0; i < SAMPLES; i++) {
        const cycleBase = Math.floor(i / 256) * 256
        const phaseIdx = ((i % 256) + offset) % 256
        shifted[i] = stochastic[cycleBase + phaseIdx]
      }
      return shifted
    }

    const values = new Array(SAMPLES)
    for (let i = 0; i < SAMPLES; i++) {
      const phase = (i % 256) + offset
      values[i] = deterministicValue(wf, phase & 255, cfg)
    }
    return values
  }

  function buildPreviewValues (waveform, cfg, ctx = {}) {
    const wf = (typeof waveform === 'string' && waveform) ? waveform : 'sine'
    const raw = sampleRawWaveform(wf, cfg, ctx)
    const midi = raw.map(v => applyFloorCeiling(v, cfg))
    if (wf === 'glider') return midi
    return quantizeByResolution(midi, cfg, ctx)
  }

  function sampleWaveform (waveform, cfg, ctx = {}) {
    return buildPreviewValues(waveform, cfg, ctx)
  }

  function divisionCyclesPerBeat (division, feltBeats) {
    const beats = feltBeats || 4
    switch (division || 'quarter') {
      case '32nd': return 8
      case 'sixteenth': return 4
      case 'eighth': return 2
      case 'quarter': return 1
      case 'half': return 0.5
      case '1_bar': return 1 / beats
      case '2_bars': return 1 / (beats * 2)
      case '4_bars': return 1 / (beats * 4)
      case '8_bars': return 1 / (beats * 8)
      case '12_bars': return 1 / (beats * 12)
      case '16_bars': return 1 / (beats * 16)
      default: return 1
    }
  }

  function cyclePeriodSec (cfg, ctx = {}) {
    if (cfg?.rate_mode === 'tempo') {
      const cpb = divisionCyclesPerBeat(cfg.division, ctx.feltBeats ?? 4)
      const bpm = ctx.bpm ?? 120
      const hz = (bpm / 60) * cpb
      return hz > 0 ? 1 / hz : 1
    }
    const hz = Number(cfg?.rate_hz)
    return 1 / (Number.isFinite(hz) && hz > 0 ? hz : 1)
  }

  function formatTimeLabel (seconds) {
    if (seconds === 0) return '0'
    if (seconds < 1) return `${Math.round(seconds * 1000)}ms`
    if (seconds < 10) return `${seconds.toFixed(1)}s`
    return `${Math.round(seconds)}s`
  }

  function divisionLabel (division) {
    const idx = DIVISION_VALUES.indexOf(division || 'quarter')
    return idx >= 0 ? DIVISION_LABELS[idx] : (division || '1/4')
  }

  function rateCaption (cfg, ctx = {}) {
    if (cfg?.rate_mode === 'tempo') {
      const bpm = ctx.bpm ?? 120
      return `${divisionLabel(cfg.division)} sync @ ${bpm} BPM`
    }
    const hz = Number(cfg?.rate_hz)
    const label = Number.isFinite(hz) ? `${hz} Hz` : '1 Hz'
    return label
  }

  function valuesToPath (values) {
    if (!values.length) return ''
    let d = `M ${xForCycle(0).toFixed(1)} ${yForMidi(values[0]).toFixed(1)}`
    for (let i = 1; i < values.length; i++) {
      const cycle = (i / (values.length - 1)) * CYCLES
      d += ` L ${xForCycle(cycle).toFixed(1)} ${yForMidi(values[i]).toFixed(1)}`
    }
    return d
  }

  function renderLevelGuides (cfg) {
    const floor = clampInt(cfg?.floor ?? 0, 0, 127)
    const ceiling = clampInt(cfg?.ceiling ?? 127, 0, 127)
    const yFloor = yForMidi(floor)
    const yCeil = yForMidi(ceiling)
    const x1 = PLOT_LEFT
    const x2 = PLOT_RIGHT
    return `<line class="scene-lfo-waveform-ref" x1="${x1}" y1="${yFloor.toFixed(1)}"
        x2="${x2}" y2="${yFloor.toFixed(1)}"></line>
      <line class="scene-lfo-waveform-ref" x1="${x1}" y1="${yCeil.toFixed(1)}"
        x2="${x2}" y2="${yCeil.toFixed(1)}"></line>
      <text class="scene-lfo-waveform-level" x="${x1 + 2}" y="${yFloor - 4}">${floor}</text>
      <text class="scene-lfo-waveform-level" x="${x2 - 2}" y="${yCeil - 4}"
        text-anchor="end">${ceiling}</text>`
  }

  function renderGrid (cfg, ctx) {
    const period = cyclePeriodSec(cfg, ctx)
    const lines = []
    const labels = []
    for (let c = 0; c <= CYCLES; c++) {
      const x = xForCycle(c)
      lines.push(`<line class="scene-lfo-waveform-grid" x1="${x.toFixed(1)}" y1="${PLOT_TOP}"
        x2="${x.toFixed(1)}" y2="${PLOT_BOT}"></line>`)
      labels.push(`<text class="scene-lfo-waveform-axis-label" x="${x.toFixed(1)}" y="${LABEL_Y}"
        text-anchor="middle">${esc(formatTimeLabel(c * period))}</text>`)
    }
    return lines.join('') + labels.join('')
  }

  function resolutionCaption (cfg, ctx = {}) {
    const steps = getEffectiveSteps(cfg, ctx)
    if (!steps) return 'smooth'
    return `${steps} steps/cycle`
  }

  function renderSvg (cfg, ctx = {}) {
    const config = cfg || {}
    const wf = (typeof config.waveform === 'string' && config.waveform) ? config.waveform : 'sine'
    const values = buildPreviewValues(wf, config, ctx)
    const path = valuesToPath(values)
    const grid = renderGrid(config, ctx)
    const levels = renderLevelGuides(config)
    const caption = `${labelFor(wf)} · ${rateCaption(config, ctx)} · ${resolutionCaption(config, ctx)}`

    return `<svg class="scene-lfo-waveform-svg" width="${VIEW_W}" height="${VIEW_H}"
      viewBox="0 0 ${VIEW_W} ${VIEW_H}" preserveAspectRatio="xMinYMid meet"
      aria-hidden="true">
      ${grid}
      ${levels}
      <path class="scene-lfo-waveform-stroke" d="${path}"></path>
      <text class="scene-lfo-waveform-caption" x="${VIEW_W / 2}" y="${VIEW_H - 2}"
        text-anchor="middle">${esc(caption)}</text>
    </svg>`
  }

  function labelFor (waveform) {
    switch (waveform) {
      case 'sine': return 'Sine'
      case 'triangle': return 'Triangle'
      case 'square': return 'Square'
      case 'saw_up': return 'Saw Up'
      case 'saw_down': return 'Saw Down'
      case 'sample_hold': return 'Sample & Hold'
      case 'bin': return 'Bin'
      case 'glider': return 'Glider'
      case 'stray': return 'Stray'
      case 'custom': return 'Custom'
      default: return waveform
    }
  }

  function sceneContext (model) {
    return {
      bpm: model?.bpm ?? 120,
      feltBeats: model?.time_signature?.numerator ?? 4
    }
  }

  function collectLfos (model) {
    const items = []
    if (!model) return items
    if (model.lfo1_config?.enabled) {
      items.push({ slot: 1, cfgKey: 'lfo1_config', cfg: model.lfo1_config })
    }
    if (model.lfo2_config?.enabled) {
      items.push({ slot: 2, cfgKey: 'lfo2_config', cfg: model.lfo2_config })
    }
    return items
  }

  function inspectLabelForSlot (slot) {
    return slot === 1 ? 'LFO 1' : 'LFO 2'
  }

  function renderReadOnlySvg (cfg, ctx) {
    return `<div class="scene-lfo-waveform-preview scene-lfo-waveform-readonly">${renderSvg(cfg, ctx)}</div>`
  }

  function renderInspectableDocument (text, model, opts = {}) {
    const lfoItems = collectLfos(model)
    const boomItems = window.BoomerangEnvelope?.collectBoomerangs(model, opts) || []
    if (!lfoItems.length && !boomItems.length) return null

    const lfoByLabel = new Map(lfoItems.map(i => [inspectLabelForSlot(i.slot), i]))
    const boomByLabel = new Map()
    for (const item of boomItems) {
      const label = window.BoomerangEnvelope.inspectLabelForPath(item.path, model)
      boomByLabel.set(label, item)
    }

    const ctx = sceneContext(model)
    const paragraphs = (text || '(empty scene)').split(/\n\n+/)
    const parts = []

    for (const para of paragraphs) {
      const trimmed = para.trim()
      if (!trimmed) continue
      const firstLine = trimmed.split('\n')[0]

      const lfoHead = firstLine.match(/^(LFO [12]): /)
      if (lfoHead) {
        const item = lfoByLabel.get(lfoHead[1])
        if (item) {
          parts.push(`<div class="scene-inspect-block">
            <div class="scene-inspect-block-text">${esc(trimmed)}</div>
            <div class="scene-inspect-block-graph">${renderReadOnlySvg(item.cfg, ctx)}</div>
          </div>`)
          continue
        }
      }

      const boomHead = firstLine.match(/^(.+): Boomerang(?:$|: )/)
      const boomItem = boomHead ? boomByLabel.get(boomHead[1]) : null
      if (boomItem) {
        parts.push(`<div class="scene-inspect-block">
          <div class="scene-inspect-block-text">${esc(trimmed)}</div>
          <div class="scene-inspect-block-graph">${window.BoomerangEnvelope.renderReadOnlySvg(boomItem.action)}</div>
        </div>`)
        continue
      }

      parts.push(`<div class="scene-inspect-paragraph">${esc(trimmed)}</div>`)
    }

    return parts.join('')
  }

  const PRINT_STYLES = `
.scene-inspect-block-graph {
  width: 2.8in;
  max-width: 2.8in;
  margin: 0.08in 0 0;
}
.scene-inspect-block-graph .scene-lfo-waveform-preview {
  width: 2.8in;
  max-width: 2.8in;
  margin: 0;
}
.scene-inspect-block-graph .scene-lfo-waveform-svg {
  display: block;
  width: 2.8in;
  height: 1in;
  max-width: 2.8in;
}
.scene-lfo-waveform-grid { stroke: #999; stroke-width: 1; stroke-dasharray: 2 3; }
.scene-lfo-waveform-ref { stroke: #666; stroke-width: 1; stroke-dasharray: 3 3; opacity: 0.55; }
.scene-lfo-waveform-stroke { stroke: #000; stroke-width: 1.5; fill: none; vector-effect: non-scaling-stroke; }
.scene-lfo-waveform-axis-label,
.scene-lfo-waveform-caption { fill: #444; font-family: system-ui, sans-serif; font-size: 8px; }
.scene-lfo-waveform-level {
  fill: #444;
  font-family: system-ui, sans-serif;
  font-size: 9px;
  font-style: italic;
}
`

  return {
    renderSvg,
    renderReadOnlySvg,
    renderInspectableDocument,
    sampleWaveform,
    labelFor,
    collectLfos,
    inspectLabelForSlot,
    printStyles: PRINT_STYLES
  }
})()
