#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if grep -RInE '\b(fallback|compat|legacy)\b' "$root/libs" "$root/tests" 2>/dev/null; then
  echo "Architecture guard failed: forbidden support-path wording in source." >&2
  exit 1
fi

if grep -RInE 'browser|downloaded|renderer-specific' "$root/libs" "$root/tests" 2>/dev/null; then
  echo "Architecture guard failed: consumer-specific render wording in source." >&2
  exit 1
fi

echo "Architecture guards passed."

