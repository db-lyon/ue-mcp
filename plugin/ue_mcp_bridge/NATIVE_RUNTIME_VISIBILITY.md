# Native Runtime Visibility Batch

This plugin exposes two raw bridge handlers for bounded, reversible visibility
changes in a running Play In Editor (PIE) world:

- `set_runtime_visibility`
- `restore_runtime_visibility`

The handlers replace repeated Python actor/component enumeration and one-off
visibility calls with one native preflighted operation. They are intentionally
plugin-only in this change. Raw bridge clients can discover both names through
`get_bridge_capabilities.actions`; adding a typed MCP server action is separate
work outside this plugin-scoped change.

## Set contract

`set_runtime_visibility` requires `hidden` and exactly one actor selector:
`actorLabels`, `actorPaths`, or `actorClass`. Optional `componentNames` and
`componentClasses` filters select scene components. Class names use the shared
UE-MCP class resolver, including C++ prefixes, script paths, and Blueprint class
paths.

Safety defaults and limits:

- `world` is fixed to `pie`; `pieInstance` selects a specific PIE context and
  is required when more than one PIE world is active.
- `dryRun` defaults to `true`.
- `maxTargets` defaults to 64 and cannot exceed 256.
- Explicit actor and component filter arrays cannot exceed 64 entries.
- `all` is rejected.
- Selection, descendant expansion, and rollback state capture finish before any
  visibility mutation.
- Component state uses `SetVisibility` and `SetHiddenInGame`; actor state uses
  `SetActorHiddenInGame`.
- No editor visibility flag is changed and no package is saved or dirtied.

When a non-dry-run call changes state, the response includes a standard rollback
payload for `restore_runtime_visibility`. Its opaque token refers to a bounded
native snapshot containing weak world and object identities, including Unreal's
object serials. Restore fails closed after PIE restarts or if any target was
destroyed, and it preflights every target before applying changes. Tokens are
process-local and expire when their snapshot is pruned or the plugin unloads.

Actor hidden state may be authority-owned or replicated. Callers should select
the intended server or client `pieInstance`; later replication can supersede a
client-local actor visibility change.

## Native validation boundary

The plugin Automation test verifies registration, raw capability discovery, and
safe parameter/class/world rejections without mutating a user's running editor.
Live apply, propagation, multi-PIE selection, and rollback behavior require the
dedicated UE-MCP smoke project so they can operate on disposable runtime actors.
