# Setup Notes

## Toolchain

- `Arduino IDE 2.x`
- `M5Stack` board package
- board selection: `M5StickS3`
- library: `M5Unified`

The starter sketches intentionally avoid a large dependency stack. They use the ESP32 `RMT` driver directly for IR instead of layering in extra protocol libraries on day one.

## Hardware Assumptions

These repo defaults match the current official StickS3 IR documentation:

- IR receive pin: `GPIO 42`
- IR send pin: `GPIO 46`
- IR send/receive power requires `EXT_5V` to be enabled
- IR receive should use `RMT`, not plain GPIO polling
- IR receive requires the speaker amplifier to be disabled

## Upload Notes

Typical Arduino flow:

1. Connect the StickS3 by USB-C.
2. Select the correct serial port.
3. Select board `M5StickS3`.
4. Upload the chosen sketch.

If the board does not upload normally, retry with the device in download mode using the official M5 process for StickS3.

## Capture Workflow

Use `firmware/ir_capture/ir_capture.ino` first.

What success looks like:

- the screen says it is waiting for IR
- Serial Monitor shows one or more captures
- each capture prints a paste-ready `uint16_t` array
- repeated presses of the same Lomo button look broadly consistent

Use the original remote at a realistic distance and angle. The M5 documentation notes that too-close IR alignment can also cause abnormal reception, so do not test with the remotes touching each other.

## Sender Workflow

After capture:

1. open `firmware/self_timer/self_timer.ino`
2. replace the placeholder timings with the learned raw durations
3. set `kHasInstantCode` to `true`
4. upload and test replay against the camera

Start with short-range tests before assuming anything about longer shooting distances.

## Recommended Validation Sequence

1. Capture `INSTANT` three times.
2. Compare the printed durations and look for stable structure.
3. Replay at `0.5m`.
4. Replay at `1m`, `2m`, and your real tripod distance.
5. Try both front and rear camera receiver windows.

## Fallback Trigger

Consider the `U002` add-on only if:

- capture is unreliable
- replay works only at impractically short range
- the emitter needs to be positioned separately from the screen and buttons
