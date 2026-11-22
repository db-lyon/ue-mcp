# 07 Landscape

Area: landscape. Depends on: 05.

## Prompt

```
In the open level, create a landscape labelled Bench_Landscape of at least 2 x 2 components that covers the world origin. Raise a hill at the world origin at least 300 units higher than the flat surface around it. Save the level.
```

## Check

`benchmark/checks/07_landscape.py` passes when one Landscape labelled Bench_Landscape exists with at least 4 components, its bounds contain the origin, and its bounds are at least 290 units tall, which a flat landscape is not.
