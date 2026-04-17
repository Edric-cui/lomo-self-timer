# StickS3 Step-by-Step Tutorial

This tutorial walks through the full bring-up flow for the current repository:

1. prepare the Arduino toolchain
2. upload the IR capture sketch
3. learn the Lomo remote signal as exact `IrSymbol` data
4. run zero-film diagnostics to prove the TX path
5. compare the synthetic and legacy hypotheses
6. upload the self-timer sketch
7. validate with a strict low-film workflow

Use this guide together with [setup.md](./setup.md) if you need a shorter reference.

## What You Need

- `M5StickS3`
- `Lomo'Instant Wide` original IR lens-cap remote
- optional: `M5Stack IR Unit (U002)`
- USB-C cable with data support
- `Arduino IDE 2.x`
- `M5Stack` board package
- `M5Unified` library

If you are using `U002`, the **verified** pin mapping is:

- `GPIO 9` → IR TX (LED output)
- `GPIO 10` → IR RX (receiver input)
- red `5V`
- black `GND`

> **Note:** This is the reverse of what the M5Stack U002 datasheet implies.
> See [success_2026-04-18.md](./success_2026-04-18.md) for how this was discovered.

The firmware does not strict-auto-detect the `U002`. It lets you choose `Built-in` or `U002` on the device and remembers that choice across reboot.

## 1. Prepare Arduino IDE

1. Install `Arduino IDE 2.x`.
2. Open Arduino IDE settings.
3. Add the M5 board manager URL:

```text
https://static-cdn.m5stack.com/resource/arduino/package_m5stack_index.json
```

4. Open `Boards Manager`.
5. Search for `M5Stack`.
6. Install the package.
7. Open `Library Manager`.
8. Search for `M5Unified`.
9. Install it.

## 2. Connect the StickS3

1. Connect the StickS3 over USB-C.
2. In Arduino IDE, choose the new serial port.
   On macOS it is usually a device like `/dev/cu.usbmodem...`.
3. Select board `M5StickS3`.

If upload fails, retry using the official M5 download-mode flow for the StickS3.

## 3. Upload the Capture Sketch

1. Open [firmware/ir_capture/ir_capture.ino](/Users/edo/Documents/GitHub/lomo-self-timer/firmware/ir_capture/ir_capture.ino:1).
2. Click `Upload`.
3. Open `Serial Monitor`.
4. Set baud rate to `115200`.

What this sketch already does for you:

- disables the internal speaker amplifier so IR receive works correctly
- enables `EXT_5V` for the IR hardware
- listens on the selected IR receive backend
- rejects captures that are too large for the sender sketch to replay

## 4. Confirm the Capture Sketch Is Running

After boot, the StickS3 screen should show:

- `Lomo IR Capture`
- `Waiting for IR`
- `IR: Built-in` or `IR: U002`

If the screen is blank or Serial Monitor shows nothing:

1. press reset once
2. confirm the correct port is selected
3. confirm the Serial Monitor baud is `115200`

## 5. Learn the Original Lomo Remote

1. Point the original Lomo remote at the StickS3.
2. Keep a realistic short distance between them.
   Do not hold them directly against each other.
3. Press the `INSTANT` button once.

If you need to switch capture hardware:

1. Press `BtnA`
2. The screen should switch between `IR: Built-in` and `IR: U002`
3. The selected backend is saved and reused after reboot

If the capture is valid, Serial Monitor will print something like:

```cpp
=== Capture 1 ===
Symbol count: 34
// Paste into firmware/self_timer/self_timer.ino
static const IrSymbol kLearnedCapture1[] = {
  {9008, 4488, 1, 0},
  {591, 568, 1, 0},
  {538, 567, 1, 0},
};
constexpr size_t kLearnedCapture1Count = 34;
```

This is now the exact learned waveform, including both levels and durations. Do not convert it into a simpler duration-only list.

If the capture is too large, Serial Monitor will show a rejection message instead of a paste-ready block. In that case, do not paste it into the sender sketch.

Important: do not automatically treat this capture as the true replay waveform. On this project, capture output is a clue source first. With `U002`, a `38kHz` demodulating receiver may produce noisy output when listening to a remote that is actually closer to `33kHz`.

## 6. Capture the Same Button Multiple Times

Do not use the first capture blindly.

1. Press `INSTANT` three times total.
2. Compare the symbol counts and overall structure.
3. Pick a capture that looks consistent with the others.

You are looking for broad structure clues, not a guaranteed final replay array. Repeated appearances of short `~480us` marks and a `~7.1-7.4ms` gap are useful evidence even if the full frame remains noisy.

## 7. Upload the Diagnostics Sketch

1. Open [firmware/ir_diagnostics/ir_diagnostics.ino](/Users/edo/Documents/GitHub/lomo-self-timer/firmware/ir_diagnostics/ir_diagnostics.ino:1).
2. Upload it.
3. Confirm the screen shows:
   - `IR Diagnostics`
   - current `TX`
   - current `RX`
   - current `Mode`

Current controls:

- `BtnA` short press: change the currently focused field
- `BtnA` long press: move focus between `TX`, `RX`, and `Mode`
- `BtnB`: run the current diagnostic

Recommended starting pair:

- `TX: Built-in`
- `RX: U002`

Then swap:

- `TX: U002`
- `RX: Built-in`

