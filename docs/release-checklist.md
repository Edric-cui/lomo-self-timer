# Release Checklist

Use this checklist before publishing any new public feature branch or merge.

## Every Release

1. Run `scripts/release_check.sh`.
2. Confirm all three sketches still compile:
   - `firmware/ir_capture`
   - `firmware/ir_diagnostics`
   - `firmware/self_timer`
3. Confirm docs match real device behavior.
4. Confirm tracked files do not contain local paths, private draft references,
   or system junk.
5. Update `docs/validation-notes.md` only with results that were actually
   tested on hardware.

## Bulb Control Release Gate

Publish `Bulb Control` only when all of the following are true:

1. The behavior is documented as a user-facing capability, not just an
   experiment in code comments.
2. The trigger model is explicit:
   - how Bulb starts
   - how Bulb stops
   - what button interactions are allowed while active
   - what happens on cancel, timeout, or reboot
3. The safety model is explicit:
   - no accidental long exposure from a casual button press
   - clear on-screen state during Bulb activity
   - a predictable stop path if the user changes their mind
4. The validation scope is explicit:
   - validated on `Lomo'Instant Wide Glass`
   - any assumptions about other `Wide` variants are still marked unverified
5. The repo stays clean:
   - no dated backup sketches
   - no private notes or local experiment artifacts
   - no stale docs describing behavior that the firmware no longer has

## Recommended Publish Shape For Bulb Control

Use a clean release in this order:

1. implement the feature
2. verify compile for all sketches
3. test on device
4. update docs
5. update validation notes with only confirmed behavior
6. run `scripts/release_check.sh` again
7. publish
