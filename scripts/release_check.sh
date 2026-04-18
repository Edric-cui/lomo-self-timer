#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo_root"

fqbn="${FQBN:-m5stack:esp32:m5stack_sticks3}"

echo "==> Compile checks"
arduino-cli compile --fqbn "$fqbn" firmware/self_timer >/dev/null
arduino-cli compile --fqbn "$fqbn" firmware/ir_capture >/dev/null
arduino-cli compile --fqbn "$fqbn" firmware/ir_diagnostics >/dev/null

echo "==> Whitespace check"
git diff --check >/dev/null

echo "==> Tracked file hygiene"
if git ls-files | rg -n '(^|/)\.DS_Store$|^private/' >/dev/null; then
  echo "Tracked private or system files detected."
  exit 1
fi

echo "==> Sensitive path scan"
if rg -n '/Users/|private/linkedin-post|Library/Arduino15|cjcnex@gmail.com' \
    README.md docs firmware LICENSE .gitignore >/dev/null; then
  echo "Sensitive or local-only content detected in tracked repo files."
  exit 1
fi

echo "Release checks passed."
