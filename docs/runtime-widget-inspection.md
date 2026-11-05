# Runtime widget inspection

These `widget` actions read and drive live UMG widgets. Each needs a running PIE
or Game world and never falls back to the editor world, so a call made before
PIE starts returns an error.

| Action | Use it for |
|--------|------------|
| `list_runtime` | What is alive right now, filtered by class substring or name prefix |
| `get_runtime` | One widget's tree with text, visibility, brush, and style values |
| `inspect_runtime_instances` | Every instance that matches, with identity and reflected properties |
| `get_runtime_delegates` | Which delegates on a live widget are bound |
| `invoke_runtime_function` | Fire a UFUNCTION or a child button click |
| `add_to_viewport` | Instantiate a WidgetBlueprint into the live viewport |

## Comparing several instances of one class

`get_runtime` takes the first match, which is ambiguous when several clients or
several copies of one widget class are live. `inspect_runtime_instances` returns
every match, sorted by object path, with `matchCount`. Select by `widgetName`
(exact) or `classFilter` (class-name substring):

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
reflected properties to serialize. A name a node lacks is listed in that node's
`missingProperties` instead of failing the query, so one call works across
related widget classes.

Subtree nodes are opt-in through `includeSubtree`. `childName` or
`childClassFilter` turn it on. The response echoes the `includeSubtree` value
that was applied. `viewportOnly` keeps only widgets added to the viewport.

## Multi-client sessions

`world` selects the scope (`pie`, `game`, or `auto`, default `pie`) and
`pieInstance` selects one client in a multi-instance session. Without
`pieInstance` the first play world is used, which in a listen-server session is
the server. The response reports `worldName`, `netMode` and the resolved
`pieInstance`.

## Bounds

`maxInstances` (default 100, cap 500) and `maxNodesPerInstance` (default 250,
cap 2000) bound the payload. `truncated` and per-instance `nodesTruncated` say
when a bound was hit.
