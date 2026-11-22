# 02 Blueprint components

Area: Blueprint components. Depends on: nothing.

## Prompt

```
Create a Blueprint class at /Game/Bench/BP_Bench_Lamp whose parent class is Actor. Add a Static Mesh component named Base that uses /Engine/BasicShapes/Cube, and a Point Light component named Bulb attached to Base, with relative location (0, 0, 120) and intensity 8000. Compile and save the Blueprint.
```

## Check

`benchmark/checks/02_blueprint_components.py` reads the Blueprint's component tree through the editor's subobject data subsystem, the same data the Components panel shows. It passes when Base is a Static Mesh component using the Cube, and Bulb is a Point Light component attached to Base at (0, 0, 120) with intensity 8000.
