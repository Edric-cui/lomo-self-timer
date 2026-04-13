# Design: Lomo Self Timer

## Overview

This project turns an `M5StickS3` into a dedicated, offline IR self-timer for the `Lomo'Instant Wide`.

The system is intentionally small:

- local buttons for input
- local display for state
- local countdown timer
- learned IR replay for shutter trigger

No Wi-Fi, cloud, phone dependency, or general-purpose smart-home integration is part of the core scope.

## Requirements

### Functional

- Learn at least one IR signal from the original Lomo remote.
- Replay that signal on demand from the M5StickS3.
- Allow delay selection directly on-device.
- Show enough on-screen state to avoid blind triggering.
- Stay portable and battery-friendly.

### Non-Functional

- Prefer official or low-risk hardware APIs over clever abstractions.
- Keep the first implementation debuggable over Serial.
- Optimize for reliable trigger distance over feature breadth.
- Preserve a fallback path to an external IR module if built-in IR is insufficient.

## Options Considered

### Option A: M5StickS3 built-in IR first

Pros:

- one-device solution
- battery included
- smaller and cleaner hardware package
- closest fit to the actual use case

Cons:

- less margin if the built-in emitter/receiver proves weak for this camera
- the built-in IR path has power and speaker-related constraints

### Option B: M5StickC Plus2 + external IR module

Pros:

- explicit external IR module from day one
- more familiar from older examples

Cons:

- worse BOM and more wiring
- older hardware direction
- not the current best-fit device

### Option C: Smart-home remote ecosystem

Pros:

- little or no firmware work

Cons:

- wrong shape for an offline pocket self-timer
- ecosystem constraints
- weak local UX for camera shooting

## Chosen Direction

Use `M5StickS3` built-in IR as the primary path.

Keep `M5Stack IR Unit (U002)` as a fallback only if one of these happens:

- capture is unstable
- replay range is too short
- alignment is too sensitive
- a separate emitter position would materially improve reliability

## Architecture

### Inputs

- `BtnA`: cycle delay
- `BtnB`: start or cancel countdown

### Runtime States

- `idle`
- `countdown`
- `sending`
- `done`
- `error`

### Firmware Modules

- `Board setup`
  Initializes M5, display, power, and button input.
- `IR capture`
  Uses ESP32 `RMT RX` to record raw durations.
- `IR send`
  Uses ESP32 `RMT TX` with a 38 kHz carrier to replay raw timings.
- `UI state`
  Renders delay, code readiness, countdown, and status text.

## Implementation Plan

1. Bootstrap the repository with docs and separate sketches.
2. Validate raw capture with the original Lomo remote.
3. Paste the captured timings into the sender scaffold.
4. Validate short-range replay against the camera.
5. Only after replay is stable, add persistence and learn mode.

## Verification Gates

- `Gate 1`: repeated captures from the same button are consistent enough to trust.
- `Gate 2`: replay succeeds `5/5` times at close range.
- `Gate 3`: replay succeeds at your actual shooting distances and angles.
- `Gate 4`: only after Gates 1-3 should extra UX work be treated as worth doing.

## Explicit Non-Goals For The First Revision

- Bulb/TIME support
- NVS persistence
- sleep/wake optimization
- polished menu system
- protocol detection beyond what is useful for debugging

## Risks

- Built-in IR power or alignment may be insufficient for real-world use.
- Learned raw timings may vary enough that replay needs small adjustments.
- Arduino examples for nearby M5 devices are easy to mix up, so this repo should stay device-specific.