## 8. Run the Zero-Film Gates

Run the diagnostics in this order.

### Gate 1: Beacon 33k Visibility

1. Set `Mode: Beacon 33k`.
2. Run it with `TX: Built-in`.
3. Point a phone camera at the emitter.
4. Look for three clear pulses.
5. Repeat with `TX: U002`.

This gate is only about proving that the selected TX path is visibly active on the current main hypothesis. Do not use camera film for this.

### Gate 2: Loop Synthetic 33k

1. Set `Mode: Loop Syn33k`.
2. Keep cross-backend wiring, for example `TX Built-in -> RX U002`.
3. Press `BtnB`.
4. Read the on-screen result and Serial Monitor output.

If you get no frame at all, treat that as a TX path, pin mapping, or power-path problem before suspecting anything else.

### Gate 3: Loop Legacy Raw

1. Set `Mode: Loop Legacy`.
2. Run the same cross-backend pair.
3. Compare its stability against `Loop Syn33k`.

If `Loop Syn33k` is clearly more stable than `Loop Legacy`, keep synthetic as the only recommended candidate. If `Loop Legacy` is unexpectedly stronger, only then raise the raw route back to candidate status.

### Gate 4: Beacon 38k Sanity Check

1. Set `Mode: Beacon 38k`.
2. Run it on both TX backends if you want a 38k reference check.

This mode is optional. It is not the main replay candidate.

## 9. Decide Whether Re-Capture Is Worth It

Do not re-capture by default.

Only do a limited re-capture session if:

1. `Loop Syn33k` fails
2. `Loop Legacy` also fails or is inconclusive
3. your earlier captures still show repeated `~480 / ~7.2ms / ~480` clues

If you do re-capture:

1. test indoors only
2. avoid sunlight, strong LED lighting, and phone notifications near the receiver
3. put the phone in airplane mode and move it away from the receiver
4. keep the remote and receiver `0.5m-1m` apart
5. short-press `INSTANT` once per try
6. capture `10-15` times
7. only look for repeated short marks and long gaps

Do not expect a perfect replay-ready raw array from `U002` if the original remote is actually off the receiver's preferred band.

## 10. Upload the Sender Sketch

1. Upload [firmware/self_timer/self_timer.ino](/Users/edo/Documents/GitHub/lomo-self-timer/firmware/self_timer/self_timer.ino:1).
2. Wait for the StickS3 to reboot.

The current proven configuration uses:

- 24-symbol captured waveform at `38kHz`
- `50%` carrier duty cycle
- `5×` repeats with `50ms` gap
- Dual TX (both BuiltIn and U002 fire simultaneously)

The screen should show:

- `Lomo Timer`
- `Dly:3s IR:Built-in` (or `U002`)
- `Code:Ready`
- `St:Ready`

## 11. Use the Self-Timer

Current controls:

- `BtnA` short press: cycle delay while idle
- `BtnA` long press: switch IR backend while idle
- `BtnB`: start countdown
- `BtnB` during countdown: cancel

Important detail: once the countdown has started, `BtnA` is intentionally locked. The displayed delay should always match the armed countdown.

During countdown the footer should show:

- `BtnA locked`
- `BtnB cancel`

When idle, the selected backend is shown on-screen and reused after reboot.

## 12. Test the Camera Trigger With a Film Budget

Only do this after diagnostics has shown that one candidate path is clearly stronger than the alternatives.

1. Place the camera where you can clearly see whether it fires.
2. Point the StickS3 at the camera’s IR receiver.
3. Set the delay to `3s`.
4. Start at about `0.5m`.
5. Press `BtnB`.
6. Wait for the countdown to finish.

Use at most one film shot per backend until you find a confirmed working path.

Expected on-screen status sequence:

1. `Counting down`
2. `Sending...`
3. `Done`

If the camera fires, the basic replay path is working.

## 13. Validate Real Shooting Distance

Do not stop after the first successful close-range trigger.

Test in this order:

1. `0.5m`
2. `1m`
3. `2m`
4. your actual tripod distance
5. both front and rear receiver positions on the camera, if applicable

You want reliable triggering under the same conditions you will use for actual shooting.

## 12. Troubleshooting

### Board uploads but capture never appears

- confirm `ir_capture.ino` is the uploaded sketch
- confirm Serial Monitor is set to `115200`
- press reset once after opening Serial Monitor
- check the remote battery
- try a slightly different angle and distance

### Capture works but sender says `Code too long`

- the learned frame exceeded the current replay limit
- capture again and compare repeated presses
- do not paste rejected captures into the sender sketch

### Sender shows `Done` but the camera does not fire

- try another one of the learned captures
- move closer to the camera
- change the aiming angle
- test both likely IR receiver locations on the camera

### Replay only works at impractically short distance

With dual TX mode (default), both BuiltIn and U002 fire simultaneously. If
range is still insufficient:

- ensure `kDualTxEnabled = true` in the firmware (this is the default)
- verify U002 is flashing by using `Beacon 38k` or `Sweep U002` in diagnostics
- consider hardware modifications (higher-power IR LED, external transistor driver, reflector)

## 13. Practical Workflow Tip

Keep notes for each learned capture:

- capture number
- symbol count
- whether the camera fired
- test distance

That will save time if you need to compare multiple learned frames.
