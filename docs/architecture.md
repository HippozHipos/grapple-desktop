# Architecture

The core architecture is:

```text
ProjectDocument -> ProjectSnapshot -> TimelineIR -> RenderPlan -> Shared Local Runtime/Render Core -> Preview/Final Render Shells
```

Rules:

- `project` will be the only canonical mutation owner.
- `ProjectSnapshot` will be the immutable read boundary for projection, tools,
  storage commits, and UI-facing reads.
- `projection` will be deterministic and side-effect free.
- `runtime` will be the only layer that executes scripts, shaders, tracking,
  segmentation, depth, motion, model-assisted runtime operations, or runtime
  caches.
- `render` will expose preview and final render shells over the same local
  runtime/render core; those shells consume `RenderPlan` through the shared
  core and will not reinterpret the project graph.
- Preview and final render may differ in latency/quality policy, frame stepping,
  surfaces, encoding, and progress reporting only.
- `agent` tools will call typed project commands and queries only.
- Agent-authored edits must produce canonical graph nodes, effect payloads, and
  user-editable parameter controls; agents should not hide durable edits in
  black-box outputs when an editable representation exists.
