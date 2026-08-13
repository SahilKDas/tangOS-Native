// Validate tangos.json descriptors against schema/tangos.schema.json.
//
// Usage: node schema/validate.mjs <descriptor path or URL> [...more]
//
// Exits non-zero if the schema itself does not compile or any descriptor fails.
// CI runs this over the published reference descriptors so the schema and the
// dialect the Console consumes (console/src/shared/types.ts) cannot drift apart
// silently again.
import { readFileSync } from 'node:fs'
import { fileURLToPath } from 'node:url'
import { dirname, join } from 'node:path'
import Ajv2020 from 'ajv/dist/2020.js'

const here = dirname(fileURLToPath(import.meta.url))
const schema = JSON.parse(readFileSync(join(here, 'tangos.schema.json'), 'utf8'))

const ajv = new Ajv2020.default({ allErrors: true })
const validate = ajv.compile(schema)

const targets = process.argv.slice(2)
if (targets.length === 0) {
  console.error('usage: node schema/validate.mjs <descriptor path or URL> [...more]')
  process.exit(2)
}

let failed = false
for (const target of targets) {
  let text
  try {
    if (/^https?:\/\//.test(target)) {
      const res = await fetch(target)
      if (!res.ok) throw new Error(`HTTP ${res.status}`)
      text = await res.text()
    } else {
      text = readFileSync(target, 'utf8')
    }
  } catch (e) {
    console.error(`FAIL ${target}: could not read (${e.message})`)
    failed = true
    continue
  }
  let doc
  try {
    doc = JSON.parse(text)
  } catch (e) {
    console.error(`FAIL ${target}: not valid JSON (${e.message})`)
    failed = true
    continue
  }
  if (validate(doc)) {
    console.log(`ok   ${target}`)
  } else {
    failed = true
    console.error(`FAIL ${target}:`)
    for (const err of validate.errors) {
      console.error(`  ${err.instancePath || '/'} ${err.message}`)
    }
  }
}
process.exit(failed ? 1 : 0)
