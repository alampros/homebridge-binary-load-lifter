# Binary Load Lifter

Binary Load Lifter is ESP32 firmware for observing a DIHOOL IPS-S2 platform lift. It
publishes filtered VL53L1X distance measurements and two isolated motor-output
detector states for the companion
[`homebridge-binary-load-lifter`](https://github.com/alampros/homebridge-binary-load-lifter)
plugin.

Binary Load Lifter is read-only: the DIHOOL controller remains responsible for driving
and stopping the motor. Never connect an ESP32 pin, sensor wire, or breadboard
rail directly to the controller's `M+` / `M−` terminals. Motor detection must
use the documented optocoupler isolation circuit, and the existing E3Z-R61
safety sensor must remain connected and unmodified.

## Setup

1. Copy `include/secrets.example.h` to `include/secrets.h`.
2. Enter the Wi-Fi credentials and an API token of at least 32 characters.
3. Build and upload with PlatformIO:

   ```sh
   uvx --from platformio==6.1.19 pio run
   uvx --from platformio==6.1.19 pio run --target upload
   uvx --from platformio==6.1.19 pio device monitor
   ```

The first firmware version that supports wireless updates must be installed by
USB. Keep USB flashing available as the recovery path if a wireless update is
interrupted or the device can no longer join Wi-Fi.

The ESP32 advertises `binary-load-lifter.local` and provides a token-authenticated,
read-only `GET /v1/status` endpoint on the local network. API requests return
the latest cached measurement immediately. Distance sampling runs at 10 Hz
while the motor is active and falls back to one reading every five seconds
while idle.

For immediate HomeKit movement updates, the Homebridge plugin can include a
URL-encoded `callback_url` query parameter in its authenticated status request.
The ESP32 keeps the latest callback URL in memory and sends it authenticated
motor-state events while regular status polling remains available as a
fallback. Each status poll renews the callback URL; it expires after two
minutes without renewal. No Homebridge address is stored in the firmware
configuration.

## Local wireless updates

Copy the local configuration template once and put the same API token used in
`include/secrets.h` into the ignored file:

```sh
cp .binary-load-lifter-ota.env.example .binary-load-lifter-ota.env
```

Leave `BINARY_LOAD_LIFTER_HOST=binary-load-lifter.local`, or replace it with the ESP32's reserved
IPv4 address if `.local` name resolution is unavailable. Do not commit
`.binary-load-lifter-ota.env`; Git ignores it.

Park the lift and make sure it cannot be operated during the update. In VS
Code, run **Tasks: Run Task**, then choose **Binary Load Lifter: Build and upload
wirelessly**. The task builds `esp32dev`, uploads the generated `firmware.bin`,
and reports the device response. The helper passes the token to curl through
standard input so it is not placed in the process command line.

After a successful upload and automatic reboot, the new firmware plays a short
rising C-major arpeggio as its ready chime. Hearing the pattern confirms that
the uploaded image started without requiring a USB connection.
If USB is connected for diagnostics, the 115200-baud serial monitor also shows
`Binary Load Lifter firmware starting. Build: <date> <time>`.

When an authenticated update is accepted, Binary Load Lifter first plays a short
low-high two-note chirp and blinks the ESP32 development board's GPIO 2 onboard LED
while it writes the image. The LED turns off when writing finishes; the
arpeggiated ready chime follows after the automatic reboot. Boards without an
onboard LED wired to GPIO 2 still provide the buzzer feedback.

The equivalent terminal command is:

```sh
./scripts/build-and-upload-ota.sh
```

To validate the configuration and build without contacting the ESP32, run:

```sh
./scripts/build-and-upload-ota.sh --dry-run
```

For reference, the underlying manual upload is:

```sh
uvx --from platformio==6.1.19 pio run
curl --fail-with-body \
  -H "Authorization: Bearer <API token>" \
  -F "firmware=@.pio/build/esp32dev/firmware.bin" \
  http://binary-load-lifter.local/v1/update
```

The update endpoint uses the same bearer token as the status API. It rejects
missing or invalid authentication and refuses or aborts an update while either
motor-sense channel is active. A successful upload is validated, installed in
the ESP32's inactive firmware partition, and followed by an automatic reboot.
An incomplete or failed upload leaves the currently running firmware in place.

Use this only on the trusted home network. HTTP does not encrypt the token, so
do not expose the endpoint through port forwarding, UPnP, a public reverse
proxy, or the internet. Do not move the lift, remove power, or disconnect Wi-Fi
during an update. Confirm that `GET /v1/status` returns normally after reboot;
if it does not, recover with the USB upload command above.

See [the hardware and product plan](docs/hardware-and-product-plan.md) for the
installed hardware, wiring, API contract, calibration guidance, and safety
boundaries. The protected motor-input circuit is documented in
[the wiring diagram](docs/pc817-protected-channel-wiring.html).
