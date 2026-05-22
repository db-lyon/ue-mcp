import { z } from "zod";

/**
 * Zod schemas describing the PIE record/replay on-disk format. The C++
 * recorder/replayer (plugin/.../Private/PIE/PIESequenceFormat.{h,cpp}) is the
 * canonical writer; these schemas are the wire-shape contract used by tests
 * and any future TS-side tooling. Keep in sync with PIESequenceFormat.h.
 */

export const PIE_FORMAT_VERSION = 1 as const;

export const ActionValueType = z.enum(["Boolean", "Axis1D", "Axis2D", "Axis3D"]);
export type ActionValueType = z.infer<typeof ActionValueType>;

export const ActionSpec = z.object({
  name: z.string(),
  path: z.string(),
  value_type: ActionValueType,
});
export type ActionSpec = z.infer<typeof ActionSpec>;

export const TrackedValueSpec = z.object({
  path: z.string(),
  type: z.string(),
});
export type TrackedValueSpec = z.infer<typeof TrackedValueSpec>;

export const Marker = z.object({
  frame: z.number().int().nonnegative(),
  time: z.number(),
  label: z.string(),
  metadata: z.record(z.unknown()).optional(),
});
export type Marker = z.infer<typeof Marker>;

export const Manifest = z.object({
  version: z.literal(PIE_FORMAT_VERSION),
  id: z.string().min(1),
  started_at: z.string(),
  ended_at: z.string(),
  duration_seconds: z.number(),
  total_frames: z.number().int().nonnegative(),
  sample_hz: z.number().int().positive(),
  pin_max_fps: z.number().int().nonnegative(),
  rng_seed: z.number(),
  pie_world: z.string(),
  pawn_class: z.string(),
  axis_threshold: z.number(),
  actions: z.array(ActionSpec),
  tracked_values: z.array(TrackedValueSpec),
  markers: z.array(Marker),
  files: z.object({
    csv: z.string(),
    sequence: z.string(),
    drift: z.string().optional(),
  }),
});
export type Manifest = z.infer<typeof Manifest>;

// Per-frame value for the input_tape step. The C++ writer emits a scalar when
// only X is non-zero, a 2-tuple when Z is zero, and a 3-tuple otherwise. The
// parser accepts any of those forms; consumers normalise as needed.
export const TapeValue = z.union([
  z.number(),
  z.tuple([z.number(), z.number()]),
  z.tuple([z.number(), z.number(), z.number()]),
]);
export type TapeValue = z.infer<typeof TapeValue>;

const StepBase = z.object({
  delay_ms: z.number().int().nonnegative(),
});

export const InputStep = StepBase.extend({
  type: z.literal("input"),
  action: z.string(),
  value_x: z.number().optional(),
  value_y: z.number().optional(),
  value_z: z.number().optional(),
});

export const HoldStep = StepBase.extend({
  type: z.literal("hold"),
  action: z.string(),
  value_x: z.number().optional(),
  value_y: z.number().optional(),
  value_z: z.number().optional(),
  duration_ms: z.number().int().nonnegative(),
});

export const CaptureStep = StepBase.extend({
  type: z.literal("capture"),
  name: z.string(),
});

export const ConsoleStep = StepBase.extend({
  type: z.literal("console"),
  command: z.string(),
});

export const InputTapeStep = StepBase.extend({
  type: z.literal("input_tape"),
  action: z.string(),
  values: z.array(TapeValue),
});

export const MarkStep = StepBase.extend({
  type: z.literal("mark"),
  label: z.string(),
});

export const Step = z.discriminatedUnion("type", [
  InputStep,
  HoldStep,
  CaptureStep,
  ConsoleStep,
  InputTapeStep,
  MarkStep,
]);
export type Step = z.infer<typeof Step>;

export const Sequence = z.object({
  version: z.literal(PIE_FORMAT_VERSION),
  source_recording_id: z.string().optional(),
  settle_ms: z.number().int().nonnegative(),
  sample_hz: z.number().int().positive(),
  rng_seed: z.number(),
  steps: z.array(Step),
});
export type Sequence = z.infer<typeof Sequence>;

export const DriftFrameEntry = z.object({
  frame: z.number().int().nonnegative(),
  position_delta_cm: z.number(),
  velocity_delta_cms: z.number(),
  rotation_delta_deg: z.number(),
});
export type DriftFrameEntry = z.infer<typeof DriftFrameEntry>;

export const DriftReport = z.object({
  version: z.literal(PIE_FORMAT_VERSION),
  source_recording_id: z.string(),
  replay_started_at: z.string(),
  frames_compared: z.number().int().nonnegative(),
  frames_missing_in_replay: z.number().int().nonnegative(),
  max_position_drift_cm: z.number(),
  max_position_drift_frame: z.number().int().nonnegative(),
  max_velocity_drift_cms: z.number(),
  max_rotation_drift_deg: z.number(),
  montage_section_mismatches: z.number().int().nonnegative(),
  tracked_value_max_deltas: z.record(z.number()),
  frames_over_threshold: z.array(DriftFrameEntry),
});
export type DriftReport = z.infer<typeof DriftReport>;
