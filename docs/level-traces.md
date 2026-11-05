# Level traces and collision inspection

`level(action="line_trace")`, `level(action="shape_trace")` and `level(action="bulk_line_trace")` use the same world and collision channel resolution. `world` is `editor` by default; use `pie` to query live geometry, with `pieInstance` to select an exact instance. An unavailable PIE world fails instead of tracing the editor world. Channel names include project-renamed channels and default to Visibility. `traceComplex` defaults to false; `ignoreActors` takes labels or full paths in the selected world, ignoring every label match.

## Shape sweeps

```text
level(action="shape_trace", shape="sphere", radius=20, start={x:0,y:0,z:100}, direction={x:1,y:0,z:0}, distance=3000, world="pie")
level(action="shape_trace", shape="box", halfExtent={x:10,y:20,z:30}, start={x:0,y:0,z:100}, end={x:1000,y:0,z:100}, multi=true)
level(action="shape_trace", shape="capsule", radius=20, halfHeight=50, start={x:0,y:0,z:100}, end={x:1000,y:0,z:100})
```

Pass `start` and either `end` or a non-zero `direction` plus optional positive `distance` (default 200000 cm). Direction is normalized. All dimensions must be finite and positive, in cm. A box uses axis-aligned `halfExtent` xyz. A capsule is vertical; `halfHeight` includes the hemispherical caps and must be at least `radius`.

The default result is the first blocking hit. Like `line_trace`, it includes `success`, `hit`, `start`, `end`, `traceComplex`, `channel`, and the answering `world`. Hits include `actorLabel`, `actorPath`, `actorClass`, `componentName`, `componentClass`, `location`, `impactPoint`, `normal`, `impactNormal`, and `distance`; `faceIndex`, `boneName`, and `physicalMaterial` are included when available. A shape hit adds `bStartPenetrating` for initial penetration.

`multi=true` uses Unreal's channel multi-sweep semantics: overlaps along the sweep through the first blocking hit. Geometry beyond that blocker is not queried. `hits` contains at most 64 entries in engine order, each with the trace fields plus `blockingHit` and `bStartPenetrating`. An overlap has `hit=false` and `blockingHit=false`; the top-level `hit` means a blocker was found. `totalHits` is the engine's hit count and `truncated` indicates omitted entries. A multi miss returns an empty `hits` array. Sweep `location` is the shape's center at impact; `impactPoint` is the contact point.

## Multi-hit line traces

```text
level(action="line_trace", start={x:0,y:0,z:1100}, end={x:0,y:0,z:-500}, multi=true)
level(action="bulk_line_trace", traces=[{start:{x:0,y:0,z:1100}, end:{x:0,y:0,z:-500}, multi:true}, {start:{x:100,y:0,z:1100}, end:{x:100,y:0,z:-500}}])
```

`multi=true` on `line_trace`, or on any `bulk_line_trace` item, returns `hits`, `totalHits` and `truncated` with the same row fields and limits as a multi sweep: overlaps through the first blocker, at most 64. A height or width profile along a line is one `bulk_line_trace` with one item per sample, up to 256.

## Component collision

```text
level(action="query_components", world="pie", actorLabelContains="Cable", componentClass="BoxComponent", includeCollision=true, collisionChannel="Visibility")
```

With `includeCollision=true`, each primitive component row gains `collisionEnabled`, `objectType`, `blocksVisibility`, and owner label/class. `collisionChannel` accepts the same channel names as a trace and adds `responseOnChannel` and the resolved `collisionChannel`. Omitting it returns `defaultResponse="Block"` and a `responses` list of named channels whose response differs from Block, capped at 32 with `responsesTruncated`. This is an encoding baseline, independent of the component's collision profile. Non-primitive components report null collision fields. Existing row filters, grouping and pagination still apply; no components are changed.

## Python scripts

The bridge plugin exposes `unreal.MCPTraceLibrary.line_trace` and `shape_trace` for scripts. Shape traces share the native handler's shape validation, channel resolution and actor selectors:

```python
hits = unreal.MCPTraceLibrary.shape_trace(
    unreal.Vector(0, 0, 100), unreal.Vector(1000, 0, 100), "sphere", [],
    radius=20.0, world="pie", channel="Visibility", multi=True)
for hit in hits:
    if hit.error:
        raise RuntimeError(hit.error)
    print(hit.actor_path, hit.component_name, hit.start_penetrating)
```

The fourth argument is the ignored actor labels/paths. The reflected Boolean parameters are `trace_complex` and `multi`. Single mode returns one entry, including a miss with `hit=false`; multi mode returns overlaps through the first blocker, capped at 64, and an empty list on a miss. An error returns one entry with `error` populated. Python entries expose the existing trace result fields plus actor/component class, normal, face index, bone name and start penetration; the native action also reports truncation metadata.

Shape traces require a finite positive `distance` when using `direction`. The existing `line_trace` action retains its earlier distance handling, including zero and negative distances.
