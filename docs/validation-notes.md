# Validation Notes

This file records the currently proven trigger profile for this repository.

## Validated Target

- camera used in the recorded test: `Lomo'Instant Wide Glass`
- controller: `M5StickS3`
- optional external emitter: `M5Stack IR Unit (U002)`

This record reflects one confirmed `Wide Glass` test setup. Other
`Lomo'Instant Wide` variants may be compatible, but have not been verified in
this repo yet.

## 2026-04-18 Confirmed Working Profile

- replay source: `firmware/common/replay_profile.h`
- TX mode: dual TX with both built-in IR and `U002`
- carrier: `38000 Hz`
- duty cycle: `50%`
- repeats: `5`
- repeat gap: `50ms`
- observed successful distance in the recorded test setup: approximately `40cm`

## Audio Behavior

- countdown beeps are enabled
- the final `1 second` beep is intentionally skipped
- start, cancel, and result tones are enabled

## U002 Pin Mapping

Verified on the StickS3 Grove adapter:

- `GPIO 9` -> IR TX
- `GPIO 10` -> IR RX

This is the reverse of what the M5Stack U002 datasheet implies.

## Notes

- `U002` is confirmed as part of the working path.
- The loopback test can still time out on U002 TX -> RX because the LED and
  receiver face the same direction on the module.
- The current working profile is a captured waveform, not a synthesized
  protocol implementation.
- The public rollback baseline for this repo is
  `firmware/common/replay_profile.h` together with this validation note and git
  history, rather than a dated backup sketch kept in-tree.

## Failed Alternatives Already Tested

These did not trigger the validated camera target:

- `33kHz` short synthetic double pulse
- `38kHz` long smoothed synthetic candidate
- `33kHz` version of the same long synthetic candidate

## Current Limitation

The repo currently has one confirmed path around `40cm` in the recorded test
setup, but longer reliable distance will likely require hardware-side changes
rather than more parameter tuning alone.
