# Viewport camera control

The native `set_viewport_camera` command (`editor` action `set_viewport`) preserves its original optional `location` and
`rotation` fields and now also accepts:

- `projection` or `viewportType`: `perspective`, `top`, `bottom`, `left`,
  `right`, `front`, `back`, or `orthoFreelook`.
- `orthoZoom`: a finite Unreal-unit value within the engine's minimum/maximum
  orthographic zoom bounds. In perspective mode it updates the stored
  orthographic zoom, allowing complete camera-state rollback across view modes.

The handler validates all supplied values before changing the viewport, marks
the viewport for redraw, and returns the resulting location, rotation,
projection/type, and orthographic zoom. Calls with no setters are therefore a
state readback. `projection` and `viewportType` may not disagree.

Invalid projection names, malformed numeric objects, non-finite values, and
non-positive orthographic zooms are errors and leave the viewport unchanged.

Older category schemas can forward these fields through their generic `args`
object: `editor(action="set_viewport", args={"projection":"top","orthoZoom":100000})`.
Supplying the same field both at the top level and inside `args` is rejected.
