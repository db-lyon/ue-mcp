# 11 Play In Editor

Area: PIE. Depends on: 05. Run it last and leave the session running for the check.

## Prompt

```
Start a Play In Editor session in the open level (L_Bench). While it is running, move the player pawn to X=1000, Y=0, keeping its current Z. Leave the session running.
```

## Check

`benchmark/checks/11_pie_session.py` passes when a PIE session is running L_Bench and its player pawn is within 50 units of X=1000, Y=0.
