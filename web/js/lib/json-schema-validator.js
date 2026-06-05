/* Lightweight JSON Schema draft-04 subset validator (offline, no deps).
 * Pedal/device JSON must be strict JSON (no comments). Integers are whole numbers. */

window.JsonSchemaValidator = (function () {
  function typeOf (value) {
    if (value === null) return 'null'
    if (Array.isArray(value)) return 'array'
    if (typeof value === 'number') {
      return Number.isInteger(value) ? 'integer' : 'number'
    }
    return typeof value
  }

  function matchesSchemaType (value, expected) {
    const actual = typeOf(value)
    if (expected === 'number') return actual === 'number' || actual === 'integer'
    return actual === expected
  }

  function pushError (errors, path, message) {
    errors.push({ path: path || '/', message })
  }

  function validate (data, schema, path, errors) {
    if (!schema || typeof schema !== 'object') return

    if (schema.enum) {
      if (!schema.enum.includes(data)) {
        pushError(errors, path, `must be one of: ${schema.enum.join(', ')}`)
      }
      return
    }

    const t = schema.type
    if (t) {
      const types = Array.isArray(t) ? t : [t]
      const actual = typeOf(data)
      if (!types.some(expected => matchesSchemaType(data, expected))) {
        pushError(errors, path, `must be ${types.join(' or ')}, got ${actual}`)
        return
      }
    }

    if (schema.pattern && typeof data === 'string') {
      const re = new RegExp(schema.pattern)
      if (!re.test(data)) {
        pushError(errors, path, `must match pattern ${schema.pattern}`)
      }
    }

    if (typeof data === 'string') {
      if (schema.minLength !== undefined && data.length < schema.minLength) {
        pushError(errors, path, `must be at least ${schema.minLength} characters`)
      }
      if (schema.maxLength !== undefined && data.length > schema.maxLength) {
        pushError(errors, path, `must be at most ${schema.maxLength} characters`)
      }
    }

    if (typeof data === 'number') {
      if (schema.minimum !== undefined && data < schema.minimum) {
        pushError(errors, path, `must be >= ${schema.minimum}`)
      }
      if (schema.maximum !== undefined && data > schema.maximum) {
        pushError(errors, path, `must be <= ${schema.maximum}`)
      }
    }

    if (t === 'object' && data && typeof data === 'object' && !Array.isArray(data)) {
      const required = schema.required || []
      for (const key of required) {
        if (!(key in data)) {
          pushError(errors, joinPath(path, key), 'is required')
        }
      }
      const props = schema.properties || {}
      for (const key of Object.keys(data)) {
        if (props[key]) {
          validate(data[key], props[key], joinPath(path, key), errors)
        }
      }
      return
    }

    if (t === 'array' && Array.isArray(data) && schema.items) {
      data.forEach((item, i) => {
        validate(item, schema.items, joinPath(path, String(i)), errors)
      })
    }
  }

  function joinPath (base, segment) {
    if (!base || base === '/') return `/${segment}`
    return `${base}/${segment}`
  }

  return {
    validate (data, schema) {
      const errors = []
      validate(data, schema, '', errors)
      return errors
    }
  }
})()
