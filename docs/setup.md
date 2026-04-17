# Setup Notes

For a full step-by-step walkthrough, see [tutorial.md](/Users/edo/Documents/GitHub/lomo-self-timer/docs/tutorial.md:1).

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

For `U002` mode, the **verified** pin mapping on the StickS3 Grove adapter is:

- `GPIO 9` → IR TX (LED output)
- `GPIO 10` → IR RX (receiver input)
- red `5V`
- black `GND`

> **Note:** This is the opposite of what the M5Stack U002 datasheet implies
> (yellow `TX` → GPIO 10, white `RX` → GPIO 9). The actual mapping was
> confirmed by a blind pin-sweep test on 2026-04-18. See
> [success_2026-04-18.md](./success_2026-04-18.md) for details.

The unit is passive, so strict plug-and-auto-detect is not implemented. The firmware exposes an on-device backend selector and remembers the chosen backend across reboot.

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
- each valid capture prints a paste-ready `IrSymbol` array and count
- repeated presses of the same Lomo button look broadly consistent
- oversize captures are rejected instead of being offered for replay
- `BtnA` switches between `Built-in` and `U002` receive backends

Use the original remote at a realistic distance and angle. The M5 documentation notes that too-close IR alignment can also cause abnormal reception, so do not test with the remotes touching each other.

Important interpretation change:

- treat capture output as a clue source, not automatic ground truth
- with `U002`, you are using a `38kHz` hardware-demodulating receiver
- if the original remote is closer to `33kHz`, high-fidelity raw capture quality may be poor by design
- repeated appearances of a short mark and a `~7.1-7.4ms` gap are useful clues, even if the full frame is noisy

## Zero-Film Diagnostics Workflow

Before any camera test, use `firmware/ir_diagnostics/ir_diagnostics.ino`.

What this sketch does:

- lets you choose `TX backend` and `RX backend` independently
- remembers those choices across reboot
- provides `Beacon 33k` and `Beacon 38k` for phone-camera confirmation
- provides `Loop Synthetic 33k` and `Loop Legacy Raw` for cross-backend comparison
- provides `Sweep U002` for blind pin scanning across GPIO 10, 9, 20, 19

Controls:

- `BtnA` tap: change the focused setting
- `BtnA` hold: move focus between `TX`, `RX`, and `Mode`
- `BtnB`: run the current test

### Sweep U002 Mode

This mode is for hardware debugging when you cannot confirm which GPIO pin
actually drives the U002 TX LED. It:

- sequentially fires a 38kHz beacon on GPIO 10, 9, 20, 19
- shows a 2-second `NEXT: Pin X` warning before each pin starts firing
- fires 12 bursts per pin with 500ms gaps
- pauses 3 seconds between pins
- You observe the IR LED through a phone camera to identify the active pin

## Sender Workflow

The self-timer firmware (`firmware/self_timer/self_timer.ino`) is the
production sketch.

Current proven configuration:

- Payload: 24-symbol captured waveform at `38kHz`
- Carrier duty cycle: `50%`
- Send repeats: `5×` with `50ms` gap
- Dual TX: both `BuiltIn` (GPIO 46) and `U002` (GPIO 9) fire simultaneously

Controls:

- `BtnA` tap: cycle delay (`3s`, `5s`, `10s`) while idle
- `BtnA` long press: switch primary IR backend while idle (both still fire in dual TX mode)
- `BtnB`: start countdown
- `BtnB` during countdown: cancel

Start with short-range tests before assuming anything about longer shooting distances.

## Recommended Validation Sequence

1. Run `Beacon 38k` on `BuiltIn` and confirm visible flashing in a phone camera.
2. Run `Beacon 38k` on `U002` and confirm visible flashing.
3. If U002 does not flash, run `Sweep U002` to verify pin mapping.
4. Upload `self_timer`, set to dual TX mode.
5. Test camera trigger at `15cm`.
6. If that works, test at increasing distances.

## Film Budget Rule

- do not spend film before beacon visibility is confirmed on at least one backend
- once beacon is confirmed, test one camera shot at close range
- allow at most one film shot per test configuration until you have a confirmed success path

## Fallback Trigger

Consider the `U002` add-on if:

- replay works only at impractically short range from the built-in emitter
- the emitter needs to be positioned separately from the screen and buttons

With dual TX mode enabled, both emitters fire regardless of the UI selection,
so there is no need to choose between them for trigger purposes.
