# Repository Instructions

READ `.codex` AT THE REPOSITORY ROOT BEFORE STARTING WORK.

This repository is the clean native Grapple implementation. Do not port old
system bloat by default. Implement minimal, typed foundations with one canonical
state path.

The native app has one local runtime/render system. Do not design a separate
preview system or a separate export renderer; playback and export are modes of
the same system consuming the same core-produced `RenderPlan`.

Do not add fallbacks, compatibility shims, alias chains, or duplicate
interpretation paths unless explicitly approved.

If implementation work exposes a non-trivial design decision, make the decision
explicitly against the root rules before coding.
