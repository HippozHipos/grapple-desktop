# Temporary Handoff Note

Delete this file after the new chat has read it.

## Repository

- Local path on Linux: `/home/rahul/Documents/grapple-native`
- GitHub remote: `git@github.com:HippozHipos/grapple-desktop.git`
- Branch: `main`
- Latest pushed commit at handoff: `a3d667e Document native engineering rules`

## First Steps For New Chat

1. Read `.codex` before doing any work.
2. Read `AGENTS.md`, `docs/product.md`, and `docs/architecture.md`.
3. Treat `/home/rahul/Documents/grapple-migration` as the Linux-side source of truth if available. On Windows, use the committed docs and `.codex` first; ask for the migration folder only if a design detail is missing.
4. Delete this `HANDOFF.md` after reading it, then commit that deletion as a small cleanup commit.

## Current Product Direction

Grapple is a native C++/Qt video editor where Steward edits the canonical composition graph, not final pixels. The core product bet is:

```text
intent -> correct editable graph change -> preview evaluated result -> user adjusts exposed parameters -> render/export through the same core
```

Primary metric:

- Time from intent to a correct, editable previewed graph change.

Do not broaden into generic NLE scope before this loop is fast, correct, and editable.

## Architecture Rules To Preserve

- Keep the flow: `ProjectDocument -> ProjectSnapshot -> TimelineIR -> RenderPlan -> shared local runtime/render core -> preview/final shells`.
- Project/core owns canonical state and mutation.
- Projection is pure and deterministic.
- Runtime owns execution, caches, scripts, shaders, tracking, segmentation, depth, motion, and model-assisted runtime work.
- Preview and final render are thin shells over the same shared core.
- UI displays snapshots and sends commands; UI does not own render-plan construction, effect semantics, or canonical state.
- No fallbacks, aliases, compatibility shims, duplicate interpretation paths, or compensating conditionals unless explicitly approved.
- If touched code is bad, rewrite/delete that path instead of layering support around it.
- Build minimal foundations for future 3D/variants/cloud/model runtimes, but do not implement those features early.
- Keep Linux/Windows/macOS support via portable C++/Qt/CMake. Isolate and justify any platform-specific code.

## Recent Work

- `e38c2a7 Use decoder default threading`
- `9b18e15 Instrument playback render timings`
- `49bbed4 Optimize render affine sampling`
- `654452a Avoid first layer compositing work`
- `cf7eda4 Smooth desktop playback ticking`
- `a3d667e Document native engineering rules`

The uncommitted POSIX-only H.264 encoder experiment was intentionally removed before pushing. Do not reintroduce POSIX process/pipe code in core export paths. If H.264 export is resumed, implement it behind a small cross-platform boundary or use a library-level encoder path.

## Verification At Handoff

On Linux before handoff:

```bash
cmake --build build --target grapple_desktop grapple_cli grapple_app_tests
ctest --test-dir build --output-on-failure
```

Result: `95/95` tests passed.

The last focused checks also passed:

```bash
QT_QPA_PLATFORM=offscreen build/apps/grapple_desktop/grapple --playback-smoke
QT_QPA_PLATFORM=offscreen build/apps/grapple_desktop/grapple --product-loop-smoke
```

## Windows Bring-Up Notes

Expected stack:

- CMake
- A C++20 compiler
- Qt6
- OpenCV
- FFmpeg/OpenCV video backend dependencies as needed by local media decode/export

The code is intended to be portable, but Windows support is not proven until configured, built, and tested on Windows. First Windows task should be build-system/dependency bring-up, not product feature work.

Suggested first commands on Windows:

```powershell
git clone git@github.com:HippozHipos/grapple-desktop.git
cd grapple-desktop
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Adjust CMake dependency discovery only at the dependency boundary. Do not add fallback source paths or duplicate platform code in product modules.

## Likely Next MVP Work

Choose the next step by the product metric, not by broad feature count. Likely high-leverage areas:

- Make Windows build/test pass cleanly.
- Continue playback/render responsiveness profiling with real imported media.
- Improve source-frame caching/proxy policy only through the media/render boundary.
- Tighten Steward edit flows so agent-created effects remain editable through named typed parameters.
- Keep render/export consuming the same `RenderPlan` and runtime semantics.

Commit small verified chunks and push frequently.
