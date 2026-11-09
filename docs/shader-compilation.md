# Shader compilation status

`editor(action="get_shader_compile_status")` returns a read-only snapshot:

- `outstandingJobs`: outstanding jobs reported by `GShaderCompilingManager`.
- `isCompiling`: the manager's asynchronous compilation state.
- `pendingShaderMaps`: distinct material shader-map compiling IDs still owned
  by the compiler, including work awaiting completion on the game thread.
- `pendingMaterials`: distinct material paths represented by those maps on
  UE 5.8 (compiler friendly names on earlier supported engines).
- `idle`: no compiling state, outstanding jobs, or pending material shader maps.
- `managerAvailable`: false when the shader compiler manager is absent, in
  which case the counters are zero and the snapshot is idle.

Job counts include global shaders. Material and shader-map counts cover
material shader maps in memory, so they measure different things from jobs.
Snapshots can change as editor work queues more compilation.

```json
{
  "action": "get_shader_compile_status",
  "waitUntilIdle": true,
  "timeoutMs": 120000
}
```

With `waitUntilIdle`, the server polls the native read between editor ticks,
using the same pattern as PCG generation waits. Each snapshot runs on the game
thread and returns immediately. The wait yields between reads and does not
force synchronous compilation. It returns immediately when already idle.

The final result includes `state: "idle"`, `durationMs`, `polls` and the final
counters. `timeoutMs` defaults to 60000 for the wait and is consumed by server
routing. On expiry the call returns `success: false`, `state: "timed_out"`, an
error and the latest counters. It does not cancel the shader jobs. A native
failure is returned immediately, and an incomplete status is reported as a
failure instead of being treated as idle.
