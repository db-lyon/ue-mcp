# Runtime Widget Layout Diagnostics

`widget.get_runtime` can return a read-only layout snapshot for every runtime
widget node while PIE is active. It complements the native Widget Reflector with
structured output that an automated client can inspect and compare.

## Opting in

The layout block is off by default. Pass `includeLayout: true`:

```json
{
  "action": "get_runtime",
  "className": "WBP_HUD",
  "includeLayout": true
}
```

It is opt-in because the block is large. Every node gains geometry, render
transform, clipping, viewport, parent and delta objects, and the `slot` object
reflects every property on the slot class, so a deep hierarchy returns far more
JSON than the default response. Without `includeLayout`, the response shape is
unchanged: `name`, `class`, `visibility`, `isVisible`, text, brush, percent and
the style values, walked to `maxDepth`.

## What each node reports

With `includeLayout`, every node adds:

- desired, local, and absolute size;
- absolute position, layout bounds, and render bounds;
- render transform, pivot, accumulated layout scale, and effective opacity;
- complete reflected slot properties;
- structured Canvas anchors, offsets, alignment, auto-size, and Z-order;
- authored clipping mode and a derived effective clipping rectangle;
- parent bounds and whether the child extends beyond them;
- viewport rectangle and whether the node falls outside it;
- `widgetPath`, the stable identifier used to match nodes between captures;
- `diagnostics`, a list of `{code, severity, message}` for suspicious layout
  relationships.

## Response shape

`tree` is the widget tree root, or the widget named by `childName`. That is the
same node the default response returns, so `maxDepth` counts from the same
place either way, and `tree` is the string `"empty"` when the widget has no root
widget.

`includeLayout` adds three top-level keys:

- `host`: the hosting `UUserWidget` node with its own layout block and no
  children. The host is not a panel parent, so its geometry, clipping and
  opacity are otherwise unreachable from the tree root. Its clip state and
  opacity seed the tree walk.
- `layoutCapture`: capture sequence number, frame, time, node count, changed
  node count, diagnostic count, and the viewport rectangle.
- `instanceId`: the unique id of the located widget instance.

## Comparing captures

The handler retains the previous capture for the same PIE widget instance and
optional `childName`. Calling `widget.get_runtime` again with `includeLayout`
after moving, resizing, toggling, or changing the viewport produces
`deltaSincePreviousCapture` for each node. This detects position-dependent
dimensions such as a vertically stretched Canvas slot whose `Bottom` offset was
mistakenly treated as height, reported as the `position_dependent_canvas_height`
diagnostic.

Calls without `includeLayout` neither read nor write the retained captures.

## Recommended debugging sequence

1. Start client-mode PIE on the required test map.
2. Open the affected screen.
3. Capture the PIE window for visual evidence.
4. Call `widget.get_runtime` with `includeLayout` for the affected widget class
   or instance.
5. Reproduce one layout-affecting change.
6. Call `widget.get_runtime` with `includeLayout` again and inspect changed
   nodes and diagnostics.
7. Inspect designer properties and bindings before changing presentation.

## Boundary with Widget Reflector

The reported clipping rectangle is derived from UMG clipping modes and cached
render bounds. It is suitable for layout diagnosis, but it is not a serialized
copy of Slate's paint-element clip stack. Use a native Widget Reflector
`.widgetsnapshot` when exact paint clipping, hit-test grids, or Slate source
addresses are required.
