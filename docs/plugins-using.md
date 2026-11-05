# Using plugins

Installing, configuring and managing plugins in a project. To write one, see [Authoring a plugin](plugins-authoring.md).

## Commands

| Command | What it does |
|---------|--------------|
| `ue-mcp plugin install <name> [--version x.y.z]` | Install a plugin. |
| `ue-mcp plugin update [name] [--version x.y.z]` | Move one or every declared plugin to a version. |
| `ue-mcp plugin uninstall <name>` | Remove a plugin. Alias `remove`. |
| `ue-mcp plugin list` | List the plugins declared in `ue-mcp.yml`. |
| `ue-mcp plugin config <name> [...]` | Turn a plugin's flow groups on or off. |

Each takes `--editor <name-or-path>` to pick the project when the server drives several editors.

## Installing

```bash
ue-mcp plugin install <name>
```

`<name>` is a registry slug or an npm package name. A slug such as `meshy` is resolved to its package (`ue-mcp-meshy`) through the registry; any other name is passed to npm as is. The command:

1. Runs `npm install --save <package>` (and `npm init -y` first when the project has no `package.json`).
2. Validates the manifest: `actionPrefix`, every `inject:` target is a built-in category, no injected action or provided category collides with a built-in, `minServerVersion`, and `nativeModule.minBridgeApi` against the deployed bridge. Any failure stops the install.
3. Warns when the plugin's `uePluginDependency` is not enabled in the `.uproject`.
4. Adds `- name: <package>` to `plugins:` in `ue-mcp.yml`, with `version:` when `--version` was given.
5. Deploys the native module, if any, to `Plugins/<uePluginName>/` and prints `REBUILD REQUIRED`.
6. Copies the plugin's skills into `.claude/skills/`.

Restart your MCP client afterwards. Plugin actions appear on the next server start.

To install by hand, run `npm install --save <package>` and add the entry to `ue-mcp.yml` yourself. The plugin's skills then arrive on the next `ue-mcp plugin update`, `ue-mcp init` or `ue-mcp deploy`.

## Updating

`ue-mcp plugin update [name] [--version x.y.z]` moves each declared plugin (or just `name`) to the given version, or to the newest published one, including across a minor version a caret range would not cross. A plugin whose package, `ue-mcp.yml` pin or deployed native module is behind goes through the install path again: npm, the pin (kept only if the entry was pinned), the native module with files the new version dropped removed, and skills. It ends by saying whether the UE project needs a rebuild and whether the MCP client needs a restart, and fails if any step did not complete.

## Removing

`ue-mcp plugin uninstall <name>` refuses while an editor has the project open. Otherwise it runs `npm uninstall`, then removes the plugin's native module folder under `Plugins/` (source, Binaries and Intermediate), its entry in the `.uproject`, its skills and its `ue-mcp.yml` entry. The actions are gone on the next server start.

## The `plugins:` array

```yaml
plugins:
  - name: ue-mcp-perforce
  - name: some-other-plugin
    version: "^0.2.0"     # optional npm semver range
```

Each entry resolves under the project's `node_modules/`. Without `version`, whatever is installed loads. Order decides collisions; see [Ordering and collisions](#ordering-and-collisions).

## Inspecting

| Action | Returns |
|--------|---------|
| `plugins(action="list")` | `pluginCount`, `active`, and per plugin: name, version, prefix, `status`, `statusReason`, `degraded`, categories, injected and provided action counts, flow count, `uePluginDependency`, `uePluginPresent`. |
| `plugins(action="describe", name="<package>")` | The same for one plugin, plus `minServerVersion`, the injected and provided action names, knowledge files, flows, and the package and manifest paths. |

Both reflect what the running server loaded. See [Troubleshooting](plugins-troubleshooting.md).

## Flow groups

A plugin's flows are grouped by the name prefix before the first underscore (`niagara_fire` is in `niagara`), unless a flow sets `group:`. Groups are on unless turned off:

```bash
ue-mcp plugin config recipes --list-groups          # groups, on or off, and which layer set each
ue-mcp plugin config recipes --disable gas,material
ue-mcp plugin config recipes --enable niagara
ue-mcp plugin config recipes                        # interactive menu
```

`<name>` is the package name or its slug (the package name without `ue-mcp-`). The toggles are stored under the `ue-mcp:` block and merge across the config layers:

```yaml
ue-mcp:
  pluginConfig:
    recipes:
      groups:
        gas: false
        material: false
```

| Flag | File | Scope |
|------|------|-------|
| none | `~/.ue-mcp/config.yml` | you, every project |
| `--local` | `ue-mcp.local.yml` | you, this project |
| `--project` | `ue-mcp.yml` | the team, this project |

The user-global layer is the weakest, so a project can turn a group back on with `--local`. Plugin flows load at server start, so restart the MCP client after a change.

## Host UE plugin dependencies

A manifest can name one Unreal plugin it needs:

```yaml
uePluginDependency: SomePlugin
```

The value is the `.uplugin` name as it appears in `Plugins[].Name` in the `.uproject`. The server reports whether it is enabled as `uePluginPresent` on `plugins(action="list")`. The check does not stop the plugin loading; its actions fail at call time until the Unreal plugin is enabled and built.

To enable it:

1. Add `{ "Name": "SomePlugin", "Enabled": true }` to the `.uproject`'s `Plugins` array, or put the plugin's source under `Plugins/SomePlugin/`.
2. Build the project (`ue-mcp build`).
3. Restart the editor, then check `uePluginPresent: true`.

## Ordering and collisions

- A plugin action never replaces a built-in. A collision is skipped with a warning and the built-in stays.
- Between plugins, the entry listed first in `plugins:` wins, for injected actions and provided categories.
- A plugin that fails a package-level rule is skipped entirely and the others keep loading. A failure confined to one action, handler, flow, task or knowledge entry drops only that unit; see [Validation rules](plugins-troubleshooting.md#validation-rules).

## Over the daemon

The project's daemon exposes the same operations over HTTP, running the CLI commands above one at a time:

| Route | |
|-------|---|
| `GET /v1/plugins` | Declared plugins: version, pin, status, categories, problems. |
| `GET /v1/plugins/catalog` | The registry's published plugins. |
| `POST /v1/plugins/install` | Body `{ name, version? }`. |
| `POST /v1/plugins/uninstall` | Body `{ name }`. |
| `POST /v1/plugins/update` | Body `{ name? }`. |

A change takes effect when the server restarts, which the response says with `restartRequired`.
