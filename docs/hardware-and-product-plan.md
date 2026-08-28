# Binary Load Lifter hardware and product plan

## Purpose

Binary Load Lifter gives the DIHOOL platform lift real position feedback. The ESP32 observes hardware; Homebridge owns calibration and HomeKit presentation. Binary Load Lifter does not drive motor power.

## Hardware

| Component | Role | Status |
| --- | --- | --- |
| ESP32 Dev Module / ESP-WROOM-32 | Sensor controller, Wi-Fi, local API | Working |
| CQRobo VL53L1X ToF sensor | Measures distance to platform | Working |
| Scissor-lift platform | Raises four steel posts through the floor; approximately 10–12 in travel | Existing lift hardware; manufacturer/model not yet confirmed |
| DIHOOL IPS-S2 controller | Reversible DC lift controller | Existing; not modified |
| IPS-P3 AC/DC adapter | 27 V DC, 3 A maximum | Existing lift hardware |
| E3Z-R61 reflective photoelectric sensor | Existing hardwired obstruction/safety sensor | Wired to IPS-S2 input `IN5`; working |
| Two-channel PC817 optocoupler module | Motor-output polarity sensing | Protected input circuit bench-tested at 9 V; ESP32 integration in progress |
| Plastic enclosure | Final electronics enclosure | Planned |

## Bench inventory

The following combines the hardware photo taken 2026-08-26 with the kit's
Amazon description.

| Item | Quantity visible | Likely use in Binary Load Lifter | Notes |
| --- | ---: | --- | --- |
| Full-size solderless breadboard | 1 | Safe prototyping | Use before soldering or working near the lift. |
| PC817 two-channel isolation modules | 2 | Future motor-state sensing | Keep disconnected from the lift until the protected input circuit is designed. |
| Resistors | 30: 220 Ω, 1 kΩ, 10 kΩ | LED tests and low-voltage prototypes | Values confirmed by kit description. |
| YUEONEWIN 1,400-piece component kit | 1 assortment | General prototyping and future interface circuits | Amazon ASIN B09YTSR9Z9. |
| 1/4 W, 1% metal-film resistors | 600; 30 values from 10 Ω to 1 MΩ | Optocoupler protection, pull-ups, LEDs, transistor drivers | Includes useful 2.2 kΩ, 4.7 kΩ, 10 kΩ, and 100 kΩ values. |
| Ceramic capacitors | 300; 30 values from 2 pF to 100 nF | Decoupling and noise suppression | 100 nF is especially useful near ESP32/interface power. |
| Electrolytic capacitors | 120; 12 values from 0.22 µF to 470 µF | Power smoothing and prototypes | Some are rated only 16 V; never use those across the 27 V lift supply. |
| Small-signal / rectifier diodes | Assortment including 1N4148 and 1N4007 | Future protected sensing circuits | Confirm exact diode and rating before final wiring. |
| Small transistors | 180; 18 types | Buzzer driver and low-voltage interface prototypes | Includes common parts such as 2N2222, 2N3904, and 2N3906. |
| 3 mm LEDs | 100; 20 each red, green, blue, white, yellow | Bench status indicators | White/green/blue are rated about 3–3.2 V; red/yellow about 2–2.2 V. |
| LEDs | 5 each red, yellow, green; 2 RGB | Bench status indicators | Always use a suitable series resistor. |
| Tactile pushbuttons | 6 | Optional local test or reset controls | Not required for the first installed version. |
| 10 kΩ potentiometer | 1 | Optional bench input / configuration testing | Not currently planned for final installation. |
| Passive buzzer | 1 | Planned audible feedback | Requires a generated tone/PWM signal. |
| Active 5 V two-channel relay module | 1 | Future low-voltage bench experiments only | Not needed for Binary Load Lifter; never use it to switch lift motor power. |
| 0.96-inch OLED | 1 | Optional local diagnostic display | Candidate future feature. |
| Obstacle-avoidance module | 1 | General kit component | Not part of Binary Load Lifter plan. |
| Photosensitive resistor | 1 | General kit component | Not part of Binary Load Lifter plan. |
| DHT11 temperature/humidity sensor | 1 | General kit component | Not part of Binary Load Lifter plan. |
| HC-SR501 PIR motion sensor | 1 | General kit component | Not part of Binary Load Lifter plan. |
| Dupont jumper leads | F–M, F–F, M–M sets | Breadboard wiring | Use for low-voltage prototyping only. |
| Micro-USB cable | 1 | ESP32 power/programming | Confirm it is data-capable. |

