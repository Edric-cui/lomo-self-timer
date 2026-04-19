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


