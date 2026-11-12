# Mutation journal

The mutation journal records what a group of calls changed, so a client can revert one chat turn, or everything after one, across call boundaries and restarts.

## Recording

A client names a group with `editor(journal_group, journalGroup="turn-12")`. From then on, every call whose declared effect is `mutate` or `unknown` is journaled into that group until another group is named. `journal_group` with no `journalGroup` stops recording. Reads are never journaled, and neither are the journal's own actions or the checkpoint actions.

Each entry records the task, its effect, and how to restore what it touched:

- **Rollback descriptor.** When the handler returned one (`MCPSetRollback` in the plugin), the entry keeps its inverse method and payload.
- **Checkpoint snapshot.** With [checkpoints](checkpoints.md) on, each journaled call is bracketed by a checkpoint before and after it, and the entry keeps both ids and the files that differ: packages saved in the editor, config and source written through the server.
- **Not revertible.** A call with neither, such as an in-memory edit with no inverse while checkpoints are off, is recorded with the reason.

Journaled calls run one at a time per editor session, so each call's checkpoints cover only that call. A flow run is one entry, restored from its snapshot.

The journal is `Saved/UE_MCP_Bridge/journal.jsonl` in the project, so it survives a server or editor restart. The current group lives in memory; name it again after a restart.

## Reverting

| Action | What it does |
|---|---|
| `list_journal(journalGroup?)` | The current group, whether checkpoints are on, and each group with its entry, revertible and reverted counts. With `journalGroup`, that group's entries too. |
| `revert_group(journalGroup)` | Revert every entry of the group not reverted yet, newest first. |
| `revert_after_group(journalGroup)` | Revert every entry of every group first recorded after it, newest first. The named group is kept. |

An entry with a rollback descriptor is reverted by replaying it through the bridge. When the inverse fails and the entry also has a snapshot, its files are restored instead. An entry with only a snapshot has its files restored to the checkpoint taken before it, and reloaded in the editor, the way [`restore`](checkpoints.md) does it. The result has one line per entry: `reverted` with how, `failed` with the error, or `not-revertible` with the reason. Nothing is skipped silently. A reverted entry is marked in the journal and not replayed again.

## Daemon API

`/v1/health` lists `journal` under `features`.

| Method | Path | Body or query |
|---|---|---|
| GET | `/v1/journal` | `?editor&group` |
| PUT | `/v1/journal/group` | `{ editor?, group: string \| null }` |
| POST | `/v1/journal/revert` | `{ editor?, group }` |
| POST | `/v1/journal/revert-after` | `{ editor?, group }` |

`editor` names a session as `/v1/health` lists it; omitted is the active session.
