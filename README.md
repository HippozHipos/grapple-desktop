# Grapple Native

Native-first Grapple implementation.

This repo is intentionally starting small. The goal is to build the clean
foundation described in `/home/rahul/Documents/grapple-migration`, not to port
the previous application module-by-module.

Initial product flow:

```text
project::ProjectDocument
  -> projection::TimelineIR
  -> projection::RenderPlan
  -> runtime::RuntimeEvaluator
  -> render preview/export consumer
```

## Build

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

