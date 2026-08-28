# Binary Load Lifter

Binary Load Lifter is a Homebridge-controlled platform-lift project built around
a DIHOOL IPS-S2 controller and an ESP32 hardware observer. The name is a nod to
the *Star Wars* binary load lifter—and the old *Saturday Night Live* sketch.

The project has two independently built components:

- [`homebridge-plugin/`](homebridge-plugin/) contains the published
  `homebridge-binary-load-lifter` npm package. It controls the DIHOOL controller
  over the local eWeLink protocol and presents each lift to HomeKit.
- The repository root is the PlatformIO ESP32 firmware project. It observes
  distance and isolated motor state and exposes a token-authenticated local API.

The ESP32 does not drive the motor or replace the lift's hardwired safety
systems. Read [`docs/hardware-and-product-plan.md`](docs/hardware-and-product-plan.md)
before changing hardware, firmware, or their integration.

## Development

Common commands are available through [Go Task](https://taskfile.dev/):

```sh
task install
task check
task build
```

Run `task check` after code changes. It verifies formatting, lint and type
safety, and tests. The underlying component commands can also be run directly.

Run Homebridge commands from `homebridge-plugin`:

```sh
cd homebridge-plugin
npm ci
npm test
npm run lint
npm run build
```

Build the ESP32 firmware from the repository root:

```sh
uvx --from platformio==6.1.19 pio run
```

The components have separate build systems and releases. Only the contents
listed by `homebridge-plugin/package.json` are included in the npm package.
