# Neon Shrine Demo

The Neon Shrine is a built-in, 19-step procedural demo that showcases UE-MCP's capabilities across multiple tool categories. It builds a complete scene from scratch — materials, meshes, actors, lights, VFX, landscape — all through MCP tool calls.

## Running the Demo

### Prerequisites

- Editor running and connected (`project(action="get_status")` shows connected)
- An empty or expendable level (the demo creates many actors)

### Step by Step

Get the full step list:
```
demo(action="step")
```

Execute steps one at a time:
```
demo(action="step", stepIndex=1)
demo(action="step", stepIndex=2)
...
demo(action="step", stepIndex=19)
```

Each step returns a description of what it created and any relevant asset paths.

### Cleanup

Remove everything the demo created:
```
demo(action="cleanup")
```

This deletes all demo actors from the level and demo assets from the project.

## What the Demo Covers

The 19 steps exercise a wide range of tools:

| Step | What It Does | Tools Used |
|------|--------------|------------|
| 1 | Create a new level | `level` |
| 2–4 | Create materials (metal, glass, stone) | `material` |
| 5–7 | Place shrine geometry | `level`, `asset` |
| 8–10 | Add colored point lights | `level` (lights) |
| 11 | Set up landscape | `landscape` |
| 12 | Paint landscape layers | `landscape` |
| 13 | Add foliage | `foliage` |
| 14 | Place spline paths | `level` (splines) |
| 15 | Add VFX | `niagara` |
| 16 | Place ambient audio | `audio` |
| 17 | Add volumes | `level` (volumes) |
| 18 | Configure viewport | `editor` (viewport) |
| 19 | Final touches and summary | Multiple |

## Purpose

The demo serves as:

- **Verification** — confirms that the bridge and major tool categories are working
- **Showcase** — demonstrates what UE-MCP can do in a single session
- **Learning tool** — each step shows a real tool invocation pattern you can reuse
