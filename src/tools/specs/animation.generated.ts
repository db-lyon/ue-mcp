// GENERATED FILE - do not edit.
//
// Written by scripts/generate-handler-specs.mjs from tests/golden/handler-specs.json,
// which is recorded from the parameter specs the C++ handlers register with
// (npm run specs:record). To change a parameter, change its RegisterHandler
// spec, re-record, and regenerate (#1057).
import { z } from "zod";
import { makeSpecBp, type HandlerSpecs } from "../../surface/handler-spec.js";

/** The recorded contract of every spec'd animation handler. */
export const handlerSpecs: HandlerSpecs = {
  "add_anim_notify": {
    "category": "animation",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "AnimSequence or AnimMontage asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "notifyName",
        "type": "string",
        "required": true,
        "description": "Notify name"
      },
      {
        "name": "triggerTime",
        "type": "number",
        "required": true,
        "description": "Trigger time in seconds, clamped to the asset length"
      },
      {
        "name": "notifyClass",
        "type": "string",
        "required": false,
        "description": "UAnimNotify class to spawn: a class name, a name without the AnimNotify_ prefix, or a path"
      },
      {
        "name": "notifyProperties",
        "type": "object",
        "required": false,
        "description": "EditAnywhere fields to set on the spawned notify object; requires a notifyClass that resolves"
      },
      {
        "name": "branchingPoint",
        "type": "boolean",
        "required": false,
        "description": "Force the montage notify's tick type. The PlayMontageNotify classes default to true on a montage, everything else to the engine's queued tick (#880)"
      }
    ]
  },
  "add_anim_notify_state": {
    "category": "animation",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "AnimSequence or AnimMontage asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "notifyName",
        "type": "string",
        "required": true,
        "description": "Notify name"
      },
      {
        "name": "notifyStateClass",
        "type": "string",
        "required": true,
        "description": "UAnimNotifyState subclass: a class name, a bare suffix such as TimedParticleEffect, or a full path"
      },
      {
        "name": "triggerTime",
        "type": "number",
        "required": true,
        "description": "Window start in seconds"
      },
      {
        "name": "duration",
        "type": "number",
        "required": true,
        "description": "Window length in seconds, greater than 0"
      },
      {
        "name": "notifyProperties",
        "type": "object",
        "required": false,
        "description": "EditAnywhere fields to set on the spawned notify state object, validated against the class first"
      },
      {
        "name": "branchingPoint",
        "type": "boolean",
        "required": false,
        "description": "On a montage, tick the window as a branching point"
      }
    ]
  },
  "add_blend_sample": {
    "category": "animation",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "BlendSpace or BlendSpace1D asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "animation",
        "type": "string",
        "required": true,
        "description": "AnimSequence to add as a sample"
      },
      {
        "name": "position",
        "type": "object",
        "required": false,
        "description": "Sample position on the blendspace axes; wins over flat x and y",
        "fields": [
          {
            "name": "x",
            "type": "number",
            "required": false,
            "description": "Horizontal axis value"
          },
          {
            "name": "y",
            "type": "number",
            "required": false,
            "description": "Vertical axis value"
          }
        ]
      },
      {
        "name": "x",
        "type": "number",
        "required": false,
        "description": "Horizontal axis value when position is omitted (default 0)"
      },
      {
        "name": "y",
        "type": "number",
        "required": false,
        "description": "Vertical axis value when position is omitted (default 0)"
      }
    ]
  },
  "add_curve": {
    "category": "animation",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "AnimSequence asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "curveName",
        "type": "string",
        "required": true,
        "description": "Float curve to add"
      }
    ]
  },
  "add_montage_section": {
    "category": "animation",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "AnimMontage asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "sectionName",
        "type": "string",
        "required": true,
        "description": "Composite section to add"
      },
      {
        "name": "startTime",
        "type": "number",
        "required": false,
        "description": "Section start in seconds; taken from the segment when segmentIndex is given"
      },
      {
        "name": "linkedSection",
        "type": "string",
        "required": false,
        "description": "Next section to link to"
      },
      {
        "name": "segmentIndex",
        "type": "number",
        "required": false,
        "description": "Segment to anchor the section to, so it moves with that segment (#826)"
      },
      {
        "name": "slotName",
        "type": "string",
        "required": false,
        "description": "Slot holding the anchor segment; wins over slotIndex"
      },
      {
        "name": "slotIndex",
        "type": "number",
        "required": false,
        "description": "Slot index holding the anchor segment (default 0)"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "skip (default) returns an existing section untouched, error refuses"
      }
    ]
  },
  "add_montage_segment": {
    "category": "animation",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "AnimMontage asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "animSequencePath",
        "type": "string",
        "required": true,
        "description": "AnimSequence or AnimComposite to append as a segment"
      },
      {
        "name": "slotName",
        "type": "string",
        "required": false,
        "description": "Target slot, created when absent"
      },
      {
        "name": "slotIndex",
        "type": "number",
        "required": false,
        "description": "Target slot index when slotName is omitted (default 0)"
      },
      {
        "name": "startPos",
        "type": "number",
        "required": false,
        "description": "Trim start inside the source, in seconds (default 0)"
      },
      {
        "name": "endPos",
        "type": "number",
        "required": false,
        "description": "Trim end inside the source, in seconds (default the source play length)"
      },
      {
        "name": "playRate",
        "type": "number",
        "required": false,
        "description": "Segment play rate; negative plays in reverse (default 1)"
      },
      {
        "name": "loopCount",
        "type": "integer",
        "required": false,
        "description": "How many times the segment repeats (default 1)"
      },
      {
        "name": "insertIndex",
        "type": "integer",
        "required": false,
        "description": "Position in the slot's segment list (default appends)"
      }
    ]
  },
  "add_motion_matching_node": {
    "category": "animation",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "AnimBlueprint asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "databasePath",
        "type": "string",
        "required": false,
        "description": "PoseSearchDatabase the node searches"
      },
      {
        "name": "graphName",
        "type": "string",
        "required": false,
        "description": "Graph to add the node to (default AnimGraph)"
      },
      {
        "name": "connectToOutput",
        "type": "boolean",
        "required": false,
        "description": "Wire the node to the Output Pose (default true)"
      },
      {
        "name": "blendTime",
        "type": "number",
        "required": false,
        "description": "Inertial blend time"
      }
    ]
  },
  "add_pose_history_node": {
    "category": "animation",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "AnimBlueprint asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "graphName",
        "type": "string",
        "required": false,
        "description": "Graph to add the node to (default AnimGraph)"
      },
      {
        "name": "poseCount",
        "type": "number",
        "required": false,
        "description": "History poses to retain"
      },
      {
        "name": "samplingInterval",
        "type": "number",
        "required": false,
        "description": "Seconds between history samples"
      },
      {
        "name": "generateTrajectory",
        "type": "boolean",
        "required": false,
        "description": "Self-generate the trajectory (default true)"
      },
      {
        "name": "trajectoryHistoryCount",
        "type": "number",
        "required": false,
        "description": "Generated trajectory history samples"
      },
      {
        "name": "trajectoryPredictionCount",
        "type": "number",
        "required": false,
        "description": "Generated trajectory prediction samples"
      },
      {
        "name": "insertBeforeOutput",
        "type": "boolean",
        "required": false,
        "description": "Splice into the pose chain feeding the Output Pose (default true)"
      }
    ]
  },
  "add_pose_search_schema_pose_channel": {
    "category": "animation",
    "params": [
      {
        "name": "schemaPath",
        "type": "string",
        "required": true,
        "description": "PoseSearchSchema to add the channel to",
        "aliases": [
          "assetPath"
        ]
      },
      {
        "name": "bones",
        "type": "array",
        "required": true,
        "description": "Sampled bones, each a bone name (position only) or {bone, flags?: velocity | position | rotation | phase, weight?}"
      },
      {
        "name": "weight",
        "type": "number",
        "required": false,
        "description": "Channel weight"
      }
    ]
  },
  "add_pose_search_schema_trajectory_channel": {
    "category": "animation",
    "params": [
      {
        "name": "schemaPath",
        "type": "string",
        "required": true,
        "description": "PoseSearchSchema to add the channel to",
        "aliases": [
          "assetPath"
        ]
      },
      {
        "name": "samples",
        "type": "array",
        "required": true,
        "description": "[{offset, flags?, weight?}]: offset in seconds, negative for history and positive for prediction; flags from position, velocity, facingDirection, velocityDirection and their XY variants",
        "items": "object"
      },
      {
        "name": "weight",
        "type": "number",
        "required": false,
        "description": "Channel weight"
      }
    ]
  },
  "add_pose_search_sequence": {
    "category": "animation",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "PoseSearchDatabase asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "sequencePath",
        "type": "string",
        "required": true,
        "description": "AnimSequence, AnimComposite, AnimMontage or BlendSpace to append"
      },
      {
        "name": "mirror",
        "type": "string",
        "required": false,
        "description": "original | mirrored | both"
      },
      {
        "name": "disableReselection",
        "type": "boolean",
        "required": false,
        "description": "Disallow reselecting poses from the same asset"
      },
      {
        "name": "sampleStart",
        "type": "number",
        "required": false,
        "description": "Sampling range start in seconds"
      },
      {
        "name": "sampleEnd",
        "type": "number",
        "required": false,
        "description": "Sampling range end in seconds"
      },
      {
        "name": "enabled",
        "type": "boolean",
        "required": false,
        "description": "Include the clip in the database"
      }
    ]
  },
  "add_sequence_evaluator": {
    "category": "animation",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "AnimBlueprint asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "sequencePath",
        "type": "string",
        "required": false,
        "description": "AnimSequence to evaluate"
      },
      {
        "name": "graphName",
        "type": "string",
        "required": false,
        "description": "AnimGraph (default) or a state's name for its inner graph"
      },
      {
        "name": "explicitTime",
        "type": "number",
        "required": false,
        "description": "Initial ExplicitTime"
      },
      {
        "name": "shouldLoop",
        "type": "boolean",
        "required": false,
        "description": "bShouldLoop"
      },
      {
        "name": "teleportToExplicitTime",
        "type": "boolean",
        "required": false,
        "description": "bTeleportToExplicitTime (default false, so time advances and root motion extracts)"
      },
      {
        "name": "connectToOutput",
        "type": "boolean",
        "required": false,
        "description": "Wire the node to the graph's result pose (default true)"
      }
    ]
  },
  "add_state": {
    "category": "animation",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "AnimBlueprint asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "stateMachineName",
        "type": "string",
        "required": true,
        "description": "State machine to edit"
      },
      {
        "name": "stateName",
        "type": "string",
        "required": true,
        "description": "State to add"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "skip (default) returns an existing state untouched, error refuses"
      }
    ]
  },
  "add_transition": {
    "category": "animation",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "AnimBlueprint asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "stateMachineName",
        "type": "string",
        "required": true,
        "description": "State machine to edit"
      },
      {
        "name": "fromState",
        "type": "string",
        "required": true,
        "description": "State the transition leaves"
      },
      {
        "name": "toState",
        "type": "string",
        "required": true,
        "description": "State the transition enters"
      },
      {
        "name": "blendDuration",
        "type": "number",
        "required": false,
        "description": "Crossfade in seconds (engine default 0.2)"
      },
      {
        "name": "blendLogic",
        "type": "string",
        "required": false,
        "description": "Standard | Inertialization"
      }
    ]
  },
  "add_virtual_bone": {
    "category": "animation",
    "params": [
      {
        "name": "skeletonPath",
        "type": "string",
        "required": true,
        "description": "USkeleton asset path"
      },
      {
        "name": "sourceBone",
        "type": "string",
        "required": true,
        "description": "Bone the virtual bone starts from"
      },
      {
        "name": "targetBone",
        "type": "string",
        "required": true,
        "description": "Bone the virtual bone points at"
      }
    ]
  },
  "analyze_animation": {
    "category": "animation",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "AnimSequence asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "skeletalMeshPath",
        "type": "string",
        "required": false,
        "description": "SkeletalMesh whose proportions to sample with; must be compatible with the sequence's skeleton"
      },
      {
        "name": "boneNames",
        "type": "array",
        "required": false,
        "description": "Bones to sample (default root, pelvis, head, hands and feet when present)",
        "items": "string"
      },
      {
        "name": "frames",
        "type": "array",
        "required": false,
        "description": "Explicit frames to sample, integers in [0, frame count]",
        "items": "number"
      },
      {
        "name": "sampleRate",
        "type": "number",
        "required": false,
        "description": "Samples per second when frames is omitted, 1 to 240 (default the source rate)"
      },
      {
        "name": "loop",
        "type": "boolean",
        "required": false,
        "description": "Include end-to-start loop continuity metrics"
      },
      {
        "name": "facingBones",
        "type": "array",
        "required": false,
        "description": "[boneA, boneB]: report the yaw of the boneA to boneB vector relative to the root bone's forward, per sample and over the clip",
        "items": "string"
      },
      {
        "name": "outputDirectory",
        "type": "string",
        "required": false,
        "description": "Directory under Project/Saved/Codex/AnimationQA for analysis artifacts; must not already contain them"
      }
    ]
  },
  "apply_animation_modifier": {
    "category": "animation",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "AnimSequence asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "modifierClass",
        "type": "string",
        "required": true,
        "description": "UAnimationModifier subclass: a short name such as DistanceCurveModifier, or a /Script path",
        "aliases": [
          "modifier"
        ]
      },
      {
        "name": "props",
        "type": "object",
        "required": false,
        "description": "EditAnywhere property values to set on the modifier before it runs"
      }
    ]
  },
  "apply_control_rig_edits": {
    "category": "animation",
    "params": [
      {
        "name": "sequencePath",
        "type": "string",
        "required": true,
        "description": "LevelSequence holding the Control Rig edit session"
      },
      {
        "name": "bindingTag",
        "type": "string",
        "required": true,
        "description": "Edit-session natural key from begin_control_rig_edit"
      },
      {
        "name": "operations",
        "type": "array",
        "required": true,
        "description": "Typed edits applied in one transaction, in order",
        "items": "object",
        "oneOf": {
          "key": "op",
          "variants": [
            {
              "tag": "set",
              "description": "Write one full absolute transform at frame or frames",
              "fields": [
                {
                  "name": "control",
                  "type": "string",
                  "required": true,
                  "description": "Control name on the session's rig"
                },
                {
                  "name": "frame",
                  "type": "integer",
                  "required": false,
                  "description": "One frame to key; pass exactly one of frame and frames"
                },
                {
                  "name": "frames",
                  "type": "array",
                  "required": false,
                  "description": "Frames to key; pass exactly one of frame and frames",
                  "items": "integer"
                },
                {
                  "name": "transform",
                  "type": "object",
                  "required": true,
                  "description": "{translation {x,y,z}, rotationDegrees {pitch,yaw,roll}, scale {x,y,z}}"
                },
                {
                  "name": "space",
                  "type": "string",
                  "required": false,
                  "description": "local (default) | component | global, where global is an alias for component"
                }
              ]
            },
            {
              "tag": "set_keys",
              "description": "Write strictly ordered full per-frame transforms from normalized quaternions",
              "fields": [
                {
                  "name": "control",
                  "type": "string",
                  "required": true,
                  "description": "Control name on the session's rig"
                },
                {
                  "name": "keys",
                  "type": "array",
                  "required": true,
                  "description": "[{frame, transform {translation {x,y,z}, rotationQuaternion {x,y,z,w}, scale {x,y,z}}}], frames strictly increasing",
                  "items": "object"
                },
                {
                  "name": "space",
                  "type": "string",
                  "required": false,
                  "description": "local (default) | component | global, where global is an alias for component"
                }
              ]
            },
            {
              "tag": "offset",
              "description": "Apply translation, rotation or scale deltas across an inclusive frame range",
              "fields": [
                {
                  "name": "control",
                  "type": "string",
                  "required": true,
                  "description": "Control name on the session's rig"
                },
                {
                  "name": "startFrame",
                  "type": "integer",
                  "required": true,
                  "description": "First frame of the range"
                },
                {
                  "name": "endFrame",
                  "type": "integer",
                  "required": true,
                  "description": "Last frame of the range, at or after startFrame"
                },
                {
                  "name": "translationCm",
                  "type": "vec3",
                  "required": false,
                  "description": "Translation delta in centimetres"
                },
                {
                  "name": "rotationDegrees",
                  "type": "rotator",
                  "required": false,
                  "description": "Rotation delta in degrees"
                },
                {
                  "name": "scaleMultiplier",
                  "type": "vec3",
                  "required": false,
                  "description": "Scale multiplier; one of translationCm, rotationDegrees and scaleMultiplier is required"
                },
                {
                  "name": "space",
                  "type": "string",
                  "required": false,
                  "description": "local (default) | component | global, where global is an alias for component"
                },
                {
                  "name": "blendInFrames",
                  "type": "integer",
                  "required": false,
                  "description": "Frames to ease in over, from 0 (default 0)"
                },
                {
                  "name": "blendOutFrames",
                  "type": "integer",
                  "required": false,
                  "description": "Frames to ease out over, from 0 (default 0)"
                }
              ]
            },
            {
              "tag": "contact_lock",
              "description": "Constrain a translatable driver control, or a driven bone or socket, to a fixed component-space target",
              "fields": [
                {
                  "name": "control",
                  "type": "string",
                  "required": true,
                  "description": "Control name on the session's rig"
                },
                {
                  "name": "drivenReference",
                  "type": "string",
                  "required": false,
                  "description": "Bone or socket to constrain instead of the control; bake and analyze before accepting it"
                },
                {
                  "name": "startFrame",
                  "type": "integer",
                  "required": true,
                  "description": "First frame of the lock"
                },
                {
                  "name": "endFrame",
                  "type": "integer",
                  "required": true,
                  "description": "Last frame of the lock, at or after startFrame"
                },
                {
                  "name": "target",
                  "type": "object",
                  "required": false,
                  "description": "{translation {x,y,z}, rotationQuaternion? {x,y,z,w}}: a fixed component-space target, or a transform relative to targetReference when that is set; pass target or targetReference"
                },
                {
                  "name": "targetReference",
                  "type": "string",
                  "required": false,
                  "description": "Source-animation bone or socket to follow. Without target, the first frame's subject-to-reference offset is kept"
                },
                {
                  "name": "blendInFrames",
                  "type": "integer",
                  "required": false,
                  "description": "Frames to ease in over, from 0 (default 0)"
                },
                {
                  "name": "blendOutFrames",
                  "type": "integer",
                  "required": false,
                  "description": "Frames to ease out over, from 0 (default 0)"
                },
                {
                  "name": "stabilizeControls",
                  "type": "array",
                  "required": false,
                  "description": "Up to 8 pole or stabilizer controls to hold steady; never the locked control itself",
                  "items": "string"
                },
                {
                  "name": "positionToleranceCm",
                  "type": "number",
                  "required": false,
                  "description": "Readback position tolerance, above 0 and up to 100 (default 0.1)"
                },
                {
                  "name": "rotationToleranceDegrees",
                  "type": "number",
                  "required": false,
                  "description": "Readback rotation tolerance, above 0 and up to 180 (default 0.5)"
                }
              ]
            },
            {
              "tag": "set_bool",
              "description": "Key a bool control at frame or frames",
              "fields": [
                {
                  "name": "control",
                  "type": "string",
                  "required": true,
                  "description": "Control name on the session's rig"
                },
                {
                  "name": "frame",
                  "type": "integer",
                  "required": false,
                  "description": "One frame to key; pass exactly one of frame and frames"
                },
                {
                  "name": "frames",
                  "type": "array",
                  "required": false,
                  "description": "Frames to key; pass exactly one of frame and frames",
                  "items": "integer"
                },
                {
                  "name": "value",
                  "type": "boolean",
                  "required": true,
                  "description": "Value to key"
                }
              ]
            },
            {
              "tag": "set_float",
              "description": "Key a float control at frame or frames",
              "fields": [
                {
                  "name": "control",
                  "type": "string",
                  "required": true,
                  "description": "Control name on the session's rig"
                },
                {
                  "name": "frame",
                  "type": "integer",
                  "required": false,
                  "description": "One frame to key; pass exactly one of frame and frames"
                },
                {
                  "name": "frames",
                  "type": "array",
                  "required": false,
                  "description": "Frames to key; pass exactly one of frame and frames",
                  "items": "integer"
                },
                {
                  "name": "value",
                  "type": "number",
                  "required": true,
                  "description": "Finite value to key"
                }
              ]
            },
            {
              "tag": "set_int",
              "description": "Key an integer or enum control at frame or frames; an enum takes one of its enumOptions values",
              "fields": [
                {
                  "name": "control",
                  "type": "string",
                  "required": true,
                  "description": "Control name on the session's rig"
                },
                {
                  "name": "frame",
                  "type": "integer",
                  "required": false,
                  "description": "One frame to key; pass exactly one of frame and frames"
                },
                {
                  "name": "frames",
                  "type": "array",
                  "required": false,
                  "description": "Frames to key; pass exactly one of frame and frames",
                  "items": "integer"
                },
                {
                  "name": "value",
                  "type": "integer",
                  "required": true,
                  "description": "Value to key"
                }
              ]
            },
            {
              "tag": "propagate_pose",
              "description": "Key the controls that changed between two live snapshots across each control's donor frames",
              "fields": [
                {
                  "name": "baseline",
                  "type": "object",
                  "required": true,
                  "description": "Snapshot from capture_control_rig_pose before the edit"
                },
                {
                  "name": "accepted",
                  "type": "object",
                  "required": true,
                  "description": "Snapshot from capture_control_rig_pose after the edit, same session, rig instance, frame and control set"
                },
                {
                  "name": "controls",
                  "type": "array",
                  "required": true,
                  "description": "[{control, mode fixed | local_delta, donorFrames strictly increasing}], each control once; bool, enum and int controls take fixed only",
                  "items": "object"
                }
              ]
            }
          ]
        }
      }
    ]
  },
  "author_blend_profile": {
    "category": "animation",
    "params": [
      {
        "name": "skeletonPath",
        "type": "string",
        "required": true,
        "description": "USkeleton that owns the blend profile"
      },
      {
        "name": "profileName",
        "type": "string",
        "required": true,
        "description": "Blend profile to create or edit"
      },
      {
        "name": "operation",
        "type": "string",
        "required": false,
        "description": "upsert (default) | remove | rename"
      },
      {
        "name": "newProfileName",
        "type": "string",
        "required": false,
        "description": "New name when operation=rename"
      },
      {
        "name": "mode",
        "type": "string",
        "required": false,
        "description": "TimeFactor | WeightFactor | BlendMask"
      },
      {
        "name": "entries",
        "type": "array",
        "required": false,
        "description": "Per-bone scales as [{bone, scale, recursive?}]",
        "items": "object"
      },
      {
        "name": "removeEntries",
        "type": "array",
        "required": false,
        "description": "Bone names to drop from the profile",
        "items": "string"
      }
    ]
  },
  "author_montages_batch": {
    "category": "animation",
    "params": [
      {
        "name": "items",
        "type": "array",
        "required": true,
        "description": "Montages to author, each reported on its own",
        "items": "object",
        "fields": [
          {
            "name": "name",
            "type": "string",
            "required": true,
            "description": "AnimMontage asset name"
          },
          {
            "name": "animSequencePath",
            "type": "string",
            "required": true,
            "description": "AnimSequence the montage plays"
          },
          {
            "name": "packagePath",
            "type": "string",
            "required": false,
            "description": "Destination folder (default /Game/Animations)"
          },
          {
            "name": "onConflict",
            "type": "string",
            "required": false,
            "description": "skip (default) | error; any other value fails the item"
          },
          {
            "name": "slotName",
            "type": "string",
            "required": false,
            "description": "Slot name to write onto the track"
          },
          {
            "name": "trackIndex",
            "type": "integer",
            "required": false,
            "description": "Slot track index (default 0)"
          },
          {
            "name": "rateScale",
            "type": "number",
            "required": false,
            "description": "Playback rate scale"
          },
          {
            "name": "blendIn",
            "type": "number",
            "required": false,
            "description": "Blend-in time in seconds"
          },
          {
            "name": "blendOut",
            "type": "number",
            "required": false,
            "description": "Blend-out time in seconds"
          },
          {
            "name": "sequenceLength",
            "type": "number",
            "required": false,
            "description": "Montage length in seconds"
          },
          {
            "name": "sections",
            "type": "array",
            "required": false,
            "description": "Sections as [{sectionName, startTime?, linkedSection?}]",
            "items": "object"
          },
          {
            "name": "notifies",
            "type": "array",
            "required": false,
            "description": "Notifies as [{notifyName, triggerTime, notifyClass?, properties?}]",
            "items": "object"
          }
        ]
      }
    ]
  },
  "auto_align_retarget_pose": {
    "category": "animation",
    "params": [
      {
        "name": "retargeterPath",
        "type": "string",
        "required": true,
        "description": "Existing IKRetargeter to edit",
        "aliases": [
          "assetPath"
        ]
      },
      {
        "name": "side",
        "type": "string",
        "required": false,
        "description": "source | target (default target)"
      }
    ]
  },
  "bake_control_rig_edit": {
    "category": "animation",
    "params": [
      {
        "name": "sequencePath",
        "type": "string",
        "required": true,
        "description": "LevelSequence holding the Control Rig edit session"
      },
      {
        "name": "bindingTag",
        "type": "string",
        "required": true,
        "description": "Edit-session natural key from begin_control_rig_edit"
      },
      {
        "name": "outputAssetPath",
        "type": "string",
        "required": true,
        "description": "Destination AnimSequence asset path"
      },
      {
        "name": "frameRate",
        "type": "number",
        "required": false,
        "description": "Frames per second of the bake (default the sequence's display rate)"
      },
      {
        "name": "reduceKeys",
        "type": "boolean",
        "required": false,
        "description": "Key reduction is not supported yet; omit or pass false",
        "literal": false
      },
      {
        "name": "tolerance",
        "type": "number",
        "required": false,
        "description": "Key-reduction tolerance (default 0.001)"
      },
      {
        "name": "createLink",
        "type": "boolean",
        "required": false,
        "description": "Sequencer links are not supported yet; omit or pass false",
        "literal": false
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "skip returns an existing output, error (default) refuses; it never overwrites"
      }
    ],
    "contractExempt": "5.8 only: before 5.8 the handler is a stub that reads nothing, and on 5.8 it bakes and saves a new AnimSequence"
  },
  "bake_keyframes_batch": {
    "category": "animation",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "AnimSequence asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "tracks",
        "type": "array",
        "required": true,
        "description": "Per-bone key arrays; a missing track is created",
        "items": "object",
        "fields": [
          {
            "name": "bone",
            "type": "string",
            "required": true,
            "description": "Bone whose track to replace"
          },
          {
            "name": "keyframes",
            "type": "array",
            "required": true,
            "description": "One key per frame as {location?, rotation? {x, y, z, w}, scale?}",
            "items": "object"
          }
        ]
      },
      {
        "name": "save",
        "type": "boolean",
        "required": false,
        "description": "Save the asset after baking (default true)"
      }
    ]
  },
  "bake_root_motion_from_bone": {
    "category": "animation",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "AnimSequence asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "sourceBone",
        "type": "string",
        "required": true,
        "description": "Bone whose translation moves onto the root, e.g. pelvis"
      },
      {
        "name": "rootBone",
        "type": "string",
        "required": false,
        "description": "Root bone name (default root)"
      },
      {
        "name": "axes",
        "type": "array",
        "required": false,
        "description": "Axes to bake: x, y, z (default [x, y])",
        "items": "string"
      },
      {
        "name": "interpolation",
        "type": "string",
        "required": false,
        "description": "linear (default) | per_frame"
      }
    ]
  },
  "batch_retarget_animations": {
    "category": "animation",
    "params": [
      {
        "name": "retargeterPath",
        "type": "string",
        "required": true,
        "description": "IKRetargeter to bake through",
        "aliases": [
          "assetPath"
        ]
      },
      {
        "name": "sourceMesh",
        "type": "string",
        "required": true,
        "description": "SkeletalMesh the source animations play on"
      },
      {
        "name": "targetMesh",
        "type": "string",
        "required": true,
        "description": "SkeletalMesh to retarget onto"
      },
      {
        "name": "animPaths",
        "type": "array",
        "required": true,
        "description": "AnimSequences to bake, without duplicates",
        "items": "string"
      },
      {
        "name": "outputPath",
        "type": "string",
        "required": false,
        "description": "Destination folder (default beside each source)"
      },
      {
        "name": "prefix",
        "type": "string",
        "required": false,
        "description": "Output name prefix"
      },
      {
        "name": "suffix",
        "type": "string",
        "required": false,
        "description": "Output name suffix (default _Retargeted)"
      },
      {
        "name": "overwrite",
        "type": "boolean",
        "required": false,
        "description": "Overwriting is not supported; omit or pass false",
        "literal": false
      },
      {
        "name": "requireCompleteMapping",
        "type": "boolean",
        "required": false,
        "description": "Refuse when any target chain is unmapped (default false)"
      }
    ]
  },
  "begin_control_rig_edit": {
    "category": "animation",
    "params": [
      {
        "name": "sequencePath",
        "type": "string",
        "required": true,
        "description": "LevelSequence to create, or to reuse with onConflict=skip"
      },
      {
        "name": "skeletalMeshPath",
        "type": "string",
        "required": true,
        "description": "SkeletalMesh the session binds"
      },
      {
        "name": "sourceAnimationPath",
        "type": "string",
        "required": true,
        "description": "AnimSequence to bake into the rig"
      },
      {
        "name": "rigMode",
        "type": "string",
        "required": false,
        "description": "fk (default) | asset. Use asset with the verified baseline controlRigPath; use fk only when generated raw FK controls are the intended editing surface"
      },
      {
        "name": "controlRigPath",
        "type": "string",
        "required": false,
        "description": "The verified baseline ControlRigBlueprint for this character; required when rigMode=asset. Create and validate the rig first when the character has none"
      },
      {
        "name": "layered",
        "type": "boolean",
        "required": false,
        "description": "Keep the source animation track active under the Control Rig layer (default false)"
      },
      {
        "name": "startFrame",
        "type": "integer",
        "required": false,
        "description": "Inclusive edit range start frame (default 0)"
      },
      {
        "name": "endFrame",
        "type": "integer",
        "required": false,
        "description": "Exclusive edit range end frame (default the source length)"
      },
      {
        "name": "displayRate",
        "type": "number",
        "required": false,
        "description": "LevelSequence display rate in frames per second (default the source rate)"
      },
      {
        "name": "bindingTag",
        "type": "string",
        "required": false,
        "description": "Stable natural key the later calls address (default derived from the mesh name)"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "skip returns the existing session, error (default) refuses; an existing session is never modified"
      }
    ],
    "contractExempt": "5.8 only: before 5.8 the handler is a stub that reads nothing, and on 5.8 it creates and saves a LevelSequence"
  },
  "begin_skeleton_edit": {
    "category": "animation",
    "params": [
      {
        "name": "skeletalMeshPath",
        "type": "string",
        "required": true,
        "description": "SkeletalMesh whose reference skeleton to edit"
      },
      {
        "name": "sessionTag",
        "type": "string",
        "required": false,
        "description": "Stable key every later call addresses (default Skel_<MeshName>)"
      }
    ]
  },
  "bind_anim_node_function": {
    "category": "animation",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "AnimBlueprint asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "nodeGuid",
        "type": "string",
        "required": true,
        "description": "Anim graph node to bind, from add_sequence_evaluator or an add_*_node action",
        "aliases": [
          "nodeId"
        ]
      },
      {
        "name": "functionName",
        "type": "string",
        "required": true,
        "description": "Existing thread-safe anim-node function on the AnimBlueprint",
        "aliases": [
          "function"
        ]
      },
      {
        "name": "graphName",
        "type": "string",
        "required": false,
        "description": "Graph holding the node (default AnimGraph)"
      },
      {
        "name": "binding",
        "type": "string",
        "required": false,
        "description": "update (default) | becomeRelevant | initialUpdate"
      }
    ]
  },
  "build_pose_search_index": {
    "category": "animation",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "PoseSearchDatabase asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "wait",
        "type": "boolean",
        "required": false,
        "description": "Block until the build resolves (default true)"
      }
    ]
  },
  "cancel_skeleton_edit": {
    "category": "animation",
    "params": [
      {
        "name": "sessionTag",
        "type": "string",
        "required": false,
        "description": "The open skeleton edit session to address; wins over skeletalMeshPath"
      },
      {
        "name": "skeletalMeshPath",
        "type": "string",
        "required": false,
        "description": "The skeletal mesh whose one open session to address, when sessionTag is omitted"
      }
    ]
  },
  "capture_control_rig_pose": {
    "category": "animation",
    "params": [
      {
        "name": "sequencePath",
        "type": "string",
        "required": true,
        "description": "LevelSequence holding the Control Rig edit session"
      },
      {
        "name": "bindingTag",
        "type": "string",
        "required": true,
        "description": "Edit-session natural key from begin_control_rig_edit"
      },
      {
        "name": "controlNames",
        "type": "array",
        "required": false,
        "description": "Live controls to capture (default the current Control Rig selection)",
        "items": "string"
      }
    ]
  },
  "commit_skeleton_edit": {
    "category": "animation",
    "params": [
      {
        "name": "sessionTag",
        "type": "string",
        "required": false,
        "description": "The open skeleton edit session to address; wins over skeletalMeshPath"
      },
      {
        "name": "skeletalMeshPath",
        "type": "string",
        "required": false,
        "description": "The skeletal mesh whose one open session to address, when sessionTag is omitted"
      }
    ]
  },
  "compare_curves_to_morph_targets": {
    "category": "animation",
    "params": [
      {
        "name": "animPath",
        "type": "string",
        "required": true,
        "description": "AnimSequence or PoseAsset whose curve names to compare",
        "aliases": [
          "assetPath"
        ]
      },
      {
        "name": "skeletalMeshPath",
        "type": "string",
        "required": true,
        "description": "SkeletalMesh whose morph targets to compare against",
        "aliases": [
          "meshPath"
        ]
      }
    ]
  },
  "configure_ik_retargeter": {
    "category": "animation",
    "params": [
      {
        "name": "retargeterPath",
        "type": "string",
        "required": true,
        "description": "Existing IKRetargeter to configure"
      },
      {
        "name": "sourceRig",
        "type": "string",
        "required": false,
        "description": "Source IKRigDefinition to assign"
      },
      {
        "name": "targetRig",
        "type": "string",
        "required": false,
        "description": "Target IKRigDefinition to assign"
      },
      {
        "name": "sourcePreviewMesh",
        "type": "string",
        "required": false,
        "description": "Source preview SkeletalMesh"
      },
      {
        "name": "targetPreviewMesh",
        "type": "string",
        "required": false,
        "description": "Target preview SkeletalMesh"
      },
      {
        "name": "ensureDefaultOps",
        "type": "boolean",
        "required": false,
        "description": "Ensure the complete default op stack (default true)"
      },
      {
        "name": "autoMapMode",
        "type": "string",
        "required": false,
        "description": "Native chain auto-map: exact | fuzzy | clear"
      },
      {
        "name": "forceRemap",
        "type": "boolean",
        "required": false,
        "description": "Replace existing mappings during auto-map (default false)"
      },
      {
        "name": "chainMappings",
        "type": "array",
        "required": false,
        "description": "Explicit target-to-source chain overrides, at most 10000",
        "items": "object",
        "fields": [
          {
            "name": "targetChain",
            "type": "string",
            "required": true,
            "description": "Target chain to map"
          },
          {
            "name": "sourceChain",
            "type": "any",
            "required": false,
            "description": "Source chain name; null or omitted clears the mapping"
          }
        ]
      },
      {
        "name": "ops",
        "type": "array",
        "required": false,
        "description": "Per-op writes, at most 64. settings writes properties on the op's own settings struct, and chainSettings merges per-chain FK properties into the named chain rather than replacing the list",
        "items": "object",
        "fields": [
          {
            "name": "name",
            "type": "string",
            "required": false,
            "description": "Op to write, by name"
          },
          {
            "name": "index",
            "type": "integer",
            "required": false,
            "description": "Op to write, by stack index, when name is omitted"
          },
          {
            "name": "enabled",
            "type": "boolean",
            "required": false,
            "description": "Enable or disable the op"
          },
          {
            "name": "settings",
            "type": "object",
            "required": false,
            "description": "Properties on the op's settings struct, e.g. RootMotionSource"
          },
          {
            "name": "chainSettings",
            "type": "array",
            "required": false,
            "description": "Per-chain properties as [{chain, ...}], e.g. RotationMode",
            "items": "object"
          }
        ]
      },
      {
        "name": "pose",
        "type": "object",
        "required": false,
        "description": "Named source or target retarget pose to author",
        "fields": [
          {
            "name": "side",
            "type": "string",
            "required": true,
            "description": "source | target"
          },
          {
            "name": "name",
            "type": "string",
            "required": true,
            "description": "Retarget pose name"
          },
          {
            "name": "create",
            "type": "boolean",
            "required": false,
            "description": "Create the pose (default false)"
          },
          {
            "name": "reset",
            "type": "boolean",
            "required": false,
            "description": "Reset the pose first, which whole-pose auto-align requires on an existing pose"
          },
          {
            "name": "autoAlign",
            "type": "string",
            "required": false,
            "description": "chain_to_chain | mesh_to_mesh | local_axes | global_axes; needs both preview meshes"
          },
          {
            "name": "bones",
            "type": "array",
            "required": false,
            "description": "Bones to auto-align; omitted aligns every bone, which replaces the pose",
            "items": "string"
          },
          {
            "name": "rotationOffsets",
            "type": "array",
            "required": false,
            "description": "Offsets as [{bone, rotationQuaternion {x, y, z, w}}], normalized",
            "items": "object"
          },
          {
            "name": "rootOffsetZ",
            "type": "number",
            "required": false,
            "description": "Root height offset; exclusive with snapBoneToGround"
          },
          {
            "name": "snapBoneToGround",
            "type": "string",
            "required": false,
            "description": "Bone to snap to the ground; exclusive with rootOffsetZ"
          }
        ]
      }
    ]
  },
  "configure_ik_rig": {
    "category": "animation",
    "params": [
      {
        "name": "rigPath",
        "type": "string",
        "required": true,
        "description": "Existing IKRigDefinition to configure"
      },
      {
        "name": "autoSetup",
        "type": "string",
        "required": false,
        "description": "Native setup pass: retarget | full_body, which installs the retarget definition and then Full Body IK"
      },
      {
        "name": "retargetRoot",
        "type": "string",
        "required": false,
        "description": "Retarget root bone"
      },
      {
        "name": "rootMotionBone",
        "type": "string",
        "required": false,
        "description": "Root-motion bone"
      },
      {
        "name": "chains",
        "type": "array",
        "required": false,
        "description": "Retarget chains to upsert, at most 256",
        "items": "object",
        "fields": [
          {
            "name": "name",
            "type": "string",
            "required": true,
            "description": "Retarget chain name"
          },
          {
            "name": "startBone",
            "type": "string",
            "required": true,
            "description": "First bone of the chain"
          },
          {
            "name": "endBone",
            "type": "string",
            "required": true,
            "description": "Last bone of the chain"
          },
          {
            "name": "goal",
            "type": "string",
            "required": false,
            "description": "IK goal the chain drives"
          }
        ]
      },
      {
        "name": "fullBodyIK",
        "type": "object",
        "required": false,
        "description": "Full Body IK solver and its desired goals",
        "fields": [
          {
            "name": "solverIndex",
            "type": "integer",
            "required": false,
            "description": "Full Body IK solver to edit (default the first)"
          },
          {
            "name": "rootBone",
            "type": "string",
            "required": true,
            "description": "Solver root bone"
          },
          {
            "name": "enabled",
            "type": "boolean",
            "required": false,
            "description": "Enable or disable the solver"
          },
          {
            "name": "goals",
            "type": "array",
            "required": true,
            "description": "Up to 256 goals as {name, bone, positionAlpha?, rotationAlpha?, chainDepth?, strengthAlpha?, pullChainAlpha?, pinRotation?}; alphas are in [0, 1]",
            "items": "object"
          }
        ]
      },
      {
        "name": "exclusions",
        "type": "array",
        "required": false,
        "description": "Per-bone solver exclusions, at most 2048",
        "items": "object",
        "fields": [
          {
            "name": "bone",
            "type": "string",
            "required": true,
            "description": "Bone to exclude or include"
          },
          {
            "name": "excluded",
            "type": "boolean",
            "required": true,
            "description": "Whether the solver excludes the bone"
          }
        ]
      }
    ],
    "choices": [
      {
        "mode": "atLeastOne",
        "branches": [
          [
            "autoSetup"
          ],
          [
            "retargetRoot"
          ],
          [
            "rootMotionBone"
          ],
          [
            "chains"
          ],
          [
            "fullBodyIK"
          ],
          [
            "exclusions"
          ]
        ]
      }
    ]
  },
  "create_anim_blueprint": {
    "category": "animation",
    "params": [
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "AnimBlueprint asset name"
      },
      {
        "name": "skeletonPath",
        "type": "string",
        "required": true,
        "description": "USkeleton the AnimBlueprint targets"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Destination folder (default /Game/Animations)"
      },
      {
        "name": "parentClass",
        "type": "string",
        "required": false,
        "description": "Parent AnimInstance class name"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "skip (default) returns an existing asset untouched, error refuses"
      }
    ]
  },
  "create_anim_composite": {
    "category": "animation",
    "params": [
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "AnimComposite asset name"
      },
      {
        "name": "skeletonPath",
        "type": "string",
        "required": true,
        "description": "USkeleton the composite targets"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Destination folder (default /Game/Animations)"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "skip (default) returns an existing asset untouched, error refuses"
      }
    ]
  },
  "create_anim_montage": {
    "category": "animation",
    "params": [
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "AnimMontage asset name"
      },
      {
        "name": "animSequencePath",
        "type": "string",
        "required": true,
        "description": "AnimSequence the montage plays"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Destination folder (default /Game/Animations)"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "skip (default) returns an existing asset untouched, error refuses"
      }
    ]
  },
  "create_blendspace": {
    "category": "animation",
    "params": [
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "BlendSpace asset name"
      },
      {
        "name": "skeletonPath",
        "type": "string",
        "required": true,
        "description": "USkeleton the blendspace targets"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Destination folder (default /Game/Animations)"
      },
      {
        "name": "axisHorizontal",
        "type": "string",
        "required": false,
        "description": "Horizontal axis name (default Speed)"
      },
      {
        "name": "axisVertical",
        "type": "string",
        "required": false,
        "description": "Vertical axis name (default Direction)"
      },
      {
        "name": "horizontalMin",
        "type": "number",
        "required": false,
        "description": "Horizontal axis minimum (default 0)"
      },
      {
        "name": "horizontalMax",
        "type": "number",
        "required": false,
        "description": "Horizontal axis maximum (default 500)"
      },
      {
        "name": "verticalMin",
        "type": "number",
        "required": false,
        "description": "Vertical axis minimum (default -180)"
      },
      {
        "name": "verticalMax",
        "type": "number",
        "required": false,
        "description": "Vertical axis maximum (default 180)"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "skip (default) returns an existing asset untouched, error refuses"
      }
    ]
  },
  "create_blendspace_1d": {
    "category": "animation",
    "params": [
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "BlendSpace1D asset name"
      },
      {
        "name": "skeletonPath",
        "type": "string",
        "required": true,
        "description": "USkeleton the blendspace targets"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Destination folder (default /Game/Animations)"
      },
      {
        "name": "axisName",
        "type": "string",
        "required": false,
        "description": "Axis display name (default Speed)"
      },
      {
        "name": "axisMin",
        "type": "number",
        "required": false,
        "description": "Axis minimum (default 0)"
      },
      {
        "name": "axisMax",
        "type": "number",
        "required": false,
        "description": "Axis maximum (default 500)"
      },
      {
        "name": "gridNum",
        "type": "number",
        "required": false,
        "description": "Grid divisions (default 4)"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "skip (default) returns an existing asset untouched, error refuses"
      }
    ]
  },
  "create_control_rig": {
    "category": "animation",
    "params": [
      {
        "name": "skeletalMeshPath",
        "type": "string",
        "required": false,
        "description": "SkeletalMesh to import the bone hierarchy from; pass exactly one of this and skeletonPath"
      },
      {
        "name": "skeletonPath",
        "type": "string",
        "required": false,
        "description": "Skeleton to import the bone hierarchy from; pass exactly one of this and skeletalMeshPath"
      },
      {
        "name": "name",
        "type": "string",
        "required": false,
        "description": "Asset name (default <Source>_CtrlRig)"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Destination folder (default the source's folder)"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "skip (default) returns the existing asset, error refuses; it never overwrites"
      }
    ]
  },
  "create_ik_retargeter": {
    "category": "animation",
    "params": [
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "IKRetargeter asset name"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Destination folder (default /Game)"
      },
      {
        "name": "sourceRig",
        "type": "string",
        "required": false,
        "description": "Source IKRigDefinition to assign"
      },
      {
        "name": "targetRig",
        "type": "string",
        "required": false,
        "description": "Target IKRigDefinition to assign"
      },
      {
        "name": "autoMapChains",
        "type": "boolean",
        "required": false,
        "description": "Assign the rigs to every op and auto-map chains (default true)"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "skip (default) returns an existing asset untouched, error refuses"
      }
    ],
    "contractExempt": "It creates the IKRetargeter before any input can fail, and before 5.8 an unresolved rig is only a warning"
  },
  "create_ik_rig": {
    "category": "animation",
    "params": [
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "IKRigDefinition asset name"
      },
      {
        "name": "skeletalMeshPath",
        "type": "string",
        "required": true,
        "description": "SkeletalMesh the rig is built on"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Destination folder (default /Game)"
      },
      {
        "name": "retargetRoot",
        "type": "string",
        "required": false,
        "description": "Retarget root bone"
      },
      {
        "name": "chains",
        "type": "array",
        "required": false,
        "description": "Retarget chains to add",
        "items": "object",
        "fields": [
          {
            "name": "name",
            "type": "string",
            "required": true,
            "description": "Retarget chain name"
          },
          {
            "name": "startBone",
            "type": "string",
            "required": true,
            "description": "First bone of the chain"
          },
          {
            "name": "endBone",
            "type": "string",
            "required": true,
            "description": "Last bone of the chain"
          },
          {
            "name": "goal",
            "type": "string",
            "required": false,
            "description": "IK goal the chain drives"
          }
        ]
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "skip (default) returns an existing asset untouched, error refuses"
      }
    ]
  },
  "create_mirror_data_table": {
    "category": "animation",
    "params": [
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "MirrorDataTable asset name"
      },
      {
        "name": "skeletonPath",
        "type": "string",
        "required": true,
        "description": "USkeleton the table mirrors"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Destination folder (default /Game/MotionMatching)"
      },
      {
        "name": "expressions",
        "type": "array",
        "required": false,
        "description": "Find/replace rules that derive the bone pairs (default the mannequin _l/_r suffix swap)",
        "items": "object",
        "fields": [
          {
            "name": "find",
            "type": "string",
            "required": true,
            "description": "Bone-name fragment to match"
          },
          {
            "name": "replace",
            "type": "string",
            "required": true,
            "description": "Replacement fragment"
          },
          {
            "name": "method",
            "type": "string",
            "required": false,
            "description": "suffix (default) | prefix | regex"
          }
        ]
      },
      {
        "name": "mirrorAxis",
        "type": "string",
        "required": false,
        "description": "X (default) | Y | Z"
      },
      {
        "name": "mirrorRootMotion",
        "type": "boolean",
        "required": false,
        "description": "Mirror root motion (default true)"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "skip (default) returns an existing asset untouched, error refuses"
      }
    ]
  },
  "create_pose_search_database": {
    "category": "animation",
    "params": [
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "PoseSearchDatabase asset name"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Destination folder (default /Game/MotionMatching)"
      },
      {
        "name": "schemaPath",
        "type": "string",
        "required": false,
        "description": "Existing PoseSearchSchema to use; a schema that cannot index is refused"
      },
      {
        "name": "skeletonPath",
        "type": "string",
        "required": false,
        "description": "Author a <name>_Schema with default channels on this skeleton when schemaPath is omitted"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "skip (default) returns an existing asset untouched, error refuses"
      }
    ]
  },
  "create_pose_search_normalization_set": {
    "category": "animation",
    "params": [
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "PoseSearchNormalizationSet asset name"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Destination folder (default /Game/MotionMatching)"
      },
      {
        "name": "databases",
        "type": "array",
        "required": false,
        "description": "PoseSearchDatabases to normalize together",
        "items": "string"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "skip (default) returns an existing asset untouched, error refuses"
      }
    ],
    "contractExempt": "Nothing it reads can fail before the asset is created"
  },
  "create_pose_search_schema": {
    "category": "animation",
    "params": [
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "PoseSearchSchema asset name"
      },
      {
        "name": "skeletonPath",
        "type": "string",
        "required": true,
        "description": "USkeleton the schema samples"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Destination folder (default /Game/MotionMatching)"
      },
      {
        "name": "mirrorDataTablePath",
        "type": "string",
        "required": false,
        "description": "MirrorDataTable to bind"
      },
      {
        "name": "sampleRate",
        "type": "number",
        "required": false,
        "description": "Schema sample rate (default 30)"
      },
      {
        "name": "addDefaultChannels",
        "type": "boolean",
        "required": false,
        "description": "Add Trajectory and Pose default channels so the schema is buildable (default true)"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "skip (default) returns an existing asset untouched, error refuses"
      }
    ]
  },
  "create_sequence": {
    "category": "animation",
    "params": [
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "AnimSequence asset name"
      },
      {
        "name": "skeletonPath",
        "type": "string",
        "required": true,
        "description": "USkeleton, or a SkeletalMesh whose skeleton to use"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Destination folder (default /Game/Animations)"
      },
      {
        "name": "numFrames",
        "type": "number",
        "required": false,
        "description": "Frame count (default 30)"
      },
      {
        "name": "frameRate",
        "type": "number",
        "required": false,
        "description": "Frames per second (default 30)"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "skip (default) returns an existing asset untouched, error refuses"
      }
    ]
  },
  "create_skeleton": {
    "category": "animation",
    "params": [
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "Skeleton asset name"
      },
      {
        "name": "skeletalMeshPath",
        "type": "string",
        "required": true,
        "description": "SkeletalMesh to build the skeleton from; the factory assigns the new skeleton to it"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Destination folder (default /Game)"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "skip (default) | error. It never overwrites"
      }
    ]
  },
  "create_state_machine": {
    "category": "animation",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "AnimBlueprint asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "name",
        "type": "string",
        "required": false,
        "description": "State machine name (default NewStateMachine)"
      },
      {
        "name": "graphName",
        "type": "string",
        "required": false,
        "description": "Graph to add it to (default AnimGraph)"
      }
    ]
  },
  "edit_curve_metadata": {
    "category": "animation",
    "params": [
      {
        "name": "skeletonPath",
        "type": "string",
        "required": true,
        "description": "USkeleton whose curve metadata to edit"
      },
      {
        "name": "add",
        "type": "array",
        "required": false,
        "description": "Curve names to add",
        "items": "string"
      },
      {
        "name": "remove",
        "type": "array",
        "required": false,
        "description": "Curve names to remove",
        "items": "string",
        "orTypes": [
          "boolean"
        ]
      },
      {
        "name": "rename",
        "type": "array",
        "required": false,
        "description": "Curve renames",
        "items": "object",
        "fields": [
          {
            "name": "from",
            "type": "string",
            "required": true,
            "description": "Current curve name"
          },
          {
            "name": "to",
            "type": "string",
            "required": true,
            "description": "New curve name"
          }
        ]
      },
      {
        "name": "flags",
        "type": "array",
        "required": false,
        "description": "Flag writes; each sets material, morphTarget or both",
        "items": "object",
        "fields": [
          {
            "name": "curve",
            "type": "string",
            "required": true,
            "description": "Curve to flag"
          },
          {
            "name": "material",
            "type": "boolean",
            "required": false,
            "description": "Drive a material parameter"
          },
          {
            "name": "morphTarget",
            "type": "boolean",
            "required": false,
            "description": "Drive a morph target"
          }
        ]
      }
    ],
    "choices": [
      {
        "mode": "atLeastOne",
        "branches": [
          [
            "add"
          ],
          [
            "remove"
          ],
          [
            "rename"
          ],
          [
            "flags"
          ]
        ]
      }
    ]
  },
  "edit_skeleton_bones": {
    "category": "animation",
    "params": [
      {
        "name": "sessionTag",
        "type": "string",
        "required": false,
        "description": "The open skeleton edit session to address; wins over skeletalMeshPath"
      },
      {
        "name": "skeletalMeshPath",
        "type": "string",
        "required": false,
        "description": "The skeletal mesh whose one open session to address, when sessionTag is omitted"
      },
      {
        "name": "edits",
        "type": "array",
        "required": true,
        "description": "Hierarchy edits, each one of {op:'add',bone,parent,transform?} | {op:'remove',bone,removeChildren?} | {op:'rename',bone,newName} | {op:'reparent',bone,parent} | {op:'set_transform',bone,transform,moveChildren?}",
        "items": "object"
      },
      {
        "name": "force",
        "type": "boolean",
        "required": false,
        "description": "Remove a bone despite dependents, which are listed in the refusal"
      }
    ]
  },
  "get_bone_transform": {
    "category": "animation",
    "params": [
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Editor label of the actor; pass this or actorPath"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Full actor object path, the unambiguous selector; wins over actorLabel"
      },
      {
        "name": "boneName",
        "type": "string",
        "required": true,
        "description": "Bone or socket to read"
      },
      {
        "name": "componentName",
        "type": "string",
        "required": false,
        "description": "SkeletalMeshComponent to read (default CharacterMesh0, then Mesh, then the first one)"
      },
      {
        "name": "world",
        "type": "string",
        "required": false,
        "description": "auto (default, prefers PIE) | pie | game | editor"
      },
      {
        "name": "space",
        "type": "string",
        "required": false,
        "description": "world (default) | component | local"
      }
    ]
  },
  "get_bone_transforms": {
    "category": "animation",
    "params": [
      {
        "name": "skeletonPath",
        "type": "string",
        "required": true,
        "description": "USkeleton, or a SkeletalMesh whose skeleton to read",
        "aliases": [
          "assetPath",
          "path"
        ]
      },
      {
        "name": "boneNames",
        "type": "array",
        "required": false,
        "description": "Bones to return (default every bone)",
        "items": "string"
      },
      {
        "name": "space",
        "type": "string",
        "required": false,
        "description": "local (default) | component, which composes the parent chain"
      }
    ]
  },
  "get_live_bone_transforms": {
    "category": "animation",
    "params": [
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Editor label of the actor; pass this or actorPath"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Full actor object path, the unambiguous selector; wins over actorLabel"
      },
      {
        "name": "componentName",
        "type": "string",
        "required": false,
        "description": "SkeletalMeshComponent to read (default CharacterMesh0, then Mesh, then the first one)"
      },
      {
        "name": "boneNames",
        "type": "array",
        "required": false,
        "description": "Bones to read (default every bone, max 1000)",
        "items": "string"
      },
      {
        "name": "space",
        "type": "string",
        "required": false,
        "description": "world (default) | component | local"
      },
      {
        "name": "world",
        "type": "string",
        "required": false,
        "description": "auto (default, prefers PIE) | pie | game | editor"
      }
    ]
  },
  "get_physics_asset_info": {
    "category": "animation",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "SkeletalMesh asset path",
        "aliases": [
          "path"
        ]
      }
    ]
  },
  "get_skeleton_info": {
    "category": "animation",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "SkeletalMesh asset path",
        "aliases": [
          "path"
        ]
      }
    ]
  },
  "inspect_anim_nodes": {
    "category": "animation",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "AnimBlueprint asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "graphName",
        "type": "string",
        "required": false,
        "description": "Graph to inspect (default AnimGraph)"
      },
      {
        "name": "nodeClass",
        "type": "string",
        "required": false,
        "description": "Node class substring filter, e.g. PoseDriver"
      }
    ]
  },
  "list_anim_assets": {
    "category": "animation",
    "params": [
      {
        "name": "directory",
        "type": "string",
        "required": false,
        "description": "Folder to scope the read to"
      },
      {
        "name": "recursive",
        "type": "boolean",
        "required": false,
        "description": "Include subfolders (default true)"
      },
      {
        "name": "cursor",
        "type": "string",
        "required": false,
        "description": "Resume a paged read: the nextCursor the previous page returned, unmodified"
      },
      {
        "name": "limit",
        "type": "integer",
        "required": false,
        "description": "Rows per page (default 200, max 5000)"
      }
    ]
  },
  "list_anim_modifiers": {
    "category": "animation",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "AnimSequence asset path",
        "aliases": [
          "path"
        ]
      }
    ]
  },
  "list_animation_sockets": {
    "category": "animation",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "SkeletalMesh asset path",
        "aliases": [
          "path"
        ]
      }
    ]
  },
  "list_bones": {
    "category": "animation",
    "params": [
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Editor label of the actor; pass this or actorPath"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Full actor object path, the unambiguous selector; wins over actorLabel"
      },
      {
        "name": "componentName",
        "type": "string",
        "required": false,
        "description": "SkeletalMeshComponent to read (default CharacterMesh0, then Mesh, then the first one)"
      },
      {
        "name": "world",
        "type": "string",
        "required": false,
        "description": "auto (default, prefers PIE) | pie | game | editor"
      },
      {
        "name": "cursor",
        "type": "string",
        "required": false,
        "description": "Resume a paged read: the nextCursor the previous page returned, unmodified"
      },
      {
        "name": "limit",
        "type": "integer",
        "required": false,
        "description": "Bones per page (default 200, max 5000)"
      }
    ]
  },
  "list_control_rig_variables": {
    "category": "animation",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Control Rig Blueprint asset path",
        "aliases": [
          "path"
        ]
      }
    ]
  },
  "list_montage_segments": {
    "category": "animation",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "AnimMontage asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "slotName",
        "type": "string",
        "required": false,
        "description": "List only this slot"
      }
    ]
  },
  "list_skeletal_meshes": {
    "category": "animation",
    "params": [
      {
        "name": "directory",
        "type": "string",
        "required": false,
        "description": "Folder to scope the read to"
      },
      {
        "name": "recursive",
        "type": "boolean",
        "required": false,
        "description": "Include subfolders (default true)"
      },
      {
        "name": "cursor",
        "type": "string",
        "required": false,
        "description": "Resume a paged read: the nextCursor the previous page returned, unmodified"
      },
      {
        "name": "limit",
        "type": "integer",
        "required": false,
        "description": "Rows per page (default 200, max 5000)"
      }
    ]
  },
  "measure_natural_speed": {
    "category": "animation",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "AnimSequence or BlendSpace asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "footBones",
        "type": "array",
        "required": true,
        "description": "Foot bones to track, e.g. [foot_l, foot_r]",
        "items": "string"
      },
      {
        "name": "contactThreshold",
        "type": "number",
        "required": false,
        "description": "Height in cm below which a foot counts as planted (default derived from the clip's lowest foot height)"
      },
      {
        "name": "skeletalMeshPath",
        "type": "string",
        "required": false,
        "description": "Evaluate with this SkeletalMesh's proportions"
      },
      {
        "name": "frames",
        "type": "array",
        "required": false,
        "description": "Frames to sample; pass this or times, not both (default every sampled key)",
        "items": "number"
      },
      {
        "name": "times",
        "type": "array",
        "required": false,
        "description": "Sample times in seconds; pass this or frames, not both",
        "items": "number"
      },
      {
        "name": "blendPosition",
        "type": "object",
        "required": false,
        "description": "BlendSpace blend input, in the blendspace's own axis units (BlendSpace only)",
        "fields": [
          {
            "name": "x",
            "type": "number",
            "required": false,
            "description": "First axis value (default 0)"
          },
          {
            "name": "y",
            "type": "number",
            "required": false,
            "description": "Second axis value (default 0)"
          },
          {
            "name": "z",
            "type": "number",
            "required": false,
            "description": "Third axis value (default 0)"
          }
        ]
      }
    ]
  },
  "populate_blendspace": {
    "category": "animation",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "BlendSpace or BlendSpace1D asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "axis",
        "type": "object",
        "required": false,
        "description": "Axis params for axisIndex (default axis 0)",
        "fields": [
          {
            "name": "name",
            "type": "string",
            "required": false,
            "description": "Axis display name"
          },
          {
            "name": "min",
            "type": "number",
            "required": false,
            "description": "Axis minimum"
          },
          {
            "name": "max",
            "type": "number",
            "required": false,
            "description": "Axis maximum"
          },
          {
            "name": "gridNum",
            "type": "integer",
            "required": false,
            "description": "Grid divisions"
          }
        ]
      },
      {
        "name": "axisIndex",
        "type": "integer",
        "required": false,
        "description": "Axis the axis object applies to (default 0)"
      },
      {
        "name": "blendspaceAxes",
        "type": "array",
        "required": false,
        "description": "Per-axis params, in axis order",
        "items": "object",
        "fields": [
          {
            "name": "name",
            "type": "string",
            "required": false,
            "description": "Axis display name"
          },
          {
            "name": "min",
            "type": "number",
            "required": false,
            "description": "Axis minimum"
          },
          {
            "name": "max",
            "type": "number",
            "required": false,
            "description": "Axis maximum"
          },
          {
            "name": "gridNum",
            "type": "integer",
            "required": false,
            "description": "Grid divisions"
          }
        ]
      },
      {
        "name": "axisHorizontal",
        "type": "string",
        "required": false,
        "description": "Horizontal axis name"
      },
      {
        "name": "horizontalMin",
        "type": "number",
        "required": false,
        "description": "Horizontal axis minimum"
      },
      {
        "name": "horizontalMax",
        "type": "number",
        "required": false,
        "description": "Horizontal axis maximum"
      },
      {
        "name": "gridNumHorizontal",
        "type": "integer",
        "required": false,
        "description": "Horizontal axis grid divisions"
      },
      {
        "name": "axisVertical",
        "type": "string",
        "required": false,
        "description": "Vertical axis name; ignored on a BlendSpace1D"
      },
      {
        "name": "verticalMin",
        "type": "number",
        "required": false,
        "description": "Vertical axis minimum; ignored on a BlendSpace1D"
      },
      {
        "name": "verticalMax",
        "type": "number",
        "required": false,
        "description": "Vertical axis maximum; ignored on a BlendSpace1D"
      },
      {
        "name": "gridNumVertical",
        "type": "integer",
        "required": false,
        "description": "Vertical axis grid divisions; ignored on a BlendSpace1D"
      },
      {
        "name": "samples",
        "type": "array",
        "required": false,
        "description": "Samples to add as [{animationPath, x, y?}]; an unloadable animation is reported under failed",
        "items": "object"
      },
      {
        "name": "clearExisting",
        "type": "boolean",
        "required": false,
        "description": "Clear the existing samples first (default true)"
      }
    ]
  },
  "preview_animation": {
    "category": "animation",
    "params": [
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Editor label of the actor; pass this or actorPath"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Full actor object path, the unambiguous selector; wins over actorLabel"
      },
      {
        "name": "enabled",
        "type": "boolean",
        "required": false,
        "description": "Tick animation in the editor (default true)"
      }
    ]
  },
  "read_anim_blueprint": {
    "category": "animation",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "AnimBlueprint asset path",
        "aliases": [
          "path"
        ]
      }
    ]
  },
  "read_anim_graph": {
    "category": "animation",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "AnimBlueprint asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "graphName",
        "type": "string",
        "required": false,
        "description": "Graph to read (default AnimGraph)"
      }
    ]
  },
  "read_anim_montage": {
    "category": "animation",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "AnimMontage asset path",
        "aliases": [
          "path"
        ]
      }
    ]
  },
  "read_anim_sequence": {
    "category": "animation",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": false,
        "description": "AnimSequence to read; omit when reading a batch",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "assetPaths",
        "type": "array",
        "required": false,
        "description": "Batch: up to 1000 AnimSequence paths, one row each",
        "items": "string"
      },
      {
        "name": "directory",
        "type": "string",
        "required": false,
        "description": "Batch: every AnimSequence in this folder"
      },
      {
        "name": "recursive",
        "type": "boolean",
        "required": false,
        "description": "With directory: include subfolders (default true)"
      },
      {
        "name": "nameFilter",
        "type": "string",
        "required": false,
        "description": "With directory: case-insensitive substring, or * ? wildcard, the asset name must match"
      },
      {
        "name": "fields",
        "type": "array",
        "required": false,
        "description": "Result keys to return; assetPath is always kept and an unknown name is refused with the valid list",
        "items": "string"
      },
      {
        "name": "cursor",
        "type": "string",
        "required": false,
        "description": "Resume a paged read: the nextCursor the previous page returned, unmodified"
      },
      {
        "name": "limit",
        "type": "integer",
        "required": false,
        "description": "Batch rows per page (default 50, max 200)"
      }
    ]
  },
  "read_blendspace": {
    "category": "animation",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "BlendSpace or BlendSpace1D asset path",
        "aliases": [
          "path"
        ]
      }
    ]
  },
  "read_bone_track": {
    "category": "animation",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "AnimSequence asset path"
      },
      {
        "name": "boneName",
        "type": "string",
        "required": true,
        "description": "Bone whose track to sample"
      },
      {
        "name": "frames",
        "type": "array",
        "required": false,
        "description": "Frames to sample (default: first, middle and last)",
        "items": "number"
      }
    ]
  },
  "read_control_rig_edit": {
    "category": "animation",
    "params": [
      {
        "name": "sequencePath",
        "type": "string",
        "required": true,
        "description": "LevelSequence holding the Control Rig edit session"
      },
      {
        "name": "bindingTag",
        "type": "string",
        "required": true,
        "description": "Edit-session natural key from begin_control_rig_edit"
      },
      {
        "name": "controlNames",
        "type": "array",
        "required": false,
        "description": "Controls to read (default every readable control)",
        "items": "string"
      },
      {
        "name": "frame",
        "type": "number",
        "required": false,
        "description": "One frame to sample, an integer; combines with frames"
      },
      {
        "name": "frames",
        "type": "array",
        "required": false,
        "description": "Integer frames to sample (default the first and last frame of the range)",
        "items": "number"
      },
      {
        "name": "space",
        "type": "string",
        "required": false,
        "description": "local (default) | component | global, where global is an alias for component"
      }
    ]
  },
  "read_control_rig_graph": {
    "category": "animation",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Control Rig Blueprint asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "graphName",
        "type": "string",
        "required": false,
        "description": "Substring filter over model names"
      },
      {
        "name": "includePins",
        "type": "boolean",
        "required": false,
        "description": "Include each node's pins (default true)"
      },
      {
        "name": "includeDefaults",
        "type": "boolean",
        "required": false,
        "description": "Include pin default values (default true)"
      },
      {
        "name": "includeLinks",
        "type": "boolean",
        "required": false,
        "description": "Include pin-to-pin links (default true)"
      },
      {
        "name": "limit",
        "type": "integer",
        "required": false,
        "description": "Nodes per graph (default 200)"
      }
    ]
  },
  "read_control_rig_hierarchy": {
    "category": "animation",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Control Rig Blueprint asset path",
        "aliases": [
          "path"
        ]
      }
    ]
  },
  "read_ik_retargeter": {
    "category": "animation",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "IKRetargeter asset path",
        "aliases": [
          "path"
        ]
      }
    ]
  },
  "read_ik_rig": {
    "category": "animation",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "IKRigDefinition asset path",
        "aliases": [
          "path"
        ]
      }
    ]
  },
  "read_mirror_data_table": {
    "category": "animation",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "MirrorDataTable asset path",
        "aliases": [
          "path"
        ]
      }
    ]
  },
  "read_pose_search_database": {
    "category": "animation",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "PoseSearchDatabase asset path",
        "aliases": [
          "path"
        ]
      }
    ]
  },
  "read_pose_search_schema": {
    "category": "animation",
    "params": [
      {
        "name": "schemaPath",
        "type": "string",
        "required": true,
        "description": "PoseSearchSchema to read",
        "aliases": [
          "assetPath"
        ]
      }
    ]
  },
  "read_state_machine": {
    "category": "animation",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "AnimBlueprint asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "stateMachineName",
        "type": "string",
        "required": true,
        "description": "State machine to read"
      }
    ]
  },
  "rebind_leader_pose": {
    "category": "animation",
    "params": [
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Editor label of the actor; pass this or actorPath"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Full actor object path, the unambiguous selector; wins over actorLabel"
      },
      {
        "name": "bodyComponent",
        "type": "string",
        "required": false,
        "description": "Body component every other one follows (default CharacterMesh0, then Mesh)"
      }
    ]
  },
  "register_compatible_skeleton": {
    "category": "animation",
    "params": [
      {
        "name": "skeletonPath",
        "type": "string",
        "required": true,
        "description": "USkeleton that gains or loses the compatible entries"
      },
      {
        "name": "compatibleSkeletonPath",
        "type": "string",
        "required": false,
        "description": "Skeleton to mark compatible"
      },
      {
        "name": "compatibleSkeletonPaths",
        "type": "array",
        "required": false,
        "description": "Several skeletons at once; wins over compatibleSkeletonPath",
        "items": "string"
      },
      {
        "name": "remove",
        "type": "array",
        "required": false,
        "description": "true to unregister instead of register (default false)",
        "items": "string",
        "orTypes": [
          "boolean"
        ]
      }
    ],
    "choices": [
      {
        "mode": "atLeastOne",
        "branches": [
          [
            "compatibleSkeletonPath"
          ],
          [
            "compatibleSkeletonPaths"
          ]
        ]
      }
    ]
  },
  "remove_anim_curve": {
    "category": "animation",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "AnimSequence asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "curveName",
        "type": "string",
        "required": true,
        "description": "Float curve to remove"
      }
    ]
  },
  "remove_anim_notify": {
    "category": "animation",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "AnimSequence or AnimMontage asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "notifyName",
        "type": "string",
        "required": false,
        "description": "Notify name to match"
      },
      {
        "name": "notifyClass",
        "type": "string",
        "required": false,
        "description": "Notify class to match. Pass at least one of notifyName and notifyClass; both filters apply together"
      }
    ]
  },
  "remove_anim_notify_state": {
    "category": "animation",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "AnimSequence or AnimMontage asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "notifyName",
        "type": "string",
        "required": false,
        "description": "Notify name to match"
      },
      {
        "name": "notifyStateClass",
        "type": "string",
        "required": false,
        "description": "Notify state class to match. Pass at least one of notifyName and notifyStateClass; both filters apply together"
      }
    ]
  },
  "remove_montage_section": {
    "category": "animation",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "AnimMontage asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "sectionName",
        "type": "string",
        "required": true,
        "description": "Composite section to remove"
      }
    ]
  },
  "remove_montage_segment": {
    "category": "animation",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "AnimMontage asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "segmentIndex",
        "type": "number",
        "required": true,
        "description": "Segment to remove"
      },
      {
        "name": "slotName",
        "type": "string",
        "required": false,
        "description": "Slot holding the segment; wins over slotIndex"
      },
      {
        "name": "slotIndex",
        "type": "number",
        "required": false,
        "description": "Slot index holding the segment (default 0)"
      }
    ]
  },
  "remove_state": {
    "category": "animation",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "AnimBlueprint asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "stateMachineName",
        "type": "string",
        "required": true,
        "description": "State machine to edit"
      },
      {
        "name": "stateName",
        "type": "string",
        "required": true,
        "description": "State to remove, with every transition that touches it"
      }
    ]
  },
  "remove_state_machine": {
    "category": "animation",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "AnimBlueprint asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "stateMachineName",
        "type": "string",
        "required": true,
        "description": "State machine to edit"
      }
    ]
  },
  "remove_transition": {
    "category": "animation",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "AnimBlueprint asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "stateMachineName",
        "type": "string",
        "required": true,
        "description": "State machine to edit"
      },
      {
        "name": "transitionGuid",
        "type": "string",
        "required": false,
        "description": "Transition to remove, from add_transition or read_state_machine"
      },
      {
        "name": "fromState",
        "type": "string",
        "required": false,
        "description": "With toState, removes every transition between the two"
      },
      {
        "name": "toState",
        "type": "string",
        "required": false,
        "description": "With fromState, removes every transition between the two"
      }
    ]
  },
  "remove_virtual_bone": {
    "category": "animation",
    "params": [
      {
        "name": "skeletonPath",
        "type": "string",
        "required": true,
        "description": "USkeleton asset path"
      },
      {
        "name": "virtualBoneName",
        "type": "string",
        "required": true,
        "description": "Virtual bone to remove"
      }
    ]
  },
  "reset_retarget_pose": {
    "category": "animation",
    "params": [
      {
        "name": "retargeterPath",
        "type": "string",
        "required": true,
        "description": "Existing IKRetargeter to edit",
        "aliases": [
          "assetPath"
        ]
      },
      {
        "name": "side",
        "type": "string",
        "required": false,
        "description": "source | target (default target)"
      }
    ]
  },
  "reverse_sequence": {
    "category": "animation",
    "params": [
      {
        "name": "sourcePath",
        "type": "string",
        "required": true,
        "description": "AnimSequence to reverse"
      },
      {
        "name": "destinationPath",
        "type": "string",
        "required": false,
        "description": "Asset path for the reversed copy; alternative to name and packagePath"
      },
      {
        "name": "name",
        "type": "string",
        "required": false,
        "description": "Name of the reversed copy (default <Source>_Reversed)"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Folder of the reversed copy (default the source's folder)"
      },
      {
        "name": "inPlace",
        "type": "boolean",
        "required": false,
        "description": "Reverse the source itself instead of writing a copy (default false)"
      },
      {
        "name": "cycleOffsetFrames",
        "type": "integer",
        "required": false,
        "description": "Rotate the reversed loop to start this many frames in"
      },
      {
        "name": "cycleOffsetSeconds",
        "type": "number",
        "required": false,
        "description": "Rotate the reversed loop to start this many seconds in, rounded to whole frames"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "skip (default) returns an existing destination untouched, error refuses; it never overwrites"
      }
    ]
  },
  "sample_pose": {
    "category": "animation",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "AnimSequence or BlendSpace asset path; a montage is refused",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "boneNames",
        "type": "array",
        "required": false,
        "description": "Bones to return (default every bone)",
        "items": "string"
      },
      {
        "name": "frames",
        "type": "array",
        "required": false,
        "description": "Frames to sample; pass this or times, not both (default every sampled key)",
        "items": "number"
      },
      {
        "name": "times",
        "type": "array",
        "required": false,
        "description": "Sample times in seconds; pass this or frames, not both",
        "items": "number"
      },
      {
        "name": "space",
        "type": "string",
        "required": false,
        "description": "component (default) | local | world, which resolves to component on an asset"
      },
      {
        "name": "skeletalMeshPath",
        "type": "string",
        "required": false,
        "description": "Evaluate with this SkeletalMesh's proportions"
      },
      {
        "name": "incorporateRootMotion",
        "type": "boolean",
        "required": false,
        "description": "Fold the clip's root motion into the pose (default true)"
      },
      {
        "name": "blendPosition",
        "type": "object",
        "required": false,
        "description": "BlendSpace blend input, in the blendspace's own axis units (BlendSpace only)",
        "fields": [
          {
            "name": "x",
            "type": "number",
            "required": false,
            "description": "First axis value (default 0)"
          },
          {
            "name": "y",
            "type": "number",
            "required": false,
            "description": "Second axis value (default 0)"
          },
          {
            "name": "z",
            "type": "number",
            "required": false,
            "description": "Third axis value (default 0)"
          }
        ]
      }
    ]
  },
  "scan_animation_tracks": {
    "category": "animation",
    "params": [
      {
        "name": "directory",
        "type": "string",
        "required": false,
        "description": "Content root to scan (default every root)"
      },
      {
        "name": "recursive",
        "type": "boolean",
        "required": false,
        "description": "Include subfolders (default true)"
      },
      {
        "name": "assetPaths",
        "type": "array",
        "required": false,
        "description": "Scan exactly these AnimSequences instead of a directory",
        "items": "string"
      },
      {
        "name": "skeletonPath",
        "type": "string",
        "required": false,
        "description": "Only sequences on this skeleton; a path that names no skeleton is refused"
      },
      {
        "name": "targetTrackCount",
        "type": "number",
        "required": false,
        "description": "Flag sequences with more than this many bone tracks"
      },
      {
        "name": "includeTrackNames",
        "type": "boolean",
        "required": false,
        "description": "Include each sequence's bone track names"
      }
    ]
  },
  "set_anim_blueprint_skeleton": {
    "category": "animation",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "AnimBlueprint asset path"
      },
      {
        "name": "skeletonPath",
        "type": "string",
        "required": true,
        "description": "USkeleton to target"
      }
    ]
  },
  "set_anim_curve_keys": {
    "category": "animation",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "AnimSequence asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "curveName",
        "type": "string",
        "required": true,
        "description": "Float curve to write; a missing curve is added"
      },
      {
        "name": "keys",
        "type": "array",
        "required": true,
        "description": "Keys that replace the curve's own",
        "items": "object",
        "fields": [
          {
            "name": "time",
            "type": "number",
            "required": true,
            "description": "Key time in seconds"
          },
          {
            "name": "value",
            "type": "number",
            "required": true,
            "description": "Key value"
          },
          {
            "name": "interp",
            "type": "string",
            "required": false,
            "description": "linear | constant | cubic (default the call's interpolation)"
          }
        ]
      },
      {
        "name": "interpolation",
        "type": "string",
        "required": false,
        "description": "Interpolation of keys without their own interp: linear (default) | constant | cubic"
      }
    ]
  },
  "set_blend_sample": {
    "category": "animation",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "BlendSpace or BlendSpace1D asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "sampleIndex",
        "type": "integer",
        "required": true,
        "description": "Sample to edit, as read_blendspace lists it"
      },
      {
        "name": "position",
        "type": "object",
        "required": false,
        "description": "New sample position; wins over flat x and y, and an omitted axis keeps its value",
        "fields": [
          {
            "name": "x",
            "type": "number",
            "required": false,
            "description": "Horizontal axis value"
          },
          {
            "name": "y",
            "type": "number",
            "required": false,
            "description": "Vertical axis value"
          }
        ]
      },
      {
        "name": "x",
        "type": "number",
        "required": false,
        "description": "New horizontal axis value when position is omitted"
      },
      {
        "name": "y",
        "type": "number",
        "required": false,
        "description": "New vertical axis value when position is omitted"
      },
      {
        "name": "animation",
        "type": "string",
        "required": false,
        "description": "AnimSequence to swap into the sample"
      }
    ],
    "choices": [
      {
        "mode": "atLeastOne",
        "branches": [
          [
            "position"
          ],
          [
            "x"
          ],
          [
            "y"
          ],
          [
            "animation"
          ]
        ]
      }
    ]
  },
  "set_bone_keyframes": {
    "category": "animation",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "AnimSequence asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "boneName",
        "type": "string",
        "required": true,
        "description": "Bone whose track to replace; a missing track is created"
      },
      {
        "name": "keyframes",
        "type": "array",
        "required": true,
        "description": "One key per frame, in order; an omitted channel keeps the reference pose",
        "items": "object",
        "fields": [
          {
            "name": "location",
            "type": "vec3",
            "required": false,
            "description": "Bone-local translation"
          },
          {
            "name": "rotation",
            "type": "object",
            "required": false,
            "description": "Bone-local rotation as a quaternion {x, y, z, w}"
          },
          {
            "name": "scale",
            "type": "vec3",
            "required": false,
            "description": "Bone-local scale"
          }
        ]
      }
    ]
  },
  "set_bone_retargeting": {
    "category": "animation",
    "params": [
      {
        "name": "skeletonPath",
        "type": "string",
        "required": true,
        "description": "USkeleton whose bone tree to edit"
      },
      {
        "name": "mode",
        "type": "string",
        "required": false,
        "description": "Animation | Skeleton | AnimationScaled | AnimationRelative | OrientAndScale"
      },
      {
        "name": "bones",
        "type": "array",
        "required": false,
        "description": "Bone names to set (default every bone)"
      },
      {
        "name": "bone",
        "type": "string",
        "required": false,
        "description": "A single bone, when bones is omitted"
      },
      {
        "name": "includeChildren",
        "type": "boolean",
        "required": false,
        "description": "Apply recursively down the bone tree (default false)"
      },
      {
        "name": "restore",
        "type": "array",
        "required": false,
        "description": "Per-bone modes to put back, which is how this action's rollback replays; wins over mode",
        "items": "object",
        "fields": [
          {
            "name": "bone",
            "type": "string",
            "required": true,
            "description": "Bone to restore"
          },
          {
            "name": "mode",
            "type": "string",
            "required": true,
            "description": "Mode to restore it to"
          }
        ]
      }
    ],
    "choices": [
      {
        "mode": "atLeastOne",
        "branches": [
          [
            "mode"
          ],
          [
            "restore"
          ]
        ]
      }
    ]
  },
  "set_ik_retargeter_rig": {
    "category": "animation",
    "params": [
      {
        "name": "retargeterPath",
        "type": "string",
        "required": true,
        "description": "Existing IKRetargeter to edit",
        "aliases": [
          "assetPath"
        ]
      },
      {
        "name": "rigPath",
        "type": "string",
        "required": true,
        "description": "IKRigDefinition to assign",
        "aliases": [
          "ikRig"
        ]
      },
      {
        "name": "side",
        "type": "string",
        "required": false,
        "description": "source | target (default target)"
      }
    ]
  },
  "set_ik_rig_mesh": {
    "category": "animation",
    "params": [
      {
        "name": "rigPath",
        "type": "string",
        "required": true,
        "description": "Existing IKRigDefinition to edit",
        "aliases": [
          "assetPath"
        ]
      },
      {
        "name": "meshPath",
        "type": "string",
        "required": true,
        "description": "SkeletalMesh to set as the rig's preview and source mesh",
        "aliases": [
          "skeletalMesh"
        ]
      }
    ]
  },
  "set_live_post_process_anim_blueprint": {
    "category": "animation",
    "params": [
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Editor label of the actor; pass this or actorPath"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Full actor object path, the unambiguous selector; wins over actorLabel"
      },
      {
        "name": "animBlueprintClassPath",
        "type": "string",
        "required": false,
        "description": "AnimBlueprintGeneratedClass object path, e.g. /Game/Animations/ABP_Name.ABP_Name_C, not the AnimBlueprint asset path"
      },
      {
        "name": "clear",
        "type": "boolean",
        "required": false,
        "description": "Clear the transient override and fall back to the skeletal mesh asset's post-process AnimBP"
      },
      {
        "name": "componentName",
        "type": "string",
        "required": false,
        "description": "SkeletalMeshComponent to read (default CharacterMesh0, then Mesh, then the first one)"
      },
      {
        "name": "world",
        "type": "string",
        "required": false,
        "description": "auto (default, prefers PIE) | pie | game | editor"
      }
    ]
  },
  "set_montage_properties": {
    "category": "animation",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "AnimMontage asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "sequenceLength",
        "type": "number",
        "required": false,
        "description": "Montage length in seconds"
      },
      {
        "name": "rateScale",
        "type": "number",
        "required": false,
        "description": "Playback rate scale"
      },
      {
        "name": "blendIn",
        "type": "number",
        "required": false,
        "description": "Blend-in time in seconds"
      },
      {
        "name": "blendOut",
        "type": "number",
        "required": false,
        "description": "Blend-out time in seconds"
      }
    ]
  },
  "set_montage_sequence": {
    "category": "animation",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "AnimMontage asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "animSequencePath",
        "type": "string",
        "required": true,
        "description": "AnimSequence to put in the slot"
      },
      {
        "name": "slotIndex",
        "type": "number",
        "required": false,
        "description": "Slot track index (default 0)"
      },
      {
        "name": "segmentIndex",
        "type": "number",
        "required": false,
        "description": "Replace only this segment; without it every segment in the slot is replaced (#626)"
      }
    ]
  },
  "set_montage_slot": {
    "category": "animation",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "AnimMontage asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "slotName",
        "type": "string",
        "required": true,
        "description": "Slot name to write onto the track"
      },
      {
        "name": "trackIndex",
        "type": "integer",
        "required": false,
        "description": "Slot track index (default 0)"
      }
    ]
  },
  "set_motion_matching_chooser": {
    "category": "animation",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "AnimBlueprint asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "chooserPath",
        "type": "string",
        "required": true,
        "description": "ChooserTable that selects the database",
        "aliases": [
          "table"
        ]
      },
      {
        "name": "graphName",
        "type": "string",
        "required": false,
        "description": "Graph holding the Motion Matching node (default AnimGraph)"
      },
      {
        "name": "contextSource",
        "type": "string",
        "required": false,
        "description": "What the chooser reads its columns from: self (default, the anim instance) | pawn (the owning pawn)"
      }
    ]
  },
  "set_pose_search_clips": {
    "category": "animation",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "PoseSearchDatabase asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "clips",
        "type": "array",
        "required": true,
        "description": "Clips, each an animation asset path or {sequencePath (or asset, assetPath, animationPath), mirror?: original | mirrored | both, disableReselection?, sampleStart?, sampleEnd?, enabled?}"
      },
      {
        "name": "clearExisting",
        "type": "boolean",
        "required": false,
        "description": "Replace the clip list rather than append to it (default true)"
      }
    ]
  },
  "set_pose_search_database_settings": {
    "category": "animation",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "PoseSearchDatabase asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "continuingPoseCostBias",
        "type": "number",
        "required": false,
        "description": "Bias to keep playing the current clip"
      },
      {
        "name": "baseCostBias",
        "type": "number",
        "required": false,
        "description": "Flat cost added to every pose"
      },
      {
        "name": "loopingCostBias",
        "type": "number",
        "required": false,
        "description": "Bias for looping clips"
      },
      {
        "name": "kdTreeQueryNumNeighbors",
        "type": "number",
        "required": false,
        "description": "KD-tree neighbours to consider"
      },
      {
        "name": "numberOfPrincipalComponents",
        "type": "number",
        "required": false,
        "description": "PCA components for PCAKDTree mode"
      },
      {
        "name": "poseSearchMode",
        "type": "string",
        "required": false,
        "description": "bruteforce | pcakdtree | vptree | eventonly"
      },
      {
        "name": "normalizationSetPath",
        "type": "string",
        "required": false,
        "description": "PoseSearchNormalizationSet to assign"
      }
    ]
  },
  "set_pose_search_schema": {
    "category": "animation",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "PoseSearchDatabase asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "schemaPath",
        "type": "string",
        "required": true,
        "description": "PoseSearchSchema to assign; a schema that cannot index is refused"
      }
    ]
  },
  "set_root_motion_settings": {
    "category": "animation",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "AnimSequence asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "enableRootMotion",
        "type": "boolean",
        "required": false,
        "description": "Extract root motion from the root bone"
      },
      {
        "name": "forceRootLock",
        "type": "boolean",
        "required": false,
        "description": "Lock the root bone even without root motion"
      },
      {
        "name": "useNormalizedRootMotionScale",
        "type": "boolean",
        "required": false,
        "description": "Normalize root motion scale"
      },
      {
        "name": "rootMotionRootLock",
        "type": "string",
        "required": false,
        "description": "Root lock mode: RefPose | AnimFirstFrame | Zero"
      }
    ]
  },
  "set_sequence_properties": {
    "category": "animation",
    "params": [
      {
        "name": "assetPaths",
        "type": "array",
        "required": true,
        "description": "AnimSequences to write, or montages when resolveFromMontages is on",
        "items": "string"
      },
      {
        "name": "properties",
        "type": "object",
        "required": true,
        "description": "{enableRootMotion?, forceRootLock?, useNormalizedRootMotionScale?, rootMotionRootLock?}"
      },
      {
        "name": "resolveFromMontages",
        "type": "boolean",
        "required": false,
        "description": "Resolve a montage to its first AnimSequence (default true)"
      }
    ]
  },
  "set_state_animation": {
    "category": "animation",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "AnimBlueprint asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "stateMachineName",
        "type": "string",
        "required": true,
        "description": "State machine to edit"
      },
      {
        "name": "stateName",
        "type": "string",
        "required": true,
        "description": "State whose animation to set"
      },
      {
        "name": "animAssetPath",
        "type": "string",
        "required": true,
        "description": "AnimSequence or BlendSpace the state plays"
      }
    ]
  },
  "set_state_machine_entry": {
    "category": "animation",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "AnimBlueprint asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "stateMachineName",
        "type": "string",
        "required": true,
        "description": "State machine to edit"
      },
      {
        "name": "stateName",
        "type": "string",
        "required": false,
        "description": "State the entry points at; omitted or empty clears the link"
      }
    ]
  },
  "set_sync_markers": {
    "category": "animation",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "AnimSequence asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "markers",
        "type": "array",
        "required": false,
        "description": "Sync markers to author; with markerMode=replace an empty array clears them",
        "items": "object",
        "fields": [
          {
            "name": "name",
            "type": "string",
            "required": true,
            "description": "Marker name"
          },
          {
            "name": "time",
            "type": "number",
            "required": true,
            "description": "Marker time in seconds, within the clip"
          }
        ]
      },
      {
        "name": "removeMarkers",
        "type": "array",
        "required": false,
        "description": "Marker names to drop, applied after markers",
        "items": "string"
      },
      {
        "name": "markerMode",
        "type": "string",
        "required": false,
        "description": "replace (default: markers becomes the whole list) | merge (add or move only the named markers)"
      }
    ],
    "choices": [
      {
        "mode": "atLeastOne",
        "branches": [
          [
            "markers"
          ],
          [
            "removeMarkers"
          ]
        ]
      }
    ]
  },
  "set_transition_blend": {
    "category": "animation",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "AnimBlueprint asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "stateMachineName",
        "type": "string",
        "required": true,
        "description": "State machine to edit"
      },
      {
        "name": "fromState",
        "type": "string",
        "required": true,
        "description": "State the transition leaves"
      },
      {
        "name": "toState",
        "type": "string",
        "required": true,
        "description": "State the transition enters"
      },
      {
        "name": "blendDuration",
        "type": "number",
        "required": false,
        "description": "Crossfade in seconds"
      },
      {
        "name": "blendLogic",
        "type": "string",
        "required": false,
        "description": "Standard | Inertialization"
      }
    ]
  },
  "set_transition_condition": {
    "category": "animation",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "AnimBlueprint asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "stateMachineName",
        "type": "string",
        "required": true,
        "description": "State machine to edit"
      },
      {
        "name": "variableName",
        "type": "string",
        "required": true,
        "description": "Existing bool variable the condition reads"
      },
      {
        "name": "transitionGuid",
        "type": "string",
        "required": false,
        "description": "Transition to condition, from add_transition or read_state_machine; wins over fromState and toState"
      },
      {
        "name": "fromState",
        "type": "string",
        "required": false,
        "description": "With toState, the transition to condition when transitionGuid is omitted"
      },
      {
        "name": "toState",
        "type": "string",
        "required": false,
        "description": "With fromState, the transition to condition when transitionGuid is omitted"
      },
      {
        "name": "negate",
        "type": "boolean",
        "required": false,
        "description": "Enter when the variable is false (default false)"
      }
    ]
  }
};

