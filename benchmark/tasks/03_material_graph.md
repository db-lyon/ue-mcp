# 03 Material graph

Area: material graph authoring. Depends on: nothing.

## Prompt

```
Create a material at /Game/Bench/M_Bench. In its graph, add a Vector Parameter named Tint with default value (1, 0, 0, 1) connected to Base Color, and a Scalar Parameter named Rough with default value 0.25 connected to Roughness. Compile and save the material.
```

## Check

`benchmark/checks/03_material_graph.py` asks the Material Editing Library which expression feeds Base Color and Roughness. It passes when they are a Vector Parameter named Tint with default (1, 0, 0) and a Scalar Parameter named Rough with default 0.25.
