# lomo-self-timer

Battery-powered IR self-timer for the `Lomo'Instant Wide Glass`, built around
the `M5StickS3`.

This repo stays intentionally narrow:

- learn timing clues from the original Lomo lens-cap remote
- validate the TX path without spending film
- replay a proven IR profile after a local countdown
- run fully offline on the StickS3 battery

## What It Is

`lomo-self-timer` is a small hardware-first firmware repo for Lomo users who
want a self-timer without relying on a phone or network connection.

The current workflow is:

1. capture IR clues from the original remote
2. validate TX behavior with diagnostics
3. run the production self-timer with a proven replay profile

## Validated Hardware Target

- `M5StickS3`
- `Lomo'Instant Wide Glass` used in the current confirmed test setup
- original Lomo lens-cap IR remote for signal learning
- optional `M5Stack IR Unit (U002)` via Grove adapter

The current confirmed trigger test was run on a `Wide Glass` body. Other
`Lomo'Instant Wide` variants may behave similarly, but that has not been
verified in this repo yet.

## Current Proven Result

Confirmed on `2026-04-18`:

- proven replay profile: 24-symbol captured waveform at `38kHz`
- output mode: dual TX with both the built-in emitter and `U002`
- observed successful distance in the recorded test setup: approximately `40cm`
- audio feedback: countdown beeps plus start/cancel/result tones

See [validation notes](docs/validation-notes.md) for the recorded setup and
measured behavior.

## Quick Start

1. Install `Arduino IDE 2.x`.
2. Install the `M5Stack` board package and select `M5StickS3`.
3. Install the `M5Unified` library.
4. Upload `firmware/self_timer/self_timer.ino`.
5. Aim the StickS3 toward the camera's IR receiver at about `40cm`.
6. Press `BtnB` to start the countdown.

If you are bringing up new hardware, start with
[docs/setup.md](docs/setup.md) and then follow the full
[tutorial](docs/tutorial.md).

## Repository Layout

```text
.
├── docs/
│   ├── release-checklist.md      # Publish gates for future feature releases
│   ├── setup.md                  # Short hardware and toolchain reference
│   ├── tutorial.md               # Full capture -> diagnostics -> sender flow
│   └── validation-notes.md       # Confirmed working profile and test notes
├── firmware/
    ├── common/
    │   ├── ir_backend.h          # Pin mapping and backend preferences
    │   ├── ir_frame.h            # Shared IrSymbol type
    │   └── replay_profile.h      # Validated replay payload and send params
    ├── ir_capture/
    │   └── ir_capture.ino        # Capture timing clues from the original remote
    ├── ir_diagnostics/
    │   └── ir_diagnostics.ino    # Zero-film TX/RX validation
    └── self_timer/
        └── self_timer.ino        # Production countdown + trigger path
└── scripts/
    └── release_check.sh          # Compile + privacy + release hygiene checks
```

## Known Limitations

- The current public validation record is based on one confirmed `Lomo'Instant
  Wide Glass` setup.
- Other `Lomo'Instant Wide` variants may be applicable, but are still
  unverified here.
- The recorded successful distance is around `40cm` for that test setup, not a
  universal guarantee.
- Further distance improvement is expected to come from hardware changes, not
  from more software tuning alone.
- `arduino-cli` compile checks are documented and were validated locally for
  the current three-sketch workflow, but hardware flashing and on-device
  behavior still need real device verification.

## Next Improvements

- factor the remaining shared RMT TX helpers out of `ir_diagnostics` and
  `self_timer`
- add a cleaner path for swapping replay profiles without editing the sender
  logic directly
- validate whether the same profile works unchanged on other `Wide` variants
- improve range through hardware changes such as a stronger emitter or driver

For future feature work such as `Bulb Control`, use
[docs/release-checklist.md](docs/release-checklist.md) together with
`scripts/release_check.sh` before publishing.
