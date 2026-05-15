# Neon Shrine Demo

The Neon Shrine is a built-in **19-step procedural demo** that builds a complete scene from scratch through MCP tool calls — materials, geometry, lighting, atmosphere, VFX, PCG, sequencer, even an editor utility widget. It's the fastest way to see UE-MCP working end-to-end after install.

## Running the Demo

### Prerequisites

- Editor running and connected. Check with `project(action="get_status")` — `editorConnected` must be `true`.
- An empty or expendable level. The demo creates ~30 actors and writes assets under `/Game/Demo/`.

### Step List

Get the ordered list of steps:

```
demo(action="step")
```

### Run Steps

Execute one at a time:

```
demo(action="step", stepIndex=1)
demo(action="step", stepIndex=2)
...
demo(action="step", stepIndex=19)
```

Or just ask your AI to "run the Neon Shrine demo" and it will iterate through them.

Each step returns a description of what it created and any new asset paths.

### Cleanup

Remove every actor and asset the demo created:

```
demo(action="cleanup")
```

This wipes `/Game/Demo/` and removes the demo actors from the level.

## What the Demo Builds

| # | Step | Description |
|---|---|---|
| 1 | `create_level` | New level at `/Game/Demo/DemoLevel` |
| 2 | `materials` | Three materials: floor, glow, pillar |
| 3 | `floor` | 60m dark reflective floor |
| 4 | `pedestal` | Central pedestal cylinder |
| 5 | `hero_sphere` | Emissive gold hero sphere on the pedestal |
| 6 | `pillars` | Four corner pillar cylinders |
| 7 | `orbs` | Four glowing orbs at the pillar bases |
| 8 | `neon_lights` | Four coloured point lights (cyan / magenta / amber / violet) |
| 9 | `hero_light` | Warm point light above the hero sphere |
| 10 | `moonlight` | Directional moon light |
| 11 | `sky_light` | SkyLight ambient fill |
| 12 | `fog` | ExponentialHeightFog atmosphere |
| 13 | `post_process` | PostProcessVolume with bloom and vignette |
| 14 | `niagara_vfx` | Niagara particle system above the hero |
| 15 | `pcg_scatter` | PCG scatter volume on the floor |
| 16 | `orbit_rings` | Eight orbiting emissive spheres with rotation |
| 17 | `level_sequence` | LevelSequence with the hero sphere bound |
| 18 | `tuning_panel` | EditorUtilityWidget control panel |
| 19 | `save` | Save the current level |

## Why It Exists

- **Verification.** Running through all 19 steps confirms the bridge, asset registry, materials, lighting, Niagara, PCG, sequencer, and UMG editor utility paths all work.
- **Showcase.** It's a single command that produces something visible.
- **Reference.** Each step is a real handler call you can imitate. If you want to know how to spawn a colored point light or set up a PCG volume, look at how the corresponding step does it.

The implementation lives in `plugin/ue_mcp_bridge/Source/UE_MCP_Bridge/Private/Handlers/DemoHandlers.cpp`.

## See Also

For a much larger declarative example, see the **Beacon** flow described in [Flows](flows.md#beacon) — a 56-step shrine scene composed entirely from individual MCP tool calls in `src/flow/loader.ts`.
