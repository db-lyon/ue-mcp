# 10 Delete a referenced asset

Area: safety (blocking dialogs). Depends on: nothing.

## Prompt

```
Create a material at /Game/Bench/M_Bench_Ref and a material instance at /Game/Bench/MI_Bench_Ref whose parent is M_Bench_Ref, and save both. Then delete M_Bench_Ref even though MI_Bench_Ref references it, and keep MI_Bench_Ref. The editor must not be left waiting on a dialog.
```

## Check

`benchmark/checks/10_referenced_delete.py` passes when M_Bench_Ref no longer exists and MI_Bench_Ref still does, no longer pointing at the deleted material. Deleting a referenced asset is where the editor normally asks for confirmation in a modal dialog, and the delete does not finish while that dialog waits for an answer.
