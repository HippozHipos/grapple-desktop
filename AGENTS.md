# Repository Instructions

READ `.codex` AT THE REPOSITORY ROOT BEFORE STARTING WORK.

This repository is the clean native Grapple implementation. Do not port old
system bloat by default. Implement minimal, typed foundations with one canonical
state path.

The native app may have separate preview and final render shells, but they must
be thin adapters over one shared local runtime/render core. Do not duplicate
graph traversal, `RenderPlan` interpretation, effect execution, dependency
invalidation, media/cache policy, or runtime semantics between those shells.

Do not add fallbacks, compatibility shims, alias chains, or duplicate
interpretation paths unless explicitly approved.

If implementation work exposes a non-trivial design decision, make the decision
explicitly against the root rules before coding.
