# CLAUDE.md

Operating guide for Claude Code (and any AI agent) working in this repo. Shared team knowledge - checked into the repo so every contributor gets the same rulebook.

## Repo at a glance

- **TS server** (`src/`) - the MCP server. Wraps the UE bridge over WebSocket, exposes <!-- count:tools -->24<!-- /count --> category tools with <!-- count:actions -->1091+<!-- /count --> actions. The numbers between those markers are stamped by `scripts/generate-tool-metadata.ts` from `ALL_TOOLS`; do not hand-edit them, and do not remove the markers, because a hand-written count here is the first thing every agent reads and the last thing anyone remembers to update.
- **C++ plugin** (`plugin/ue_mcp_bridge/`) - the editor-side bridge. Lives in `Private/Handlers/*.cpp`, registers actions with `FMCPHandlerRegistry`.
- **Test project** (`tests/ue_mcp/`) - the dedicated UE project used for smoke testing. The plugin is deployed here from `plugin/` via the deployer. This is the **only** safe target for live tests.
- **Docs** (`docs/`) - MkDocs site. `docs/release-notes-X.Y.Z.md` is the canonical release body.

Edit only under `plugin/ue_mcp_bridge/`. The deployer syncs to `tests/ue_mcp/Plugins/UE_MCP_Bridge/` - never hand-copy.

## Development workflow

### Git and commits

- **Atomic commits per logical change.** One fix or feature per commit with a clear message explaining the *why*. Don't batch unrelated changes.
- **No batching "housekeeping" commits** that sweep up everything at once. If you wrote five fixes, write five commits.
- **Claude owns git.** Stage, commit, push when ready. Don't push the user into running git commands.

#### Squash or merge commit

The merge style follows the commit count, and writing five commits only to squash them back into one throws the work away.

- **One logical change in the PR: squash-merge.** `gh pr merge <n> --squash --delete-branch`. Review fixups and typo passes are noise on main.
- **Several independently meaningful commits: merge commit.** `gh pr merge <n> --merge`. Each fix keeps its own SHA, which is what issue comments cite when they say which commit closed the issue. Squashing seven fixes leaves seven issues pointing at one SHA whose message describes something else.
- **Never rewrite a SHA that has already been cited** in an issue, a release note, or a comment. The citation is a promise that the commit exists.

### Versioning

**Hard rule: patch-level bumps only.** Version bumps in this repo are always `X.Y.Z → X.Y.(Z+1)`. Never increment major or minor without explicit sign-off. This holds even for genuinely large features - the answer is still a patch bump, and if you think otherwise, ask first.

- Bump `package.json` version.
- Commit and push. **Do not create git tags.** CI detects the version bump on `main` and publishes to npm + creates the GitHub release automatically.

### Building the plugin

- TS: `npx tsc --noEmit` for type-checking, `npx tsc` for emit. Build output goes to `dist/`.
- UE C++: `npm run build`. This calls `scripts/build.js`, which always builds the `ue_mcpEditor` target against `tests/ue_mcp/ue_mcp.uproject` and nothing else.
- Test builds pass `-NoEngineChanges`, so Unreal aborts with the offending file list if the build would overwrite anything already in the engine tree. Set `UE_MCP_ALLOW_TEST_ENGINE_CHANGES=true` only to bootstrap engine outputs in an engine you are willing to have written to.
- Optional: `UE_MCP_TEST_ENGINE_ROOT` pins the engine to build and run with, and `UE_MCP_PROTECTED_ENGINE_ROOTS` is a deny list of roots the scripts refuse to touch at all. The deny list outranks every other setting, including the opt-in above.
- `npm run up:build` stops the editor, builds, and relaunches. Use when iterating live.
- The deployer (`scripts/deploy.mjs`, also called implicitly by `npm run up`) syncs `plugin/` → `tests/ue_mcp/Plugins/UE_MCP_Bridge/`. Run it after plugin source edits before building.

### Smoke tests - REQUIRED

`npm run test:smoke` exercises every registered handler via the live WebSocket bridge. **Run this before shipping a release.**

