# Bulk DataAsset upsert

`bulk_upsert_data_assets` is a native editor bridge handler for creating or
updating a bounded batch of `UDataAsset` instances without repeated Python
calls. The equivalent dotted bridge alias is
`asset.bulk_upsert_data_assets`.

## Request

```json
{
  "items": [
    {
      "name": "DA_Example_Sword",
      "packagePath": "/Game/Data/Items",
      "className": "/Script/ExampleGame.ExampleItemDefinition",
      "properties": {
        "DisplayName": "Example Sword",
        "RequiredLevel": 12,
        "Tags": ["Item.Weapon.Sword"]
      }
    }
  ],
  "onConflict": "update",
  "dryRun": true,
  "save": true
}
```

The request accepts at most 500 items. Every item requires `name`,
`packagePath`, and `className`. `properties` is optional and accepts the same
JSON values and dotted property paths as the native asset property handlers.

`onConflict` applies to existing assets:

- `update` (default) validates the existing asset class and applies only the
  supplied properties.
- `skip` leaves compatible existing assets unchanged.
- `error` rejects the entire batch during preflight when any asset exists.

`save` defaults to `true`. Only newly created or changed packages are saved.
The handler never deletes assets that are absent from the request and never
resets unspecified properties.

## Preflight and idempotency

The handler resolves every class, validates every package and object name,
rejects duplicate destinations and protected mounts, and applies all requested
property values to transient DataAsset instances before mutating a package.
An invalid descriptor, class, property path, or value rejects the full batch.

Set `dryRun` to `true` to run that complete preflight without creating,
modifying, dirtying, or saving an asset. Per-item statuses report
`wouldCreate`, `wouldUpdate`, `wouldRemainUnchanged`, or `wouldSkip`.

Normal execution reports `created`, `updated`, `unchanged`, or `skipped` for
each item plus aggregate creation, update, save, and property counts. Replaying
the same `update` request returns `unchanged` for existing assets whose
requested values already match.

## Rollback

A successful mutating response includes a `bulk_restore_data_assets` rollback
descriptor. It restores the supplied previous values on updated assets and
deletes only the assets created by that response. The rollback handler is an
internal inverse operation; callers should use the descriptor returned by the
upsert response rather than constructing one manually.
