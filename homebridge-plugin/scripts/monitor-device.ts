/**
 * Monitors a DIHOOL IPS-S2 eWeLink device via mDNS TXT record polling.
 * Uses dns-sd (macOS built-in) which we know works, instead of raw UDP.
 *
 * Usage:
 *   npx tsx scripts/monitor-device.ts <deviceId> <lanKey>
 */

import { spawn } from 'node:child_process'
import { createDecipheriv, createHash } from 'node:crypto'

const [, , deviceId, lanKey] = process.argv

if (!deviceId || !lanKey) {
  console.error('Usage: npx tsx scripts/monitor-device.ts <deviceId> <lanKey>')
  process.exit(1)
}

const aesKey = createHash('md5').update(Buffer.from(lanKey, 'utf8')).digest()

function decrypt(ciphertext: string, ivBase64: string): string {
  const iv = Buffer.from(ivBase64, 'base64')
  const decipher = createDecipheriv('aes-128-cbc', aesKey, iv)
  const decrypted = Buffer.concat([
    decipher.update(Buffer.from(ciphertext, 'base64')),
    decipher.final(),
  ])
  let plaintext = decrypted.toString('utf8')
  while (plaintext.length > 0 && plaintext.charCodeAt(plaintext.length - 1) <= 0x1f) {
    plaintext = plaintext.slice(0, -1)
  }
  return plaintext
}

let lastIv = ''
let lastSwitches = ''

function processLine(line: string): void {
  // dns-sd -Q outputs TXT records as they change.
  // But dns-sd -L is simpler - it outputs the full record each time.
  // We'll parse the output looking for our fields.

  const ivMatch = line.match(/iv=(\S+)/)
  if (!ivMatch) return

  const dataFields: string[] = []
  for (let i = 1; i <= 4; i++) {
    const m = line.match(new RegExp(`data${i}=(\\S+)`))
    if (m) dataFields.push(m[1])
  }

  if (dataFields.length === 0) return

  const iv = ivMatch[1]
  if (iv === lastIv) return
  lastIv = iv

  const concatenatedData = dataFields.join('')

  try {
    const plaintext = decrypt(concatenatedData, iv)
    const state = JSON.parse(plaintext)
    const now = new Date().toLocaleTimeString()

    const switches = state.switches
    if (switches) {
      const switchStr = switches
        .map((s: { outlet: number; switch: string }) => `CH${s.outlet}:${s.switch.padEnd(3)}`)
        .join('  ')

      if (switchStr !== lastSwitches) {
        lastSwitches = switchStr
        console.log(`[${now}]  ${switchStr}`)
      }
    }
  } catch {
    // Decryption errors are common during partial reads, ignore
  }
}

// Use dns-sd -Q to watch for TXT record changes in real-time
// -Q queries a specific record and streams updates
console.log(`\nMonitoring device ${deviceId} via dns-sd...`)
console.log('Trigger the lift via eWeLink app. Press Ctrl+C to stop.\n')

// dns-sd -L keeps the connection open and re-reports when TXT changes
const child = spawn('dns-sd', ['-L', `eWeLink_${deviceId}`, '_ewelink._tcp', 'local'], {
  stdio: ['ignore', 'pipe', 'pipe'],
})

let buffer = ''
child.stdout.on('data', (data: Buffer) => {
  buffer += data.toString()
  const lines = buffer.split('\n')
  buffer = lines.pop() || ''
  for (const line of lines) {
    if (line.includes('iv=')) {
      processLine(line)
    }
  }
})

child.stderr.on('data', (data: Buffer) => {
  // dns-sd sometimes writes to stderr
  const str = data.toString()
  if (str.includes('iv=')) {
    processLine(str)
  }
})

child.on('close', (code) => {
  console.log(`dns-sd exited with code ${code}`)
})

process.on('SIGINT', () => {
  child.kill()
  process.exit(0)
})
