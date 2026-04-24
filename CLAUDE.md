# CLAUDE.md

Operating guide for Claude Code (and any AI agent) working in this repo. Shared team knowledge — checked into the repo so every contributor gets the same rulebook.

## Repo at a glance

- **TS server** (`src/`) — the MCP server. Wraps the UE bridge over WebSocket, exposes 19 category tools with 440+ actions.
- **C++ plugin** (`plugin/ue_mcp_bridge/`) — the editor-side bridge. Lives in `Private/Handlers/*.cpp`, registers actions with `FMCPHandlerRegistry`.
- **Test project** (`tests/ue_mcp/`) — the dedicated UE project used for smoke testing. The plugin is deployed here from `plugin/` via the deployer. This is the **only** safe target for live tests.
- **Docs** (`docs/`) — MkDocs site. `docs/release-notes-X.Y.Z.md` is the canonical release body.

Edit only under `plugin/ue_mcp_bridge/`. The deployer syncs to `tests/ue_mcp/Plugins/UE_MCP_Bridge/` — never hand-copy.

## Development workflow

### Git and commits

- **Atomic commits per logical change.** One fix or feature per commit with a clear message explaining the *why*. Don't batch unrelated changes.
- **No batching "housekeeping" commits** that sweep up everything at once. If you wrote five fixes, write five commits.
- **Claude owns git.** Stage, commit, push when ready. Don't push the user into running git commands.

### Versioning

**Hard rule: patch-level bumps only.** Version bumps in this repo are always `X.Y.Z → X.Y.(Z+1)`. Never increment major or minor without explicit sign-off. This holds even for genuinely large features — the answer is still a patch bump, and if you think otherwise, ask first.

- Bump `package.json` version.
- Commit and push. **Do not create git tags.** CI detects the version bump on `main` and publishes to npm + creates the GitHub release automatically.

### Building the plugin

- TS: `npx tsc --noEmit` for type-checking, `npx tsc` for emit. Build output goes to `dist/`.
- UE C++: `npm run build`. This calls `scripts/build.js` which invokes Unreal's Build.bat against `tests/ue_mcp/ue_mcp.uproject`.
- `npm run up:build` stops the editor, builds, and relaunches. Use when iterating live.
- The deployer (`scripts/deploy.mjs`, also called implicitly by `npm run up`) syncs `plugin/` → `tests/ue_mcp/Plugins/UE_MCP_Bridge/`. Run it after plugin source edits before building.

### Smoke tests — REQUIRED

`npm run test:smoke` exercises every registered handler via the live WebSocket bridge. **Run this before shipping a release.**

