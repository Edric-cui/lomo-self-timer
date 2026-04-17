# lomo-self-timer

Battery-powered IR self-timer for the `Lomo'Instant Wide`, built around the `M5StickS3`.

The project goal is narrow on purpose:

- learn the IR signal from the original Lomo lens-cap remote
- replay that signal after a local countdown to trigger the shutter
- run fully offline on the M5StickS3 battery

## Current Status

**Camera trigger is confirmed working** as of 2026-04-18.

- Proven payload: 24-symbol captured waveform at `38kHz`
- Dual TX: both the built-in IR and external `U002` fire simultaneously
- Effective range: approximately `15cm` (software-side optimizations exhausted; further range requires hardware changes)
- See [docs/success_2026-04-18.md](docs/success_2026-04-18.md) for full details

## Hardware Target

- `M5StickS3`
- `Lomo'Instant Wide` original IR lens-cap remote (for learning the signal)
- `M5Stack IR Unit (U002)` connected via Grove adapter

## U002 Pin Mapping

> **Important:** The actual U002 pin mapping on the StickS3 Grove port is the
> **reverse** of what the M5Stack datasheet implies.

| Function | GPIO | Wire color (per datasheet) |
|---|---|---|
| IR TX (LED output) | `GPIO 9` | white |
| IR RX (receiver input) | `GPIO 10` | yellow |

This was verified by a blind pin-sweep test. See [docs/success_2026-04-18.md](docs/success_2026-04-18.md).

## Current Interaction Model

- `BtnA` tap: cycle delay (`3s`, `5s`, `10s`) while idle
- `BtnA` long-press: switch primary IR backend label (`Built-in` / `U002`) while idle
- `BtnB`: start countdown
- `BtnB` during countdown: cancel

With dual TX enabled (default), both emitters fire regardless of the UI label.

## Quick Start

1. Install `Arduino IDE 2.x`.
2. Install the `M5Stack` board package and select `M5StickS3`.
3. Install the `M5Unified` library.
4. Upload `firmware/self_timer/self_timer.ino`.
5. Aim the StickS3 at the camera's IR receiver at about `15cm`.
6. Press `BtnB` to start the countdown.

For the full bring-up walkthrough (capture → diagnostics → self-timer), see [docs/tutorial.md](docs/tutorial.md).

## Repository Layout

```text
.
├── docs/
│   ├── setup.md                   # Hardware setup and workflows
│   ├── success_2026-04-18.md      # Confirmed working config + pin discovery
│   └── tutorial.md                # Full step-by-step bring-up guide
└── firmware/
    ├── backups/                   # Known-good firmware snapshots
    ├── common/
    │   ├── ir_backend.h           # Pin mapping and backend selection
    │   └── ir_frame.h             # IrSymbol data type
    ├── ir_capture/
    │   └── ir_capture.ino         # Learn original remote signals
    ├── ir_diagnostics/
    │   └── ir_diagnostics.ino     # Zero-film hardware validation
    └── self_timer/
        └── self_timer.ino         # Production countdown + trigger
```

## Implementation Notes

- Primary target: M5StickS3
- Dual TX mode fires both BuiltIn (GPIO 46) and U002 (GPIO 9) simultaneously
- Carrier: `38kHz`, duty cycle `50%`, 5× repeat with `50ms` gap
- The payload is a captured waveform from the original Lomo remote, not a synthesized protocol
- Backend selection is done on-device and remembered across reboot
- Diagnostics has independent persisted TX and RX backend selection
- Diagnostics includes a `Sweep U002` mode for blind GPIO pin identification

## Milestones

- `M0`: ✅ toolchain boots, screen works, buttons work
- `M1`: ✅ raw capture from the original remote yields repeatable timing clues
- `M2`: ✅ diagnostics prove TX paths emit IR visible to phone camera
- `M3`: ✅ captured waveform replay triggers the camera
- `M4`: ✅ countdown UX is usable on battery power with audio feedback
- `M5`: learned code persistence and optional TIME/Bulb support (future)

## Notes

- The StickS3 built-in IR path needs `EXT_5V` enabled.
- StickS3 IR receive should use the ESP32 `RMT` peripheral.
- StickS3 IR receive conflicts with the internal speaker amplifier, so the capture sketch disables it.
- U002 uses a `38kHz` hardware-demodulating receiver; not ideal for capture of remotes on other frequencies.
- U002 loopback tests always time out because the TX LED and RX sensor face the same direction on the module — this is expected, not a fault.
- The recommended validation order is `capture → diagnostics → self_timer`, not direct camera testing after capture.
