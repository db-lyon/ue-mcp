# Guards

A plugin, or a project in its own `ue-mcp.yml`, can gate **every** bridge call - not just its own actions - with a guard. Guards run in an ordered pipeline around `IBridge.call`, in the shape of NestJS guards/interceptors: a `before` hook may veto a call (deny) or act on it (e.g. check a file out of source control), and an `after` hook may observe or replace the result (audit, transform). The pipeline is agnostic - source control, access policy, audit, and rate limiting are all just guards.

## Declaring a guard

A guard is declared, not named into existence. The same shape works in a plugin manifest and in a project's `ue-mcp.yml`:

```yaml
guards:
  sandbox:                          # the guard's name
    description: Block writes outside the sandbox
    scope: writes                   # all | mutations | writes | reads | unknown  (default: all)
    order: 10                       # lower runs first before, last after
    before:
      class_path: my.tasks.SandboxCheck
      options:
        allow: ["/Game/Sandbox/"]
    after:
      class_path: my.tasks.SandboxAudit
```

Five scopes:

| Scope | Sees |
|-------|------|
| `all` | every call, reads included. For audit and rate limiting. |
| `mutations` | every call that changes editor state, whether or not it names an asset. This is what "block everything that mutates" means. |
| `writes` | calls that modify content already on disk. The source-control question. |
| `reads` | only the calls that observe. For auditing what an agent looked at. |
| `unknown` | only the calls whose effect a parameter decides: the escape hatches. |

`mutations` and `writes` are different questions, and the difference matters. Spawning an actor, starting a play session or answering a dialog all change something and name no content path, so `writes` never sees them. A call that creates a new asset modifies nothing that exists yet, so `writes` does not see that either. If the rule is "nothing may change until X", the scope is `mutations`.

### Where the answer comes from

Every action declares what it does to the editor, as `read`, `mutate` or `unknown`, and the scopes are read off that declaration. Nothing here matches an action's name against a list of verbs.

That is worth stating because it used to. `mutations` was once "a call counts unless its leading verb is a known read", which is a guess, and it was wrong in both directions: reads whose names do not open with a read verb (`metasound_get_graph`, `line_trace`, `hit_test_viewport_pixel`) were guarded for nothing, and before that inversion a hand-written list of mutating verbs had let `write_cpp_file`, `build`, `sculpt`, `place_actor` and every bare verb like `save` and `create` straight through. A list of verbs is open-ended; the set of actions is not.

A call this server does not recognise at all - a plugin reaching the bridge with a method of its own - counts as a change. Nothing vouches for it, so `mutations` sees it and `reads` does not.

`unknown` is the scope the declaration made possible rather than merely made correct. It is the set whose behaviour is decided by an argument instead of by the action: `editor(execute_python)`, `editor(execute_command)`, every wrapped Epic tool reached through `epic(call_tool)`, and the reflected `invoke_*` family. Those are the calls that can do anything, and a verb list cannot pick them out, because `unknown` is not a property of a name. If the rule is "a person approves the escape hatches, and ordinary edits go through", that is one guard:

```yaml
guards:
  approve_escape_hatches:
    description: Arbitrary code and wrapped third-party tools need a human
    scope: unknown
    before:
      class_path: tasks/AskFirst
```

`mutations` includes `unknown`, since a call that might change something is treated as one everywhere in this server.

Both narrower scopes are computed lazily, so a read pays nothing and a pipeline of guards that all ask pays once.

`before` may deny: returning `success: false`, or throwing, stops the call and the caller gets a `WRITE_BLOCKED` error carrying the message. `after` runs on a call that already happened, so it cannot deny; returning `data` replaces the result, and a failure or a throw is logged and the call stands.

A guard needs at least one hook. One with neither would be registered, reported at boot, and never run.

Both hooks are optional and independent, so one named guard can hold both halves of a concern and they know they belong together. Ordering is a number rather than something a name has to imply.

`options` are bound when the guard is built. The call being guarded is layered over them, so a hook reads its configuration and its subject from one object: `method`, `params`, `paths` (the existing files the call will touch, empty for reads) and, for `after`, `result`.

A `class_path` that cannot be resolved stops the server at boot, naming the guard and the manifest or config that declared it. With no guard registered the pipeline is a pass-through, so a guard that failed to build would leave the calls it covers ungated while the configuration says they are guarded.

## A deny guard (access policy)

Blocks writes outside a sandbox path. A `before` hook denies by returning `success: false` or throwing; the underlying call never runs and the caller gets a `WRITE_BLOCKED` error carrying your message.

```yaml
# ue-mcp.plugin.yml, or the guards: section of a project's ue-mcp.yml
guards:
  policy:
    description: Deny writes outside /Game/Sandbox
    scope: writes
    before:
      class_path: tasks/PolicyGuard
```

```ts
// tasks/PolicyGuard.ts
import { UeMcpTask, type TaskResult } from "ue-mcp/task";

export default class PolicyGuard extends UeMcpTask<{ paths?: string[]; method?: string }> {
  get taskName() { return "policy-guard"; }
  async execute(): Promise<TaskResult> {
    const outside = (this.options.paths ?? []).filter((p) => !p.includes("/Content/Sandbox/"));
    if (outside.length) {
      return { success: false, error: new Error(`writes outside the sandbox are not allowed: ${outside.join(", ")}`) };
    }
    return { success: true };
  }
}
```

## An observe guard (audit)

Runs after every successful call for its side effect. An `after` hook's failure is logged and the call stands, because the call already happened: it is for observation, not veto.

```yaml
guards:
  audit:
    description: Append every action to an audit log
    scope: all
    after:
      class_path: tasks/AuditGuard
```

```ts
// tasks/AuditGuard.ts
import { UeMcpTask, type TaskResult } from "ue-mcp/task";
import { appendFileSync } from "node:fs";

export default class AuditGuard extends UeMcpTask<{ method?: string; paths?: string[] }> {
  get taskName() { return "audit-guard"; }
  async execute(): Promise<TaskResult> {
    const line = JSON.stringify({ method: this.options.method, paths: this.options.paths });
    appendFileSync("ue-mcp-audit.log", line + "\n");
    return { success: true };
  }
}
```

## Composition and ordering

Multiple guards compose into one pipeline. `before` hooks run in order, `after` hooks in reverse order (the same nesting an interceptor stack gives you). A guard that denies short-circuits the rest of the `before` chain and the call itself.

Guards from different plugins coexist - a source-control checkout guard, an access-policy deny guard, and an audit guard can all be installed at once, each shipped by whatever plugin owns that concern. The [`ue-mcp-perforce`](https://github.com/db-lyon/ue-mcp-perforce) plugin's source-control guard is the canonical example: a `before` hook scoped to writes, checking the target files out, or refusing when a person holds the lock, before the write reaches the editor.
