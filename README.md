# lomo-self-timer

Battery-powered IR self-timer for the `Lomo'Instant Wide`, built around the `M5StickS3`.

The project goal is narrow on purpose:

- learn the IR signal from the original Lomo lens-cap remote
- save a known-good raw capture
- trigger that signal after a local countdown
- run fully offline on the M5StickS3 battery

## Why This Repo Exists

The risky part of this project is not the UI or the countdown logic. It is the IR learning and replay path.

So the repository starts with two separate Arduino sketches:

- `firmware/ir_capture/ir_capture.ino`
  Captures raw IR timings from the original remote and prints a paste-ready C array to Serial.
- `firmware/self_timer/self_timer.ino`
  A countdown sender scaffold for the M5StickS3. It is ready for learned raw timings, but does not assume the Lomo code is already known.

That split keeps bring-up simple:

1. prove the board, IR receiver, and toolchain work
2. prove the captured raw timings are stable
3. only then wire the capture into the sender UX

## Hardware Target

- `M5StickS3`
- `Lomo'Instant Wide` original IR lens-cap remote
- optional fallback: `M5Stack IR Unit (U002)` if the built-in IR path turns out unstable in your real shooting setup

## Current Interaction Model

For the first sender scaffold:

- `BtnA`: cycle delay (`3s`, `5s`, `10s`)
- `BtnB`: start countdown
- `BtnB` again while counting down: cancel

This keeps the first working build dead simple. More complex interactions like long-press learn mode, Bulb/TIME support, and NVS persistence can layer on after raw replay is validated.

## Quick Start

1. Install `Arduino IDE 2.x`.
2. Install the `M5Stack` board package and select `M5StickS3`.
3. Install the `M5Unified` library.
4. Open `firmware/ir_capture/ir_capture.ino`.
5. Upload it to the board and open Serial Monitor at `115200`.
6. Point the original Lomo remote at the StickS3 and press `INSTANT`.
7. Copy the printed raw durations into `firmware/self_timer/self_timer.ino`.
8. Set `kHasInstantCode` to `true`, rebuild, and test the sender sketch.

## Repository Layout

```text
.
├── docs/
│   └── setup.md
└── firmware/
    ├── ir_capture/
    │   └── ir_capture.ino
    └── self_timer/
        └── self_timer.ino
```

## Implementation Notes

- Primary target: `M5StickS3` built-in IR first
- Fallback: use `M5Stack IR Unit (U002)` only if built-in capture or replay is not reliable enough
- First revision runtime states: `idle`, `countdown`, `sending`, `done`, `error`
- First revision non-goals: Bulb/TIME support, NVS persistence, deep sleep, and a more complex menu system

## Milestones

- `M0`: toolchain boots, screen works, buttons work
- `M1`: raw capture from the original remote is stable across repeated presses
- `M2`: hardcoded raw replay triggers the camera reliably
- `M3`: countdown UX is usable on battery power
- `M4`: learned code persistence and optional TIME/Bulb support

## Notes

- The StickS3 built-in IR path needs `EXT_5V` enabled.
- StickS3 IR receive should use the ESP32 `RMT` peripheral.
- StickS3 IR receive conflicts with the internal speaker amplifier, so the capture sketch disables it.

Those constraints are documented in `docs/setup.md` and reflected in the starter sketches.
