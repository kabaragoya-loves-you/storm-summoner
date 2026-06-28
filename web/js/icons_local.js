/* Override Web Awesome default icon library to use vendored Font Awesome SVGs. */

import { registerIconLibrary } from '/assets/webawesome/dist-cdn/webawesome.loader.js'

const FA_SVG_BASE = '/assets/fontawesome/svgs'

const NAME_ALIASES = {
  'info-circle': 'circle-info'
}

function iconFolder (family, variant) {
  if (family === 'brands') return 'brands'
  if (family !== 'classic' && family) return 'solid'
  if (variant === 'regular' || variant === 'thin' || variant === 'light') return variant
  return 'solid'
}

registerIconLibrary('default', {
  resolver: (name, family = 'classic', variant = 'solid') => {
    const resolved = NAME_ALIASES[name] || name
    const folder = iconFolder(family, variant)
    return `${FA_SVG_BASE}/${folder}/${resolved}.svg`
  }
})
