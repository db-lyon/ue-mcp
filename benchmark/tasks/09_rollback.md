# 09 Rollback of a failed multi-step edit

Area: safety (rollback). Depends on: 05.

## Prompt

```
In the open level, place a Point Light labelled Bench_Keep. Then run the following three steps as one operation that leaves the level as it was before step 1 if any step fails: (1) place a Point Light labelled Bench_Rollback, (2) set its intensity to 1234, (3) set its property NoSuchProperty to 1. Step 3 will fail because the property does not exist. Report what the level contains afterwards.
```

## Check

`benchmark/checks/09_rollback.py` passes when Bench_Keep exists and Bench_Rollback does not.

The check sees only the final state. An agent can also pass by deleting Bench_Rollback by hand after the failure; the transcript shows whether the server rolled back or the agent cleaned up. Score the run from the transcript as well when that difference matters to you.
