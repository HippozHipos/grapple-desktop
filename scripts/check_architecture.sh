#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

check_no_matches() {
  local description="$1"
  local pattern="$2"
  shift 2

  local output
  output="$(grep -RInE "$pattern" "$@" 2>/dev/null || true)"
  if [[ -n "$output" ]]; then
    echo "$output"
    echo "Architecture guard failed: $description" >&2
    exit 1
  fi
}

if grep -RInE '\b(fallback|compat|legacy)\b' "$root/libs" "$root/tests" 2>/dev/null; then
  echo "Architecture guard failed: forbidden support-path wording in source." >&2
  exit 1
fi

if grep -RInE 'browser|downloaded|renderer-specific' "$root/libs" "$root/tests" 2>/dev/null; then
  echo "Architecture guard failed: consumer-specific render wording in source." >&2
  exit 1
fi

check_no_matches \
  "project must not depend on downstream or infrastructure modules." \
  '#include <grapple/(projection|runtime|render|model|agent|storage|media|jobs)/' \
  "$root/libs/project"

check_no_matches \
  "projection must remain pure and must not depend on runtime/render/model/agent/media/storage/jobs." \
  '#include <grapple/(runtime|render|model|agent|media|storage|jobs)/' \
  "$root/libs/projection"

check_no_matches \
  "history must record facts only and must not depend on product execution modules." \
  '#include <grapple/(project|graph|timeline|asset|projection|runtime|render|model|agent|storage|media|jobs)/' \
  "$root/libs/history"

check_no_matches \
  "storage must own IO only and must not call product execution modules." \
  '#include <grapple/(projection|runtime|render|model|agent|media|jobs)/' \
  "$root/libs/storage"

check_no_matches \
  "media must not depend on project graph, runtime, render, model, agent, storage, or jobs." \
  '#include <grapple/(project|graph|timeline|projection|runtime|render|model|agent|storage|jobs)/' \
  "$root/libs/media"

check_no_matches \
  "jobs must schedule work without owning graph/runtime/render/model/agent/storage semantics." \
  '#include <grapple/(graph|timeline|asset|projection|runtime|render|model|agent|storage|media)/' \
  "$root/libs/jobs"

check_no_matches \
  "runtime must not depend on project, graph, render, agent, storage, jobs, asset, or timeline." \
  '#include <grapple/(project|graph|render|agent|storage|jobs|asset|timeline)/' \
  "$root/libs/runtime"

check_no_matches \
  "render must consume RenderPlan/runtime output and must not depend on project, graph, agent, storage, jobs, asset, or timeline." \
  '#include <grapple/(project|graph|agent|storage|jobs|asset|timeline)/' \
  "$root/libs/render"

check_no_matches \
  "agent tools must not hold projection builders, runtime, render, storage, media, jobs, graph, asset, or timeline dependencies." \
  '#include <grapple/(projection|runtime|render|storage|media|jobs|graph|asset|timeline)/' \
  "$root/libs/agent"

echo "Architecture guards passed."
