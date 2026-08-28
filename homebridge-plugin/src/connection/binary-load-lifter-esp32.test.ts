import { describe, expect, it } from 'vitest'

import {
  ACTIVE_POLL_INTERVAL_MS,
  binaryLoadLifterPollDelay,
  binaryLoadLifterStatusPath,
  motorDirectionFromStatus,
  parseBinaryLoadLifterStatus,
} from './binary-load-lifter-esp32.js'

describe('parseBinaryLoadLifterStatus', () => {
  it('prefers the filtered distance and preserves the raw sample', () => {
    expect(
      parseBinaryLoadLifterStatus(
        JSON.stringify({
          distance_mm: 500,
          raw_distance_mm: 520,
          filtered_distance_mm: 505,
          filter_ready: true,
          sensor_timeout: false,
          motor_channel_1_active: true,
          motor_channel_2_active: false,
        }),
      ),
    ).toEqual({
      distanceMm: 505,
      rawDistanceMm: 520,
      sensorTimeout: false,
      motorChannel1Active: true,
      motorChannel2Active: false,
    })
  })

  it('supports the legacy distance response', () => {
    expect(
      parseBinaryLoadLifterStatus(
        JSON.stringify({
          distance_mm: 500,
          sensor_timeout: false,
        }),
      ),
    ).toEqual({
      distanceMm: 500,
      rawDistanceMm: undefined,
      sensorTimeout: false,
      motorChannel1Active: undefined,
      motorChannel2Active: undefined,
    })
  })

  it('treats a warming filter as unavailable', () => {
    expect(
      parseBinaryLoadLifterStatus(
        JSON.stringify({
          distance_mm: 0,
          raw_distance_mm: 500,
          filtered_distance_mm: 0,
          filter_ready: false,
          sensor_timeout: true,
        }),
      ).sensorTimeout,
    ).toBe(true)
  })

  it('rejects malformed responses', () => {
    expect(() => parseBinaryLoadLifterStatus('{"distance_mm":"500"}')).toThrow(
      'ESP32 returned an invalid status response',
    )
  })

  it('rejects incomplete motor channel pairs', () => {
    expect(() =>
      parseBinaryLoadLifterStatus(
        JSON.stringify({
          distance_mm: 500,
          sensor_timeout: false,
          motor_channel_1_active: true,
        }),
      ),
    ).toThrow('ESP32 returned an invalid status response')
  })
})

describe('motorDirectionFromStatus', () => {
  const status = (channel1?: boolean, channel2?: boolean) => ({
    distanceMm: 500,
    sensorTimeout: false,
    motorChannel1Active: channel1,
    motorChannel2Active: channel2,
  })

  it('maps channel 1 to up and channel 2 to down by default', () => {
    expect(motorDirectionFromStatus(status(true, false))).toBe('up')
    expect(motorDirectionFromStatus(status(false, true))).toBe('down')
  })

  it('swaps channel meanings when configured', () => {
    expect(motorDirectionFromStatus(status(true, false), true)).toBe('down')
    expect(motorDirectionFromStatus(status(false, true), true)).toBe('up')
  })

  it('reports stopped, invalid, and unknown detector states', () => {
    expect(motorDirectionFromStatus(status(false, false))).toBe('stopped')
    expect(motorDirectionFromStatus(status(true, true))).toBe('invalid')
    expect(motorDirectionFromStatus(status())).toBe('unknown')
  })
})

describe('binaryLoadLifterPollDelay', () => {
  it('uses the active interval while moving and during the post-stop window', () => {
    expect(binaryLoadLifterPollDelay(5000, true, 0, 1000)).toBe(ACTIVE_POLL_INTERVAL_MS)
    expect(binaryLoadLifterPollDelay(5000, false, 2000, 1000)).toBe(ACTIVE_POLL_INTERVAL_MS)
  })

  it('returns to the configured idle interval after the post-stop window', () => {
    expect(binaryLoadLifterPollDelay(5000, false, 2000, 2000)).toBe(5000)
  })
})

describe('binaryLoadLifterStatusPath', () => {
  it('includes a fully URL-encoded callback URL when configured', () => {
    expect(
      binaryLoadLifterStatusPath('http://homebridge.local:8582/v1/binary-load-lifter/motor'),
    ).toBe(
      '/v1/status?callback_url=http%3A%2F%2Fhomebridge.local%3A8582%2Fv1%2Fbinary-load-lifter%2Fmotor',
    )
  })

  it('keeps the original status path when callbacks are disabled', () => {
    expect(binaryLoadLifterStatusPath()).toBe('/v1/status')
  })
})
