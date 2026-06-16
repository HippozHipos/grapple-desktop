# Grapple Native

Native-first Grapple implementation.

This repo is intentionally starting small. The goal is to build the clean
foundation described in `/home/rahul/Documents/grapple-migration`, not to port
the previous application module-by-module.

Product bet: Grapple turns an editing intent into a correct, previewed,
user-editable graph change. See [docs/product.md](docs/product.md).

Initial product flow:

```text
project::ProjectDocument
  -> project::ProjectSnapshot
  -> projection::TimelineIR
  -> projection::RenderPlan
  -> runtime::RuntimeEvaluator
  -> render::LocalRenderCore
  -> preview/final render shells
```

## Build

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Install Locally

```bash
cmake --install build --prefix /tmp/grapple-native
/tmp/grapple-native/bin/grapple
```
