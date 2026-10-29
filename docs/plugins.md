# Plugins

A ue-mcp plugin is an npm package that extends the server. It can:

- **Inject** actions into a built-in category, prefixed with the plugin's `actionPrefix`.
- **Provide** a new top-level category it owns.
- **Ship native C++** that registers handlers on the editor bridge.

It can also ship [flows](flows.md), [guards](plugins-guards.md), knowledge text for the categories it touches, and Claude Code skills.

Published plugins are listed at **[plugins.ue-mcp.com](https://plugins.ue-mcp.com)**.

## Quick start

In your Unreal project directory:

```bash
ue-mcp plugin install <slug-or-package>
```

This installs the package with npm, validates its manifest, adds it to `plugins:` in `ue-mcp.yml`, copies its skills, and deploys its native module if it has one. Rebuild the UE project when a native module was deployed, then restart your MCP client (in Claude Code, `/mcp`).

Check it loaded:

```text
plugins(action="list")
```

The entry reports `status: "active"` and the categories it injects into or provides. See [Using plugins](plugins-using.md).

## How plugins load

A plugin package ships a `ue-mcp.plugin.yml` manifest and its compiled task classes under `dist/`. At server start ue-mcp:

1. Reads `plugins:` from the project's `ue-mcp.yml`.
2. Resolves each entry under `<project>/node_modules/`.
3. Validates each plugin's manifest.
4. Imports its task classes and registers them as tasks.
5. Adds injected actions to the host category tools (`<category>(action="<prefix>_<action>")`) and registers provided categories as their own tools.
6. Attaches the plugin's knowledge files to those categories' AI-facing docs.

All of this happens before the tools are registered with the MCP client, so plugin actions are listed beside the built-in ones.

## Shapes

| Shape | Manifest block | Use it when |
|-------|----------------|-------------|
| Inject | `inject:` | The action belongs in an existing category. The default choice. |
| Provide | `provides:` | The actions form a domain no built-in category covers. |
| Native C++ | `nativeModule:` | The plugin needs engine APIs the bridge does not expose. `nativeModule.category` surfaces the C++ handlers as actions with no TypeScript. See [Native C++ modules](plugins-native-modules.md). |

A plugin can combine them.

## What belongs in core and what belongs in a plugin

Core is a provider-neutral bridge to engine modules and to the Epic plugins that ship with the engine, reached through public or reflected APIs. Core holds no third-party credentials and calls no third-party service itself.

The exception is first-party infrastructure the server needs: the plugin registry client, feedback routing, the daemon extension host, and registry login.

Everything else is a plugin:

- Integrations with third-party Unreal plugins.
- Provider-specific behaviour on a neutral core surface. For example, `p4_` actions injected into `source_control`, which itself works with any provider the editor supports.
- Opinionated workflows, test harnesses, demos and products built on the core surface.
- Anything where ue-mcp itself would call an external service or hold a credential.
- Anything that reaches undocumented interfaces, or interfaces whose terms restrict that use.

## Pages

- [Using plugins](plugins-using.md): install, update, configure, inspect and remove.
- [Authoring a plugin](plugins-authoring.md): scaffold, manifest, tasks, provided categories, skills and publishing.
- [Native C++ modules](plugins-native-modules.md): C++ handlers on the bridge.
- [Guards](plugins-guards.md): gate every bridge call.
- [Validation and troubleshooting](plugins-troubleshooting.md): the loader's rules and common failures.
