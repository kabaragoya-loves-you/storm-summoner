/* Boomerang ADSR envelope SVG (editor + read-only inspect) */

window.BoomerangEnvelope = (function () {
  const DIVISION_LABELS = {
    '1_beat': '1 Beat',
    beat: '1 Beat',
    '2_beats': '2 Beats',
    '3_beats': '3 Beats',
    beat_2: 'Beat 2',
    beat_3: 'Beat 3',
    beat_4: 'Beat 4',
    '1_bar': '1 Bar',
    bar: '1 Bar',
    '2_bars': '2 Bars',
    '3_bars': '3 Bars',
    '4_bars': '4 Bars'
  }

  const VIEW_W = 280
  const VIEW_H = 100
  const Y_TOP = 18
  const Y_BOT = 78
  const LABEL_Y = 96
  const INST_PHASE_W = 6

  function esc (s) {
    return String(s ?? '')
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/"/g, '&quot;')
  }

  function valueMax (outputType) {
    return outputType === 'pitch_bend' ? 16383 : 127
  }

  function normValue (a, field, outputType, fallback) {
    const max = valueMax(outputType)
    const raw = a[field]
    const v = raw == null ? fallback : Number(raw)
    return Math.max(0, Math.min(1, v / max))
  }

  function curveApply (type, slope, input) {
    const t = Math.max(0, Math.min(127, Math.round(input)))
    const normalized = t / 127
    switch (Number(type)) {
      case 0:
        return t
      case 1: {
        const exp = slope === 0 ? 1.5 : slope === 2 ? 3 : 2
        return Math.round(Math.pow(normalized, exp) * 127)
      }
      case 2: {
        if (t === 0) return 0
        const base = slope === 0 ? 1.5 : slope === 2 ? 3 : 2
        return Math.round(Math.log(1 + normalized * (base - 1)) / Math.log(base) * 127)
      }
      case 3: {
        const n = normalized * 2 - 1
        const steepness = slope === 0 ? 3 : slope === 2 ? 8 : 5
        const sigmoid = 1 / (1 + Math.exp(-steepness * n))
        return Math.round(sigmoid * 127)
      }
      case 4:
        return 127 - curveApply(3, slope, 127 - t)
      case 5:
        return Math.round(normalized * normalized * 127)
      case 6:
        return Math.round(Math.sqrt(normalized) * 127)
      case 7:
        return Math.round(Math.sin(normalized * Math.PI / 2) * 127)
      default:
        return t
    }
  }

  function phaseLabel (a, phase) {
    const mode = a[`${phase}_mode`] || 'instant'
    if (mode === 'instant') return 'inst'
    if (mode === 'time_ms') {
      const ms = Number(a[`${phase}_time_ms`] ?? 0)
      return `${ms}ms`
    }
    const div = a[`${phase}_division`] || '1_beat'
    return DIVISION_LABELS[div] || div.replace(/_/g, ' ')
  }

  function phaseWidths (a) {
    const phases = ['attack', 'sustain', 'release']
    const total = 264
    const instW = INST_PHASE_W
    let instCount = 0
    for (const p of phases) {
      if ((a[`${p}_mode`] || 'instant') === 'instant') instCount++
    }
    const flexCount = phases.length - instCount
    const flexW = flexCount > 0
      ? Math.max(24, (total - instCount * instW) / flexCount)
      : total / 3
    const out = {}
    for (const p of phases) {
      out[p] = (a[`${p}_mode`] || 'instant') === 'instant' ? instW : flexW
    }
    return out
  }

  function curvePath (x0, y0, x1, y1, curve, slope, dashed) {
    const c = Number(curve ?? 0)
    const s = Number(slope ?? 1)
    const dash = dashed ? ' stroke-dasharray="4 3"' : ''
    if (c === 8) {
      return `<line x1="${x0}" y1="${y0}" x2="${x1}" y2="${y1}"${dash}/>`
    }
    if (c === 0) {
      return `<line x1="${x0}" y1="${y0}" x2="${x1}" y2="${y1}"/>`
    }

    const dx = x1 - x0
    const dy = y1 - y0
    const steps = 32
    let d = `M ${x0} ${y0}`
    for (let i = 1; i <= steps; i++) {
      const t = Math.round((i / steps) * 127)
      const shaped = curveApply(c, s, t)
      const frac = shaped / 127
      const x = x0 + dx * (i / steps)
      const y = y0 + dy * frac
      d += ` L ${x.toFixed(1)} ${y.toFixed(1)}`
    }
    return `<path d="${d}" fill="none"/>`
  }

  function formatLevelValue (a, field, outputType, fallback) {
    const max = valueMax(outputType)
    const raw = a[field]
    return String(raw == null ? fallback : Number(raw))
  }

  function renderSvg (a) {
    if (!a) return ''
    const ot = a.output_type || 'cc'
    const x0 = 8
    const widths = phaseWidths(a)
    const valueToY = (norm) => Y_BOT - norm * (Y_BOT - Y_TOP)

    const startCurrent = (a.start_mode || 'current') === 'current'
    const startNorm = startCurrent ? 0 : normValue(a, 'start_value', ot, 0)
    const targetRandom = (a.target_mode || 'explicit') === 'random'
    const targetNorm = targetRandom ? 1 : normValue(a, 'target_value', ot, 127)

    const yStart = valueToY(startNorm)
    const yTarget = valueToY(targetNorm)

    let x = x0
    const labels = []
    const parts = []

    parts.push(`<line class="scene-boomerang-env-ref" x1="${x0}" y1="${yStart}" x2="${x0 + widths.attack + widths.sustain + widths.release}" y2="${yStart}" stroke-dasharray="3 3"/>`)

    const ax0 = x
    const ax1 = x + widths.attack
    parts.push(`<line class="scene-boomerang-env-ref" x1="${ax1}" y1="${yTarget}" x2="${ax1 + widths.sustain + widths.release}" y2="${yTarget}" stroke-dasharray="3 3"/>`)
    parts.push(curvePath(
      ax0, yStart, ax1, yTarget,
      a.attack_curve ?? 0, a.attack_curve_slope ?? 1,
      Number(a.attack_curve ?? 0) === 8
    ))
    if ((a.attack_mode || 'instant') !== 'instant' || widths.attack > INST_PHASE_W + 2) {
      labels.push({ x: (ax0 + ax1) / 2, text: phaseLabel(a, 'attack') })
    }
    x = ax1

    const sx0 = x
    const sx1 = x + widths.sustain
    parts.push(`<line x1="${sx0}" y1="${yTarget}" x2="${sx1}" y2="${yTarget}"/>`)
    if ((a.sustain_mode || 'instant') !== 'instant' || widths.sustain > INST_PHASE_W + 2) {
      labels.push({ x: (sx0 + sx1) / 2, text: phaseLabel(a, 'sustain') })
    }
    x = sx1

    const rx0 = x
    const rx1 = x + widths.release
    parts.push(curvePath(
      rx0, yTarget, rx1, yStart,
      a.release_curve ?? 0, a.release_curve_slope ?? 1,
      Number(a.release_curve ?? 0) === 8
    ))
    if ((a.release_mode || 'instant') !== 'instant' || widths.release > INST_PHASE_W + 2) {
      labels.push({ x: (rx0 + rx1) / 2, text: phaseLabel(a, 'release') })
    }

    const levelLabels = []
    if (startCurrent) {
      levelLabels.push(`<text class="scene-boomerang-env-level" x="${x0 + 2}" y="${yStart - 4}">Current</text>`)
    } else {
      levelLabels.push(`<text class="scene-boomerang-env-level" x="${x0 + 2}" y="${yStart - 4}">${esc(formatLevelValue(a, 'start_value', ot, 0))}</text>`)
    }
    if (targetRandom) {
      levelLabels.push(`<text class="scene-boomerang-env-level" x="${ax1 - 2}" y="${yTarget - 4}" text-anchor="end">Random</text>`)
    } else {
      levelLabels.push(`<text class="scene-boomerang-env-level" x="${ax1 - 2}" y="${yTarget - 4}" text-anchor="end">${esc(formatLevelValue(a, 'target_value', ot, 127))}</text>`)
    }

    const labelEls = labels.map(l =>
      `<text class="scene-boomerang-env-label" x="${l.x}" y="${LABEL_Y}" text-anchor="middle">${esc(l.text)}</text>`
    ).join('')

    return `<svg class="scene-boomerang-env-svg" width="${VIEW_W}" height="${VIEW_H}" viewBox="0 0 ${VIEW_W} ${VIEW_H}" preserveAspectRatio="xMidYMid meet" aria-hidden="true">
      <g class="scene-boomerang-env-stroke">${parts.join('')}</g>
      ${levelLabels.join('')}
      ${labelEls}
    </svg>`
  }

  const TOUCHPAD_INSPECT_NAMES = [
    'Pad 1', 'Pad 2', 'Pad 3', 'Pad 4', 'Pad 5', 'Pad 6', 'Pad 7', 'Pad 8',
    'Omega', 'Alpha', 'Beta', 'Gamma'
  ]

  function inspectLabelForPath (path, model) {
    let m = path.match(/^touchpads\.(\d+)\.action$/)
    if (m) return TOUCHPAD_INSPECT_NAMES[Number(m[1])] || `Pad ${Number(m[1]) + 1}`
    if (path === 'button_left') return 'Left'
    if (path === 'button_right') return 'Right'
    if (path === 'button_both') return 'Both'
    if (path === 'bump') return 'Bump'
    if (path === 'cv_trigger_action') return 'Action'
    m = path.match(/^on_load\.(\d+)$/)
    if (m) return `On-Load ${Number(m[1]) + 1}`
    m = path.match(/^on_play\.(\d+)$/)
    if (m) return `On-Play ${Number(m[1]) + 1}`
    m = path.match(/^cc_triggers\.(\d+)\.action$/)
    if (m) {
      const idx = Number(m[1])
      const cc = model?.cc_triggers?.[idx]?.cc_number ?? 0
      return `Trigger ${idx + 1} (CC ${cc})`
    }
    return path
  }

  function boomerangByInspectLabel (items, model) {
    const map = new Map()
    for (const item of items) {
      map.set(inspectLabelForPath(item.path, model), item)
    }
    return map
  }

  function renderReadOnlySvg (action) {
    return `<div class="scene-boomerang-env scene-boomerang-env-readonly">${renderSvg(action)}</div>`
  }

  function renderInspectDocument (text, model, opts = {}) {
    const items = collectBoomerangs(model, opts)
    if (!items.length) return esc(text || '(empty scene)')

    const byLabel = boomerangByInspectLabel(items, model)
    const paragraphs = (text || '(empty scene)').split(/\n\n+/)
    const parts = []

    for (const para of paragraphs) {
      const trimmed = para.trim()
      if (!trimmed) continue
      const firstLine = trimmed.split('\n')[0]
      const head = firstLine.match(/^(.+): Boomerang(?:$|: )/)
      const item = head ? byLabel.get(head[1]) : null

      if (item) {
        parts.push(`<div class="scene-inspect-block">
          <div class="scene-inspect-block-text">${esc(trimmed)}</div>
          <div class="scene-inspect-block-graph">${renderReadOnlySvg(item.action)}</div>
        </div>`)
        continue
      }

      parts.push(`<div class="scene-inspect-paragraph">${esc(trimmed)}</div>`)
    }

    return parts.join('')
  }

  function collectBoomerangs (model, opts = {}) {
    const items = []
    if (!model) return items
    const midiControl = opts.midiControl !== false
    SceneActions.forEachAction(model, (action, path) => {
      if (!midiControl && path.startsWith('cc_triggers.')) return
      if (action?.type === 'boomerang') items.push({ action, path })
    })
    return items
  }

  const PRINT_STYLES = `
.scene-inspect-block {
  display: block;
  margin-bottom: 0.2in;
  break-inside: avoid;
  page-break-inside: avoid;
}
.scene-inspect-block-graph {
  width: 2.8in;
  max-width: 2.8in;
  margin: 0.08in 0 0;
}
.scene-inspect-paragraph {
  white-space: pre-wrap;
  word-break: break-word;
  margin-bottom: 0.15in;
}
.scene-boomerang-env { width: 2.8in; height: 1in; margin: 0; }
.scene-boomerang-env-svg { display: block; width: 2.8in; height: 1in; }
.scene-boomerang-env-stroke line,
.scene-boomerang-env-stroke path {
  stroke: #000;
  stroke-width: 1.5;
  fill: none;
  vector-effect: non-scaling-stroke;
}
.scene-boomerang-env-ref { stroke: #666; stroke-width: 1; opacity: 0.55; }
.scene-boomerang-env-label,
.scene-boomerang-env-level {
  fill: #444;
  font-family: system-ui, sans-serif;
  font-size: 9px;
}
.scene-boomerang-env-level { font-style: italic; }
`

  return {
    renderSvg,
    renderReadOnlySvg,
    renderInspectDocument,
    collectBoomerangs,
    inspectLabelForPath,
    printStyles: PRINT_STYLES
  }
})()
