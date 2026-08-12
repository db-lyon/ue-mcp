---
name: ue-mcp-animation
description: Use when creating, modifying, retargeting, rigging, or validating skeletal animation through UE-MCP. Covers the native UE 5.8 Control Rig begin/read/apply/bake loop, per-rig anatomical and mirrored-axis discovery, quaternion keying, deterministic bone analysis, and exact fixed-frame Unreal capture.
---

# UE-MCP native animation workflow

Create quality animation from measured pose constraints, not guessed control
angles. Control names, local axes, palm axes, handedness, and scale are
properties of the selected rig. Discover them again for every unfamiliar rig;
never paste values from another rig or negate a left-side Euler pose to make a
right-side pose.

## Required loop

1. Call `project(action="get_status")`; verify the intended project and editor.
2. Establish the character's authoring baseline before editing clips. Search
   for a Control Rig already bound to the target mesh/skeleton and inspect it
   with `read_control_rig_hierarchy` and `read_control_rig_graph`. It must have
   the controls, Forward Solve, and Backward Solve required for the intended
   edits. If the project or character has no suitable rig, create one first
   with the bundled Epic 5.8 controlrig actions (`epic_create`,
   `epic_import_bones_from_asset`, `epic_add_control`, deliberate Forward Solve
   nodes/links, and `epic_add_backward_solve_graph`), then save the exact rig
   with `asset(epic_save_assets)`. `epic_create` alone creates no imported
   bones, authored controls, or solver wiring. Run an unchanged
   source-to-controls-to-bones round trip on the exact production mesh. Do not
   begin production work on an unverified or merely name-compatible rig.
3. Resolve the source AnimSequence, mesh, skeleton, verified Control Rig, and
   frame rate.
4. Create a versioned session with `begin_control_rig_edit`. Use a unique
   LevelSequence path and `bindingTag`, an end-exclusive frame range, and
   `onConflict="error"`. Use `rigMode="asset"` for a project rig or `"fk"` only
   when generated FK controls are intentional. The source must be non-additive
   and mesh-compatible; flatten an additive clip against its intended base
   first. `layered` controls the session layer, not an additive source base.
   A finite non-zero source `RateScale` is compensated in Sequencer so the raw
   timeline maps once without modifying the source asset; zero is rejected.
5. Call `read_control_rig_edit` at rest, transitions, extrema, and end in both
   `local` and `global` space. Here `global` is rig/global (normally mesh
   component) space, not actor world space.
6. Inspect every control's `controlType`, `animatable`, and enum metadata. Write
   scalars with the matching `set_bool`, `set_float`, or `set_int`; enum values
   must come from `enumOptions`. Never write `animatable=false` controls.
7. Define anatomical component-space targets, then solve proximal to distal:
   shoulder/upper arm, elbow pole and bend, forearm direction, wrist, palm
   normal, then secondary motion. For a wave, the forearm must rise, the wrist
   must sit above the elbow/near the shoulder region, and the probed palm normal
   must face the intended viewer before wrist oscillation is added.
8. When an axis is uncertain, make an immutable probe session. Apply a small
   positive and negative rotation to one local axis at one fixed frame, bake,
   and inspect the resulting component-space shoulder/forearm/hand landmarks
   with `analyze_animation`. Probe the right side separately; mirrored parents
   or negative scale can reverse anatomical meanings. Preserve the full scale
   read from the control.
9. Apply absolute `set_keys` transforms with finite normalized quaternions,
   complete translation/rotationQuaternion/scale payloads, and strictly
   increasing frames. Preserve translation and scale unless intentionally
   editing them. If the source bake has dense keys, key every affected frame;
   sparse keys will not replace the intervening source motion. Apply related
   controls and scalar switches in one transaction.
10. Read back the edited frames in local and global space. Reject elbow flips,
   discontinuities, wrong forearm direction, wrong palm normal, or unexpected
   changes outside the edited chain before baking.
11. Bake to a new versioned AnimSequence with `bake_control_rig_edit`,
    `reduceKeys=false`, and `onConflict="error"`. Never overwrite source,
    another iteration's session, or prior approved output assets.

## Validation and visual review

Run `analyze_animation` on source and output using the same mesh, explicit
frames, and bones. Include root, pelvis, the complete edited chain, feet,
opposite side, and any controls/bones expected to remain unchanged. Write its
native `manifest.json` and `samples.ndjson` beneath
`Saved/Codex/AnimationQA`.

Check numeric integrity, invalid transforms, selected-bone bounds, root
displacement/speed, and loop seam metrics. Derive gesture-specific checks from
component transforms: shoulder-relative wrist height, elbow-to-wrist vector,
elbow angle/plane stability, probed palm-normal alignment, speed/acceleration,
direction changes, and drift in untouched bones. Numeric samples are the source
of truth; screenshots are the human visual gate.

Use the analyzer's `rateScale`, `effectiveDurationSeconds`, and per-notify
`rawTriggerTimeSeconds` / `effectiveTriggerTimeSeconds` fields when validating
gameplay release timing. Effective times use the asset-rate magnitude and are
null when the asset rate is zero.

For an exact native frame capture, without Computer Use or Python:

1. `editor(action="open_asset", assetPath=<baked_anim>)`.
2. `editor(action="find_object")` for `className="AnimSingleNodeInstance"`,
   `nameContains="AnimPreviewInstance"`, `world="any"`; select the match under
   the current `AnimationEditorPreviewActor`.
3. In one `editor(action="invoke_object_functions")` call, invoke `SetPlaying` with
   `bIsPlaying=false`, then `SetPosition` with
   `InPosition=frame*rateDenominator/rateNumerator` and
   `bFireNotifies=false` on that object path.
4. Open the asset again to focus its window and call
   `editor(action="capture_screenshot", target="window")`.
5. Capture start, entry, both extrema, exit, and end with a consistent view.

Keep four versioned V&V fixtures: a from-scratch gesture, a retarget between
known skeletons, a copied animation modified through IK plus its pole target,
and an edge-case pack covering mirrored/negative scale, dense keys,
additive-source rejection/flattening, layered sessions, scalar enums, root
motion, loops, and short clips.

The loop generalizes beyond humanoid arms. Re-discover the rig mapping, then
express legs as hip/knee-pole/foot/contact constraints; spine and head as
arc/twist/aim constraints; tails, tentacles, and ropes as length-preserving
chain curves with delayed phase; and props or mechanisms as pivot, attachment,
contact, and clearance constraints. Only control/bone names, axes/signs,
mirrored scale, limits, and motion constraints are rig-specific.

## Compatibility and endpoint rule

The four Control Rig session actions are UE 5.8 only and return
`unsupported_engine_version` on older engines; do not invent a raw-bone
fallback. `analyze_animation` is cross-version through the native APIs in the
compiled engine. See the public
[Native Control Rig Animation](https://ue-mcp.com/docs/control-rig-animation/)
guide for full call shapes and fixture guidance.

Edit ranges are `[startFrame, endFrameExclusive)`. The bridge keeps an internal
support frame for Unreal's exact-duration export sample without making the
exclusive end authorable. Validate the last visible frame and exact-duration
sample separately. A non-looping endpoint must hold the intended final pose
without an adjacent-frame teleport or rotation jump; a looping endpoint must
pass the requested seam check. A large endpoint discontinuity fails the bake.
