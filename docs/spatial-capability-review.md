# Spatial awareness and human communication review

UE-MCP has strong engine-side measurements and reversible edits, but the
beta.4 baseline is not yet an optimal interface for agents with weaker spatial
reasoning. Too much depends on converting prose into signed rotations and
manually connecting separate observations. This assessment is based on source
inspection and focused regression tests, not comparative model benchmarks.

Baseline: [`v1.3.1-beta.4`, `1758dc9a`](https://github.com/db-lyon/ue-mcp/tree/1758dc9af455cdd56417a3151521db86aa1aae4e).

## What already works

| Capability | Value to an agent | Source |
|---|---|---|
| Exact actor paths and duplicate-label refusal | Prevents selecting whichever matching actor happens to be first | `Public/HandlerUtils.h`, `MCPResolveActor` |
| Frame-relative centimetres and quaternion nudges | Avoids fragile edits to relative Euler angles | `LevelHandlers_Spatial.cpp`, `NudgeComponent` |
| Before/after transforms and rollback | Separates intent from observed result and permits recovery | `LevelHandlers_Spatial.cpp` |
| Component/bone/socket inspection, traces and transient captures | Supplies engine facts alongside images | `level`, `editor`, `animation` tool categories |
| Per-action schemas, typo suggestions and full/lean/micro contexts | Makes a large action surface discoverable | `action-schema.ts`, `lean-context.ts` |
| User-mediated dialogs, deferred feedback and progress | Preserves human decisions and exposes long-running work | `editor-control.ts`, `tools/feedback.ts`, `types.ts` |

Unreal remains the source of 3D truth. A visual grounding model can identify a
feature in an image, but cannot supply authoritative depth or prove contact.
See [Epic's coordinate-space documentation](https://dev.epicgames.com/documentation/en-us/unreal-engine/coordinate-system-and-spaces-in-unreal-engine).

## Improvements in this PR

| Baseline failure | Change | Why it helps smaller models |
|---|---|---|
| A model must turn "clockwise from above" into signed degrees; the guide even shows the wrong sign | `viewRotation` accepts a named viewpoint, direction and positive magnitude; Unreal resolves the sign | Removes coordinate arithmetic from language reasoning |
| Nudge inspection requires a non-zero mutation | `dryRun=true` returns current state and requested setter inputs without mutation; no delta means inspect | Gives the agent and user a concrete interpretation before editing |
| A screenshot returns a file path with no camera provenance | `captureMetadata` pairs image, camera, world and focused target | Stops accidental use of a different viewport's coordinate system |
| Per-action schemas flatten nested values to `object` | Publish nested properties, array items, enum choices and defaults from the existing Zod declarations | Gives exact field names rather than requiring reconstruction from prose |
| Lean search uses different ranking and micro has no direct search | Reuse the intent search in both compact modes; `describe` accepts `method` for one action | Reduces category dumps and inconsistent discovery |
| Discovery can name disabled actions or another editor's plugin actions | Use the addressed editor's enabled graph, including lean catalogs | Keeps suggested actions callable in the selected project |
| The spatial instruction contract disappears from compact startup guidance | Share a short interpretation and verification block across all modes | Keeps frame/viewpoint and evidence requirements visible |

Existing signed-angle calls and default apply behavior remain compatible.
The preview explicitly distinguishes requested inputs from observed output;
it does not promise a fully solved attachment or physics outcome.

## Human communication

The useful exchange is: identify the exact target, state the intended frame and
viewpoint, show a numerical preview when needed, apply the authorized edit,
then report the measured and visible result. "Move the character's right hand
5 cm forward" normally supplies enough context; "rotate it clockwise" may not.
Ask one specific question only when the missing information changes the edit.

This PR does not add another approval system. Existing client chat handles
ordinary ambiguity; existing elicitation handles the actions that already need
a user-mediated answer. An agent-generated acceptance is not evidence that a
human chose a dialog button. MCP supports structured tool results and leaves
the interaction UI to the client ([MCP tools specification](https://modelcontextprotocol.io/specification/2025-11-25/server/tools)).

For less capable models, favor short named fields, explicit units, exact
identities, actionable errors and bounded discovery. For stronger models, keep
the same contract: additional reasoning should help interpret evidence, not
replace deterministic coordinate conversion. No automatic model routing or
claim that one context mode wins on every model is introduced.

## Remaining priorities

1. **Capture-bound measurement.** Add a projection/deprojection contract tied
   to the captured camera, with trace/depth provenance. The existing live
   editor viewport hit test cannot interpret an arbitrary saved capture.
2. **Repeatable multiview packets.** Capture close front/side/top/three-quarter
   evidence in a fixed pose, with component/socket focus and framing/lighting
   checks. Camera metadata is a prerequisite, not that complete workflow.
3. **Human visual references.** Resolve a clicked point or marked region
   against a named capture and exact actor/component. Preserve the original
   image and annotation; do not guess screen-space depth or silently choose a
   similarly named component.
4. **Measured model evaluation.** Compare unchanged prompts across model
   capability tiers and context modes before choosing defaults. Retain raw
   calls, engine measurements, images and final user-facing claims.

## Evaluation cases

| Case | Pass condition |
|---|---|
| Six named viewpoints, both rotation senses | Rotation projects into the requested clockwise/counterclockwise screen direction |
| Actor rotated in world; attached component has a large existing rotation | Requested frame controls the delta; unrequested location/scale are preserved |
| Dry-run with and without a delta | No transform change, dirty package or rollback operation is created |
| Socket, absolute flags, non-uniform parent scale | State is reported; requested inputs are not described as exact final geometry |
| Duplicate actor labels | Explicit path resolves; ambiguous labels fail without mutation |
| Missing/invalid viewpoint, negative or non-finite magnitude | Explicit rejection before mutation |
| Compact search then single-action describe | Same ranked action and nested argument vocabulary as the full-mode route |
| Headless capture while editor camera differs | Metadata describes the capture camera and selected world, not the viewport |
| Ambiguous request | Agent asks only for the unresolved field that changes the result |
| Valid tool write but dark/occluded evidence | Agent reports visual verification incomplete |

The deterministic cases belong in native/unit/live tests. The final two require
model-run evaluation. Record successful task completion, wrong-direction and
wrong-target edits, unnecessary questions, unsupported success claims, tool
calls and context size. No improvement percentage is claimed without those runs.