## Current sensor wiring

The VL53L1X cable plugs into the sensor board; its individual connectors then attach to the ESP32. Unplug the ESP32 before moving any wire.

| VL53L1X | ESP32 label | GPIO |
| --- | --- | --- |
| VCC | 3V3 | — |
| GND | GND | — |
| SDA | D21 | GPIO 21 |
| SCL | D22 | GPIO 22 |
| INT / SHUT | Not connected | — |

## Motor-sense wiring

The two PC817 outputs are active-low raw inputs. The module's isolation jumpers
must remain removed. Both output-side `G` terminals connect only to ESP32 ground;
the isolated input side has no electrical connection to the ESP32 side.

| PC817 output | ESP32 label | GPIO |
| --- | --- | --- |
| `V1` | D32 | GPIO 32 |
| Channel 1 output `G` | GND | — |
| `V2` | D33 | GPIO 33 |
| Channel 2 output `G` | GND | — |

## Buzzer wiring

The passive buzzer module includes onboard drive circuitry and is powered from
the same regulated 3.3 V supply as the distance sensor. Its input is marked as
low-level triggered.

| Buzzer module | ESP32 label | GPIO |
| --- | --- | --- |
| `VCC` | 3V3 | — |
| `GND` | GND | — |
| `I/O` | D27 | GPIO 27 |

The firmware holds GPIO 27 high for silence and uses a non-blocking tone player,
so buzzer patterns do not pause distance sampling or API handling.

Current patterns:

- Ready: a short rising C-major arpeggio after Wi-Fi, mDNS, and the API start.
- Firmware update accepted: a short low-high two-note chirp before writing;
  the GPIO 2 onboard ESP32 LED blinks during the upload and turns off when
  writing finishes.
- Channel 1 movement start: one short high note.
- Channel 2 movement start: one short low note.
- Movement stopped: two descending notes.
- Sensor fault: three low warning notes after a timeout persists for two
  seconds. The warning resets after valid filtered readings resume.

Channel patterns remain deliberately named channel 1 and channel 2 until an
installed polarity test establishes which one physically means up or down.

Each PC817 input channel uses three approximately 1 kΩ, 1/4 W external series
resistors and a 1N4007 reverse-protection diode. The diode bridges that
channel's `IN` and `G` terminals with its silver-banded cathode toward `IN`.
Both channels passed 9 V forward- and reverse-polarity bench tests. See
[the protected-channel wiring diagram](pc817-protected-channel-wiring.html).

## Controller safety

The IPS-S2 accepts 10–30 V DC. The installed adapter supplies 27 V DC, and the controller can put about 25 V DC on `M+` / `M−`, reversing polarity for direction. Never connect an ESP32, breadboard rail, or sensor lead directly to `M+` / `M−`.

The E3Z-R61 sensor is part of the existing, independent safety system. It is a
reflective photoelectric sensor used to stop the lift when its detection path
is interrupted. It remains wired to controller input `IN5`; Binary Load Lifter will not
replace, bypass, or control it.

## Firmware

This PlatformIO Arduino project targets `esp32dev`. It reads the VL53L1X,
connects to Wi-Fi, advertises `binary-load-lifter.local` using mDNS, and serves a
token-authenticated status API plus a local firmware-update endpoint. Lift
state remains read-only; the update endpoint changes only the ESP32 firmware.

