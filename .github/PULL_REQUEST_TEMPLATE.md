## What changed

<!-- One or two sentences. What this does, not why it is good. -->

## Why

<!-- The problem. Link the issue: Closes #NNN. Delete if the title says it. -->

## Verification

<!-- What you actually ran. Delete the lines you did not run; do not leave
     one ticked that you skipped. Live rows need the editor on tests/ue_mcp. -->

- [ ] `npx tsc --noEmit`
- [ ] `npm run test:unit`
- [ ] `npm run build` (required for any change under `plugin/`)
- [ ] `npm run test:smoke`
- [ ] `npm run test:live`
- [ ] `npm run audit:em-dash` / `audit:unity` / docs / params

## Notes for the release

<!-- Optional. A line in the voice of the release notes, under the section it
     belongs to. Saves rewriting it at release time.

     ### Features   -> new actions and capabilities
     ### Fixes      -> defects, one line each, in past tense
     ### Mentions   -> breaking changes, deprecations, upgrade steps -->
