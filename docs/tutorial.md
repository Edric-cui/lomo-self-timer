# StickS3 Bring-Up Tutorial

This walkthrough covers the full repository flow:

1. prepare the toolchain
2. capture timing clues from the original remote
3. validate TX behavior without spending film
4. upload the production self-timer
5. verify trigger reliability on `Lomo'Instant Wide Glass`

Use this together with [setup.md](./setup.md) if you want a shorter reference.

## What You Need

- `M5StickS3`
- `Lomo'Instant Wide Glass`
- original Lomo lens-cap remote
- optional `M5Stack IR Unit (U002)`
- USB-C cable with data support
- `Arduino IDE 2.x`
- `M5Stack` board package
- `M5Unified`

This workflow is validated on `Wide Glass`. Other `Lomo'Instant Wide` variants
are likely applicable, but have not been verified yet.

If you are using `U002`, the verified pin mapping is:

- `GPIO 9` -> IR TX
- `GPIO 10` -> IR RX
- red -> `5V`
- black -> `GND`

## 1. Prepare Arduino IDE

1. Install `Arduino IDE 2.x`.
2. Add the M5 board manager URL:

```text
https://static-cdn.m5stack.com/resource/arduino/package_m5stack_index.json
```

3. Install the `M5Stack` board package.
4. Install the `M5Unified` library.
5. Select board `M5StickS3`.

If upload fails, retry with the official StickS3 download-mode flow.

## 2. Capture Timing Clues

1. Open `firmware/ir_capture/ir_capture.ino`.
2. Upload it.
3. Open Serial Monitor at `115200`.
4. Point the original remote at the StickS3.
5. Press the `INSTANT` button.

What to expect:

- the screen shows `Lomo IR Capture`
- `BtnA` switches receive backend between `Built-in` and `U002`
- the selected receive backend is saved across reboot
- valid captures print `IrSymbol` arrays to Serial Monitor

Treat capture output as clue data, not automatic ground truth. With `U002`, the
hardware-demodulating receiver can still produce noisy raw captures if the
original remote is not centered on its preferred band.

Take several captures of the same button and compare the broad structure. In
this repo, repeated short marks plus a long `~7.2ms` gap were more useful than
any single "perfect" capture.

## 3. Run Zero-Film Diagnostics

1. Open `firmware/ir_diagnostics/ir_diagnostics.ino`.
2. Upload it.
3. Use `BtnA` short press to change the focused setting.
4. Use `BtnA` long press to move focus between `TX`, `RX`, and `Mode`.
5. Use `BtnB` to run the active diagnostic.

Recommended order:

1. `Beacon 38k` on `Built-in`
2. `Beacon 38k` on `U002`
3. `Sweep U002` if the external LED does not flash
4. optional loopback comparisons if you want more confidence in TX/RX routing

Use a phone camera to confirm that the selected TX path is visibly active before
spending film.

## 4. Upload the Production Self-Timer

1. Open `firmware/self_timer/self_timer.ino`.
2. Upload it.
3. Wait for the device to reboot.

The production sketch uses the shared profile in
`firmware/common/replay_profile.h`:

- 24-symbol waveform
- `38kHz` carrier
- `50%` duty cycle
- `5` repeats with `50ms` gap
- dual TX on both built-in IR and `U002`

Current self-timer controls:

- `BtnA`: cycle delay (`3s`, `5s`, `10s`, `15s`, `20s`) while idle
- `BtnB`: start countdown
- `BtnB` during countdown: cancel

The production sender does not expose backend switching on-device. Dual TX is
the fixed production behavior in the current repo.

## 5. Validate the Camera Trigger

1. Point the StickS3 toward the camera's IR receiver.
2. Set the delay to `3s`.
3. Start around `40cm`.
4. Press `BtnB`.
5. Watch for the status flow:
   - `Counting down`
   - `Sending...`
   - `Done`

Once you have a confirmed trigger, repeat the same setup several times before
changing distance or angle.

## Troubleshooting

### Capture sketch shows no useful output

- confirm `ir_capture.ino` is uploaded
- confirm Serial Monitor is at `115200`
- press reset once after opening Serial Monitor
- try a different angle or short distance
- check the remote battery

### Diagnostics can send a beacon but the camera still does not fire

- confirm you uploaded `self_timer.ino`, not `ir_diagnostics.ino`
- move back to the validated `~40cm` range
- aim more directly at the camera receiver window
- retry with the same alignment that worked during your best test

### Range is still too short

The current repo has already applied the main software-side changes:

- `38kHz` replay profile
- `50%` duty cycle
- `5` repeats
- dual TX

If you still need more distance, focus on hardware improvements next:

- stronger IR LED drive
- a higher-power emitter
- improved aiming optics or reflector geometry
