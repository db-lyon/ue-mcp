import { describe, it, expect } from "vitest";
import {
  Manifest,
  Sequence,
  DriftReport,
  ActorStateRow,
  PIE_FORMAT_VERSION,
} from "../../src/pie/schema.js";

describe("PIE manifest schema", () => {
  it("accepts a fully-populated manifest", () => {
    const manifest = {
      version: PIE_FORMAT_VERSION,
      id: "recording-20260521-143052-7af3",
      started_at: "2026-05-21T14:30:52Z",
      ended_at: "2026-05-21T14:31:05Z",
      duration_seconds: 12.45,
      total_frames: 747,
      sample_hz: 60,
      pin_max_fps: 60,
      rng_seed: 1234567890,
      pie_world: "/Game/Maps/M_Combat",
      pawn_class: "/Game/Blueprints/BP_Hero.BP_Hero_C",
      axis_threshold: 0.15,
      actions: [
        { name: "Move",   path: "/Game/Input/IA_Move",   value_type: "Axis2D" },
        { name: "Attack", path: "/Game/Input/IA_Attack", value_type: "Boolean" },
      ],
      tracked_values: [
        { path: "Hero.AbilitySystem.Health", type: "float" },
      ],
      markers: [
        { frame: 312, time: 5.2, label: "enemy spawned" },
      ],
      files: {
        csv: "recording.csv",
        sequence: "sequence.json",
      },
    };
    expect(Manifest.parse(manifest).id).toBe(manifest.id);
  });

  it("requires version === 1", () => {
    const bad = { version: 2, id: "x" };
    expect(() => Manifest.parse(bad)).toThrow();
  });

  it("accepts an empty actions array", () => {
    const manifest = {
      version: PIE_FORMAT_VERSION,
      id: "recording-x",
      started_at: "", ended_at: "",
      duration_seconds: 0, total_frames: 0,
      sample_hz: 60, pin_max_fps: 60, rng_seed: 0,
      pie_world: "", pawn_class: "", axis_threshold: 0.15,
      actions: [], tracked_values: [], markers: [],
      files: { csv: "recording.csv", sequence: "sequence.json" },
    };
    expect(() => Manifest.parse(manifest)).not.toThrow();
  });

  it("optional drift file", () => {
    const m = {
      version: PIE_FORMAT_VERSION,
      id: "x", started_at: "", ended_at: "",
      duration_seconds: 0, total_frames: 0,
      sample_hz: 60, pin_max_fps: 60, rng_seed: 0,
      pie_world: "", pawn_class: "", axis_threshold: 0.15,
      actions: [], tracked_values: [], markers: [],
      files: { csv: "recording.csv", sequence: "sequence.json", drift: "drift.json" },
    };
    expect(Manifest.parse(m).files.drift).toBe("drift.json");
  });
});

describe("PIE sequence schema", () => {
  it("accepts all six step types in one sequence", () => {
    const seq = {
      version: PIE_FORMAT_VERSION,
      source_recording_id: "recording-x",
      settle_ms: 500,
      sample_hz: 60,
      rng_seed: 1234,
      steps: [
        { type: "input_tape", action: "/Game/Input/IA_Move", delay_ms: 0,
          values: [[0.5, 0.3], [0.6, 0.3], 0.1] },
        { type: "hold", action: "/Game/Input/IA_Attack",
          delay_ms: 200, duration_ms: 100, value_x: 1 },
        { type: "input", action: "/Game/Input/IA_Jump",
          delay_ms: 500, value_x: 1 },
        { type: "mark", delay_ms: 1500, label: "enemy spawned" },
        { type: "capture", delay_ms: 5000, name: "boss_intro" },
        { type: "console", delay_ms: 6000, command: "stat fps" },
      ],
    };
    const parsed = Sequence.parse(seq);
    expect(parsed.steps).toHaveLength(6);
  });

  it("rejects unknown step types", () => {
    const seq = {
      version: PIE_FORMAT_VERSION,
      settle_ms: 0, sample_hz: 60, rng_seed: 0,
      steps: [{ type: "wat", delay_ms: 0 }],
    };
    expect(() => Sequence.parse(seq)).toThrow();
  });

  it("input_tape accepts scalar, 2-tuple, and 3-tuple values in the same array", () => {
    const seq = {
      version: PIE_FORMAT_VERSION,
      settle_ms: 0, sample_hz: 60, rng_seed: 0,
      steps: [
        { type: "input_tape", action: "/p", delay_ms: 0,
          values: [0.5, [0.5, 0.3], [0.1, 0.2, 0.3]] },
      ],
    };
    expect(() => Sequence.parse(seq)).not.toThrow();
  });

  it("source_recording_id is optional (inline-steps replay)", () => {
    const seq = {
      version: PIE_FORMAT_VERSION,
      settle_ms: 0, sample_hz: 60, rng_seed: 0,
      steps: [],
    };
    expect(Sequence.parse(seq).source_recording_id).toBeUndefined();
  });
});

describe("PIE drift report schema", () => {
  it("accepts a populated drift report", () => {
    const drift = {
      version: PIE_FORMAT_VERSION,
      source_recording_id: "recording-x",
      replay_started_at: "2026-05-21T14:35:10Z",
      frames_compared: 745,
      frames_missing_in_replay: 2,
      max_position_drift_cm: 12.4,
      max_position_drift_frame: 412,
      max_velocity_drift_cms: 47.1,
      max_rotation_drift_deg: 3.2,
      montage_section_mismatches: 0,
      tracked_value_max_deltas: { "Hero.AbilitySystem.Health": 0 },
      actor_drift: {
        "BP_Boss_C": {
          max_position_cm: 11.7,
          max_rotation_deg: 0.4,
          max_velocity_cms: 36.2,
          frames_unresolved_in_source: 0,
          frames_unresolved_in_replay: 3,
        },
      },
      frames_over_threshold: [
        { frame: 412, position_delta_cm: 12.4, velocity_delta_cms: 47.1, rotation_delta_deg: 0.3 },
      ],
    };
    expect(DriftReport.parse(drift).frames_compared).toBe(745);
  });
});

describe("PIE tracked.jsonl schema", () => {
  it("accepts a row with resolved + unresolved actors", () => {
    const row = {
      frame: 17,
      time: 0.283,
      actors: {
        "BP_Hero_C": {
          resolved: true,
          pos: [120.5, -30.2, 88.0],
          rot: [45.0, 0.0, 0.0],
          vel: [200.1, 0.0, 0.0],
        },
        "BP_Boss_C": { resolved: false },
      },
    };
    const parsed = ActorStateRow.parse(row);
    expect(parsed.actors["BP_Hero_C"].resolved).toBe(true);
    expect(parsed.actors["BP_Boss_C"].resolved).toBe(false);
  });
});
