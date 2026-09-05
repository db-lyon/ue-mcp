# Native spline handlers

`set_spline_points` updates an actor's `USplineComponent` through the native
UE-MCP bridge. Existing `actorLabel` and `{x,y,z}` point callers remain valid.

The optional `componentName` is an exact Unreal component name. If supplied,
the handler fails unless that named component is a `USplineComponent`; it does
not select a different spline.

Each point may provide `pointType` (`Linear`, `Curve`, `CurveClamped`,
`Constant`, or `CurveCustomTangent`). Custom-tangent points must provide finite
`arriveTangent` and `leaveTangent` vectors. Coordinates and supplied tangents
must be finite. Validation completes before the existing spline is cleared.

Edits are recorded in an editor transaction, mark the actor/component package
dirty, and expose prior positions, types, tangents, rotations, scales, input
keys, and loop endpoint state for MCP rollback. Input keys must be increasing
and distinct under Unreal's near-equality comparison. `loopPositionOverride`
and `loopPosition` preserve an explicit closing key.

Splines with subclass metadata require their specialized authoring APIs and are
rejected before mutation. The response marks MCP rollback
as potentially lossy for custom rotation/scale-curve interpolation. Standard
Unreal Editor Undo records the complete component state and is preferred when
those ancillary curve details matter.

For older category schemas with an XYZ-only `points` array, pass the typed array
through `level(action="set_spline_points", actorLabel="Route", input={"points":[...]})`.
The `input` object also accepts componentName, closedLoop, loopPositionOverride,
and loopPosition. Duplicate fields at both levels are rejected.
