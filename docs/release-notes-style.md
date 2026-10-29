# Release notes style

Every release uses this format. Match the previous stable release rather than
inventing a voice per release.

## Structure

Four top-level sections, in this order. Omit one only when it is empty.

| Section | Holds |
|---|---|
| `## Features` | New actions and capabilities, grouped by category the way Unreal groups by subsystem |
| `## Fixes` | Defects that were repaired |
| `## Mentions` | Breaking changes, deprecations, upgrade steps, internals worth knowing |
| `## Contributions` | Thanks, with GitHub handles |

Under each top-level section, every subsection (Server, Editor, a category
name) sits inside its own `<details>` block with its name in the summary, and
every one is collapsed. GitHub renders the markdown inside only when a blank
line follows `</summary>`.

`scripts/compose-release-notes.mjs` sorts merged prerelease subsections into the
four sections by heading. `classifySection` is the rule; extend its patterns
rather than hand-sorting a composed body.

## The opening

A compatibility line, an install block, and at most one plain sentence of scale.

~~~markdown
## v1.3.1

Unreal Engine 5.4 to 5.8.

```bash
npx ue-mcp@latest
```

259 new actions across 16 categories.
~~~

**The engine range is 5.4 to 5.8.** State it. Do not annotate it with which
versions have been compiled, verified, gated or built. Someone on 5.5 who hits
a problem files an issue.

## Writing the entries

- Present tense for a feature, past tense for a fix. "Adds X." / "Fixed Y."
- One line each. The detail belongs in the linked docs or the PR.
- Cite the issue or PR number where there is one.
- Name the action or the parameter. `set_height_region`, not "the new heightmap write".

## What not to write

**No thesis sentence.** Not "The bridge stopped being one-way", not "Two
guarantees nothing had ever checked". Release notes are a list of what changed.

**No three-part lists as a summary.** "N new actions across M categories, every
X doing Y, and the Z now W" is a bulleted list flattened into a sentence. If
three facts matter, they are three bullets under a section.

**No editorialising the value.** Say what shipped. The reader decides whether it
is significant.

**No archaeology.** Do not narrate that something used to be broken, which beta
fixed it, or what a previous release got wrong. State the current behaviour.
A stable release is cumulative: the betas are invisible to anyone reading it.

**No competitor or comparison projects.** Ever, in any public artifact.

**No em dashes.** Hyphens, colons, parentheses, or two sentences.

## Headline frontmatter

The notes file must start with YAML frontmatter holding a `headline:` array.
CI validates it with `scripts/release-headline.mjs`, posts `landing/headline`
and strips the frontmatter from the published body.

- One to six items, 3 to 30 characters each.
- Each item starts with a letter or digit and uses only letters, digits, spaces
  and `_ - / ( ) . , +`. No colons, semicolons, question marks, exclamation
  marks or the `·` joiner.
- The items joined with ` · ` are at most 140 characters.
- Concrete noun phrases naming features. No sentences.

~~~yaml
---
headline:
  - First feature noun phrase
  - Second feature noun phrase
---
~~~

For a stable release composed from prereleases, the union of headlines is
usually over the six-item cap. The composer keeps items in order of first
appearance, which favours the earliest beta, and prints what it dropped. Swap
items deliberately.

## Contributions

Take the handles from the commit range, not from memory:

```bash
gh api repos/OWNER/REPO/compare/vPREV...vNEW --jq '.commits[].author.login' | sort -u
```

Credit anyone outside the maintainers who landed a PR. For a stable release the
composer writes this section from the commit range since the previous stable,
excluding the repository owner.
