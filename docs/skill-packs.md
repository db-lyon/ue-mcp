# Skill Packs

A skill pack is a written workflow: prose that says which calls to make, in what order, to get a particular job done in Unreal. It is not code and it runs nothing. An agent reads one to learn a sequence it would otherwise have to discover by trial.

Packs live on the `flow` tool as five `skill_*` actions.

```
flow(action="skill_list")
flow(action="skill_get", skillName="ue-mcp-niagara")
flow(action="skill_install")
flow(action="skill_check")
```

None of these actions reaches the editor. They work with the editor down.

## The three sources

| Source | Where it comes from | Path |
|--------|---------------------|------|
| `packaged` | shipped inside the ue-mcp package | `<package>/skills/<name>/SKILL.md` |
| `plugin` | contributed by a loaded plugin | `<plugin package>/skills/<name>/SKILL.md` |
| `project` | installed into your project | `<project>/.claude/skills/<name>/SKILL.md` |

Plugin packs are found by directory convention rather than a manifest key: any active plugin package that ships a `skills/<name>/SKILL.md` contributes it. A directory without a `SKILL.md` is not a pack and is skipped silently, because `.claude/skills/` legitimately holds skills that have nothing to do with ue-mcp.

ue-mcp currently ships six packaged packs, covering animation, blueprints, Niagara, native C++, Epic tool routing, and the general workflow.

## Installing

`ue-mcp init` installs the packaged set. `flow(action="skill_install")` does the same thing at any time, and takes an optional `skillName` to install one pack rather than all of them.

Installation copies `SKILL.md` and its `ue-mcp.yml` sidecar together into `<project>/.claude/skills/<name>/`. It is idempotent, and specific about it: a pack whose destination already holds the same bytes comes back under `unchanged` rather than being counted as installed, so a repeat call is distinguishable from a real update.

`flow(action="skill_remove")` reverses it. It deletes the `SKILL.md` and its sidecar, then the pack directory only if that leaves it empty, then the skills directory only if that leaves it empty too. Anything you put in `.claude/skills` yourself survives. A pack that was not installed is reported as absent rather than failing.

Both need a loaded project. The other three actions do not; they simply see no `project`-sourced packs without one.

## What a pack declares

A pack's metadata comes from the YAML frontmatter of its `SKILL.md`, merged with an optional `ue-mcp.yml` sidecar beside it, the sidecar winning field by field.

| Key | Meaning |
|-----|---------|
| `name` | the pack's name, which must agree with its directory |
| `description` | the line an agent reads to decide whether to load the pack |
| `categories` | the tool categories the workflow drives |
| `actions` | the actions it teaches, declared explicitly |
| `uePlugins` | Unreal plugins the workflow needs enabled |
| `unrealClasses` | UE classes it works with |
| `triggers` | phrases that suggest reaching for it |
| `engine` | engine versions it applies to |

The actions a pack actually teaches are also read out of its body, from any `category(action="name")` spelling, code fences included. `categories` and the action count are derived from that when they are not declared.

## Verifying packs against the live surface

This is the action worth knowing about. A pack is prose that names calls, so a renamed or removed action leaves behind a document that confidently teaches a call the server refuses. Nothing about the pack itself changes when that happens, and nothing complains.

`flow(action="skill_check")` reads every `category(action="x")` reference out of every pack and checks it against the action surface the server is actually advertising, reporting the ones that no longer resolve along with the closest real spellings.

```
flow(action="skill_check")
```

It reports six kinds of problem:

| Kind | Meaning |
|------|---------|
| `unknown_action` | the pack teaches an action that category does not have |
| `unknown_category` | the pack names a category that does not exist |
| `missing_description` | frontmatter has no `description`, so an agent cannot tell what the pack is for |
| `name_mismatch` | the declared `name` disagrees with the directory name |
| `unparsable_sidecar` | the `ue-mcp.yml` beside the pack could not be read |
| `undeclared_dependency` | the sidecar's `actions:` list omits something the body teaches |

`epic_*` references are reported separately, under `enrichmentOnly` rather than as problems. Those actions exist only once a UE 5.8 editor enriches the surface through the ToolsetRegistry, so their absence means the editor is down or older, not that the pack is wrong.

Run it after any release that renames actions, and after writing a pack of your own.

## Actions

| Action | Description |
|--------|-------------|
| `skill_list` | Every pack this server can see, with the description an agent reads to decide whether to load it, the categories it drives, and any Unreal plugin it needs. Params: `source?` (`packaged`, `project` or `plugin`, default all), `detail?`. Each row also says whether it is installed. |
| `skill_get` | One pack in full, optionally including its markdown, so an agent can read the workflow without leaving the tool surface. Params: `skillName`, `includeBody?` (default `true`). Also reports whether the project's installed copy matches the available one, which is how a stale install is spotted. |
| `skill_check` | Verify every pack against the live action surface. Params: none. |
| `skill_install` | Copy packs into this project's `.claude/skills`. Params: `skillName?` (omitted, every packaged and plugin-contributed pack). |
| `skill_remove` | Remove installed packs. Params: exactly one of `skillName` or `all=true`. |

## Writing one

Create `<project>/.claude/skills/<name>/SKILL.md` with frontmatter and a body:

```markdown
---
name: my-lighting-pass
description: Stand up a three-point lighting rig and verify it from the camera.
ue-mcp:
  categories: [level, material]
  uePlugins: []
---

# Lighting pass

1. `level(action="place_actor", className="DirectionalLight", ...)`
2. `level(action="place_actor", className="SkyLight", ...)`
3. `level(action="set_actor_property", ...)` to set intensity.
```

Then run `flow(action="skill_check")`. Every call the body names is resolved against the live surface, so a typo in an action name is caught before an agent ever follows the instructions.
