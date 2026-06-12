# Product

Grapple is a native video editor where the agent edits the composition graph,
not the final pixels.

The core bet is that users can describe a video edit, get a concrete editable
graph change, and then steer that change through exposed parameters instead of
rerunning the agent for every adjustment.

Primary product metric:

- Time from intent to a correct, editable previewed graph change.

Non-goals for the core path:

- Agent outputs that only exist as rendered pixels.
- Hidden one-off agent state that cannot be inspected or adjusted by the user.
- Separate preview/export semantics for the same graph.
- Generic NLE breadth before the agent-edit loop is fast, correct, and editable.

The minimum durable product loop is:

```text
import media -> inspect timeline -> ask Steward for an edit -> apply canonical graph command
-> preview evaluated result -> adjust exposed effect parameters -> render/export through the same core
```
