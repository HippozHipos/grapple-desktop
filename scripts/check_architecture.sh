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

if grep -RInE '\b(fallback|alias|compat|legacy)\b' "$root/libs" "$root/tests" 2>/dev/null; then
  echo "Architecture guard failed: forbidden support-path wording in source." >&2
  exit 1
fi

if grep -RInE 'browser|downloaded|renderer-specific' "$root/libs" "$root/tests" 2>/dev/null; then
  echo "Architecture guard failed: consumer-specific render wording in source." >&2
  exit 1
fi

check_no_matches \
  "graph and timeline core must not use generic metadata fields." \
  '\bmetadata\b' \
  "$root/libs/graph" \
  "$root/libs/timeline"

if grep -RInE '(Preview|Final)(GraphInterpreter|PlanInterpreter|RuntimeEvaluator|RuntimeCore|RenderCore|EffectRuntime|DependencyPlanner|CachePolicy|MediaPolicy)' "$root/libs" "$root/tests" 2>/dev/null; then
  echo "Architecture guard failed: preview/final shells must not duplicate shared render/runtime core semantics." >&2
  exit 1
fi

if grep -RInE 'runtime::RuntimeEvaluator|render::LocalRenderCore core_' \
  "$root/libs/app/include/grapple/app/NativePreviewSession.hpp" \
  "$root/libs/app/include/grapple/app/NativeExportSession.hpp" 2>/dev/null; then
  echo "Architecture guard failed: app preview/export sessions must use the workspace-owned render core." >&2
  exit 1
fi

check_no_matches \
  "project must not depend on downstream or infrastructure modules." \
  '#include <grapple/(projection|runtime|render|model|agent|storage|media|jobs)/' \
  "$root/libs/project"

check_no_matches \
  "project CMake target must not link downstream or infrastructure modules." \
  'Grapple::(Projection|Runtime|Render|Model|Agent|Storage|Media|Jobs)([^A-Za-z0-9_]|$)' \
  "$root/libs/project/CMakeLists.txt"

check_no_matches \
  "projection must remain pure and must not depend on runtime/render/model/agent/media/storage/jobs." \
  '#include <grapple/(runtime|render|model|agent|media|storage|jobs)/' \
  "$root/libs/projection"

check_no_matches \
  "projection CMake target must not link runtime/render/model/agent/media/storage/jobs." \
  'Grapple::(Runtime|Render|Model|Agent|Media|Storage|Jobs)([^A-Za-z0-9_]|$)' \
  "$root/libs/projection/CMakeLists.txt"

check_no_matches \
  "history must record facts only and must not depend on product execution modules." \
  '#include <grapple/(project|graph|timeline|asset|projection|runtime|render|model|agent|storage|media|jobs)/' \
  "$root/libs/history"

check_no_matches \
  "history CMake target must not link product execution modules." \
  'Grapple::(Project|Graph|Timeline|Asset|Projection|Runtime|Render|Model|Agent|Storage|Media|Jobs)([^A-Za-z0-9_]|$)' \
  "$root/libs/history/CMakeLists.txt"

check_no_matches \
  "storage must own IO only and must not call product execution modules." \
  '#include <grapple/(projection|runtime|render|model|agent|media|jobs)/' \
  "$root/libs/storage"

check_no_matches \
  "storage CMake target must not link projection/runtime/render/model/agent/media/jobs." \
  'Grapple::(Projection|Runtime|Render|Model|Agent|Media|Jobs)([^A-Za-z0-9_]|$)' \
  "$root/libs/storage/CMakeLists.txt"

check_no_matches \
  "media must not depend on project graph, runtime, render, model, agent, storage, or jobs." \
  '#include <grapple/(project|graph|timeline|projection|runtime|render|model|agent|storage|jobs)/' \
  "$root/libs/media"

check_no_matches \
  "media CMake target must not link project graph, runtime, render, model, agent, storage, or jobs." \
  'Grapple::(Project|Graph|Timeline|Projection|Runtime|Render|Model|Agent|Storage|Jobs)([^A-Za-z0-9_]|$)' \
  "$root/libs/media/CMakeLists.txt"

check_no_matches \
  "jobs must schedule work without owning graph/runtime/render/model/agent/storage semantics." \
  '#include <grapple/(graph|timeline|asset|projection|runtime|render|model|agent|storage|media)/' \
  "$root/libs/jobs"

check_no_matches \
  "jobs CMake target must not link graph/runtime/render/model/agent/storage semantics." \
  'Grapple::(Graph|Timeline|Asset|Projection|Runtime|Render|Model|Agent|Storage|Media)([^A-Za-z0-9_]|$)' \
  "$root/libs/jobs/CMakeLists.txt"

check_no_matches \
  "runtime must not depend on project, graph, render, agent, storage, jobs, asset, or timeline." \
  '#include <grapple/(project|graph|render|agent|storage|jobs|asset|timeline)/' \
  "$root/libs/runtime"

check_no_matches \
  "runtime CMake target must not link project, graph, render, agent, storage, jobs, asset, or timeline." \
  'Grapple::(Project|Graph|Render|Agent|Storage|Jobs|Asset|Timeline)([^A-Za-z0-9_]|$)' \
  "$root/libs/runtime/CMakeLists.txt"

check_no_matches \
  "render must consume RenderPlan/runtime output and must not depend on project, graph, agent, storage, jobs, asset, or timeline." \
  '#include <grapple/(project|graph|agent|storage|jobs|asset|timeline)/' \
  "$root/libs/render"

check_no_matches \
  "render CMake target must not link project, graph, agent, storage, jobs, asset, or timeline." \
  'Grapple::(Project|Graph|Agent|Storage|Jobs|Asset|Timeline)([^A-Za-z0-9_]|$)' \
  "$root/libs/render/CMakeLists.txt"

check_no_matches \
  "agent tools must not hold projection builders, runtime, render, storage, media, jobs, graph, asset, or timeline dependencies." \
  '#include <grapple/(projection|runtime|render|storage|media|jobs|graph|asset|timeline)/' \
  "$root/libs/agent"

check_no_matches \
  "agent CMake target must not link projection/runtime/render/storage/media/jobs/graph/asset/timeline." \
  'Grapple::(Projection|Runtime|Render|Storage|Media|Jobs|Graph|Asset|Timeline)([^A-Za-z0-9_]|$)' \
  "$root/libs/agent/CMakeLists.txt"

check_no_matches \
  "agent tools must request ids from the project id allocator instead of deriving ids from revisions." \
  'revisionNumber \+ 1|nextRevisionNumber|cmd_agent_.*rev_|node_agent_.*rev_|edge_agent_.*rev_' \
  "$root/libs/agent"

check_no_matches \
  "lower-level modules must not depend on the app orchestration layer." \
  '#include <grapple/app/' \
  "$root/libs/foundation" \
  "$root/libs/asset" \
  "$root/libs/timeline" \
  "$root/libs/graph" \
  "$root/libs/project" \
  "$root/libs/projection" \
  "$root/libs/history" \
  "$root/libs/storage" \
  "$root/libs/jobs" \
  "$root/libs/media" \
  "$root/libs/runtime" \
  "$root/libs/model" \
  "$root/libs/render" \
  "$root/libs/agent"

echo "Architecture guards passed."