Distance sampling is adaptive. The sensor runs continuously at 10 Hz while a
motor channel is active and for two seconds after movement stops. While idle,
continuous ranging is stopped and the firmware takes one single measurement
every five seconds. Startup and recovery from a sensor timeout temporarily use
continuous ranging until the distance filter is ready. API requests return the
latest cached measurement immediately rather than blocking for a new reading.
Idle single-shot measurements are also handled asynchronously, allowing HTTP
requests to be serviced while the sensor completes a measurement.

Motor-state callbacks are optional and outbound-only. An authenticated status
request may include a URL-encoded `callback_url` query parameter. The firmware
keeps the latest value in RAM and sends it an authenticated HTTP event after
each debounced motor-state change. The callback runs in a separate FreeRTOS
task so a slow or unavailable Homebridge host cannot pause sensor acquisition,
the status API, or motor-input debouncing. Normal Homebridge polling remains
the fallback. Every status request carrying the callback URL renews a two-minute
lease; the firmware clears it if Homebridge stops polling. Restarting the ESP32
also clears the callback until the next status poll.

`include/secrets.h` contains Wi-Fi credentials and the API token; it is ignored by Git. Use `include/secrets.example.h` as the template. Do not commit secrets. The API remains disabled until the token is at least 32 characters.

### API

`GET /v1/status`

```text
Authorization: Bearer <API token>
```

```json
{"distance_mm":524,"sensor_timeout":false,"motor_channel_1_active":false,"motor_channel_2_active":false,"uptime_ms":123456}
```

Keep this API on the home network: no port forwarding or UPnP.

### Firmware updates

`POST /v1/update` accepts a PlatformIO `firmware.bin` as the multipart form
field `firmware` and requires the same bearer-token header as the status API.
The firmware rejects unauthenticated uploads and refuses or aborts an update if
either motor channel is active. After a complete image is written and
validated in the inactive firmware partition, the ESP32 responds successfully
and restarts. A failed or interrupted upload does not replace the running
firmware.

The OTA-capable firmware must be flashed over USB once before this endpoint is
available. Build with `task firmware:build`, then—with the lift parked and prevented from
operating—upload from the trusted local network:

```sh
curl --fail-with-body \
  -H "Authorization: Bearer <API token>" \
  -F "firmware=@.pio/build/esp32dev/firmware.bin" \
  http://binary-load-lifter.local/v1/update
```

Do not operate the lift, remove ESP32 power, or interrupt its Wi-Fi connection
during an update. Verify `/v1/status` after the automatic reboot. Retain USB
access for recovery if the device cannot start or reconnect. The endpoint uses
HTTP because it is local-only, so the bearer token is not encrypted in transit:
keep it on the trusted home network and never add port forwarding, UPnP, public
proxying, or internet exposure.

## Homebridge plugin

The companion plugin lives in [`homebridge-plugin/`](../homebridge-plugin/). It
commands DIHOOL through eWeLink LAN and independently polls the Binary Load
Lifter ESP32 for physical feedback.

Per-lift settings:

- `esp32Host`: normally `binary-load-lifter.local`
- `esp32Token`: token from `include/secrets.h`
- `esp32PollIntervalSec`: normally `2`
- `esp32DebugLogging`: enable per-poll logs while calibrating
- `esp32SyncTargetPosition`: optionally show the measured percentage as the
  HomeKit target after movement stops
- `invertMotorChannelDirections`: normally `false`; enable it if the installed
  polarity test shows channel 1 is down and channel 2 is up
- `minDistanceMm`: temporary default `150`, measured fully raised
- `maxDistanceMm`: temporary default `1000`, measured fully lowered
- `binaryLoadLifterCallbackHost`: hostname or reserved IPv4 address the ESP32 can use
  to reach Homebridge; leave blank to disable callbacks
- `binaryLoadLifterCallbackPort`: normally `8582`

The plugin maps distance to HomeKit `CurrentPosition` and maps the two raw motor
channels to HomeKit's increasing/decreasing/stopped state. Channel 1 means up
and channel 2 means down by default; `invertMotorChannelDirections` swaps that
presentation mapping without changing either DIHOOL command channel. Endpoint
values are temporary bench-test defaults and must be calibrated after
installation. The plugin performs direct IPv4 mDNS lookup if the Homebridge
host cannot resolve `.local` itself.

