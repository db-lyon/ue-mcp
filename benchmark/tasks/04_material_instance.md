# 04 Material instance

Area: materials. Depends on: 03.

## Prompt

```
Create a material instance at /Game/Bench/MI_Bench_Blue whose parent is /Game/Bench/M_Bench. Override its Tint parameter to (0, 0, 1, 1) and its Rough parameter to 0.8, then save it.
```

## Check

`benchmark/checks/04_material_instance.py` passes when MI_Bench_Blue is a material instance of M_Bench whose Tint reads (0, 0, 1) and Rough reads 0.8.
