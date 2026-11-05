# Native Control Rig Animation

UE-MCP authors and edits animation through Unreal's Control Rig and Sequencer.
The workflow is data first: describe the pose as component-space anatomical
constraints, discover how the rig reaches them, write normalized quaternion keys,
bake, validate the baked bones numerically, and capture a few exact frames for
human review.

Do not reuse control names, Euler angles or axis signs from another skeleton.
They are rig-specific observations, not a recipe.

The same rules ship as the
[`ue-mcp-animation` agent skill](https://github.com/db-lyon/ue-mcp/blob/main/skills/ue-mcp-animation/SKILL.md),
which `ue-mcp init` installs with the other bundled skills when Claude Code is
configured.

## Compatibility

| Capability | Engine support |
|------------|----------------|
| `begin_control_rig_edit`, `read_control_rig_edit`, `capture_control_rig_pose`, `apply_control_rig_edits`, `bake_control_rig_edit` | UE 5.8 only. Older engines return `unsupported_engine_version`; there is no fallback to raw bone tracks |
| `configure_ik_rig`, `configure_ik_retargeter` | UE 5.8 only, same error on older engines |
| `analyze_animation` | Every supported engine |
| Other IK Rig and IK Retargeter create/read actions | See each action in the [tool reference](tool-reference.md) |

The loop is native. `execute_python` is not part of it. If a required datum is
missing, add a native read or write action.

## IK and retarget context

Control Rig edits are most predictable when the skeleton already has explicit
chains, goals and a solver. Configure an existing IK Rig with
`configure_ik_rig` rather than writing reflected properties:

```text
animation(
  action="configure_ik_rig",
  rigPath="/Game/Rigs/IK_<character>",
  retargetRoot="pelvis",
  rootMotionBone="root",
  chains=[...],
  fullBodyIK={rootBone:"pelvis", goals:[...]}
)
```

It validates every bone, chain ancestry, goal, solver connection and numeric
setting before saving. `autoSetup="retarget"` installs the native retarget
definition; `"full_body"` adds Full Body IK as well. Verify with
`read_ik_rig`: preview mesh, roots, chains and goal assignments, goals,
exclusions, solver stack and effector settings. A chain carrying a goal *name*
does not prove the goal or solver exists.

Configure retargeting with `configure_ik_retargeter`:

```text
animation(
  action="configure_ik_retargeter",
  retargeterPath="/Game/Rigs/RTG_<source>_<target>",
  sourceRig="/Game/Rigs/IK_<source>",
  targetRig="/Game/Rigs/IK_<target>",
  sourcePreviewMesh="/Game/Meshes/SK_<source>",
  targetPreviewMesh="/Game/Meshes/SK_<target>",
  ensureDefaultOps=true,
  autoMapMode="exact",
  forceRemap=true,
  chainMappings=[...],
  pose={side:"target", name:"<named_pose>", create:true, autoAlign:"chain_to_chain", rotationOffsets:[...]}
)
```

The 5.8 operation stack needs both rigs; the top-level retargeter reference
alone is not enough. Auto-map first, apply manual overrides second, then create
and adjust a named pose. A whole-pose auto-align resets that pose before the
manual offsets apply, so create a new pose or pass `pose.reset=true`. `ops`
writes per-op `settings` and per-chain `chainSettings`. Verify with
`read_ik_retargeter`: per-op rig assignments and mappings, pose deltas, preview
meshes and processor validation messages.

Partial mapping is valid when the target has extra twist, metacarpal or
accessory chains. Record the chains left unmapped on purpose and require every
motion-critical chain.

## The authoring loop

### 1. Establish a per-character Control Rig baseline

`begin_control_rig_edit` edits through a Control Rig; it does not invent one.
Look for an existing rig bound to the target mesh or skeleton and inspect it with
`read_control_rig_hierarchy` and `read_control_rig_graph`. A usable baseline
exposes the controls the motion needs, a Forward Solve that drives the bones,
and a Backward Solve that initializes those controls from source animation
without a pose jump. `rigMode="asset"` rejects a rig without inverse execution.

If none fits, build one with the UE 5.8 Control Rig toolset wrapped on the
animation category: `epic_create`, then `epic_import_bones_from_asset` for the
production skeleton, `epic_add_control` for each control, and
`epic_add_backward_solve_graph`. Wire the rest with the other Control Rig graph,
node and link actions and save with `asset(epic_save_assets)`. `epic_create`
alone creates no imported bones, controls or solver wiring.

Build only the controls and solvers the current work needs. Verify the saved
hierarchy and graph, then round-trip an unchanged source clip through Backward
Solve, Forward Solve and bake; bone transforms must stay within the project's
tolerance. Matching bone or control names are not compatibility proof. Keep one
verified rig per character. Do not create a new rig per animation.

### 2. Create a session

Call `project(get_status)` to confirm the project and connection, and resolve
the source AnimSequence, skeletal mesh, skeleton and Control Rig before writing.

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

- `rigMode="fk"` uses Unreal's generated `UFKControlRig`; `rigMode="asset"`
  uses the authored rig at `controlRigPath`.
- `startFrame` is inclusive and `endFrame` exclusive: `[0, 91)` allows keys at
  frames 0 through 90.
- The source must be a non-additive AnimSequence compatible with the mesh.
  Flatten an additive clip against its base pose first. `layered` (default
  false) keeps the source track active under the rig layer; it does not supply a
  base pose.
- A finite non-zero source `RateScale` is compensated so the session maps the
  raw source timeline once, without changing the asset. A zero `RateScale` is
  rejected.
- `onConflict` is `error` (default) or `skip`. Begin and bake never overwrite an
  existing asset. Give every iteration a new session path, binding tag and
  output path.

### 3. Read controls before choosing them

Read the candidate controls at the rest, transition, peak, opposite peak and
final frames, in both spaces:

```text
animation(action="read_control_rig_edit", sequencePath=<session>,
          bindingTag=<tag>, controlNames=[...], frames=[...], space="local")
animation(action="read_control_rig_edit", sequencePath=<session>,
          bindingTag=<tag>, controlNames=[...], frames=[...], space="global")
```

`local` is relative to the control's rig parent. `global` is the rig hierarchy's
global space, normally skeletal-mesh component space, not actor world space.

Control metadata reports `controlType`, `transformControl`, `animatable`, and for
enums `enumName`, `enumPath` and `enumOptions` (name, display name, integer
value). Samples are transforms or typed bool, float, integer and enum values.

Never write a control with `animatable=false`. For an enum, pass an exact integer
from `enumOptions`, not one inferred from position or label.

### Capture and propagate a viewport pose

`read_control_rig_edit` evaluates Sequencer, so it discards an unkeyed viewport
adjustment. Keep the session's sequence focused and use this order:

```text
baseline = animation(action="capture_control_rig_pose", sequencePath=<session>,
                     bindingTag=<tag>, controlNames=[...]).snapshot

# Adjust the Control Rig shapes in the viewport without scrubbing.

accepted = animation(action="capture_control_rig_pose", sequencePath=<session>,
                     bindingTag=<tag>, controlNames=[...]).snapshot

animation(action="apply_control_rig_edits", sequencePath=<session>, bindingTag=<tag>,
  operations=[{op:"propagate_pose", baseline:<baseline>, accepted:<accepted>,
    controls:[
      {control:"finger_ctrl", mode:"fixed", donorFrames:[0, 1, 2]},
      {control:"wrist_ctrl", mode:"local_delta", donorFrames:[0, 1, 2]}
    ]}])
```

Pass the same full `controlNames` set to both captures; omitting it captures
only the current selection. Keep both snapshots unchanged and pass only the
controls that changed. `fixed` copies the accepted channels to each donor key;
`local_delta` applies the captured local change while keeping donor motion. Bool,
enum and int controls support `fixed` only. The handler rejects mismatched
sessions, rig instances, frames, control sets or types, non-finite values,
unchanged requested controls and a moved playhead. The explicit control list
decides what is keyed; selection is recorded only as provenance.

Before handoff, check that the control shapes are visible and selectable in the
viewport and show the rotation gizmo. A hidden host skeletal-mesh component also
hides Control Rig shapes; for a driver overlay, disable its rendering passes
instead of setting `bVisible` false.

### 4. Solve anatomy in component space

Define observable targets first. For an arm gesture, solve proximal to distal:

1. **Shoulder and upper arm:** reach and elevation without collapsing the
   shoulder into the torso.
2. **Elbow:** a pole target with a stable bend plane that does not flip between
   frames.
3. **Forearm and wrist:** point the elbow-to-wrist vector where the action needs
   it. A wave raises the forearm instead of swinging the upper arm sideways.
4. **Palm:** align a palm normal discovered from data toward the target. X, Y and
   Z are not anatomical facts.
5. **Secondary motion:** add wrist oscillation after the raised pose is correct,
   ease in and out, and leave fingers alone unless the gesture needs them.

Use IK when the hand target and elbow pole are exposed and well behaved, FK
otherwise, and judge either by component-space bones and landmarks.

The same loop applies to other motions with different landmarks:

- Legs: hip placement, knee pole, foot target and orientation, ground contact,
  foot slip.
- Spine, neck and head: a component-space arc, twist distribution, aim or gaze
  with per-joint limits.
- Tails, ropes and other chains: a target curve, segment-length preservation,
  bend limits, phase-delayed keys.
- Props, sockets and mechanisms: pivot axes, attachment transforms, contact and
  clearance.
- A retargeted character repeats control, axis, scale and limit discovery on the
  target side.

### 5. Probe mirrored and ambiguous axes

Never derive right-side rotations by negating left-side Euler values; a
right-side control may have mirrored axes or negative scale. For each unfamiliar
rig, and each side when needed:

1. Read the controls in local and global space, keeping translation, rotation
   and scale. Use `get_bone_transforms` in local and component space for the
   reference-pose chain orientation.
2. In a disposable session, apply a small positive and negative rotation about
   one local axis at one frame.
3. Bake and run `analyze_animation` on the shoulder, upper arm, forearm, hand and
   a palm or finger landmark. Record the component-space movement.
4. Build the axis and sign mapping for that rig and side, keeping any negative
   scale. Repeat after changing rigs.

### 6. Apply edits

`apply_control_rig_edits` takes a list of `operations`:

| op | Writes |
|----|--------|
| `set_keys` | Full absolute transforms at strictly increasing, unique frames |
| `set` | One full absolute transform at `frame` or `frames` |
| `offset` | Translation, rotation and scale deltas over an inclusive range, with optional edge blends |
| `set_bool`, `set_float`, `set_int` | Scalar controls; enums use `set_int` |
| `propagate_pose` | Captured viewport changes, as above |
| `contact_lock` | Dense driver keys holding a contact, below |

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

The values show the shape only; read the real base transform first.

- Quaternions must be finite and normalized. Shortest-arc continuity is
  preserved. Sequencer's current interpolation mode is kept.
- Keep the read-back translation and scale unless the motion changes them,
  especially on mirrored or negative-scale controls.
- A baked source may have a key on every frame. Write the edited control on every
  frame of the affected interval, because sparse keys do not replace the source
  keys between them.
- One call is validated first, runs in one transaction, is read back, and is
  undone if applying or reading back fails.
- Operations are keyed in order, each sampled after the earlier ones land, so a
  component-space `offset` on a child after one on its parent adds to it. Put the
  parent first when the child should ride on it.
- A contact using `targetReference`, or `drivenReference` without the FK
  rotation-chain solver, samples the source animation. It needs a fresh session
  with no earlier rig edits, including from earlier calls (the bridge cannot
  detect those), and must be first in the batch or the call returns
  `contact_lock_reference_batch_unsupported`. Bake prior edits and begin a new
  session first.

Read the same frames again after applying and check local continuity and the
component-space targets before baking.

### Contact constraints

`contact_lock` makes a driver hold a control, bone or socket at a fixed or moving
component-space transform across an inclusive frame range:

```json
{
  "op": "contact_lock",
  "control": "<keyable_ik_driver>",
  "drivenReference": "<bone_or_socket>",
  "startFrame": 12,
  "endFrame": 38,
  "target": {
    "translation": { "x": 0, "y": 0, "z": 0 },
    "rotationQuaternion": { "x": 0, "y": 0, "z": 0, "w": 1 }
  },
  "blendInFrames": 4,
  "blendOutFrames": 4,
  "stabilizeControls": ["<pole_or_secondary_control>"],
  "positionToleranceCm": 0.1,
  "rotationToleranceDegrees": 0.5
}
```

Read the target from the source or session. The session must contain one source
animation section. The bridge samples it at each frame, measures the bone or
socket offset from the driver, and solves dense driver keys. Edge weights blend
into and out of the lock; at least one frame must be fully constrained.

To follow a moving bone or socket, pass `targetReference`. Without `target`, the
relative position (and orientation, when the driver rotates) at the first
constrained frame is held (`targetMode=captured_relative`). With `target`, it is
interpreted in the reference's space (`targetMode=explicit_relative`). This
covers a second hand following the first or a hand following a prop socket.

FK bones commonly use skeleton translation retargeting, which discards
translation keys at playback. For an FK `drivenReference` on a bone, the bridge
solves the driver-to-reference chain with FABRIK per frame and writes local
rotation keys, keeping source bone translations (`solver=fk_rotation_chain`).
This path needs a driven bone, the FK rig in Replace apply mode, and no
stabilizers; use an asset Control Rig for sockets or stabilizers. The end
control's rotation is keyed only when `target.rotationQuaternion` is given.

Driver and stabilizer keys are read back in the transaction. `passed=true` is
returned only for a direct control contact. With `drivenReference` or
`targetReference`, `contactQa.verification` is `bake_and_analyze_required`: bake,
run `analyze_animation` on every constrained frame, and reject the output if the
reference misses the target by more than the motion's tolerance. A
`targetReference` contact checks the driver against the sampled source; it does
not re-evaluate the target after later rig edits.

`contact_lock` is generic: hands on props, planted feet, held tools, linkages.
It does not model collision, friction, joint limits, foot roll or pelvis
compensation; the caller handles those and verifies the baked bones.

### 7. Bake to a new asset

```text
animation(
  action="bake_control_rig_edit",
  sequencePath=<session>,
  bindingTag=<tag>,
  outputAssetPath="/Game/AnimationWork/A_<motion>_V001",
  frameRate=<fps>,
  onConflict="error"
)
```

The source animation and LevelSequence are unchanged. Key reduction and
Sequencer links are not supported: omit `reduceKeys` and `createLink` or pass
`false`. The rollback deletes the created asset.

## Validation

Run `analyze_animation` on the source and the baked output with the same mesh,
bones and frames:

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

`outputDirectory` is under `Saved/Codex/AnimationQA` and must not already hold
artifacts. The result and the optional `manifest.json` and `samples.ndjson` hold
local and component transforms. The summary reports `invalidTransformCount`,
`rootDisplacementCm`, `maxRootSpeedCmPerSecond`, selected-bone bounds and, with
`loop=true`, `loopSeam` errors. The manifest records centimetres and Unreal's +X
forward, +Y right, +Z up convention.

- `durationSeconds` is the raw duration, `rateScale` the asset rate, and
  `effectiveDurationSeconds` the raw duration divided by the rate magnitude.
  Each `notifies` entry has `rawTriggerTimeSeconds` and
  `effectiveTriggerTimeSeconds`. A zero rate reports null effective times.
- `facingBones=[boneA, boneB]` reports `facingYawDegrees` per frame: the
  boneA-to-boneB vector in the root bone's frame (0 is root +X, 90 is root +Y).
  `summary.facing` gives `averageYawDegrees` (circular mean),
  `minYawDegrees`, `maxYawDegrees`, `spreadDegrees`, `resultantLength` (1 when
  every sample agrees) and `degenerateSampleCount` (vertical vectors). A hip or
  shoulder pair separates a clip that faces its travel from one that strafes.
- To compare length, rate and frame counts across many clips, use
  `read_sequence` with `assetPaths` or `directory` and `fields`.

Derive assertions from the samples, not screenshots: wrist height against the
shoulder, elbow angle and plane, palm-normal alignment on the probed axis, wrist
speed and direction changes, and untouched bones (root, feet, opposite side,
fingers) staying within tolerance of the source. Check the first frame,
transitions, extrema, the last visible frame and the frame with the largest
delta.

### Exact fixed-frame capture

1. `editor(action="open_asset", assetPath=<baked_anim>)`.
2. `editor(action="find_object", className="AnimSingleNodeInstance",
   nameContains="AnimPreviewInstance", world="any")`. If several come back,
   take the one under the current `AnimationEditorPreviewActor` (or pass
   `outerPath`).
3. Freeze and seek in one `editor(action="invoke_object_functions")` call:

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
         "args": { "InPosition": 1.1333333333, "bFireNotifies": false }
       }
     ]
   }
   ```

   `InPosition` is `frame * rateDenominator / rateNumerator`.
4. Open the asset again to focus its window, then
   `editor(action="capture_screenshot", target="window", filename=<png>)`.
5. Repeat with the same camera and window for start, raised pose, both extrema
   and end. Keep the images beside the numeric artifacts.

### Endpoint check

Edit ranges are end-exclusive: the last authorable frame is
`endFrameExclusive - 1`. A baked AnimSequence can also expose a sample at its
exact duration (`sourceFrameCount`), one frame after the last visible authored
frame. Validate both. For a non-looping clip the endpoint must hold the final
pose within tolerance with no adjacent-frame jump. For a loop, pass `loop=true`
and require the seam to match. A large endpoint discontinuity is a failed bake.

## Fixtures

Before trusting a rig or workflow in production, keep these small versioned
fixtures:

| Fixture | Proves |
|---------|--------|
| From-scratch gesture over a neutral source pose | Per-rig discovery, typed controls, dense quaternion keys, bake, numeric checks, fixed-frame review |
| Retarget between two known skeletons | Chain mapping, retarget pose, root behaviour, proportions, target-side axis discovery |
| Copy an animation, switch to IK, edit the hand and elbow target | IK/FK scalar handling, endpoint and pole control, preservation of unedited motion |
| Full-body IK from an existing mesh | Roots, valid chains, goals, solver and effector connections, save and reload |
| Bone or socket contact plus an unrelated edit | Dense constraint solving, smooth transitions, key readback, post-bake QA, preservation outside the constraint |
| Edge cases | Mirroring or negative scale, dense source keys, additive rejection, layered sessions, scalar enums and floats, root motion, loop seams, short clips |

Each records the source, session and output paths, binding tag, engine version,
controls and metadata, probe observations, sampled frames and bones, numeric
checks and screenshots. Human approval is the final visual gate, not a
replacement for the records.
