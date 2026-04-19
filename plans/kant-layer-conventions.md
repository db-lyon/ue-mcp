# Repo-local .kant layer conventions

How to author `.ue-mcp/*.kant` files that compose with the projected
ontology.

## Where they live

At the root of your UE project: `<projectDir>/.ue-mcp/*.kant`. Every
`.kant` file in that directory is picked up and composed at priority 2
(above projected layers at priority 1, which are above distributed
kernel at priority 0).

Any file is valid; the convention below is to keep intent discoverable.

| File | Intent | Typical paths it writes |
|---|---|---|
| `policy.kant` | Per-project overrides on tool metadata | `/UE/Mediation/Registry/Tools/*/Actions/*` |
| `feedback.kant` | Known workarounds, agent hints | `/UE/Audit/Workarounds/**`, `/UE/Mediation/**` |
| `intent.kant` | Project-specific vocabulary, naming rules | `/UE/Project/**` |

## The one hard rule: root at `UE@UE:`

Every layer file must have a top-level `UE@UE:` block and express its
contribution as nested children at real `/UE/...` paths.

```kant
UE@UE:
  Mediation:
    Registry:
      Tools:
        editor:
          Actions:
            execute_python:
              purpose: "Disabled in this project - use native tools instead"
              approval: "explicit"
```

This attaches at `/UE/Mediation/Registry/Tools/editor/Actions/execute_python`
and deep-merges with the projected point. Fields you declare override;
fields you omit fall through from the projected layer.

## The other rules inherited from kantext

- **No lists.** Multiplicity is a named children space. See the
  `Modules` subtree under a plugin point for the pattern.
- **No booleans.** Enabled/disabled states are `Signal` values with a
  numeric value and marker name. A shorthand acceptable to the parser:
  `enabled: 1 # enabled`.
- **No nulls.** Omit a field to leave it unset. To explicitly erase an
  inherited value, the current composer has no erase operator -
  document the override in a comment instead.
- **Comments** start with `#`. Trailing `#` on a value line is treated
  as a marker hint for Signals.
- **Strings** quoted with double quotes: `purpose: "..."`.
- **Numbers** bare: `timeoutMs: 30000`.
- **Indentation** is exactly two spaces per level. No tabs.

## Example: project-specific override of tool metadata

`/projectDir/.ue-mcp/policy.kant`:

```kant
# My project forbids execute_python in automated flows; agents must
# confirm any invocation with the user.

UE@UE:
  Mediation:
    Registry:
      Tools:
        editor:
          Actions:
            execute_python:
              purpose: "Requires user confirmation in this repo"
              # Category-level category default would otherwise say 'explicit';
              # we pin it here so overrides upstream cannot soften it.
              approval: 1 # explicit
              risk: 1 # catastrophic
```

An agent calling `ontology(describe_action, tool="editor",
actionName="execute_python")` sees the overridden `purpose` and the
pinned signals. The composer does not know which layer contributed
each field today; that will come when we route composition through
the kantext binary and get per-field provenance.

## Example: project intent vocabulary

`/projectDir/.ue-mcp/intent.kant`:

```kant
# Declares conventions the agent should respect in this project.

UE@UE:
  Project:
    Conventions:
      meaning: "Project Conventions"
      purpose: "Rules agents should follow in this repo"
      NamingRules:
        abilities: "GA_<verb>_<noun> (e.g. GA_Activate_Shield)"
        attributeSets: "AS_<Domain> (e.g. AS_Combat)"
        cues: "GC_<category>.<action> (e.g. GC_Combat.Impact)"
      TagHierarchy:
        root: "Combat, Movement, Interaction, UI"
        depth: 4
```

Agents can then query `/UE/Project/Conventions/NamingRules@abilities`
and cite it before creating a new ability asset.

## What NOT to put in a repo-local layer

- **Data the projector already emits.** Do not hand-maintain plugin
  metadata, class declarations, or the handler registry. Those are
  projected layers; hand-edits there will be shadowed on every
  projection cycle.
- **Secrets.** Layers are committed to git along with the rest of the
  project.
- **Per-session state.** Workarounds, flow runs, audit entries are
  projected layers; put them in memory or transient projected cache.

## Refresh model

- Projected layers regenerate when an agent calls
  `ontology(project_all)` or `ontology(project_by_event, event=<name>)`.
- Repo-local layers are read fresh on every `ontology(query)` or
  `ontology(compose)` call. Editing a `.kant` file and re-querying is
  enough; no restart.
- Startup primes only projectors subscribed to the `startup` event
  (handler-registry and plugins today). The engine symbol index is
  `manual` so it does not block ready-up.
