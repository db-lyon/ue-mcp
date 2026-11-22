# 08 PCG scatter

Area: PCG. Depends on: 05.

## Prompt

```
Create a PCG graph at /Game/Bench/PCG_Bench that samples points inside its volume with a Volume Sampler (voxel size 200 on each axis) and spawns /Engine/BasicShapes/Cube at each point with a Static Mesh Spawner. In the open level, place a PCG Volume labelled Bench_PCG at (0, 0, 1000) with an extent of 500 on each axis, assign PCG_Bench to it, and generate it. Save the graph and the level.
```

## Check

`benchmark/checks/08_pcg_scatter.py` passes when Bench_PCG has a PCG component running PCG_Bench and owns instanced static mesh components holding at least one Cube instance, which exist only after the graph is wired and generated.
