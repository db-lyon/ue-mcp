# Neon Shrine Demo

The Neon Shrine is a built-in 19-step demo that builds a lit scene from scratch: materials, geometry, lights, fog, post process, Niagara, PCG, a level sequence and an editor utility widget. Running it checks that the bridge works end to end after install.

## Running the demo

The editor must be connected: `project(action="get_status")` reports `editorConnected: true`. The demo creates its own level at `/Game/Demo/DemoLevel` and writes its assets under `/Game/Demo/`.

```text
demo(action="get_steps")              # index, id and description of every step
demo(action="step", step=1)           # run one step; stepIndex is an alias
demo(action="step", step=19)
demo(action="step")                   # no step: the step list
```

Or ask your agent to run the Neon Shrine demo.

Steps are not idempotent. Running a step again spawns a second set of `Demo_` actors.

## Cleanup

```text
demo(action="cleanup")
```

Cleanup switches the editor to `/Game/MCP_Home` (creating it on first use), destroys every actor whose label starts with `Demo_` in that level, deletes the demo's materials, Niagara system, PCG graph, level sequence and `DemoLevel`, then deletes `/Game/Demo` when it is empty. It has no inverse; rebuilding means running steps 1 to 19 again.

`demo(action="go_home")` switches the editor to `/Game/MCP_Home` on its own, for use before anything that would leave the editor on an Untitled map.

## Steps

| # | Step | Builds |
|---|---|---|
| 1 | `create_level` | New level at `/Game/Demo/DemoLevel` |
| 2 | `materials` | Three materials: floor, glow, pillar |
| 3 | `floor` | 60 m dark reflective floor |
| 4 | `pedestal` | Central pedestal cylinder |
| 5 | `hero_sphere` | Emissive gold hero sphere |
| 6 | `pillars` | Four corner pillar cylinders |
| 7 | `orbs` | Four glowing orbs at the pillar bases |
| 8 | `neon_lights` | Four coloured point lights |
| 9 | `hero_light` | Warm point light above the hero sphere |
| 10 | `moonlight` | Directional moon light |
| 11 | `sky_light` | SkyLight ambient fill |
| 12 | `fog` | ExponentialHeightFog |
| 13 | `post_process` | PostProcessVolume with bloom and vignette |
| 14 | `niagara_vfx` | Niagara particle system above the hero |
| 15 | `pcg_scatter` | PCG scatter volume on the floor |
| 16 | `orbit_rings` | Eight orbiting emissive spheres with rotation |
| 17 | `level_sequence` | LevelSequence with the hero sphere bound |
| 18 | `tuning_panel` | EditorUtilityWidget tuning panel |
| 19 | `save` | Save the current level |

The implementation is `plugin/ue_mcp_bridge/Source/UE_MCP_Bridge/Private/Handlers/Demo/DemoHandlers.cpp`.

For a scene built from individual tool calls in a flow, see the [Beacon flow](flows.md#beacon).