- Target **only** `tests/ue_mcp/ue_mcp.uproject`. Confirm the MCP connection via `project(get_status)` before running. If the editor is connected to anything else (the user's real project, another workspace), abort.
- Smoke tests execute real mutations (create blueprints, delete assets, modify levels). A misrouted run against a real project can corrupt an active editor session.
- <!-- count:bridgeActions -->1030<!-- /count --> bridge actions. Pass = every handler responds either with success or an expected parameter-validation error. Any timeout or `Unknown method` is a real failure.

### Golden baseline - the advertised surface

`tests/golden/editor-down.json` is a recording of what a client is handed at startup with one project and no editor running: the `initialize` instructions and every tool in `tools/list` with its full input schema. `tests/unit/golden-editor-down.test.ts` starts the real server over stdio, records the same thing again, and fails if the two differ. It runs as part of `npm run test:unit`, so CI gates on it.

- **A failure is not automatically a bug.** It says the startup contract moved. Read the diff.
- **Re-record intentionally** with `npm run golden:record`, then review the diff before committing it. Never edit the JSON by hand.
- The recording is hermetic: a throwaway project in a temp directory, `UE_MCP_PORT=1` so nothing can be listening, every inherited `UE_MCP_*` variable dropped, and the user-scoped config/state/auth files redirected. Absolute paths are rewritten before serialization and asserted absent, so the file verifies on any machine.
- The `epic_*` actions enrichment injects are **sorted in the recording**, alongside the path, port and timestamp rewrites. Unreal's toolset registry promises the set of tools, not the sequence, and a restart that reshuffles it would otherwise report a surface change on a healthy editor. Only the snapshot is normalised; the server advertises exactly what it always did. A category's own actions keep their declared order, which is authored and does carry meaning.
- The editor-connected half (`tests/golden/editor-connected.json`) is the same recording made with a live editor attached, guarded by `tests/live/golden-connected.test.ts` in the live tier because it needs a running editor. Re-record it with `npm run golden:record -- --connected`. The recorder asserts the surface really was enriched from the live editor rather than a cache or the baked snapshot, so the two baselines cannot be recorded from the same source by accident.

### Live tier

`npm run test:live` runs `tests/live/` against an editor that is **already running**, through the shipped server: the connected golden baseline, per-path dispatch and leak assertions, addressing, gating, the union surface, and the records the bridge publishes. It never starts or stops an editor.

- Targets `tests/ue_mcp` only, verified by asking the editor which project it has open, and aborts before sending anything otherwise.
- One editor is enough. Cases needing more than one use a second session for a project whose editor is not running, since the session count is what arms targeting and gating.
- The leak assertions need the parameter echo, which is armed at editor startup: launch with `UE_MCP_PARAM_ECHO=1` to include them. Without it they skip and say why.
- `tests/live/matrix.ts` is the written form of plan item 7.3 of #817: every case, and where its assertion lives (live, engine-free and referenced, owned by the C++ tier, or pending on unshipped work). `tests/live/coverage.test.ts` fails when a reference stops resolving.

### Clean plugin rebuild recipe

If new handlers return `"Unknown method"` at runtime even though source + build reported success:

1. Delete `tests/ue_mcp/Plugins/UE_MCP_Bridge/` entirely.
2. Delete any `*.patch_*.{dll,pdb,lib,exp}` files under `tests/ue_mcp/Binaries/Win64/`. Live Coding will otherwise load stale patches on top of a fresh DLL.
3. Redeploy (`node scripts/deploy.mjs`), then `npm run build`.

UBT's incremental build + Live Coding can mask registration failures from earlier compile errors. A clean rebuild surfaces the real error.

## Release process

CI **gates the publish job** on a single pre-staged input: the draft GitHub release for `vX.Y.Z`. The draft body must begin with YAML frontmatter declaring a `headline:` array. CI parses, validates against the regex enforced by `scripts/release-headline.mjs`, joins with ` · `, posts the `landing/headline` commit status itself, strips the frontmatter from the body, then promotes the draft. No manual `gh api ... statuses` step.

1. **Author release notes locally** with frontmatter. Notes file lives anywhere (gitignored, scratch, /tmp). The frontmatter must be the first thing in the file:
   ```yaml
   ---
   headline:
     - First feature noun phrase
     - Second feature noun phrase
     - Third feature noun phrase
   ---

   ## vX.Y.Z

   One-line summary.

   ### Server / Bug fixes / Internals
   ...
   ```
   **Headline rules** (enforced by CI; format violations fail the publish job):
   - 1-6 items, each 3-30 chars (keep them tight; concrete noun phrases).
   - Allowed characters: letters, digits, spaces, `_ - / ( ) . , +`.
   - **Forbidden:** `:`, `;`, `?`, `!`, the `·` joiner, leading/trailing whitespace.
   - Joined string (`items.join(" · ")`) must be ≤140 chars.
   - Style: concrete noun phrases naming features. **No sentences. No editorializing the value.** Match the historical voice (see prior releases on the GitHub releases page).

2. **Create the draft release:**
   ```bash
   gh release create vX.Y.Z --draft --notes-file /path/to/local-notes.md
   ```

3. **Bump `package.json` version, commit, push.** That's it - CI takes over.

4. **CI validates → publishes → promotes.** If the headline frontmatter is missing or malformed, the publish job fails with a pointer to the offending item *before* npm publish runs. On success, the published release page shows the body with frontmatter stripped, and the `landing/headline` commit status is posted automatically.

Release notes structure (below the frontmatter): one-line summary, then `### Server` / `### Bug fixes` / `### Internals` sections. See prior releases on GitHub for the style. (The `docs/release-notes-*.md` files in the repo predate this flow and are kept as references only.)

### Prereleases

A version with a prerelease suffix (`1.2.0-beta`, `1.2.0-beta.2`, `1.3.0-rc.1`) goes through the exact same flow: draft release with headline frontmatter, bump, push. CI routes it to its own channel automatically.

- **npm dist-tag** comes from the first prerelease identifier, so `1.2.0-beta` and `1.2.0-beta.2` publish under `beta` and `1.3.0-rc.1` under `rc`. `npx ue-mcp` keeps resolving to the newest plain `X.Y.Z`; testers opt in with `ue-mcp@beta`.
- **The GitHub release** is marked as a prerelease, so it does not take the "Latest" badge or answer `/releases/latest`.
- **The publish gate** asks whether that exact version is already on the registry, so a prerelease does not wedge every later push.
- The rules live in `scripts/release-version.mjs` (unit tested in `tests/unit/release-version.test.ts`), mirrored for the shipped CLI in `src/version-check.ts`. Change one and the parity test will tell you to change the other.

The tag name still has to match the version exactly: `gh release create v1.2.0-beta --draft --notes-file ...`.

#### Cutting the stable release the betas led up to

Prerelease notes stay incremental: the `1.2.0-beta.3` page says what changed since `beta.2`. Stable notes are cumulative, because the betas never took the "Latest" badge and everything they shipped is invisible from the `1.2.0` page otherwise. `npm run release:notes -- --version 1.2.0 --notes-file <what landed since the last beta> --out /tmp/v1.2.0.md` builds that union: it reads every published prerelease of the same `X.Y.Z` in semver order, merges their bodies section by section, folds bullets that repeat or cite the same issue number, and unions the headlines back off the `landing/headline` commit statuses. It prints what it merged, what it deduped and any headline item it had to drop for the six-item cap, then runs its own output through `scripts/release-headline.mjs` so the file cannot fail the publish gate later. Reread the summary paragraph before you create the draft, since it may have come from a beta. A stable release with no prereleases passes its notes file straight through.

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
- **Never copy a file-local helper into a second handler file.** The module is compiled as a unity build, so two `.cpp` files sharing a blob merge their anonymous namespaces and the second definition is a redefinition (`error C2084`). Unity grouping shifts with file count, file order, and the `git status` derived adaptive-unity working set, so a duplicate builds clean locally and breaks on the next machine. Shared helper goes in a header: `Public/HandlerUtils.h`. `npm run audit:unity` reports duplicates and is gated by a unit test in CI. Copies drift as well as collide - the protected-mount guardrail had four copies and two of them enforced weaker rules.

### Writing style - public artifacts

- **No em dashes (`—`).** Use hyphens (` - `), colons, parentheses, or split into sentences. Applies to commit messages, release notes, docs, PR bodies, code comments. <!-- em-dash-allowed: the rule has to show the character it bans -->
  Git hooks enforce this locally, not CI. `.husky/pre-commit` scans the staged files and `.husky/commit-msg` scans the message, so a bad character is rejected before the commit exists rather than after a runner picks the job up. Run `npm run audit:em-dash` for the whole tracked tree, and `npm run audit:em-dash -- --explain` for the exemption policy.
- **Never name competitor or comparison projects in public artifacts.** Commit messages, release notes, PR bodies, GitHub release bodies, code comments, docs - any of these. Even when the work is literally closing a gap against another project, describe the work on its own terms ("adds module input authoring"), not as "catching up to X" or "matching Y". Gap-analysis context belongs in private discussion, never in public git history.

### MCP design principle

The bridge must be self-sufficient. Every system (Niagara, materials, PCG, blueprints, etc.) should be fully creatable and configurable through the bridge without manual editor work or pre-existing placeholder assets. If a handler can't do something, **extend the handler** rather than punt to a Python escape hatch or editor workflow. Every `execute_python` fallback is a gap, and tracked as agent feedback.

## Useful commands

```bash
npm run up              # Start MCP server + launch editor
npm run up:build        # Stop editor, build plugin, relaunch
npm run build           # Build the UE C++ plugin only
npx tsc --noEmit        # Type-check TS
npm run test:smoke      # Live smoke tests (tests/ue_mcp only)
npm run test:live       # Live tier against a running editor (tests/ue_mcp only)
npm run golden:record   # Re-record tests/golden/editor-down.json (review the diff)
npm run release:notes   # Compose cumulative stable notes from a version's prereleases
npm test                # Vitest unit tests
node scripts/deploy.mjs # Sync plugin/ → tests/ue_mcp/Plugins/
```

## Don'ts

- Don't create git tags; CI handles releases from version bumps on main.
- Don't push a version bump without first creating the draft release. CI fails the publish job if it's missing or its body lacks valid `headline:` frontmatter.
- Don't post the `landing/headline` commit status manually anymore - CI authors it from the validated frontmatter array. Manual posts are overwritten and waste an API call.
- Don't manually copy plugin files to `tests/ue_mcp/`. The deployer does it.
- Don't use `TaskOutput` with `block=true` on long-running background tasks; it freezes the conversation. Background + poll or notify.
- Don't run live tests without verifying the MCP target first.
- Don't close issues "because a prerequisite shipped". Keep them open and label instead.
