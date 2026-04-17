# StickS3 Step-by-Step Tutorial

This tutorial walks through the full bring-up flow for the current repository:

1. prepare the Arduino toolchain
2. upload the IR capture sketch
3. learn the Lomo remote signal as exact `IrSymbol` data
4. paste that learned signal into the self-timer sketch
5. upload the self-timer sketch
6. validate that the camera triggers reliably

Use this guide together with [setup.md](./setup.md) if you need a shorter reference.

## What You Need

- `M5StickS3`
- `Lomo'Instant Wide` original IR lens-cap remote
- USB-C cable with data support
- `Arduino IDE 2.x`
- `M5Stack` board package
- `M5Unified` library

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
- listens on the StickS3 built-in IR receive pin
- rejects captures that are too large for the sender sketch to replay

## 4. Confirm the Capture Sketch Is Running

After boot, the StickS3 screen should show:

- `Lomo IR Capture`
- `Waiting for IR`
- `Open Serial @115200`

If the screen is blank or Serial Monitor shows nothing:

1. press reset once
2. confirm the correct port is selected
3. confirm the Serial Monitor baud is `115200`

## 5. Learn the Original Lomo Remote

1. Point the original Lomo remote at the StickS3.
2. Keep a realistic short distance between them.
   Do not hold them directly against each other.
3. Press the `INSTANT` button once.

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

## 6. Capture the Same Button Multiple Times

Do not use the first capture blindly.

1. Press `INSTANT` three times total.
2. Compare the symbol counts and overall structure.
3. Pick a capture that looks consistent with the others.

You are looking for broadly similar waveform structure, not perfectly identical numbers down to the microsecond.

## 7. Paste the Learned Signal Into the Sender Sketch

1. Open [firmware/self_timer/self_timer.ino](/Users/edo/Documents/GitHub/lomo-self-timer/firmware/self_timer/self_timer.ino:1).
2. Find the placeholder block near the top:

```cpp
constexpr bool kHasInstantCode = false;
static const IrSymbol kInstantSymbols[] = {
    {9008, 4488, 1, 0},
    {591, 568, 1, 0},
    {538, 567, 1, 0},
    {565, 567, 1, 0},
};
constexpr size_t kInstantSymbolCount =
    sizeof(kInstantSymbols) / sizeof(kInstantSymbols[0]);
```

3. Replace `kInstantSymbols[]` with one of the learned `kLearnedCapture...` arrays from Serial Monitor.
4. Replace `kInstantSymbolCount` with the learned count.
5. Change:

```cpp
constexpr bool kHasInstantCode = false;
```

to:

```cpp
constexpr bool kHasInstantCode = true;
```

After editing, the sender sketch should contain the exact `IrSymbol` data from the capture sketch.

## 8. Upload the Sender Sketch

1. Upload [firmware/self_timer/self_timer.ino](/Users/edo/Documents/GitHub/lomo-self-timer/firmware/self_timer/self_timer.ino:1).
2. Wait for the StickS3 to reboot.

The screen should show:

- `Lomo Self Timer`
- `Delay: 3s`, `5s`, or `10s`
- `Code: Ready`
- `Status: Ready`

If it shows `Code: Missing`, you did not change `kHasInstantCode` to `true`.

## 9. Use the Self-Timer

Current controls:

- `BtnA`: cycle delay while idle
- `BtnB`: start countdown
- `BtnB` during countdown: cancel

Important detail: once the countdown has started, `BtnA` is intentionally locked. The displayed delay should always match the armed countdown.

During countdown the footer should show:

- `BtnA locked`
- `BtnB cancel`

## 10. Test the Camera Trigger

Start with the easiest possible test.

1. Place the camera where you can clearly see whether it fires.
2. Point the StickS3 at the camera’s IR receiver.
3. Set the delay to `3s`.
4. Start at about `0.5m`.
5. Press `BtnB`.
6. Wait for the countdown to finish.

Expected on-screen status sequence:

1. `Counting down`
2. `Sending...`
3. `Done`

If the camera fires, the basic replay path is working.

## 11. Validate Real Shooting Distance

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

Use the built-in StickS3 IR path first, but consider the `M5Stack IR Unit (U002)` fallback if:

- capture is unreliable
- replay range is too short for real use
- emitter placement needs to be separate from the StickS3 body

## 13. Practical Workflow Tip

Keep notes for each learned capture:

- capture number
- symbol count
- whether the camera fired
- test distance

That will save time if you need to compare multiple learned frames.
