# Journal

The journal is the record a session leaves behind: what was done, what it produced, and how it ended. It lives on the `flow` tool as nine `journal_*` actions.

Every `flow(action="run")` writes one automatically, named after the flow and carrying its per-step outcome. Open one by hand for work that is **not** a flow, so the next session can read back what this one did.

```
flow(action="journal_start", title="Rebuild the shrine lighting", tags=["lighting"])
flow(action="journal_note", text="Sky light intensity 1.4 washed out the emissives. Dropped to 0.6.")
flow(action="journal_attach", artifactPath="/Game/Maps/NeonShrine", artifactKind="asset")
flow(action="journal_finish", summary="Lighting reworked; emissives now read at distance.")
```

None of these actions reaches the editor. They work with the editor down.

## Where it lives

`~/.ue-mcp/journal/<project>-<hash>.jsonl`, one file per project, outside the project's own tree so it never lands in source control or in a packaged build.

The name is the project directory's basename plus the first eight hex characters of a hash of its absolute path. Two checkouts of the same repo are two projects with two journals; a bare basename would merge their histories into one unreadable stream.

The format is append-only JSONL, one record per line, written with mode `0600`. A run is a fold over its records, so a half-written last line is skipped by the reader rather than corrupting the file. `journal_delete` is the only operation that rewrites it.

There is no initialise step. The file is created on the first write and read fresh on every call, so nothing has to be started or shut down.

| Variable | Effect |
|----------|--------|
| `UE_MCP_JOURNAL=0` | Stop recording. The six writing actions refuse with a clear error rather than dropping the write silently; the three reading actions still report what was recorded before. |
| `UE_MCP_JOURNAL_DIR` | Move the directory somewhere other than `~/.ue-mcp/journal`. |

Every journal action needs a loaded project, reads included, since the file is keyed by project.

## The open run

Six of the nine actions take an optional `runId`. Omit it and the call goes to **the open run**: the most recently started run whose status is still `active`. The response says which it chose, in a `resolvedFrom` field reading either `the open run` or `runId`, so a call that landed somewhere unexpected is visible rather than silent.

If there is no open run and no `runId`, the call fails and names the recent runs it could have meant.

## Actions

| Action | Description |
|--------|-------------|
| `journal_start` | Open a run. Params: `title`, `runId?` (supply one to make a retry safe), `tags?`, `flowName?`. Idempotent on `runId`: starting an id that already exists returns it untouched with `existed=true`. Returns the run and the call that undoes it. |
| `journal_note` | Append a note: what you decided, what surprised you, what the next session needs to know. Params: `text`, `runId?`. |
| `journal_attach` | Record something the run produced: a content path, a file on disk, a URL. Params: `artifactPath`, `artifactKind?` (free-form label such as `asset`, `screenshot`, `log`, `report`), `note?`, `runId?`. Idempotent per run on `artifactPath`. |
| `journal_finish` | Close a run with a verdict. Params: `status?` (`completed` or `failed`, default `completed`), `summary?`, `runId?`. |
| `journal_cancel` | Close a run as cancelled, for work abandoned rather than finished. Params: `reason?`, `runId?`. |
| `journal_list` | List this project's runs, newest first, with every filter applied together. Params: `status?`, `flowName?`, `tag?`, `since?`, `contains?`, `limit?` (default 20, `0` for all), `detail?`. |
| `journal_get` | Read one run in full: every note, every artifact, the outcome, and the per-step record if it was a flow run. Params: `runId`. |
| `journal_delete` | Delete runs, rewriting the file without their records. Params: exactly one of `runId` or `all=true`. Not undoable. |
| `journal_status` | Where the journal lives, whether it is recording, how much is in it, and which run is open. Params: none. |

### Notes and artifacts are append-only

Notes accumulate and are never rewritten, so a correction is another note. Artifacts are keyed on `artifactPath` within a run, so re-attaching the same path reports `existed=true` instead of duplicating the row.

The granularity of deletion is therefore the whole run. `journal_delete` reports how many runs and how many records went, so a delete that matched nothing is distinguishable from one that did, and it removes the file entirely rather than leaving an empty stub behind.

### Closing is idempotent

A run that has already ended keeps its first verdict and reports `existed=true`. The first answer about a run is the one that stands, which is what makes a retried `journal_finish` safe.

Passing `status="cancelled"` to `journal_finish` is refused with a pointer to `journal_cancel`, so the two verdicts cannot drift apart depending on which call was used.

## Reading a journal back

`journal_get` is the handover call. Give the next session a `runId` and it can reconstruct what happened without asking anyone.

```
flow(action="journal_list", since="7d", status="failed")
flow(action="journal_get", runId="j-mfa2k9-3")
```

`journal_list` filters are ANDed:

| Param | Accepts |
|-------|---------|
| `status` | `active`, `completed`, `failed`, `cancelled` |
| `flowName` | the name a run was started under |
| `tag` | one tag the run carries |
| `since` | epoch milliseconds, an ISO date (`2026-08-29`), or a relative age (`30m`, `2h`, `7d`) |
| `contains` | case-insensitive substring over the title, the summary and every note |
| `limit` | how many rows, default 20, `0` for all |
| `detail` | `true` returns every note and artifact rather than counts |

A run that came from `flow(action="run")` also carries a `steps` array: step number, name, type, success, whether it was skipped, duration, and the error if there was one.

## Journal and multiple editors

When the server drives more than one editor session, the six writing actions are classed as mutations and an untargeted call is refused until you name the editor. Neither the journal nor the skill surface reaches a bridge, but both are per project, and a misrouted write would record one project's history under another project's name.

At a single session nothing changes: the gate does not engage.
