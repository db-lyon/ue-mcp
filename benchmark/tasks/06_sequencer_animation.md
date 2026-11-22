# 06 Sequencer animation

Area: animation (Sequencer). Depends on: 05.

## Prompt

```
Create a Level Sequence at /Game/Bench/LS_Bench. Bind the Bench_Cube actor in the open level to it and add a Transform track. Key its location X at 100 on frame 0 and at 600 on frame 60, in the sequence's display rate (30 fps unless you change it). Save the sequence.
```

## Check

`benchmark/checks/06_sequencer_animation.py` finds the binding named Bench_Cube, reads the Location.X channel of its Transform track, and passes when it has a key of 100 at frame 0 and a key of 600 at frame 60, measured in display-rate frames.
