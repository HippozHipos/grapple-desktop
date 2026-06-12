# Architecture

The core architecture is:

```text
ProjectDocument -> ProjectSnapshot -> TimelineIR -> RenderPlan -> Runtime Evaluation -> Render Consumer
```

Rules:

- `project` will be the only canonical mutation owner.
- `ProjectSnapshot` will be the immutable read boundary for projection, tools,
  storage commits, and UI-facing reads.
- `projection` will be deterministic and side-effect free.
- `runtime` will be the only layer that executes scripts, shaders, tracking,
  segmentation, depth, motion, model-assisted runtime operations, or runtime
  caches.
- `render` consumers will consume `RenderPlan`; they will not reinterpret the
  project graph.
- `agent` tools will call typed project commands and queries only.
