# Release Notes

## 2026-04 Open-Source Cleanup

This release reshapes the repo for public publishing while keeping the proven
IR trigger path intact.

### Included

- extracted the production replay payload and send parameters into
  `firmware/common/replay_profile.h`
- added public-facing validation notes in `docs/validation-notes.md`
- added `scripts/release_check.sh` and `docs/release-checklist.md` for compile,
  hygiene, and release checks
- added an MIT `LICENSE`
- expanded self-timer delay options to `3s`, `5s`, `10s`, `15s`, and `20s`
- updated countdown audio so `11-20s`, `4-10s`, and `2-3s` are audibly distinct

### Removed Or Consolidated

- removed dated backup sketches from the tracked public repo
- removed the old dated success record in favor of the shared replay profile,
  validation notes, and git history
- tightened ignore rules for private and temporary local files

### Verification

- `arduino-cli compile --fqbn m5stack:esp32:m5stack_sticks3 firmware/self_timer`
- `arduino-cli compile --fqbn m5stack:esp32:m5stack_sticks3 firmware/ir_capture`
- `arduino-cli compile --fqbn m5stack:esp32:m5stack_sticks3 firmware/ir_diagnostics`
- `bash scripts/release_check.sh`
