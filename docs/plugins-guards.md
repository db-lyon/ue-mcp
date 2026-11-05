# Guards

A guard gates every bridge call, not only a plugin's own actions. Guards are declared in a plugin manifest or in a project's `ue-mcp.yml`, in the same shape. They run in an ordered pipeline around each bridge call: a `before` hook can deny the call or act on it (for example check a file out), and an `after` hook can observe or replace the result. Source control, access policy, audit and rate limiting are all guards.

## Declaring a guard

```yaml
guards:
  sandbox:                          # name: letters, digits, _ and -, starting with a letter
    description: Block writes outside the sandbox
    scope: writes                   # all | mutations | writes | reads | unknown (default all)
    order: 10                       # lower runs first before the call, last after it (default 0)
    before:
      class_path: guards/SandboxCheck
      options:
        allow: ["/Game/Sandbox/"]
    after:
      class_path: guards/SandboxAudit
```

A guard needs a `before` hook, an `after` hook, or both. Each hook names the class that implements it and the `options` it is built with.

`class_path` resolution:

- **In `ue-mcp.yml`**, it resolves from the folder that holds `ue-mcp.yml`, the same way as a [flow task](flows.md#class-path-resolution): `guards/SandboxCheck` looks for `guards/SandboxCheck.ts`, `.js`, or an `index` file in that folder.
- **In a plugin**, use the `class_path` of a task declared under the manifest's `tasks:`, which resolves inside the package's `dist/`.

A `class_path` that cannot be resolved stops the server at startup, naming the guard and the file that declared it, so a configured guard is never silently missing.

## Scopes

| Scope | Sees |
|-------|------|
| `all` | Every call, reads included. For audit and rate limiting. |
| `mutations` | Every call that changes editor state, whether or not it names an asset. Includes `unknown`. |
| `writes` | Calls that modify content already on disk. The source control question. |
| `reads` | Only calls that observe. |
| `unknown` | Only calls whose effect a parameter decides. |

Every action declares its effect as `read`, `mutate` or `unknown`, and the scopes are read from that declaration (a plugin action declares it with [`effect:`](plugins-authoring.md#ue-mcppluginyml)). A bridge method the server does not recognise counts as a change: `mutations` sees it and `reads` does not.

`mutations` and `writes` differ. Spawning an actor, starting PIE or answering a dialog changes state and names no content path, so `writes` does not see it. Creating a new asset modifies nothing that exists yet, so `writes` does not see that either. For "nothing may change until X", use `mutations`.

`unknown` covers the escape hatches: `editor(execute_python)`, `editor(execute_command)`, the `invoke_*` family and `epic(call_tool)`. To require a person's approval for those and let ordinary edits through:

```yaml
guards:
  approve_escape_hatches:
    description: Arbitrary code needs a human
    scope: unknown
    before:
      class_path: guards/AskFirst
```

The <!-- count:nativeToolActions -->830<!-- /count --> actions that wrap Unreal's own toolsets all dispatch through the bridge method `epic_call_tool`. Their scope is decided from the method and the tool named in its arguments, so `gas(epic_list_attributes)` is in `reads` and `widget(epic_add_widget)` is in `mutations`, as each one declares.

`writes` and `mutations` are computed only when a guard asks, so a read pays nothing for them. The `source_control_*` calls are never in `writes`: they are the operations a source control guard would make itself.

## Hooks

- **`before`** denies by returning `success: false` or throwing. The call does not run, and the caller gets a `WRITE_BLOCKED` error with the message.
- **`after`** runs after a successful call and cannot deny it. Returning data replaces the call's result. A failure or a throw is logged and the result stands.

`before` hooks run in `order`, `after` hooks in reverse order. A denial stops the remaining `before` hooks and the call.

## Writing a guard

Extend `UeMcpGuard` from `ue-mcp/guard` and implement `before`, `after`, or both. `this.config` is the declared `options`. `call` holds `phase`, `method`, `params`, `paths` (the existing files the call would modify, empty for a read) and `session`.

### A deny guard

```yaml
# ue-mcp.plugin.yml, or the guards: section of a project's ue-mcp.yml
guards:
  policy:
    description: Deny writes outside /Game/Sandbox
    scope: writes
    before:
      class_path: guards/PolicyGuard
```

```ts
// guards/PolicyGuard.ts
import { UeMcpGuard, type GuardedCall } from "ue-mcp/guard";

export default class PolicyGuard extends UeMcpGuard<{ allow?: string[] }> {
  get taskName() { return "policy-guard"; }
  async before(call: GuardedCall) {
    const allow = this.config.allow ?? ["/Content/Sandbox/"];
    const outside = call.paths.filter((p) => !allow.some((a) => p.includes(a)));
    if (outside.length) {
      return this.deny(`writes outside the sandbox are not allowed: ${outside.join(", ")}`);
    }
    return this.allow();
  }
}
```

### An audit guard

```yaml
guards:
  audit:
    description: Append every call to an audit log
    scope: all
    after:
      class_path: guards/AuditGuard
```

```ts
// guards/AuditGuard.ts
import { UeMcpGuard, type GuardedCall } from "ue-mcp/guard";
import { appendFileSync } from "node:fs";

export default class AuditGuard extends UeMcpGuard<{ file?: string }> {
  get taskName() { return "audit-guard"; }
  async after(call: GuardedCall, result: unknown) {
    appendFileSync(this.config.file ?? "ue-mcp-audit.log", JSON.stringify({ method: call.method, paths: call.paths }) + "\n");
    // Return nothing to keep the result; return an object to replace it.
  }
}
```

A guard written as a plain `UeMcpTask` with `execute()` also works. It reads `this.options.method`, `this.options.params`, `this.options.paths` and, after the call, `this.options.result`, merged over its declared options.

Guards from several plugins and the project combine into one pipeline. The [`ue-mcp-perforce`](https://github.com/db-lyon/ue-mcp-perforce) plugin's guard is an example: a `before` hook scoped to `writes` that checks the target files out, or refuses when someone else holds the lock.

## Reloading

The `options` of guards declared in YAML reload when a config file changes. The layers are those of the [flow config](flows.md#config-layers): `~/.ue-mcp/config.yml` (or `UE_MCP_GLOBAL_CONFIG`), `ue-mcp.yml`, `ue-mcp.{env}.yml`, `ue-mcp.local.yml`. A plugin guard keeps the options in its manifest; a YAML guard with the same name is a separate guard.

- Malformed YAML, an invalid declaration or a file read error keeps the last valid options and logs a warning. A read error is retried after five seconds.
- Removing a layer, guard or hook keeps the last valid options until restart.
- Adding or removing guards or hooks, or changing `class_path`, `scope` or `order`, needs a restart of ue-mcp. Until then the previous configuration stays and a warning is logged. Plugin manifest changes also need a restart.
- At startup, an unreadable or malformed global file is ignored with a warning. An invalid project config stops startup.

## Source control

Checkout on write is core's. `source_control(action="set_checkout_mode")` decides what a bridge write does with a read-only file under source control, for whichever provider the editor has configured:

- `manual` (default) refuses the write and names `source_control(checkout)`.
- `auto` checks the file out first and lists it on the result as `autoCheckedOut`.

`UE_MCP_CHECKOUT_MODE=auto` in the editor's environment turns on `auto` from startup. No plugin is needed for either mode.

A source control guard from a plugin adds policy core does not have, such as refusing early when someone holds the lock or opening files in a named changelist.
