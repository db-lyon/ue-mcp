# Material insertion and filtered inspection

`material(insert_expression_between)` inserts a node into a wire without
discarding its previous source output or channel masks:

```json
{
  "action": "insert_expression_between",
  "materialPath": "/Game/Materials/M_Master",
  "targetExpression": "MakeMaterialAttributes_0",
  "targetInput": "BaseColor",
  "expressionType": "Multiply",
  "inputForUpstream": "A",
  "properties": {"ConstB": 0.5}
}
```

Pass exactly one of `targetExpression` or `property`. Expression inputs accept
names or indices, including MakeMaterialAttributes and SetMaterialAttributes
attribute names with compact or spaced spelling (`BaseColor` or `Base Color`).
A material property can be `BaseColor`, `Roughness`,
`MaterialAttributes`, or another supported property. `targetInput` and
`inputForUpstream` default to the first input. The new node uses output zero.
An unconnected target produces a new node with an unconnected upstream input.
Properties must be editable reflected fields and are validated before mutation.

The result includes `expressionName`, `previousSource` and a rollback record.
Replaying the same target, type and upstream input returns `existed` while the
inserted node still feeds that target. Use a separate explicit graph edit if
another node of the same type is needed in that location. The insertion and
its inverse dirty the material; save it explicitly when ready.

`material(restore_expression_insertion)` accepts the rollback record's
parameters, restores the original output and channel masks, and removes the
inserted node. It refuses if another edit replaced the target wire, if the
source was removed, or if another wire now uses the inserted node. Repeating a
completed removal reports `alreadyDeleted`.

`material(read_summary, materialPath=...)` always returns `expressionCount` and
`countsByClass`. Without filters, `nodes` is empty. `classes` selects full class
names or short names such as `Multiply`; `parameterNames` selects parameter
names. When both are supplied, both must match. Selected rows carry
`expressionName`, `class`, `parameterName` and `group`.

Use `propertyChain: {"property":"MaterialAttributes","depthLimit":8}` to trace
one material property, or
`propertyChain: {"targetExpression":"MakeMaterialAttributes_0","targetInput":"BaseColor","depthLimit":4}`
to trace a specific attributes input. The source starts at depth zero. Each
node lists its input names, source outputs and masks. The read reports
`truncated` when the depth limit (0-64, default 8) or the 2000 node cap cuts the
chain short. Shared nodes and cycles are visited once. Reads do not create an
editor graph, dirty the material, or save a package.
