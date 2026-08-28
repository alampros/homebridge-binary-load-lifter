# Binary Load Lifter project guide

Read [docs/hardware-and-product-plan.md](docs/hardware-and-product-plan.md)
before making hardware, firmware, or Homebridge-integration decisions. It is the
source of truth for installed hardware, wiring, API, safety boundaries, and the
future product plan.

## Repository layout

- The repository root contains the ESP32 PlatformIO project.
- `homebridge-plugin/` contains the independently built and published npm package.
- `docs/` contains shared product and hardware documentation.

## Required checks

After making code changes, run `task check` from the repository root. It must
pass formatting verification, lint and type checks, and tests before the work is
considered complete. Run `task firmware:build` as well after firmware changes.

## Safety rules

- Do not connect ESP32 pins, breadboard rails, or sensor wiring directly to
  DIHOOL controller motor terminals `M+` / `M−`.
- Do not modify, bypass, or replace the hardwired E3Z-R61 safety sensor or its
  `IN5` controller connection.
- Treat motor-output sensing as a separate, isolated, bench-tested circuit.
  Work with lift power disconnected while changing wiring.
- Preserve the DIHOOL controller as the authority for motor drive and stopping.
- Keep secrets only in `include/secrets.h`; never commit credentials or
  API tokens.
- Keep the local HTTP API read-only and LAN-only. Do not add port forwarding,
  UPnP, or internet exposure.
