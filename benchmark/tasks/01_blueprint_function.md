# 01 Blueprint function graph

Area: Blueprint graph authoring. Depends on: nothing.

## Prompt

```
Create a Blueprint class at /Game/Bench/BP_Bench_Math whose parent class is Actor. Give it a function named AddBench with two integer inputs, A and B, and one integer output named Result that returns A + B. Compile and save the Blueprint.
```

## Check

`benchmark/checks/01_blueprint_function.py` loads the Blueprint's generated class and calls `AddBench` on its class default object with (2, 3), (-4, 10) and (1000, 234). It passes when every call returns the sum, which needs the function's graph to be wired and compiled.