/** The Params: clause of each spec'd bridge method. */
export const paramsClauses: Readonly<Record<string, string>> = {
  add_anim_notify: "Params: assetPath (or path), notifyName, triggerTime, notifyClass?, notifyProperties?, branchingPoint?",
  add_anim_notify_state: "Params: assetPath (or path), notifyName, notifyStateClass, triggerTime, duration, notifyProperties?, branchingPoint?",
  add_blend_sample: "Params: assetPath (or path), animation, position?, x?, y?",
  add_curve: "Params: assetPath (or path), curveName",
  add_montage_section: "Params: assetPath (or path), sectionName, startTime?, linkedSection?, segmentIndex?, slotName?, slotIndex?, onConflict?",
  add_montage_segment: "Params: assetPath (or path), animSequencePath, slotName?, slotIndex?, startPos?, endPos?, playRate?, loopCount?, insertIndex?",
  add_motion_matching_node: "Params: assetPath (or path), databasePath?, graphName?, connectToOutput?, blendTime?",
  add_pose_history_node: "Params: assetPath (or path), graphName?, poseCount?, samplingInterval?, generateTrajectory?, trajectoryHistoryCount?, trajectoryPredictionCount?, insertBeforeOutput?",
  add_pose_search_schema_pose_channel: "Params: schemaPath (or assetPath), bones, weight?",
  add_pose_search_schema_trajectory_channel: "Params: schemaPath (or assetPath), samples, weight?",
  add_pose_search_sequence: "Params: assetPath (or path), sequencePath, mirror?, disableReselection?, sampleStart?, sampleEnd?, enabled?",
  add_sequence_evaluator: "Params: assetPath (or path), sequencePath?, graphName?, explicitTime?, shouldLoop?, teleportToExplicitTime?, connectToOutput?",
  add_state: "Params: assetPath (or path), stateMachineName, stateName, onConflict?",
  add_transition: "Params: assetPath (or path), stateMachineName, fromState, toState, blendDuration?, blendLogic?",
  add_virtual_bone: "Params: skeletonPath, sourceBone, targetBone",
  analyze_animation: "Params: assetPath (or path), skeletalMeshPath?, boneNames?, frames?, sampleRate?, loop?, facingBones?, outputDirectory?",
  apply_animation_modifier: "Params: assetPath (or path), modifierClass (or modifier), props?",
  apply_control_rig_edits: "Params: sequencePath, bindingTag, operations",
  author_blend_profile: "Params: skeletonPath, profileName, operation?, newProfileName?, mode?, entries?, removeEntries?",
  author_montages_batch: "Params: items",
  auto_align_retarget_pose: "Params: retargeterPath (or assetPath), side?",
  bake_control_rig_edit: "Params: sequencePath, bindingTag, outputAssetPath, frameRate?, reduceKeys?, tolerance?, createLink?, onConflict?",
  bake_keyframes_batch: "Params: assetPath (or path), tracks, save?",
  bake_root_motion_from_bone: "Params: assetPath (or path), sourceBone, rootBone?, axes?, interpolation?",
  batch_retarget_animations: "Params: retargeterPath (or assetPath), sourceMesh, targetMesh, animPaths, outputPath?, prefix?, suffix?, overwrite?, requireCompleteMapping?",
  begin_control_rig_edit: "Params: sequencePath, skeletalMeshPath, sourceAnimationPath, rigMode?, controlRigPath?, layered?, startFrame?, endFrame?, displayRate?, bindingTag?, onConflict?",
  begin_skeleton_edit: "Params: skeletalMeshPath, sessionTag?",
  bind_anim_node_function: "Params: assetPath (or path), nodeGuid (or nodeId), functionName (or function), graphName?, binding?",
  build_pose_search_index: "Params: assetPath (or path), wait?",
  cancel_skeleton_edit: "Params: sessionTag?, skeletalMeshPath?",
  capture_control_rig_pose: "Params: sequencePath, bindingTag, controlNames?",
  commit_skeleton_edit: "Params: sessionTag?, skeletalMeshPath?",
  compare_curves_to_morph_targets: "Params: animPath (or assetPath), skeletalMeshPath (or meshPath)",
  configure_ik_retargeter: "Params: retargeterPath, sourceRig?, targetRig?, sourcePreviewMesh?, targetPreviewMesh?, ensureDefaultOps?, autoMapMode?, forceRemap?, chainMappings?, ops?, pose?",
  configure_ik_rig: "Params: rigPath, at least one of autoSetup/retargetRoot/rootMotionBone/chains/fullBodyIK/exclusions",
  create_anim_blueprint: "Params: name, skeletonPath, packagePath?, parentClass?, onConflict?",
  create_anim_composite: "Params: name, skeletonPath, packagePath?, onConflict?",
  create_anim_montage: "Params: name, animSequencePath, packagePath?, onConflict?",
  create_blendspace: "Params: name, skeletonPath, packagePath?, axisHorizontal?, axisVertical?, horizontalMin?, horizontalMax?, verticalMin?, verticalMax?, onConflict?",
  create_blendspace_1d: "Params: name, skeletonPath, packagePath?, axisName?, axisMin?, axisMax?, gridNum?, onConflict?",
  create_control_rig: "Params: skeletalMeshPath?, skeletonPath?, name?, packagePath?, onConflict?",
  create_ik_retargeter: "Params: name, packagePath?, sourceRig?, targetRig?, autoMapChains?, onConflict?",
  create_ik_rig: "Params: name, skeletalMeshPath, packagePath?, retargetRoot?, chains?, onConflict?",
  create_mirror_data_table: "Params: name, skeletonPath, packagePath?, expressions?, mirrorAxis?, mirrorRootMotion?, onConflict?",
  create_pose_search_database: "Params: name, packagePath?, schemaPath?, skeletonPath?, onConflict?",
  create_pose_search_normalization_set: "Params: name, packagePath?, databases?, onConflict?",
  create_pose_search_schema: "Params: name, skeletonPath, packagePath?, mirrorDataTablePath?, sampleRate?, addDefaultChannels?, onConflict?",
  create_sequence: "Params: name, skeletonPath, packagePath?, numFrames?, frameRate?, onConflict?",
  create_skeleton: "Params: name, skeletalMeshPath, packagePath?, onConflict?",
  create_state_machine: "Params: assetPath (or path), name?, graphName?",
  edit_curve_metadata: "Params: skeletonPath, at least one of add/remove/rename/flags",
  edit_skeleton_bones: "Params: sessionTag?, skeletalMeshPath?, edits, force?",
  get_bone_transform: "Params: actorLabel?, actorPath?, boneName, componentName?, world?, space?",
  get_bone_transforms: "Params: skeletonPath (or assetPath, or path), boneNames?, space?",
  get_live_bone_transforms: "Params: actorLabel?, actorPath?, componentName?, boneNames?, space?, world?",
  get_physics_asset_info: "Params: assetPath (or path)",
  get_skeleton_info: "Params: assetPath (or path)",
  inspect_anim_nodes: "Params: assetPath (or path), graphName?, nodeClass?",
  list_anim_assets: "Params: directory?, recursive?, cursor?, limit?",
  list_anim_modifiers: "Params: assetPath (or path)",
  list_animation_sockets: "Params: assetPath (or path)",
  list_bones: "Params: actorLabel?, actorPath?, componentName?, world?, cursor?, limit?",
  list_control_rig_variables: "Params: assetPath (or path)",
  list_montage_segments: "Params: assetPath (or path), slotName?",
  list_skeletal_meshes: "Params: directory?, recursive?, cursor?, limit?",
  measure_natural_speed: "Params: assetPath (or path), footBones, contactThreshold?, skeletalMeshPath?, frames?, times?, blendPosition?",
  populate_blendspace: "Params: assetPath (or path), axis?, axisIndex?, blendspaceAxes?, axisHorizontal?, horizontalMin?, horizontalMax?, gridNumHorizontal?, axisVertical?, verticalMin?, verticalMax?, gridNumVertical?, samples?, clearExisting?",
  preview_animation: "Params: actorLabel?, actorPath?, enabled?",
  read_anim_blueprint: "Params: assetPath (or path)",
  read_anim_graph: "Params: assetPath (or path), graphName?",
  read_anim_montage: "Params: assetPath (or path)",
  read_anim_sequence: "Params: assetPath? (or path), assetPaths?, directory?, recursive?, nameFilter?, fields?, cursor?, limit?",
  read_blendspace: "Params: assetPath (or path)",
  read_bone_track: "Params: assetPath, boneName, frames?",
  read_control_rig_edit: "Params: sequencePath, bindingTag, controlNames?, frame?, frames?, space?",
  read_control_rig_graph: "Params: assetPath (or path), graphName?, includePins?, includeDefaults?, includeLinks?, limit?",
  read_control_rig_hierarchy: "Params: assetPath (or path)",
  read_ik_retargeter: "Params: assetPath (or path)",
  read_ik_rig: "Params: assetPath (or path)",
  read_mirror_data_table: "Params: assetPath (or path)",
  read_pose_search_database: "Params: assetPath (or path)",
  read_pose_search_schema: "Params: schemaPath (or assetPath)",
  read_state_machine: "Params: assetPath (or path), stateMachineName",
  rebind_leader_pose: "Params: actorLabel?, actorPath?, bodyComponent?",
  register_compatible_skeleton: "Params: skeletonPath, at least one of compatibleSkeletonPath/compatibleSkeletonPaths, remove?",
  remove_anim_curve: "Params: assetPath (or path), curveName",
  remove_anim_notify: "Params: assetPath (or path), notifyName?, notifyClass?",
  remove_anim_notify_state: "Params: assetPath (or path), notifyName?, notifyStateClass?",
  remove_montage_section: "Params: assetPath (or path), sectionName",
  remove_montage_segment: "Params: assetPath (or path), segmentIndex, slotName?, slotIndex?",
  remove_state: "Params: assetPath (or path), stateMachineName, stateName",
  remove_state_machine: "Params: assetPath (or path), stateMachineName",
  remove_transition: "Params: assetPath (or path), stateMachineName, transitionGuid?, fromState?, toState?",
  remove_virtual_bone: "Params: skeletonPath, virtualBoneName",
  reset_retarget_pose: "Params: retargeterPath (or assetPath), side?",
  reverse_sequence: "Params: sourcePath, destinationPath?, name?, packagePath?, inPlace?, cycleOffsetFrames?, cycleOffsetSeconds?, onConflict?",
  sample_pose: "Params: assetPath (or path), boneNames?, frames?, times?, space?, skeletalMeshPath?, incorporateRootMotion?, blendPosition?",
  scan_animation_tracks: "Params: directory?, recursive?, assetPaths?, skeletonPath?, targetTrackCount?, includeTrackNames?",
  set_anim_blueprint_skeleton: "Params: assetPath, skeletonPath",
  set_anim_curve_keys: "Params: assetPath (or path), curveName, keys, interpolation?",
  set_blend_sample: "Params: assetPath (or path), sampleIndex, at least one of position/x/y/animation",
  set_bone_keyframes: "Params: assetPath (or path), boneName, keyframes",
  set_bone_retargeting: "Params: skeletonPath, at least one of mode/restore, bones?, bone?, includeChildren?",
  set_ik_retargeter_rig: "Params: retargeterPath (or assetPath), rigPath (or ikRig), side?",
  set_ik_rig_mesh: "Params: rigPath (or assetPath), meshPath (or skeletalMesh)",
  set_live_post_process_anim_blueprint: "Params: actorLabel?, actorPath?, animBlueprintClassPath?, clear?, componentName?, world?",
  set_montage_properties: "Params: assetPath (or path), sequenceLength?, rateScale?, blendIn?, blendOut?",
  set_montage_sequence: "Params: assetPath (or path), animSequencePath, slotIndex?, segmentIndex?",
  set_montage_slot: "Params: assetPath (or path), slotName, trackIndex?",
  set_motion_matching_chooser: "Params: assetPath (or path), chooserPath (or table), graphName?, contextSource?",
  set_pose_search_clips: "Params: assetPath (or path), clips, clearExisting?",
  set_pose_search_database_settings: "Params: assetPath (or path), continuingPoseCostBias?, baseCostBias?, loopingCostBias?, kdTreeQueryNumNeighbors?, numberOfPrincipalComponents?, poseSearchMode?, normalizationSetPath?",
  set_pose_search_schema: "Params: assetPath (or path), schemaPath",
  set_root_motion_settings: "Params: assetPath (or path), enableRootMotion?, forceRootLock?, useNormalizedRootMotionScale?, rootMotionRootLock?",
  set_sequence_properties: "Params: assetPaths, properties, resolveFromMontages?",
  set_state_animation: "Params: assetPath (or path), stateMachineName, stateName, animAssetPath",
  set_state_machine_entry: "Params: assetPath (or path), stateMachineName, stateName?",
  set_sync_markers: "Params: assetPath (or path), at least one of markers/removeMarkers, markerMode?",
  set_transition_blend: "Params: assetPath (or path), stateMachineName, fromState, toState, blendDuration?, blendLogic?",
  set_transition_condition: "Params: assetPath (or path), stateMachineName, variableName, transitionGuid?, fromState?, toState?, negate?",
};

