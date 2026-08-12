# Native Control Rig Animation

UE-MCP can author and modify animation through Unreal's Control Rig and
Sequencer APIs. The reliable workflow is data first: describe the pose as
component-space anatomical constraints, discover how the selected rig reaches
those constraints, write normalized quaternion keys, then validate the baked
bones and capture a few exact frames for human review.

Do not reuse control names, Euler angles, or axis signs from another skeleton.
Those are rig-specific observations, not an animation recipe.

The npm package ships the same operating rules as the
[`ue-mcp-animation` agent skill](https://github.com/db-lyon/ue-mcp/blob/main/skills/ue-mcp-animation/SKILL.md),
so `ue-mcp init` can install them with the other bundled workflow skills.

## Compatibility

| Capability | Engine support |
|------------|----------------|
| `begin_control_rig_edit`, `read_control_rig_edit`, `apply_control_rig_edits`, `bake_control_rig_edit` | UE 5.8 only; older engines return `unsupported_engine_version` and do not fall back to raw bone tracks |
| `analyze_animation` | Cross-version; it uses the native animation APIs available in the engine against which the bridge was compiled |
| IK Rig and IK Retargeter actions | See each action in the [tool reference](tool-reference.md); support predates the UE 5.8 Control Rig editing slice |

The editing loop is native. `execute_python` and Computer Use are not part of
it. If a required datum is missing, add a native UE-MCP read or write action
instead of making an escape hatch part of the workflow.

## The authoring loop

### 1. Establish a per-character Control Rig baseline

`begin_control_rig_edit` edits through a Control Rig; it does not invent one.
Before authoring the first clip for a new project or character, search for an
existing rig bound to the target mesh/skeleton. Inspect it with
`read_control_rig_hierarchy` and `read_control_rig_graph`. A usable baseline
must expose the controls needed by the intended motion, a Forward Solve that
drives the bones, and a Backward Solve that can initialize those controls from
source animation without a pose jump.

If no suitable rig exists, create that baseline first. UE 5.8's bundled
Control Rig toolset is available through the animation category: start with
`epic_create`, import the production skeleton with
`epic_import_bones_from_asset`, add the intended controls with
`epic_add_control`, and add inverse initialization with
`epic_add_backward_solve_graph`; then use the remaining Control Rig graph,
node, and link actions and save the exact rig with `asset(epic_save_assets)`.
`epic_create` alone creates no imported bones, authored controls, or solver
wiring.
Build only the controls and solvers required by the character's current
animation work. Verify the saved hierarchy and graph, then round-trip an
unchanged source clip through Backward Solve, Forward Solve, and bake. Bone
transforms must remain within the project's tolerance before the rig is trusted
for production edits.

Do not treat matching bone or control names as compatibility proof. Do not
create a new rig per animation; the verified character rig is the reusable
authoring foundation and baked AnimSequences remain the runtime output.

### 2. Orient and create an immutable session

Start with `project(action="get_status")` and verify the active project and
editor connection. Resolve the source AnimSequence, skeletal mesh, skeleton,
and Control Rig before writing.

Create a versioned LevelSequence and binding tag:

```text
animation(
  action="begin_control_rig_edit",
  sequencePath="/Game/AnimationWork/LS_<motion>_V001",
  skeletalMeshPath="/Game/<mesh>",
  sourceAnimationPath="/Game/<source_anim>",
  rigMode="asset",
  controlRigPath="/Game/<control_rig>",
  layered=false,
  startFrame=0,
  endFrame=<exclusive_end>,
  displayRate=<fps>,
  bindingTag="<motion>.v001",
  onConflict="error"
)
```

Use `rigMode="fk"` only when Unreal's generated `UFKControlRig` is the intended
editing surface. Use `rigMode="asset"` for a project's authored Control Rig.
`startFrame` is inclusive and `endFrame` is exclusive, so `[0, 91)` permits
keys at frames 0 through 90.

The source must be a non-additive AnimSequence compatible with the selected
mesh. Flatten an additive clip against its intended base pose first. The
session's `layered` option controls whether the source track remains active
under the Control Rig layer; it does not supply an additive source's base pose.
Sequencer compensates a finite non-zero AnimSequence `RateScale`, so the session
maps the raw source timeline from start to end exactly once without changing the
source asset. A zero `RateScale` is rejected because it has no invertible time
mapping.

Every material iteration gets a new session path, binding tag, and eventual
output path. Keep `onConflict="error"` while developing. `skip` is suitable
only for a deliberately idempotent replay; begin and bake never overwrite an
existing asset.

### 3. Read controls before choosing them

Call `read_control_rig_edit` on the candidate controls at the rest, transition,
peak, opposite peak, and final frames. Read both spaces:

```text
animation(action="read_control_rig_edit", sequencePath=<session>,
          bindingTag=<tag>, controlNames=[...], frames=[...], space="local")
animation(action="read_control_rig_edit", sequencePath=<session>,
          bindingTag=<tag>, controlNames=[...], frames=[...], space="global")
```

`local` is relative to the control's rig parent. `global` is the Control Rig
hierarchy's global space, which normally corresponds to skeletal-mesh
component space. It is not actor world space. World-space review must also
compose the skeletal mesh component or actor transform.

Control metadata reports:

- `controlType`, `transformControl`, and `animatable`
- `enumName`, `enumPath`, and `enumOptions` with each option's name, display
  name, and integer value
- transform samples, or typed bool, float/scale-float, integer, and enum values

Never write a control with `animatable=false`. Use `set_bool`, `set_float`, or
`set_int` for scalar controls. For an enum, pass an exact integer listed in
`enumOptions`; do not infer it from the option's position or label.

### 4. Solve anatomy in component space

Define observable targets before changing controls. For an arm gesture, solve
proximal to distal:

1. **Shoulder and upper arm:** place the arm's reach and elevation without
   collapsing the shoulder into the torso.
2. **Elbow:** treat the elbow as a pole target. Preserve a plausible bend and a
   stable elbow plane; do not let the elbow flip between frames.
3. **Forearm and wrist:** make the elbow-to-wrist vector point where the action
   requires. A wave, for example, needs the forearm to rise rather than merely
   moving the upper arm sideways.
4. **Palm:** align a rig-discovered palm normal toward the intended viewer or
   interaction target. Establish which local hand axis represents that normal
   from data; names such as X, Y, or Z are not anatomical facts.
5. **Secondary motion:** add the wrist oscillation only after the raised pose is
   correct. Ease the entry and exit, and preserve fingers unless the gesture
   explicitly needs them changed.

IK is useful when the hand target and elbow pole are exposed and well behaved.
FK is valid when those controls are absent or their orientation contract is
unclear, but still judge the result using component-space bones and landmarks.
Do not tune a wrist control in isolation and assume the arm chain followed.

### What transfers to other rigs and motions

The loop transfers; the rig mapping does not. Keep the same discover, constrain,
solve, key, bake, and validate stages, then choose landmarks suited to the
motion:

- Legs use pelvis/hip placement, a knee pole, a foot target and orientation,
  ground contact, and foot-slip measurements.
- Spine, neck, and head motion uses a component-space arc, twist distribution,
  and an aim or gaze direction with per-joint limits.
- Tails, tentacles, ropes, and other chains use a target curve, segment-length
  preservation, bend limits, and phase-delayed keys along the chain.
- Prop, socket, and mechanical animation uses pivot axes, attachment transforms,
  contact/clearance constraints, and the same quaternion continuity checks.
- A retargeted character repeats target-side control, axis, scale, and limit
  discovery before edits; source-rig constants are not portable evidence.

This makes the method reusable across skeletons and animated hierarchies while
keeping the only unavoidable custom data small: control/bone mapping, local
axes and signs, mirrored scale, joint limits, and the motion's constraints.

### 5. Probe mirrored and ambiguous axes

Never obtain right-side rotations by negating left-side Euler values. A
right-side control may inherit mirrored axes or negative scale, so the same
local rotation can have a different anatomical meaning.

For every unfamiliar rig, and separately for each side when needed:

1. Read the relevant controls in local and global space, preserving their full
   translation, rotation, and scale. Use `get_bone_transforms` in local and
   component space for reference-pose chain orientation.
2. In a disposable versioned session, apply a small positive and negative
   rotation around one local axis at one fixed frame. Change only one axis per
   probe.
3. Bake and run `analyze_animation` for the shoulder, upper arm, forearm, hand,
   and a palm/finger landmark. Record the observed component-space movement.
4. Build the axis/sign mapping for that rig and side. Preserve any negative
   scale from the read transform. Repeat the probe after changing rigs or
   retargeting to a materially different hierarchy.

This small probe is cheaper and safer than correcting a full clip built on an
assumed axis convention.

### 6. Apply absolute quaternion keys

Use `set_keys` for reproducible transform authoring:

```json
{
  "op": "set_keys",
  "control": "<discovered_control>",
  "space": "local",
  "keys": [
    {
      "frame": 12,
      "transform": {
        "translation": { "x": 0, "y": 0, "z": 0 },
        "rotationQuaternion": { "x": 0, "y": 0, "z": 0, "w": 1 },
        "scale": { "x": 1, "y": 1, "z": 1 }
      }
    }
  ]
}
```

The numbers above illustrate the payload shape only; read the real base
transform first. The rules are:

- Keys are full, absolute transforms with strictly increasing, unique frames.
- Quaternions must be finite and normalized. UE-MCP preserves shortest-arc
  continuity; use quaternions rather than interpolating Euler angles.
- Preserve read-back translation and scale unless the motion intentionally
  changes them. This is especially important for mirrored/negative-scale
  controls.
- A source animation baked into Control Rig may already contain a key on every
  frame. In that case, write the edited control on every frame of the affected
  interval; sparse keys do not replace intervening source keys.
- Apply related controls and scalar switches in one
  `apply_control_rig_edits` call. The batch is prevalidated, transacted,
  read back, and undone if application or readback fails.

Read the same frames again after applying. Check both local continuity and the
global/component anatomical targets before baking.

### 7. Bake to a new asset

```text
animation(
  action="bake_control_rig_edit",
  sequencePath=<session>,
  bindingTag=<tag>,
  outputAssetPath="/Game/AnimationWork/A_<motion>_V001",
  frameRate=<fps>,
  reduceKeys=false,
  createLink=false,
  onConflict="error"
)
```

The source animation and LevelSequence are not overwritten. Key reduction and
Sequencer/AnimSequence links are not supported by this workflow, so omit
`reduceKeys` and `createLink` or pass `false`.

## Deterministic validation

Run `analyze_animation` on both the source and baked output with the same mesh,
bones, and frames:

```text
animation(
  action="analyze_animation",
  assetPath=<baked_anim>,
  skeletalMeshPath=<mesh>,
  boneNames=[<root>, <pelvis>, <edited_chain>, <feet>, <opposite_side>],
  frames=[...],
  loop=false,
  outputDirectory="<motion>/v001"
)
```

The native result and optional `manifest.json` / `samples.ndjson` contain exact
local and component transforms. The built-in summary reports numeric integrity,
invalid-transform count, root displacement and maximum root speed, selected-bone
bounds, and optional loop-seam root/joint errors. The manifest also records
centimeter units and Unreal's +X forward, +Y right, +Z up convention.

Timing metadata is explicit: `durationSeconds` is the raw sequence duration,
`rateScale` is the AnimSequence asset rate, and `effectiveDurationSeconds` is
the raw duration divided by the rate magnitude. Each `notifies` record includes
`rawTriggerTimeSeconds` and its rate-scaled `effectiveTriggerTimeSeconds`. A
zero asset rate reports null effective times because playback does not advance.

Derive motion-specific assertions from the samples rather than screenshots:

- wrist height relative to shoulder and the elbow-to-wrist direction
- elbow angle, elbow-plane change, and per-frame joint angular change
- palm-normal alignment using the axis established by the probe
- wrist speed/acceleration and deliberate wave direction changes
- root, feet, opposite side, fingers, and other supposedly untouched bones
  remaining within the fixture's tolerance of the source

Always inspect the first frame, transition frames, extrema, last visible frame,
and any frame with the largest numeric delta. Screenshots are review evidence;
the sampled transforms are the source of truth.

### Exact fixed-frame Unreal capture

Capture through native UE-MCP calls, without Computer Use or Python:

1. `editor(action="open_asset", assetPath=<baked_anim>)`.
2. `editor(action="find_object", className="AnimSingleNodeInstance",
   nameContains="AnimPreviewInstance", world="any")`. If several objects are
   returned, choose the one under the current `AnimationEditorPreviewActor`
   (or constrain `outerPath`) rather than a stale preview instance.
3. Freeze and seek atomically with `editor(action="invoke_object_functions")`:

   ```json
   {
     "calls": [
       {
         "objectPath": "<resolved preview instance>",
         "functionName": "SetPlaying",
         "args": { "bIsPlaying": false }
       },
       {
         "objectPath": "<resolved preview instance>",
         "functionName": "SetPosition",
         "args": {
           "InPosition": 1.1333333333,
           "bFireNotifies": false
         }
       }
     ]
   }
   ```

   Compute `InPosition` exactly as `frame * rateDenominator / rateNumerator`;
   the decimal above is only an example payload.
4. Open the asset again to focus its Slate window, then call
   `editor(action="capture_screenshot", target="window", filename=<png>)`.
5. Repeat the same camera and window setup for start, raised pose, both motion
   extrema, and end. Keep clean images beside the numeric analysis artifacts.

## V&V fixtures

Before treating a rig or workflow as production-ready, retain these small,
versioned fixtures:

| Fixture | Proves |
|---------|--------|
| From-scratch gesture over a neutral source pose | Per-rig discovery, typed controls, dense quaternion keys, bake, numeric checks, and fixed-frame review |
| Retarget between two known skeletons | Chain mapping, retarget pose, root behavior, proportions, and target-side axis discovery |
| Copy an existing animation, switch/use IK, then modify the hand and elbow target | IK/FK scalar handling, endpoint and pole control, and preservation of unedited motion |
| Edge-case pack | Right-side mirroring or negative scale, dense source keys, additive-source rejection/flattening, layered sessions, scalar enums/floats, root motion, loop seams, and short clips |

Each fixture records source/session/output asset paths, binding tag, engine
version, selected controls and metadata, probe observations, sampled frames and
bones, numeric acceptance checks, and clean screenshots. Human approval is the
final visual gate, not a replacement for those records.

## Exact-duration endpoint gate

Control Rig edit ranges are end-exclusive: the last authorable frame is
`endFrameExclusive - 1`. A baked AnimSequence can additionally expose a sample
at its exact duration (`sourceFrameCount`) even though the animation editor's
last visible authored frame is one frame earlier. The bridge keeps an internal
evaluation support frame so Unreal's exporter cannot sample outside the source
and Control Rig sections; the exclusive end is still not authorable.

Validate the last visible authored frame and exact-duration sample separately.
For a non-looping clip, the endpoint must hold the intended final pose within
the fixture tolerance and must not introduce an adjacent-frame teleport or
rotation jump. For a loop, pass `loop=true` and require the endpoint to satisfy
the intended seam. Treat a large endpoint discontinuity as a failed bake, not
as an Unreal sampling quirk.