- Target **only** `tests/ue_mcp/ue_mcp.uproject`. Confirm the MCP connection via `project(get_status)` before running. If the editor is connected to anything else (the user's real project, another workspace), abort.
- Smoke tests execute real mutations (create blueprints, delete assets, modify levels). A misrouted run against a real project can corrupt an active editor session.
- 440+ handlers. Pass = every handler responds either with success or an expected parameter-validation error. Any timeout or `Unknown method` is a real failure.

### Clean plugin rebuild recipe

If new handlers return `"Unknown method"` at runtime even though source + build reported success:

1. Delete `tests/ue_mcp/Plugins/UE_MCP_Bridge/` entirely.
2. Delete any `*.patch_*.{dll,pdb,lib,exp}` files under `tests/ue_mcp/Binaries/Win64/`. Live Coding will otherwise load stale patches on top of a fresh DLL.
3. Redeploy (`node scripts/deploy.mjs`), then `npm run build`.

UBT's incremental build + Live Coding can mask registration failures from earlier compile errors. A clean rebuild surfaces the real error.

## Release process

After `package.json` version bump lands on `main`:

1. **Wait for CI** (~1-2 min) to create the GitHub release. It ships with an empty body.
2. **Fill the release body:**
   ```bash
   gh release edit vX.Y.Z --notes-file docs/release-notes-X.Y.Z.md
   ```
3. **Post the `landing/headline` commit status** on the release commit. The [ue-mcp-landing](https://github.com/db-lyon/ue-mcp-landing) site reads this to render the latest-version badge. Miss this and the landing page sits on the previous version.
   ```bash
   gh api -X POST repos/db-lyon/ue-mcp/statuses/<FULL_SHA> \
     -f state=success \
     -f context=landing/headline \
     -f description="2-3 headline features separated by · (~70 chars)" \
     -f target_url=https://github.com/db-lyon/ue-mcp/releases/tag/vX.Y.Z
   ```
   Use `git rev-parse <short-sha>` — the API rejects short SHAs.

Release notes structure: one-line summary, then `## New actions` / `## Bug fixes` / `## Internals` sections. See `docs/release-notes-0.7.18.md` and `docs/release-notes-0.7.19.md` for the style.

## Issue handling

- **Never close an issue without shipping code that resolves it.** Not "out of scope for this patch", not "prerequisite shipped", not "follow-up". Issues close only when the fix ships.
- If you can't implement now, label it `planned` (feature deferred), `limitation` (blocked by engine/private API), or leave open with a comment.
- "Resolve all open issues" means **implement them**, not close them.
- Release notes can reference deferred work but must not double as a closure justification.

## Code conventions

### Handler conventions

Each category has a paired `Private/Handlers/<Category>Handlers.{h,cpp}`. Handler methods are static, take `const TSharedPtr<FJsonObject>& Params`, and return `TSharedPtr<FJsonValue>`. They self-register in `RegisterHandlers(FMCPHandlerRegistry&)`.

- Use the helpers in `Private/HandlerUtils.h`: `MCPError`, `MCPSuccess`, `MCPResult`, `MCPSetCreated`/`Existed`/`Updated`, `MCPSetRollback`, `RequireString`, `OptionalString`/`Int`/`Number`/`Bool`, `REQUIRE_EDITOR_WORLD`.
- For JSON-driven property assignment (TArray, TSet, nested structs, UObject path refs, dotted paths), use `Private/HandlerJsonProperty.h::MCPJsonProperty::SetJsonOnProperty`. Introduced for `set_pcg_node_settings` (#149), now also used by `blueprint(set_component_property)` and `level(set_water_body_property)`.
- **Param names must exactly match between the TS schema and the C++ handler.** Drift is how silent failures start. Audit new actions in both places.
- Modules that may not be loaded (Water, WaterSpline, etc.) should be reached via `LoadClass<>()` at runtime rather than a `Build.cs` dependency. Fail with a clear "plugin X not available" error instead of a link-time break.

### Writing style — public artifacts

- **No em dashes (`—`).** Use hyphens (` - `), colons, parentheses, or split into sentences. Applies to commit messages, release notes, docs, PR bodies, code comments.
- **Never name competitor or comparison projects in public artifacts.** Commit messages, release notes, PR bodies, GitHub release bodies, code comments, docs — any of these. Even when the work is literally closing a gap against another project, describe the work on its own terms ("adds module input authoring"), not as "catching up to X" or "matching Y". Gap-analysis context belongs in private discussion, never in public git history.

### MCP design principle

The bridge must be self-sufficient. Every system (Niagara, materials, PCG, blueprints, etc.) should be fully creatable and configurable through the bridge without manual editor work or pre-existing placeholder assets. If a handler can't do something, **extend the handler** rather than punt to a Python escape hatch or editor workflow. Every `execute_python` fallback is a gap, and tracked as agent feedback.

## Useful commands

```bash
npm run up              # Start MCP server + launch editor
npm run up:build        # Stop editor, build plugin, relaunch
npm run build           # Build the UE C++ plugin only
npx tsc --noEmit        # Type-check TS
npm run test:smoke      # Live smoke tests (tests/ue_mcp only)
npm test                # Vitest unit tests
node scripts/deploy.mjs # Sync plugin/ → tests/ue_mcp/Plugins/
```

## Don'ts

- Don't create git tags; CI handles releases from version bumps on main.
- Don't manually copy plugin files to `tests/ue_mcp/`. The deployer does it.
- Don't use `TaskOutput` with `block=true` on long-running background tasks; it freezes the conversation. Background + poll or notify.
- Don't run live tests without verifying the MCP target first.
- Don't close issues "because a prerequisite shipped". Keep them open and label instead.
