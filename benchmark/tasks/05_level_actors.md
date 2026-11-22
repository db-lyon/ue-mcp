# 05 Level and actors

Area: levels, actors and components. Depends on: 04.

## Prompt

```
Create a new empty level at /Game/Bench/L_Bench and open it. Place a Static Mesh Actor labelled Bench_Cube that uses /Engine/BasicShapes/Cube at location (100, 200, 50) with scale (2, 2, 2), and set its material slot 0 to /Game/Bench/MI_Bench_Blue. Place a Point Light labelled Bench_Light, attach it to Bench_Cube, and set its intensity to 5000. Save the level.
```

## Check

`benchmark/checks/05_level_actors.py` passes when L_Bench exists on disk and is the open level, Bench_Cube has the Cube mesh, MI_Bench_Blue in slot 0, location (100, 200, 50) and scale 2, and Bench_Light is a Point Light attached to Bench_Cube with intensity 5000.