/** Every key the spec'd animation handlers declare, aliases included. */
export const schema: Record<string, z.ZodType> = {
  actorLabel: z.string().optional().describe("Editor label of the actor; pass this or actorPath"),
  actorPath: z.string().optional().describe("Full actor object path, the unambiguous selector; wins over actorLabel"),
  add: z.array(z.string()).optional().describe("Curve names to add"),
  addDefaultChannels: z.boolean().optional().describe("Add Trajectory and Pose default channels so the schema is buildable (default true)"),
  animAssetPath: z.string().optional().describe("AnimSequence or BlendSpace the state plays"),
  animation: z.string().optional().describe("AnimSequence to add as a sample (add_blend_sample). AnimSequence to swap into the sample (set_blend_sample)"),
  animBlueprintClassPath: z.string().optional().describe("AnimBlueprintGeneratedClass object path, e.g. /Game/Animations/ABP_Name.ABP_Name_C, not the AnimBlueprint asset path"),
  animPath: z.string().optional().describe("AnimSequence or PoseAsset whose curve names to compare"),
  animPaths: z.array(z.string()).optional().describe("AnimSequences to bake, without duplicates"),
  animSequencePath: z.string().optional().describe("AnimSequence or AnimComposite to append as a segment (add_montage_segment). AnimSequence the montage plays (create_anim_montage). AnimSequence to put in the slot (set_montage_sequence)"),
  assetPath: z.string().optional().describe("AnimSequence or AnimMontage asset path (add_anim_notify, add_anim_notify_state, remove_anim_notify, remove_anim_notify_state). BlendSpace or BlendSpace1D asset path (add_blend_sample, populate_blendspace, read_blendspace, set_blend_sample). AnimSequence asset path (add_curve, analyze_animation, apply_animation_modifier, bake_keyframes_batch, bake_root_motion_from_bone, list_anim_modifiers, read_bone_track, remove_anim_curve, set_anim_curve_keys, set_bone_keyframes, set_root_motion_settings, set_sync_markers). AnimMontage asset path (add_montage_section, add_montage_segment, list_montage_segments, read_anim_montage, remove_montage_section, remove_montage_segment, set_montage_properties, set_montage_sequence, set_montage_slot). AnimBlueprint asset path (add_motion_matching_node, add_pose_history_node, add_sequence_evaluator, add_state, add_transition, bind_anim_node_function, create_state_machine, inspect_anim_nodes, read_anim_blueprint, read_anim_graph, read_state_machine, remove_state, remove_state_machine, remove_transition, set_anim_blueprint_skeleton, set_motion_matching_chooser, set_state_animation, set_state_machine_entry, set_transition_blend, set_transition_condition). Alias for schemaPath (add_pose_search_schema_pose_channel, add_pose_search_schema_trajectory_channel, read_pose_search_schema). PoseSearchDatabase asset path (add_pose_search_sequence, build_pose_search_index, read_pose_search_database, set_pose_search_clips, set_pose_search_database_settings, set_pose_search_schema). Alias for retargeterPath (auto_align_retarget_pose, batch_retarget_animations, reset_retarget_pose, set_ik_retargeter_rig). Alias for animPath (compare_curves_to_morph_targets). Alias for skeletonPath (get_bone_transforms). SkeletalMesh asset path (get_physics_asset_info, get_skeleton_info, list_animation_sockets). Control Rig Blueprint asset path (list_control_rig_variables, read_control_rig_graph, read_control_rig_hierarchy). AnimSequence or BlendSpace asset path (measure_natural_speed). AnimSequence to read; omit when reading a batch (read_anim_sequence). IKRetargeter asset path (read_ik_retargeter). IKRigDefinition asset path (read_ik_rig). MirrorDataTable asset path (read_mirror_data_table). AnimSequence or BlendSpace asset path; a montage is refused (sample_pose). Alias for rigPath (set_ik_rig_mesh)"),
  assetPaths: z.array(z.string()).optional().describe("Batch: up to 1000 AnimSequence paths, one row each (read_anim_sequence). Scan exactly these AnimSequences instead of a directory (scan_animation_tracks). AnimSequences to write, or montages when resolveFromMontages is on (set_sequence_properties)"),
  autoMapChains: z.boolean().optional().describe("Assign the rigs to every op and auto-map chains (default true)"),
  autoMapMode: z.string().optional().describe("Native chain auto-map: exact | fuzzy | clear"),
  autoSetup: z.string().optional().describe("Native setup pass: retarget | full_body, which installs the retarget definition and then Full Body IK"),
  axes: z.array(z.string()).optional().describe("Axes to bake: x, y, z (default [x, y])"),
  axis: z.object({ name: z.string().optional().describe("Axis display name"), min: z.number().optional().describe("Axis minimum"), max: z.number().optional().describe("Axis maximum"), gridNum: z.number().int().optional().describe("Grid divisions") }).optional().describe("Axis params for axisIndex (default axis 0)"),
  axisHorizontal: z.string().optional().describe("Horizontal axis name (default Speed) (create_blendspace). Horizontal axis name (populate_blendspace)"),
  axisIndex: z.number().int().optional().describe("Axis the axis object applies to (default 0)"),
  axisMax: z.number().optional().describe("Axis maximum (default 500)"),
  axisMin: z.number().optional().describe("Axis minimum (default 0)"),
  axisName: z.string().optional().describe("Axis display name (default Speed)"),
  axisVertical: z.string().optional().describe("Vertical axis name (default Direction) (create_blendspace). Vertical axis name; ignored on a BlendSpace1D (populate_blendspace)"),
  baseCostBias: z.number().optional().describe("Flat cost added to every pose"),
  binding: z.string().optional().describe("update (default) | becomeRelevant | initialUpdate"),
  bindingTag: z.string().optional().describe("Edit-session natural key from begin_control_rig_edit (apply_control_rig_edits, bake_control_rig_edit, capture_control_rig_pose, read_control_rig_edit). Stable natural key the later calls address (default derived from the mesh name) (begin_control_rig_edit)"),
  blendDuration: z.number().optional().describe("Crossfade in seconds (engine default 0.2) (add_transition). Crossfade in seconds (set_transition_blend)"),
  blendIn: z.number().optional().describe("Blend-in time in seconds"),
  blendLogic: z.string().optional().describe("Standard | Inertialization"),
  blendOut: z.number().optional().describe("Blend-out time in seconds"),
  blendPosition: z.object({ x: z.number().optional().describe("First axis value (default 0)"), y: z.number().optional().describe("Second axis value (default 0)"), z: z.number().optional().describe("Third axis value (default 0)") }).optional().describe("BlendSpace blend input, in the blendspace's own axis units (BlendSpace only)"),
  blendspaceAxes: z.array(z.object({ name: z.string().optional().describe("Axis display name"), min: z.number().optional().describe("Axis minimum"), max: z.number().optional().describe("Axis maximum"), gridNum: z.number().int().optional().describe("Grid divisions") })).optional().describe("Per-axis params, in axis order"),
  blendTime: z.number().optional().describe("Inertial blend time"),
  bodyComponent: z.string().optional().describe("Body component every other one follows (default CharacterMesh0, then Mesh)"),
  bone: z.string().optional().describe("A single bone, when bones is omitted"),
  boneName: z.string().optional().describe("Bone or socket to read (get_bone_transform). Bone whose track to sample (read_bone_track). Bone whose track to replace; a missing track is created (set_bone_keyframes)"),
  boneNames: z.array(z.string()).optional().describe("Bones to sample (default root, pelvis, head, hands and feet when present) (analyze_animation). Bones to return (default every bone) (get_bone_transforms, sample_pose). Bones to read (default every bone, max 1000) (get_live_bone_transforms)"),
  bones: z.array(z.unknown()).optional().describe("Sampled bones, each a bone name (position only) or {bone, flags?: velocity | position | rotation | phase, weight?} (add_pose_search_schema_pose_channel). Bone names to set (default every bone) (set_bone_retargeting)"),
  branchingPoint: z.boolean().optional().describe("Force the montage notify's tick type. The PlayMontageNotify classes default to true on a montage, everything else to the engine's queued tick (#880) (add_anim_notify). On a montage, tick the window as a branching point (add_anim_notify_state)"),
  chainMappings: z.array(z.object({ targetChain: z.string().describe("Target chain to map"), sourceChain: z.unknown().optional().describe("Source chain name; null or omitted clears the mapping") })).optional().describe("Explicit target-to-source chain overrides, at most 10000"),
  chains: z.array(z.object({ name: z.string().describe("Retarget chain name"), startBone: z.string().describe("First bone of the chain"), endBone: z.string().describe("Last bone of the chain"), goal: z.string().optional().describe("IK goal the chain drives") })).optional().describe("Retarget chains to upsert, at most 256 (configure_ik_rig). Retarget chains to add (create_ik_rig)"),
  chooserPath: z.string().optional().describe("ChooserTable that selects the database"),
  clear: z.boolean().optional().describe("Clear the transient override and fall back to the skeletal mesh asset's post-process AnimBP"),
  clearExisting: z.boolean().optional().describe("Clear the existing samples first (default true) (populate_blendspace). Replace the clip list rather than append to it (default true) (set_pose_search_clips)"),
  clips: z.array(z.unknown()).optional().describe("Clips, each an animation asset path or {sequencePath (or asset, assetPath, animationPath), mirror?: original | mirrored | both, disableReselection?, sampleStart?, sampleEnd?, enabled?}"),
  compatibleSkeletonPath: z.string().optional().describe("Skeleton to mark compatible"),
  compatibleSkeletonPaths: z.array(z.string()).optional().describe("Several skeletons at once; wins over compatibleSkeletonPath"),
  componentName: z.string().optional().describe("SkeletalMeshComponent to read (default CharacterMesh0, then Mesh, then the first one)"),
  connectToOutput: z.boolean().optional().describe("Wire the node to the Output Pose (default true) (add_motion_matching_node). Wire the node to the graph's result pose (default true) (add_sequence_evaluator)"),
  contactThreshold: z.number().optional().describe("Height in cm below which a foot counts as planted (default derived from the clip's lowest foot height)"),
  contextSource: z.string().optional().describe("What the chooser reads its columns from: self (default, the anim instance) | pawn (the owning pawn)"),
  continuingPoseCostBias: z.number().optional().describe("Bias to keep playing the current clip"),
  controlNames: z.array(z.string()).optional().describe("Live controls to capture (default the current Control Rig selection) (capture_control_rig_pose). Controls to read (default every readable control) (read_control_rig_edit)"),
  controlRigPath: z.string().optional().describe("The verified baseline ControlRigBlueprint for this character; required when rigMode=asset. Create and validate the rig first when the character has none"),
  createLink: z.literal(false).optional().describe("Sequencer links are not supported yet; omit or pass false"),
  cursor: z.string().optional().describe("Resume a paged read: the nextCursor the previous page returned, unmodified"),
  curveName: z.string().optional().describe("Float curve to add (add_curve). Float curve to remove (remove_anim_curve). Float curve to write; a missing curve is added (set_anim_curve_keys)"),
  cycleOffsetFrames: z.number().int().optional().describe("Rotate the reversed loop to start this many frames in"),
  cycleOffsetSeconds: z.number().optional().describe("Rotate the reversed loop to start this many seconds in, rounded to whole frames"),
  databasePath: z.string().optional().describe("PoseSearchDatabase the node searches"),
  databases: z.array(z.string()).optional().describe("PoseSearchDatabases to normalize together"),
  destinationPath: z.string().optional().describe("Asset path for the reversed copy; alternative to name and packagePath"),
  directory: z.string().optional().describe("Folder to scope the read to (list_anim_assets, list_skeletal_meshes). Batch: every AnimSequence in this folder (read_anim_sequence). Content root to scan (default every root) (scan_animation_tracks)"),
  disableReselection: z.boolean().optional().describe("Disallow reselecting poses from the same asset"),
  displayRate: z.number().optional().describe("LevelSequence display rate in frames per second (default the source rate)"),
  duration: z.number().optional().describe("Window length in seconds, greater than 0"),
  edits: z.array(z.record(z.unknown())).optional().describe("Hierarchy edits, each one of {op:'add',bone,parent,transform?} | {op:'remove',bone,removeChildren?} | {op:'rename',bone,newName} | {op:'reparent',bone,parent} | {op:'set_transform',bone,transform,moveChildren?}"),
  enabled: z.boolean().optional().describe("Include the clip in the database (add_pose_search_sequence). Tick animation in the editor (default true) (preview_animation)"),
  enableRootMotion: z.boolean().optional().describe("Extract root motion from the root bone"),
  endFrame: z.number().int().optional().describe("Exclusive edit range end frame (default the source length)"),
  endPos: z.number().optional().describe("Trim end inside the source, in seconds (default the source play length)"),
  ensureDefaultOps: z.boolean().optional().describe("Ensure the complete default op stack (default true)"),
  entries: z.array(z.record(z.unknown())).optional().describe("Per-bone scales as [{bone, scale, recursive?}]"),
  exclusions: z.array(z.object({ bone: z.string().describe("Bone to exclude or include"), excluded: z.boolean().describe("Whether the solver excludes the bone") })).optional().describe("Per-bone solver exclusions, at most 2048"),
  explicitTime: z.number().optional().describe("Initial ExplicitTime"),
  expressions: z.array(z.object({ find: z.string().describe("Bone-name fragment to match"), replace: z.string().describe("Replacement fragment"), method: z.string().optional().describe("suffix (default) | prefix | regex") })).optional().describe("Find/replace rules that derive the bone pairs (default the mannequin _l/_r suffix swap)"),
  facingBones: z.array(z.string()).optional().describe("[boneA, boneB]: report the yaw of the boneA to boneB vector relative to the root bone's forward, per sample and over the clip"),
  fields: z.array(z.string()).optional().describe("Result keys to return; assetPath is always kept and an unknown name is refused with the valid list"),
  flags: z.array(z.object({ curve: z.string().describe("Curve to flag"), material: z.boolean().optional().describe("Drive a material parameter"), morphTarget: z.boolean().optional().describe("Drive a morph target") })).optional().describe("Flag writes; each sets material, morphTarget or both"),
  footBones: z.array(z.string()).optional().describe("Foot bones to track, e.g. [foot_l, foot_r]"),
  force: z.boolean().optional().describe("Remove a bone despite dependents, which are listed in the refusal"),
  forceRemap: z.boolean().optional().describe("Replace existing mappings during auto-map (default false)"),
  forceRootLock: z.boolean().optional().describe("Lock the root bone even without root motion"),
  frame: z.number().optional().describe("One frame to sample, an integer; combines with frames"),
  frameRate: z.number().optional().describe("Frames per second of the bake (default the sequence's display rate) (bake_control_rig_edit). Frames per second (default 30) (create_sequence)"),
  frames: z.array(z.number()).optional().describe("Explicit frames to sample, integers in [0, frame count] (analyze_animation). Frames to sample; pass this or times, not both (default every sampled key) (measure_natural_speed, sample_pose). Frames to sample (default: first, middle and last) (read_bone_track). Integer frames to sample (default the first and last frame of the range) (read_control_rig_edit)"),
  fromState: z.string().optional().describe("State the transition leaves (add_transition, set_transition_blend). With toState, removes every transition between the two (remove_transition). With toState, the transition to condition when transitionGuid is omitted (set_transition_condition)"),
  fullBodyIK: z.object({ solverIndex: z.number().int().optional().describe("Full Body IK solver to edit (default the first)"), rootBone: z.string().describe("Solver root bone"), enabled: z.boolean().optional().describe("Enable or disable the solver"), goals: z.array(z.record(z.unknown())).describe("Up to 256 goals as {name, bone, positionAlpha?, rotationAlpha?, chainDepth?, strengthAlpha?, pullChainAlpha?, pinRotation?}; alphas are in [0, 1]") }).optional().describe("Full Body IK solver and its desired goals"),
  function: z.string().optional().describe("Alias for functionName"),
  functionName: z.string().optional().describe("Existing thread-safe anim-node function on the AnimBlueprint"),
  generateTrajectory: z.boolean().optional().describe("Self-generate the trajectory (default true)"),
  graphName: z.string().optional().describe("Graph to add the node to (default AnimGraph) (add_motion_matching_node, add_pose_history_node). AnimGraph (default) or a state's name for its inner graph (add_sequence_evaluator). Graph holding the node (default AnimGraph) (bind_anim_node_function). Graph to add it to (default AnimGraph) (create_state_machine). Graph to inspect (default AnimGraph) (inspect_anim_nodes). Graph to read (default AnimGraph) (read_anim_graph). Substring filter over model names (read_control_rig_graph). Graph holding the Motion Matching node (default AnimGraph) (set_motion_matching_chooser)"),
  gridNum: z.number().optional().describe("Grid divisions (default 4)"),
  gridNumHorizontal: z.number().int().optional().describe("Horizontal axis grid divisions"),
  gridNumVertical: z.number().int().optional().describe("Vertical axis grid divisions; ignored on a BlendSpace1D"),
  horizontalMax: z.number().optional().describe("Horizontal axis maximum (default 500) (create_blendspace). Horizontal axis maximum (populate_blendspace)"),
  horizontalMin: z.number().optional().describe("Horizontal axis minimum (default 0) (create_blendspace). Horizontal axis minimum (populate_blendspace)"),
  ikRig: z.string().optional().describe("Alias for rigPath"),
  includeChildren: z.boolean().optional().describe("Apply recursively down the bone tree (default false)"),
  includeDefaults: z.boolean().optional().describe("Include pin default values (default true)"),
  includeLinks: z.boolean().optional().describe("Include pin-to-pin links (default true)"),
  includePins: z.boolean().optional().describe("Include each node's pins (default true)"),
  includeTrackNames: z.boolean().optional().describe("Include each sequence's bone track names"),
  incorporateRootMotion: z.boolean().optional().describe("Fold the clip's root motion into the pose (default true)"),
  inPlace: z.boolean().optional().describe("Reverse the source itself instead of writing a copy (default false)"),
  insertBeforeOutput: z.boolean().optional().describe("Splice into the pose chain feeding the Output Pose (default true)"),
  insertIndex: z.number().int().optional().describe("Position in the slot's segment list (default appends)"),
  interpolation: z.string().optional().describe("linear (default) | per_frame (bake_root_motion_from_bone). Interpolation of keys without their own interp: linear (default) | constant | cubic (set_anim_curve_keys)"),
  items: z.array(z.object({ name: z.string().describe("AnimMontage asset name"), animSequencePath: z.string().describe("AnimSequence the montage plays"), packagePath: z.string().optional().describe("Destination folder (default /Game/Animations)"), onConflict: z.string().optional().describe("skip (default) | error; any other value fails the item"), slotName: z.string().optional().describe("Slot name to write onto the track"), trackIndex: z.number().int().optional().describe("Slot track index (default 0)"), rateScale: z.number().optional().describe("Playback rate scale"), blendIn: z.number().optional().describe("Blend-in time in seconds"), blendOut: z.number().optional().describe("Blend-out time in seconds"), sequenceLength: z.number().optional().describe("Montage length in seconds"), sections: z.array(z.record(z.unknown())).optional().describe("Sections as [{sectionName, startTime?, linkedSection?}]"), notifies: z.array(z.record(z.unknown())).optional().describe("Notifies as [{notifyName, triggerTime, notifyClass?, properties?}]") })).optional().describe("Montages to author, each reported on its own"),
  kdTreeQueryNumNeighbors: z.number().optional().describe("KD-tree neighbours to consider"),
  keyframes: z.array(z.object({ location: z.object({ x: z.number(), y: z.number(), z: z.number() }).optional().describe("Bone-local translation"), rotation: z.record(z.unknown()).optional().describe("Bone-local rotation as a quaternion {x, y, z, w}"), scale: z.object({ x: z.number(), y: z.number(), z: z.number() }).optional().describe("Bone-local scale") })).optional().describe("One key per frame, in order; an omitted channel keeps the reference pose"),
  keys: z.array(z.object({ time: z.number().describe("Key time in seconds"), value: z.number().describe("Key value"), interp: z.string().optional().describe("linear | constant | cubic (default the call's interpolation)") })).optional().describe("Keys that replace the curve's own"),
  layered: z.boolean().optional().describe("Keep the source animation track active under the Control Rig layer (default false)"),
  limit: z.number().int().optional().describe("Rows per page (default 200, max 5000) (list_anim_assets, list_skeletal_meshes). Bones per page (default 200, max 5000) (list_bones). Batch rows per page (default 50, max 200) (read_anim_sequence). Nodes per graph (default 200) (read_control_rig_graph)"),
  linkedSection: z.string().optional().describe("Next section to link to"),
  loop: z.boolean().optional().describe("Include end-to-start loop continuity metrics"),
  loopCount: z.number().int().optional().describe("How many times the segment repeats (default 1)"),
  loopingCostBias: z.number().optional().describe("Bias for looping clips"),
  markerMode: z.string().optional().describe("replace (default: markers becomes the whole list) | merge (add or move only the named markers)"),
  markers: z.array(z.object({ name: z.string().describe("Marker name"), time: z.number().describe("Marker time in seconds, within the clip") })).optional().describe("Sync markers to author; with markerMode=replace an empty array clears them"),
  meshPath: z.string().optional().describe("Alias for skeletalMeshPath (compare_curves_to_morph_targets). SkeletalMesh to set as the rig's preview and source mesh (set_ik_rig_mesh)"),
  mirror: z.string().optional().describe("original | mirrored | both"),
  mirrorAxis: z.string().optional().describe("X (default) | Y | Z"),
  mirrorDataTablePath: z.string().optional().describe("MirrorDataTable to bind"),
  mirrorRootMotion: z.boolean().optional().describe("Mirror root motion (default true)"),
  mode: z.string().optional().describe("TimeFactor | WeightFactor | BlendMask (author_blend_profile). Animation | Skeleton | AnimationScaled | AnimationRelative | OrientAndScale (set_bone_retargeting)"),
  modifier: z.string().optional().describe("Alias for modifierClass"),
  modifierClass: z.string().optional().describe("UAnimationModifier subclass: a short name such as DistanceCurveModifier, or a /Script path"),
  name: z.string().optional().describe("AnimBlueprint asset name (create_anim_blueprint). AnimComposite asset name (create_anim_composite). AnimMontage asset name (create_anim_montage). BlendSpace asset name (create_blendspace). BlendSpace1D asset name (create_blendspace_1d). Asset name (default <Source>_CtrlRig) (create_control_rig). IKRetargeter asset name (create_ik_retargeter). IKRigDefinition asset name (create_ik_rig). MirrorDataTable asset name (create_mirror_data_table). PoseSearchDatabase asset name (create_pose_search_database). PoseSearchNormalizationSet asset name (create_pose_search_normalization_set). PoseSearchSchema asset name (create_pose_search_schema). AnimSequence asset name (create_sequence). Skeleton asset name (create_skeleton). State machine name (default NewStateMachine) (create_state_machine). Name of the reversed copy (default <Source>_Reversed) (reverse_sequence)"),
  nameFilter: z.string().optional().describe("With directory: case-insensitive substring, or * ? wildcard, the asset name must match"),
  negate: z.boolean().optional().describe("Enter when the variable is false (default false)"),
  newProfileName: z.string().optional().describe("New name when operation=rename"),
  nodeClass: z.string().optional().describe("Node class substring filter, e.g. PoseDriver"),
  nodeGuid: z.string().optional().describe("Anim graph node to bind, from add_sequence_evaluator or an add_*_node action"),
  nodeId: z.string().optional().describe("Alias for nodeGuid"),
  normalizationSetPath: z.string().optional().describe("PoseSearchNormalizationSet to assign"),
  notifyClass: z.string().optional().describe("UAnimNotify class to spawn: a class name, a name without the AnimNotify_ prefix, or a path (add_anim_notify). Notify class to match. Pass at least one of notifyName and notifyClass; both filters apply together (remove_anim_notify)"),
  notifyName: z.string().optional().describe("Notify name (add_anim_notify, add_anim_notify_state). Notify name to match (remove_anim_notify, remove_anim_notify_state)"),
  notifyProperties: z.record(z.unknown()).optional().describe("EditAnywhere fields to set on the spawned notify object; requires a notifyClass that resolves (add_anim_notify). EditAnywhere fields to set on the spawned notify state object, validated against the class first (add_anim_notify_state)"),
  notifyStateClass: z.string().optional().describe("UAnimNotifyState subclass: a class name, a bare suffix such as TimedParticleEffect, or a full path (add_anim_notify_state). Notify state class to match. Pass at least one of notifyName and notifyStateClass; both filters apply together (remove_anim_notify_state)"),
  numberOfPrincipalComponents: z.number().optional().describe("PCA components for PCAKDTree mode"),
  numFrames: z.number().optional().describe("Frame count (default 30)"),
  onConflict: z.string().optional().describe("skip (default) returns an existing section untouched, error refuses (add_montage_section). skip (default) returns an existing state untouched, error refuses (add_state). skip returns an existing output, error (default) refuses; it never overwrites (bake_control_rig_edit). skip returns the existing session, error (default) refuses; an existing session is never modified (begin_control_rig_edit). skip (default) returns an existing asset untouched, error refuses (create_anim_blueprint, create_anim_composite, create_anim_montage, create_blendspace, create_blendspace_1d, create_ik_retargeter, create_ik_rig, create_mirror_data_table, create_pose_search_database, create_pose_search_normalization_set, create_pose_search_schema, create_sequence). skip (default) returns the existing asset, error refuses; it never overwrites (create_control_rig). skip (default) | error. It never overwrites (create_skeleton). skip (default) returns an existing destination untouched, error refuses; it never overwrites (reverse_sequence)"),
  operation: z.string().optional().describe("upsert (default) | remove | rename"),
  operations: z.array(z.discriminatedUnion("op", [z.object({ op: z.literal("set"), control: z.string().describe("Control name on the session's rig"), frame: z.number().int().optional().describe("One frame to key; pass exactly one of frame and frames"), frames: z.array(z.number().int()).optional().describe("Frames to key; pass exactly one of frame and frames"), transform: z.record(z.unknown()).describe("{translation {x,y,z}, rotationDegrees {pitch,yaw,roll}, scale {x,y,z}}"), space: z.string().optional().describe("local (default) | component | global, where global is an alias for component") }).strict().describe("Write one full absolute transform at frame or frames"), z.object({ op: z.literal("set_keys"), control: z.string().describe("Control name on the session's rig"), keys: z.array(z.record(z.unknown())).describe("[{frame, transform {translation {x,y,z}, rotationQuaternion {x,y,z,w}, scale {x,y,z}}}], frames strictly increasing"), space: z.string().optional().describe("local (default) | component | global, where global is an alias for component") }).strict().describe("Write strictly ordered full per-frame transforms from normalized quaternions"), z.object({ op: z.literal("offset"), control: z.string().describe("Control name on the session's rig"), startFrame: z.number().int().describe("First frame of the range"), endFrame: z.number().int().describe("Last frame of the range, at or after startFrame"), translationCm: z.object({ x: z.number(), y: z.number(), z: z.number() }).optional().describe("Translation delta in centimetres"), rotationDegrees: z.object({ pitch: z.number(), yaw: z.number(), roll: z.number() }).optional().describe("Rotation delta in degrees"), scaleMultiplier: z.object({ x: z.number(), y: z.number(), z: z.number() }).optional().describe("Scale multiplier; one of translationCm, rotationDegrees and scaleMultiplier is required"), space: z.string().optional().describe("local (default) | component | global, where global is an alias for component"), blendInFrames: z.number().int().optional().describe("Frames to ease in over, from 0 (default 0)"), blendOutFrames: z.number().int().optional().describe("Frames to ease out over, from 0 (default 0)") }).strict().describe("Apply translation, rotation or scale deltas across an inclusive frame range"), z.object({ op: z.literal("contact_lock"), control: z.string().describe("Control name on the session's rig"), drivenReference: z.string().optional().describe("Bone or socket to constrain instead of the control; bake and analyze before accepting it"), startFrame: z.number().int().describe("First frame of the lock"), endFrame: z.number().int().describe("Last frame of the lock, at or after startFrame"), target: z.record(z.unknown()).optional().describe("{translation {x,y,z}, rotationQuaternion? {x,y,z,w}}: a fixed component-space target, or a transform relative to targetReference when that is set; pass target or targetReference"), targetReference: z.string().optional().describe("Source-animation bone or socket to follow. Without target, the first frame's subject-to-reference offset is kept"), blendInFrames: z.number().int().optional().describe("Frames to ease in over, from 0 (default 0)"), blendOutFrames: z.number().int().optional().describe("Frames to ease out over, from 0 (default 0)"), stabilizeControls: z.array(z.string()).optional().describe("Up to 8 pole or stabilizer controls to hold steady; never the locked control itself"), positionToleranceCm: z.number().optional().describe("Readback position tolerance, above 0 and up to 100 (default 0.1)"), rotationToleranceDegrees: z.number().optional().describe("Readback rotation tolerance, above 0 and up to 180 (default 0.5)") }).strict().describe("Constrain a translatable driver control, or a driven bone or socket, to a fixed component-space target"), z.object({ op: z.literal("set_bool"), control: z.string().describe("Control name on the session's rig"), frame: z.number().int().optional().describe("One frame to key; pass exactly one of frame and frames"), frames: z.array(z.number().int()).optional().describe("Frames to key; pass exactly one of frame and frames"), value: z.boolean().describe("Value to key") }).strict().describe("Key a bool control at frame or frames"), z.object({ op: z.literal("set_float"), control: z.string().describe("Control name on the session's rig"), frame: z.number().int().optional().describe("One frame to key; pass exactly one of frame and frames"), frames: z.array(z.number().int()).optional().describe("Frames to key; pass exactly one of frame and frames"), value: z.number().describe("Finite value to key") }).strict().describe("Key a float control at frame or frames"), z.object({ op: z.literal("set_int"), control: z.string().describe("Control name on the session's rig"), frame: z.number().int().optional().describe("One frame to key; pass exactly one of frame and frames"), frames: z.array(z.number().int()).optional().describe("Frames to key; pass exactly one of frame and frames"), value: z.number().int().describe("Value to key") }).strict().describe("Key an integer or enum control at frame or frames; an enum takes one of its enumOptions values"), z.object({ op: z.literal("propagate_pose"), baseline: z.record(z.unknown()).describe("Snapshot from capture_control_rig_pose before the edit"), accepted: z.record(z.unknown()).describe("Snapshot from capture_control_rig_pose after the edit, same session, rig instance, frame and control set"), controls: z.array(z.record(z.unknown())).describe("[{control, mode fixed | local_delta, donorFrames strictly increasing}], each control once; bool, enum and int controls take fixed only") }).strict().describe("Key the controls that changed between two live snapshots across each control's donor frames")])).optional().describe("Typed edits applied in one transaction, in order"),
  ops: z.array(z.object({ name: z.string().optional().describe("Op to write, by name"), index: z.number().int().optional().describe("Op to write, by stack index, when name is omitted"), enabled: z.boolean().optional().describe("Enable or disable the op"), settings: z.record(z.unknown()).optional().describe("Properties on the op's settings struct, e.g. RootMotionSource"), chainSettings: z.array(z.record(z.unknown())).optional().describe("Per-chain properties as [{chain, ...}], e.g. RotationMode") })).optional().describe("Per-op writes, at most 64. settings writes properties on the op's own settings struct, and chainSettings merges per-chain FK properties into the named chain rather than replacing the list"),
  outputAssetPath: z.string().optional().describe("Destination AnimSequence asset path"),
  outputDirectory: z.string().optional().describe("Directory under Project/Saved/Codex/AnimationQA for analysis artifacts; must not already contain them"),
  outputPath: z.string().optional().describe("Destination folder (default beside each source)"),
  overwrite: z.literal(false).optional().describe("Overwriting is not supported; omit or pass false"),
  packagePath: z.string().optional().describe("Destination folder (default /Game/Animations) (create_anim_blueprint, create_anim_composite, create_anim_montage, create_blendspace, create_blendspace_1d, create_sequence). Destination folder (default the source's folder) (create_control_rig). Destination folder (default /Game) (create_ik_retargeter, create_ik_rig, create_skeleton). Destination folder (default /Game/MotionMatching) (create_mirror_data_table, create_pose_search_database, create_pose_search_normalization_set, create_pose_search_schema). Folder of the reversed copy (default the source's folder) (reverse_sequence)"),
  parentClass: z.string().optional().describe("Parent AnimInstance class name"),
  path: z.string().optional().describe("Alias for assetPath (add_anim_notify, add_anim_notify_state, add_blend_sample, add_curve, add_montage_section, add_montage_segment, add_motion_matching_node, add_pose_history_node, add_pose_search_sequence, add_sequence_evaluator, add_state, add_transition, analyze_animation, apply_animation_modifier, bake_keyframes_batch, bake_root_motion_from_bone, bind_anim_node_function, build_pose_search_index, create_state_machine, get_physics_asset_info, get_skeleton_info, inspect_anim_nodes, list_anim_modifiers, list_animation_sockets, list_control_rig_variables, list_montage_segments, measure_natural_speed, populate_blendspace, read_anim_blueprint, read_anim_graph, read_anim_montage, read_anim_sequence, read_blendspace, read_control_rig_graph, read_control_rig_hierarchy, read_ik_retargeter, read_ik_rig, read_mirror_data_table, read_pose_search_database, read_state_machine, remove_anim_curve, remove_anim_notify, remove_anim_notify_state, remove_montage_section, remove_montage_segment, remove_state, remove_state_machine, remove_transition, sample_pose, set_anim_curve_keys, set_blend_sample, set_bone_keyframes, set_montage_properties, set_montage_sequence, set_montage_slot, set_motion_matching_chooser, set_pose_search_clips, set_pose_search_database_settings, set_pose_search_schema, set_root_motion_settings, set_state_animation, set_state_machine_entry, set_sync_markers, set_transition_blend, set_transition_condition). Alias for skeletonPath (get_bone_transforms)"),
  playRate: z.number().optional().describe("Segment play rate; negative plays in reverse (default 1)"),
  pose: z.object({ side: z.string().describe("source | target"), name: z.string().describe("Retarget pose name"), create: z.boolean().optional().describe("Create the pose (default false)"), reset: z.boolean().optional().describe("Reset the pose first, which whole-pose auto-align requires on an existing pose"), autoAlign: z.string().optional().describe("chain_to_chain | mesh_to_mesh | local_axes | global_axes; needs both preview meshes"), bones: z.array(z.string()).optional().describe("Bones to auto-align; omitted aligns every bone, which replaces the pose"), rotationOffsets: z.array(z.record(z.unknown())).optional().describe("Offsets as [{bone, rotationQuaternion {x, y, z, w}}], normalized"), rootOffsetZ: z.number().optional().describe("Root height offset; exclusive with snapBoneToGround"), snapBoneToGround: z.string().optional().describe("Bone to snap to the ground; exclusive with rootOffsetZ") }).optional().describe("Named source or target retarget pose to author"),
  poseCount: z.number().optional().describe("History poses to retain"),
  poseSearchMode: z.string().optional().describe("bruteforce | pcakdtree | vptree | eventonly"),
  position: z.object({ x: z.number().optional().describe("Horizontal axis value"), y: z.number().optional().describe("Vertical axis value") }).optional().describe("Sample position on the blendspace axes; wins over flat x and y (add_blend_sample). New sample position; wins over flat x and y, and an omitted axis keeps its value (set_blend_sample)"),
  prefix: z.string().optional().describe("Output name prefix"),
  profileName: z.string().optional().describe("Blend profile to create or edit"),
  properties: z.record(z.unknown()).optional().describe("{enableRootMotion?, forceRootLock?, useNormalizedRootMotionScale?, rootMotionRootLock?}"),
  props: z.record(z.unknown()).optional().describe("EditAnywhere property values to set on the modifier before it runs"),
  rateScale: z.number().optional().describe("Playback rate scale"),
  recursive: z.boolean().optional().describe("Include subfolders (default true) (list_anim_assets, list_skeletal_meshes, scan_animation_tracks). With directory: include subfolders (default true) (read_anim_sequence)"),
  reduceKeys: z.literal(false).optional().describe("Key reduction is not supported yet; omit or pass false"),
  remove: z.union([z.array(z.string()), z.boolean()]).optional().describe("Curve names to remove (edit_curve_metadata). true to unregister instead of register (default false) (register_compatible_skeleton)"),
  removeEntries: z.array(z.string()).optional().describe("Bone names to drop from the profile"),
  removeMarkers: z.array(z.string()).optional().describe("Marker names to drop, applied after markers"),
  rename: z.array(z.object({ from: z.string().describe("Current curve name"), to: z.string().describe("New curve name") })).optional().describe("Curve renames"),
  requireCompleteMapping: z.boolean().optional().describe("Refuse when any target chain is unmapped (default false)"),
  resolveFromMontages: z.boolean().optional().describe("Resolve a montage to its first AnimSequence (default true)"),
  restore: z.array(z.object({ bone: z.string().describe("Bone to restore"), mode: z.string().describe("Mode to restore it to") })).optional().describe("Per-bone modes to put back, which is how this action's rollback replays; wins over mode"),
  retargeterPath: z.string().optional().describe("Existing IKRetargeter to edit (auto_align_retarget_pose, reset_retarget_pose, set_ik_retargeter_rig). IKRetargeter to bake through (batch_retarget_animations). Existing IKRetargeter to configure (configure_ik_retargeter)"),
  retargetRoot: z.string().optional().describe("Retarget root bone"),
  rigMode: z.string().optional().describe("fk (default) | asset. Use asset with the verified baseline controlRigPath; use fk only when generated raw FK controls are the intended editing surface"),
  rigPath: z.string().optional().describe("Existing IKRigDefinition to configure (configure_ik_rig). IKRigDefinition to assign (set_ik_retargeter_rig). Existing IKRigDefinition to edit (set_ik_rig_mesh)"),
  rootBone: z.string().optional().describe("Root bone name (default root)"),
  rootMotionBone: z.string().optional().describe("Root-motion bone"),
  rootMotionRootLock: z.string().optional().describe("Root lock mode: RefPose | AnimFirstFrame | Zero"),
  sampleEnd: z.number().optional().describe("Sampling range end in seconds"),
  sampleIndex: z.number().int().optional().describe("Sample to edit, as read_blendspace lists it"),
  sampleRate: z.number().optional().describe("Samples per second when frames is omitted, 1 to 240 (default the source rate) (analyze_animation). Schema sample rate (default 30) (create_pose_search_schema)"),
  samples: z.array(z.record(z.unknown())).optional().describe("[{offset, flags?, weight?}]: offset in seconds, negative for history and positive for prediction; flags from position, velocity, facingDirection, velocityDirection and their XY variants (add_pose_search_schema_trajectory_channel). Samples to add as [{animationPath, x, y?}]; an unloadable animation is reported under failed (populate_blendspace)"),
  sampleStart: z.number().optional().describe("Sampling range start in seconds"),
  samplingInterval: z.number().optional().describe("Seconds between history samples"),
  save: z.boolean().optional().describe("Save the asset after baking (default true)"),
  schemaPath: z.string().optional().describe("PoseSearchSchema to add the channel to (add_pose_search_schema_pose_channel, add_pose_search_schema_trajectory_channel). Existing PoseSearchSchema to use; a schema that cannot index is refused (create_pose_search_database). PoseSearchSchema to read (read_pose_search_schema). PoseSearchSchema to assign; a schema that cannot index is refused (set_pose_search_schema)"),
  sectionName: z.string().optional().describe("Composite section to add (add_montage_section). Composite section to remove (remove_montage_section)"),
  segmentIndex: z.number().optional().describe("Segment to anchor the section to, so it moves with that segment (#826) (add_montage_section). Segment to remove (remove_montage_segment). Replace only this segment; without it every segment in the slot is replaced (#626) (set_montage_sequence)"),
  sequenceLength: z.number().optional().describe("Montage length in seconds"),
  sequencePath: z.string().optional().describe("AnimSequence, AnimComposite, AnimMontage or BlendSpace to append (add_pose_search_sequence). AnimSequence to evaluate (add_sequence_evaluator). LevelSequence holding the Control Rig edit session (apply_control_rig_edits, bake_control_rig_edit, capture_control_rig_pose, read_control_rig_edit). LevelSequence to create, or to reuse with onConflict=skip (begin_control_rig_edit)"),
  sessionTag: z.string().optional().describe("Stable key every later call addresses (default Skel_<MeshName>) (begin_skeleton_edit). The open skeleton edit session to address; wins over skeletalMeshPath (cancel_skeleton_edit, commit_skeleton_edit, edit_skeleton_bones)"),
  shouldLoop: z.boolean().optional().describe("bShouldLoop"),
  side: z.string().optional().describe("source | target (default target)"),
  skeletalMesh: z.string().optional().describe("Alias for meshPath"),
  skeletalMeshPath: z.string().optional().describe("SkeletalMesh whose proportions to sample with; must be compatible with the sequence's skeleton (analyze_animation). SkeletalMesh the session binds (begin_control_rig_edit). SkeletalMesh whose reference skeleton to edit (begin_skeleton_edit). The skeletal mesh whose one open session to address, when sessionTag is omitted (cancel_skeleton_edit, commit_skeleton_edit, edit_skeleton_bones). SkeletalMesh whose morph targets to compare against (compare_curves_to_morph_targets). SkeletalMesh to import the bone hierarchy from; pass exactly one of this and skeletonPath (create_control_rig). SkeletalMesh the rig is built on (create_ik_rig). SkeletalMesh to build the skeleton from; the factory assigns the new skeleton to it (create_skeleton). Evaluate with this SkeletalMesh's proportions (measure_natural_speed, sample_pose)"),
  skeletonPath: z.string().optional().describe("USkeleton asset path (add_virtual_bone, remove_virtual_bone). USkeleton that owns the blend profile (author_blend_profile). USkeleton the AnimBlueprint targets (create_anim_blueprint). USkeleton the composite targets (create_anim_composite). USkeleton the blendspace targets (create_blendspace, create_blendspace_1d). Skeleton to import the bone hierarchy from; pass exactly one of this and skeletalMeshPath (create_control_rig). USkeleton the table mirrors (create_mirror_data_table). Author a <name>_Schema with default channels on this skeleton when schemaPath is omitted (create_pose_search_database). USkeleton the schema samples (create_pose_search_schema). USkeleton, or a SkeletalMesh whose skeleton to use (create_sequence). USkeleton whose curve metadata to edit (edit_curve_metadata). USkeleton, or a SkeletalMesh whose skeleton to read (get_bone_transforms). USkeleton that gains or loses the compatible entries (register_compatible_skeleton). Only sequences on this skeleton; a path that names no skeleton is refused (scan_animation_tracks). USkeleton to target (set_anim_blueprint_skeleton). USkeleton whose bone tree to edit (set_bone_retargeting)"),
  slotIndex: z.number().optional().describe("Slot index holding the anchor segment (default 0) (add_montage_section). Target slot index when slotName is omitted (default 0) (add_montage_segment). Slot index holding the segment (default 0) (remove_montage_segment). Slot track index (default 0) (set_montage_sequence)"),
  slotName: z.string().optional().describe("Slot holding the anchor segment; wins over slotIndex (add_montage_section). Target slot, created when absent (add_montage_segment). List only this slot (list_montage_segments). Slot holding the segment; wins over slotIndex (remove_montage_segment). Slot name to write onto the track (set_montage_slot)"),
  sourceAnimationPath: z.string().optional().describe("AnimSequence to bake into the rig"),
  sourceBone: z.string().optional().describe("Bone the virtual bone starts from (add_virtual_bone). Bone whose translation moves onto the root, e.g. pelvis (bake_root_motion_from_bone)"),
  sourceMesh: z.string().optional().describe("SkeletalMesh the source animations play on"),
  sourcePath: z.string().optional().describe("AnimSequence to reverse"),
  sourcePreviewMesh: z.string().optional().describe("Source preview SkeletalMesh"),
  sourceRig: z.string().optional().describe("Source IKRigDefinition to assign"),
  space: z.string().optional().describe("world (default) | component | local (get_bone_transform, get_live_bone_transforms). local (default) | component, which composes the parent chain (get_bone_transforms). local (default) | component | global, where global is an alias for component (read_control_rig_edit). component (default) | local | world, which resolves to component on an asset (sample_pose)"),
  startFrame: z.number().int().optional().describe("Inclusive edit range start frame (default 0)"),
  startPos: z.number().optional().describe("Trim start inside the source, in seconds (default 0)"),
  startTime: z.number().optional().describe("Section start in seconds; taken from the segment when segmentIndex is given"),
  stateMachineName: z.string().optional().describe("State machine to edit (add_state, add_transition, remove_state, remove_state_machine, remove_transition, set_state_animation, set_state_machine_entry, set_transition_blend, set_transition_condition). State machine to read (read_state_machine)"),
  stateName: z.string().optional().describe("State to add (add_state). State to remove, with every transition that touches it (remove_state). State whose animation to set (set_state_animation). State the entry points at; omitted or empty clears the link (set_state_machine_entry)"),
  suffix: z.string().optional().describe("Output name suffix (default _Retargeted)"),
  table: z.string().optional().describe("Alias for chooserPath"),
  targetBone: z.string().optional().describe("Bone the virtual bone points at"),
  targetMesh: z.string().optional().describe("SkeletalMesh to retarget onto"),
  targetPreviewMesh: z.string().optional().describe("Target preview SkeletalMesh"),
  targetRig: z.string().optional().describe("Target IKRigDefinition to assign"),
  targetTrackCount: z.number().optional().describe("Flag sequences with more than this many bone tracks"),
  teleportToExplicitTime: z.boolean().optional().describe("bTeleportToExplicitTime (default false, so time advances and root motion extracts)"),
  times: z.array(z.number()).optional().describe("Sample times in seconds; pass this or frames, not both"),
  tolerance: z.number().optional().describe("Key-reduction tolerance (default 0.001)"),
  toState: z.string().optional().describe("State the transition enters (add_transition, set_transition_blend). With fromState, removes every transition between the two (remove_transition). With fromState, the transition to condition when transitionGuid is omitted (set_transition_condition)"),
  trackIndex: z.number().int().optional().describe("Slot track index (default 0)"),
  tracks: z.array(z.object({ bone: z.string().describe("Bone whose track to replace"), keyframes: z.array(z.record(z.unknown())).describe("One key per frame as {location?, rotation? {x, y, z, w}, scale?}") })).optional().describe("Per-bone key arrays; a missing track is created"),
  trajectoryHistoryCount: z.number().optional().describe("Generated trajectory history samples"),
  trajectoryPredictionCount: z.number().optional().describe("Generated trajectory prediction samples"),
  transitionGuid: z.string().optional().describe("Transition to remove, from add_transition or read_state_machine (remove_transition). Transition to condition, from add_transition or read_state_machine; wins over fromState and toState (set_transition_condition)"),
  triggerTime: z.number().optional().describe("Trigger time in seconds, clamped to the asset length (add_anim_notify). Window start in seconds (add_anim_notify_state)"),
  useNormalizedRootMotionScale: z.boolean().optional().describe("Normalize root motion scale"),
  variableName: z.string().optional().describe("Existing bool variable the condition reads"),
  verticalMax: z.number().optional().describe("Vertical axis maximum (default 180) (create_blendspace). Vertical axis maximum; ignored on a BlendSpace1D (populate_blendspace)"),
  verticalMin: z.number().optional().describe("Vertical axis minimum (default -180) (create_blendspace). Vertical axis minimum; ignored on a BlendSpace1D (populate_blendspace)"),
  virtualBoneName: z.string().optional().describe("Virtual bone to remove"),
  wait: z.boolean().optional().describe("Block until the build resolves (default true)"),
  weight: z.number().optional().describe("Channel weight"),
  world: z.string().optional().describe("auto (default, prefers PIE) | pie | game | editor"),
  x: z.number().optional().describe("Horizontal axis value when position is omitted (default 0) (add_blend_sample). New horizontal axis value when position is omitted (set_blend_sample)"),
  y: z.number().optional().describe("Vertical axis value when position is omitted (default 0) (add_blend_sample). New vertical axis value when position is omitted (set_blend_sample)"),
};

/** Declare an action for a spec'd bridge method: effect, summary, method. */
export const specBp = makeSpecBp(paramsClauses, handlerSpecs);
