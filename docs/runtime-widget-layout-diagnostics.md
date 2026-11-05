# Runtime widget layout diagnostics

`widget(get_runtime)` can return a read-only layout snapshot for every node of a
live widget while PIE is running, as structured output a client can compare
between captures.

## Opting in

The layout block is off by default. Pass `includeLayout: true`:

```json
{
  "action": "get_runtime",
  "className": "WBP_HUD",
  "includeLayout": true
}
```

The block is large: every node gains geometry, render transform, clipping,
viewport, parent and delta objects, and a `slot` object with every property of
the slot class. Without `includeLayout`, each node carries `name`, `class`,
`visibility`, `isVisible`, text, brush, percent and style values, walked to
`maxDepth` (default 6).

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

`tree` is the widget tree root, or the widget named by `childName`, with or
without `includeLayout`. It is the string `"empty"` when the widget has no root
widget.

`includeLayout` adds three top-level keys:

- `host`: the hosting `UUserWidget` node with its own layout block and no
  children. Its clip state and opacity seed the tree walk.
- `layoutCapture`: capture sequence number, frame, time, whether this is the
  baseline, node count, changed node count, diagnostic count, and the viewport
  rectangle.
- `instanceId`: the unique id of the located widget instance.

## Comparing captures

The handler keeps the previous capture for the same widget instance and
`childName`. Calling `widget(get_runtime)` again with `includeLayout` after
moving, resizing, toggling or changing the viewport adds
`deltaSincePreviousCapture` to each node. This exposes position-dependent sizing,
such as a vertically stretched Canvas slot whose `Bottom` offset was treated as
a height, reported as the `position_dependent_canvas_height` diagnostic.

Calls without `includeLayout` neither read nor write the retained captures.

## Recommended debugging sequence

1. Start client-mode PIE on the required test map.
2. Open the affected screen.
3. Capture the PIE window for visual evidence.
4. Call `widget(get_runtime)` with `includeLayout` for the affected widget class
   or instance.
5. Reproduce one layout-affecting change.
6. Call `widget(get_runtime)` with `includeLayout` again and inspect changed
   nodes and diagnostics.
7. Inspect designer properties and bindings before changing presentation.

## Boundary with Widget Reflector

The reported clipping rectangle is derived from UMG clipping modes and cached
render bounds, not from Slate's paint-element clip stack. Use a Widget Reflector
`.widgetsnapshot` when you need exact paint clipping, hit-test grids or Slate
source addresses.