The plugin's authenticated callback listener switches the corresponding lift
to 10 Hz polling as soon as a motor event arrives. It remains at 10 Hz while
the motor is active and for two seconds after stop, then returns to the
configured idle poll interval. Each Binary Load Lifter device must use a unique API
token so callbacks can be routed to the correct lift.

# Roadmap

### 1. Motor power and direction sensing

The PC817 modules will observe motor-output polarity:

```text
M+ / M− → protected isolated sensing circuit → ESP32 GPIO → Homebridge
```

Two channels can identify motor-output present/not-present and up/down. They do
not measure motor current in amperes. This makes the sensor-based position UI
accurately show whether the lift is moving and in which direction.

Before any lift connection: add external series resistors rated for 27 V; test
the circuit away from the lift; and keep lift power disconnected while changing
wiring.

Bench progress as of 2026-08-26:

- Both PC817 channels switch correctly from open circuit to approximately
  3.9–4.3 kΩ at 9 V.
- Each protected input uses 3 kΩ of external series resistance and a 1N4007
  reverse-protection diode.
- Forward- and reverse-polarity protection tests passed at 9 V.
- Firmware reports the active-low GPIO 32/33 signals as raw channel states.
  Homebridge treats channel 1 as up and channel 2 as down by default, with a
  per-lift inversion setting for the installed polarity result.
- A state must remain unchanged for 50 ms before firmware reports it, suppressing
  contact bounce and short noise pulses without delaying human-visible state.

### 2. Audio feedback

Binary Load Lifter provides simple local audible feedback using a passive buzzer module
with onboard drive circuitry. The module accepts an ESP32-generated tone signal
on GPIO 27 and shares the regulated 3.3 V supply with the distance sensor.

Planned events:

- ESP32 startup and Wi-Fi/API availability.
- Calibration confirmation.
- Sensor fault or prolonged communication loss.
- Movement start/completion, using the new motor-state signal.

The original bare buzzer measured approximately 15.8 Ω and must not be connected
directly to an ESP32 GPIO. It has been replaced by a three-pin module with
onboard drive circuitry; only the module's `I/O` input connects to GPIO 27.

### 3. Final sensor installation and calibration

1. Mount the VL53L1X under the floor, aimed straight down at the platform.
2. Put the ESP32 and interface hardware inside the plastic enclosure, clear of moving parts and moisture.
3. Add strain relief to the sensor cable.
4. Record stable fully-raised and fully-lowered distances.
5. Enter those values in Homebridge and verify several full lift cycles.

For this mount direction, distance decreases as the platform rises.

### 4. Closed-loop target-position control

After position sensing, motor-state sensing, and calibration have proven
reliable through full lift cycles, Homebridge should be able to respond to a
HomeKit target position by commanding the existing DIHOOL controller.

The intended control path is:

```text
HomeKit target → Homebridge position logic → isolated command interface → DIHOOL controller input → motor
```

The command interface must emulate the controller's normal low-voltage control
input or button action. It must never switch motor power or connect ESP32 GPIO
directly to the motor terminals.

Required safeguards before enabling closed-loop control:

- Keep the DIHOOL controller's limits and E3Z-R61 safety sensor hardwired and
  independent.
- Refuse movement when sensor data is stale, invalid, or uncalibrated.
- Use short, deliberate command pulses; stop requesting motion when the measured
  position reaches the requested target.
- Preserve manual controller operation at all times.
- Make automated position control an explicit opt-in setting, initially limited
  to supervised testing.

## Boundaries

- DIHOOL controls and stops the motor.
- Current Binary Load Lifter firmware observes hardware only. Future control will use an
  isolated, low-voltage command interface; it will never carry or switch motor
  power.
- Homebridge owns calibration, settings, and HomeKit behavior.
- ESP32 firmware owns sensor acquisition and raw-data publication.
