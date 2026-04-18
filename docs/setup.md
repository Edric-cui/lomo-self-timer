# Setup Notes

Use this page as the short reference. For the full bring-up flow, see
[tutorial.md](./tutorial.md).

## Toolchain

- `Arduino IDE 2.x`
- `M5Stack` board package
- board selection: `M5StickS3`
- library: `M5Unified`

The sketches intentionally keep dependencies small and use the ESP32 `RMT`
driver directly for IR work.

### `arduino-cli` Checks

Validated locally with:

- `arduino-cli 1.4.1`
- FQBN: `m5stack:esp32:m5stack_sticks3`

```sh
arduino-cli compile --fqbn m5stack:esp32:m5stack_sticks3 firmware/self_timer
arduino-cli compile --fqbn m5stack:esp32:m5stack_sticks3 firmware/ir_capture
arduino-cli compile --fqbn m5stack:esp32:m5stack_sticks3 firmware/ir_diagnostics
```

The repo also includes `scripts/release_check.sh`, which runs these compile
checks together with a small privacy and release-hygiene scan.

## Hardware

Validated target:

- `M5StickS3`
- `Lomo'Instant Wide Glass`
- original Lomo lens-cap remote
- optional `M5Stack IR Unit (U002)`

This profile is validated on `Wide Glass`. Other `Lomo'Instant Wide` variants
are likely compatible, but not yet verified.

### Built-In IR Defaults

- IR receive pin: `GPIO 42`
- IR send pin: `GPIO 46`
- `EXT_5V` must be enabled for the IR path
- IR receive should use `RMT`
- IR receive requires the speaker amplifier to be disabled

### U002 Pin Mapping

Verified on the StickS3 Grove adapter:

- `GPIO 9` -> IR TX
- `GPIO 10` -> IR RX
- red -> `5V`
- black -> `GND`

This is the reverse of what the M5Stack U002 datasheet suggests. See
[validation-notes.md](./validation-notes.md) for the confirmation record.

## Sketch Roles

- `firmware/ir_capture/ir_capture.ino`
  Captures timing clues from the original remote. `BtnA` switches receive
  backend and the selected backend is persisted.
- `firmware/ir_diagnostics/ir_diagnostics.ino`
  Validates TX and RX behavior without spending film. TX and RX backends are
  selected independently and persisted.
- `firmware/self_timer/self_timer.ino`
  Production sender. `BtnA` cycles delay, `BtnB` starts or cancels the
  countdown, and dual TX is always enabled in the current production path.

## Validated Replay Profile

The current shared production profile lives in
`firmware/common/replay_profile.h`:

- payload: 24-symbol captured waveform
- carrier: `38kHz`
- duty cycle: `50%`
- repeats: `5`
- repeat gap: `50ms`

## Recommended Validation Sequence

1. Upload `ir_capture` and confirm you can switch receive backends.
2. Upload `ir_diagnostics` and confirm beacon visibility on the TX path you
   plan to use.
3. If `U002` does not flash, run `Sweep U002` to confirm the wiring.
4. Upload `self_timer`.
5. Start camera tests around `40cm` and adjust from there.

## Film Budget Rule

- confirm beacon visibility before spending film
- use one shot per configuration until a working path is confirmed
- once confirmed, verify repeatability at your real shooting distance
