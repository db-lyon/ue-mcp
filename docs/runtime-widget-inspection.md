# Runtime widget inspection

The `widget` category reads and drives live UMG widgets while Play-In-Editor is
running. Every action on this page needs a running PIE or Game world: they never
fall back to the editor world, so a call made before PIE starts errors instead of
answering with Editor Utility Widgets.

| Action | Use it for |
|--------|------------|
| `list_runtime` | What is alive right now, by class or name prefix |
| `get_runtime` | One widget's tree with text, visibility, brush, and style values |
| `inspect_runtime_instances` | Every instance that matches, with identity and reflected properties |
| `get_runtime_delegates` | Which delegates on a live widget are bound |
| `invoke_runtime_function` | Fire a UFUNCTION or a child button click |
| `add_to_viewport` | Instantiate a WidgetBlueprint into the live viewport |

## Comparing several instances of one class

`get_runtime` is convenient for a single widget, but it takes the first object
iterator match, which is ambiguous when several clients or several copies of one
widget class are live. `inspect_runtime_instances` returns every match instead,
in deterministic path order, and reports how many matched:

```json
{
  "action": "inspect_runtime_instances",
  "classFilter": "Hero",
  "propertyNames": ["MemberID", "BuffDynamic", "EffectDynamic"],
  "includeSubtree": true,
  "childClassFilter": "BuffSlot",
  "world": "pie",
  "pieInstance": 1
}
```

Each instance carries its object path, outer path, viewport state, and the
owning player controller (with player state path and controller id), so two
clients showing the same HUD stay distinguishable. `propertyNames` names exact
reflected properties to serialize from the widget; missing ones come back in
`missingProperties` per node instead of failing the whole query, which is what
makes one call usable across related Blueprint widget classes.

Subtree nodes are opt-in through `includeSubtree`, and `childName` or
`childClassFilter` turn it on implicitly since a child filter is meaningless
without it. The response echoes the `includeSubtree` value that was applied.

## Multi-client sessions

`world` selects the scope (`pie`, `game`, or `auto`, default `pie`) and
`pieInstance` selects one client in a multi-instance session. Without
`pieInstance` the first play world wins, which in a listen-server session is the
server. The response reports `worldName`, `netMode`, and the resolved
`pieInstance` so the answer names the world it came from.

## Bounds

`maxInstances` (default 100, hard cap 500) and `maxNodesPerInstance` (default
250, hard cap 2000) keep a broad diagnostic from returning an unbounded payload.
Both `truncated` and per-instance `nodesTruncated` flags say when a bound was
hit, so a narrower filter is an explicit next step rather than a guess.
