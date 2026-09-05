# Release notes style

The format is copied from projects that publish a lot of releases: Deno, Zed,
Bun, Godot, and Epic's own Unreal Engine notes. Follow it rather than inventing
a voice per release.

## Structure

Four top-level sections, in this order. Omit one only when it is empty.

| Section | Holds |
|---|---|
| `## Features` | New actions and capabilities, grouped by category the way Unreal groups by subsystem |
| `## Fixes` | Defects that were repaired |
| `## Mentions` | Breaking changes, deprecations, upgrade steps, internals worth knowing |
| `## Contributions` | Thanks, with GitHub handles |

`scripts/compose-release-notes.mjs` files merged prerelease sections into these
buckets by heading, so a section a late beta introduced cannot print after the
internals. `classifySection` is the rule; extend its patterns rather than
hand-sorting a composed body.

## The opening

A compatibility line, an install block, and at most one plain sentence of scale.

```markdown
## v1.3.1

Unreal Engine 5.4 to 5.8.

npx ue-mcp@latest

259 new actions across 16 categories.
```

**The engine range is 5.4 to 5.8.** State it. Do not annotate it with which
versions have been compiled, verified, gated or built. Someone on 5.5 who hits
a problem will file an issue, which is the system working. Hedging the range in
public copy reads as a warning about our own product.

## Writing the entries

- Present tense for a feature, past tense for a fix. "Adds X." / "Fixed Y."
- One line each. The detail belongs in the linked docs or the PR.
- Cite the issue or PR number where there is one.
- Name the action or the parameter. `set_height_region`, not "the new heightmap write".

## What not to write

**No thesis sentence.** Not "The bridge stopped being one-way", not "Two
guarantees nothing had ever checked". Release notes are a list of what changed.
No human opens a release page for a framing device.

**No three-part lists as a summary.** "N new actions across M categories, every
X doing Y, and the Z now W" is a bulleted list flattened into a sentence. It
asserts nothing and buries whatever was interesting. If three facts matter, they
are three bullets under a section.

**No editorialising the value.** Say what shipped. The reader decides whether it
is significant.

**No archaeology.** Do not narrate that something used to be broken, which beta
fixed it, or what a previous release got wrong. State the current behaviour.
A stable release is cumulative: the betas are invisible to anyone reading it.

**No competitor or comparison projects.** Ever, in any public artifact.

**No em dashes.** Hyphens, colons, parentheses, or two sentences.

## Headline frontmatter

CI parses it and posts `landing/headline`. Three to six items, 3 to 30
characters each, letters/digits/spaces and `_ - / ( ) . , +` only. No colons,
semicolons, question marks, exclamation marks or the joiner. Joined length
must stay under 140 characters.

Pick the items that represent the whole release. For a stable cut composed from
prereleases the union is usually over the six-item cap, and the composer keeps
the first six it saw, which favours the earliest beta. Reread the dropped list
it prints and swap deliberately.

## Contributions

Take the handles from the commit range, not from memory:

```bash
gh api repos/OWNER/REPO/compare/vPREV...vNEW --jq '.commits[].author.login' | sort -u
```

Credit anyone outside the maintainers who landed a PR. The composer generates
this section when it is given contributors.
