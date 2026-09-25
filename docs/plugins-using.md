# Using plugins

Installing, configuring, and managing plugins in a project. If you're writing a plugin, see [Authoring a plugin](plugins-authoring.md).

## Installing

The supported install path is:

```bash
ue-mcp plugin install <package-name>
```

It's a thin wrapper that:

1. Runs `npm install --save <package-name>` so the package lands in `node_modules/` and is recorded in your `package.json`.
2. Validates the plugin's `ue-mcp.plugin.yml` - checks that `actionPrefix` is a legal identifier, every `inject:` target is a real registered category, every `class_path` resolves, and `minServerVersion` is satisfied.
3. Appends a `- name: <package-name>` entry to your `ue-mcp.yml`'s `plugins:` array (creating the array if needed).
4. Copies any skills the plugin ships into `.claude/skills/`.
5. Prints the restart instruction.

You can also install manually - `npm install --save <package-name>` and edit `ue-mcp.yml` yourself. The plugin's skills then arrive on the next `ue-mcp plugin update`, `ue-mcp init` or `ue-mcp deploy`.

## The `plugins:` array

The consumer surface is a single block in `ue-mcp.yml`:

```yaml
plugins:
  - name: pie-studio
  - name: some-other-plugin
    version: "^0.2.0"     # optional; npm semver range against package.json
```

Each entry resolves against the project's `node_modules/`. If `version` is omitted, whatever is currently installed loads. Order matters - see [Ordering and collisions](#ordering-and-collisions).

## Introspection

Two read-only actions on the `plugins` category:

| Action | What it returns |
|--------|-----------------|
| `plugins(action="list")` | Every plugin: name, version, prefix, status, count of injected actions and flows, host UE plugin dependency check. |
| `plugins(action="describe", name="<package>")` | Full detail for one plugin: the same fields as `list`, plus the actual injected action names, knowledge file paths, flows, and the resolved package + manifest paths on disk. |

Both reflect the live state of the server, so they're the right tool when something looks wrong - see [Troubleshooting](plugins-troubleshooting.md).

## Configuring a plugin (flow groups)

A plugin that ships many flows organizes them into **groups** by the flow-name prefix - `niagara_fire` and `niagara_smoke` are the `niagara` group, `pcg_scatter_surface` is `pcg`, and so on. (A plugin can override a flow's group explicitly with a `group:` field on the flow entry.) You enable or disable whole groups without installing or uninstalling the plugin:

```bash
ue-mcp plugin config recipes --list-groups          # show groups + on/off + which layer set each
ue-mcp plugin config recipes --disable gas,material  # turn groups off
ue-mcp plugin config recipes --enable niagara        # turn a group back on
ue-mcp plugin config recipes                         # no flags => interactive menu
```

Toggles are **opt-out**: a group is on unless you turn it off. They are stored as a map under the layered `ue-mcp:` config block, so they merge across the same `global < project < env < local` cascade as the rest of your config:

```yaml
ue-mcp:
  pluginConfig:
    recipes:            # the plugin slug: its package name minus `ue-mcp-`
      groups:
        gas: false
        material: false
```

Where the toggle is written depends on the target flag, because this is usually a **personal** preference and `ue-mcp.yml` is source-tracked - putting your taste there would collide with teammates:

| Flag | File | Scope |
|------|------|-------|
| *(default)* | `~/.ue-mcp/config.yml` | you, across all your projects (untracked) |
| `--local` | `ue-mcp.local.yml` | you, this project only (untracked) |
| `--project` | `ue-mcp.yml` | the whole team, this project (tracked) |

Default is user-global: "I never use GAS recipes" is a person-level trait, and it is the weakest layer so any project can still turn a group back on with `--local`. `--project` is the deliberate act of setting a team-wide default. Keep `ue-mcp.local.yml` out of version control (`ue-mcp init` adds it to `.gitignore`).

Plugin flows are loaded once at server start, so **restart the MCP server** after changing group config.

## Host UE plugin dependencies

A plugin can declare a single Unreal-side dependency in its manifest:

```yaml
uePluginDependency: SomePlugin
```

This is the **`.uplugin` filename** - the same string that appears as `Plugins[].Name` in your `.uproject`. ue-mcp checks for it at server start and reports the result as `uePluginPresent` in `plugins(action="list")`.

The check is informational, not gating: the npm-side plugin loads regardless, and its injected actions appear in the host category tools. But until the UE plugin is enabled in `.uproject` and its C++ modules are built, the actions will fail at execute time with a clear error.

To enable a host UE plugin:

1. Add `{ "Name": "<DepName>", "Enabled": true }` to your `.uproject`'s `Plugins` array.
2. Build the project (e.g. `npm run build` or `editor(action="build_all")`).
3. Restart the editor.
4. Run `plugins(action="list")` to confirm `uePluginPresent: true`.

For source-distributed UE plugins, drop the source under `Plugins/<DepName>/` - either as a git submodule (recommended for size) or as a vendored copy. The `.uplugin` file inside that directory is what UE's plugin discovery walks.

## Ordering and collisions

- **Plugin vs built-in:** A plugin action can never override a built-in. Collisions are hard-skipped at load time with a warning in the server log; the built-in stays.
- **Plugin vs plugin:** First entry in `plugins:` wins. If two plugins both inject `gameplay.foo_bar`, only the earlier-listed one's version is registered. The order is intentionally stable - your `ue-mcp.yml` is the source of truth for resolution.
- **Failed plugins are skipped, not partially loaded.** If a plugin fails validation (bad manifest, missing class_path, server-version mismatch, etc.), it is dropped entirely with a loud warning. Other plugins keep loading. The host tools are never partially mutated.

## Removing a plugin

`ue-mcp plugin uninstall <package-name>` removes the plugin's skills and native module, deletes its entry from `ue-mcp.yml`, and runs `npm uninstall`. On next restart, the actions are gone.
