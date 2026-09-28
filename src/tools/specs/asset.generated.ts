// GENERATED FILE - do not edit.
//
// Written by scripts/generate-handler-specs.mjs from tests/golden/handler-specs.json,
// which is recorded from the parameter specs the C++ handlers register with
// (npm run specs:record). To change a parameter, change its RegisterHandler
// spec, re-record, and regenerate (#1057).
import { categorySchema, makeSpecBp, type HandlerSpecs } from "../../surface/handler-spec.js";

/** The recorded contract of every spec'd asset handler. */
export const handlerSpecs: HandlerSpecs = {
  "add_curvetable_key": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "CurveTable asset path",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "rowName",
        "type": "string",
        "required": true,
        "description": "Row to key"
      },
      {
        "name": "time",
        "type": "number",
        "required": true,
        "description": "Key time"
      },
      {
        "name": "value",
        "type": "any",
        "required": true,
        "description": "Key value (a number)"
      },
      {
        "name": "interpMode",
        "type": "string",
        "required": false,
        "description": "linear (default) | constant | cubic | none"
      },
      {
        "name": "keyTimeTolerance",
        "type": "number",
        "required": false,
        "description": "How close an existing key must be to be updated rather than added"
      }
    ]
  },
  "add_curvetable_row": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "CurveTable asset path",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "rowName",
        "type": "string",
        "required": true,
        "description": "Row to add"
      },
      {
        "name": "curveType",
        "type": "string",
        "required": false,
        "description": "simple | rich (default: the table's type, or rich for cubic)",
        "aliases": [
          "mode"
        ]
      },
      {
        "name": "interpMode",
        "type": "string",
        "required": false,
        "description": "linear (default) | constant | cubic | none"
      }
    ]
  },
  "add_datatable_row": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "DataTable asset path",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "rowName",
        "type": "string",
        "required": true,
        "description": "Row to append or overwrite"
      },
      {
        "name": "row",
        "type": "object",
        "required": true,
        "description": "Row-struct fields to write; fields not named keep their values",
        "aliases": [
          "fields",
          "data"
        ]
      }
    ]
  },
  "add_graph_node": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "EdGraph-backed asset path",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "graphName",
        "type": "string",
        "required": false,
        "description": "Graph to author in, by name or unique substring (required when the asset has several graphs)"
      },
      {
        "name": "nodeClass",
        "type": "string",
        "required": false,
        "description": "Class the node action spawns"
      },
      {
        "name": "actionName",
        "type": "string",
        "required": false,
        "description": "Schema menu entry name, or 'category|name'"
      },
      {
        "name": "spawnMode",
        "type": "string",
        "required": false,
        "description": "auto (default) | action | direct"
      },
      {
        "name": "posX",
        "type": "number",
        "required": false,
        "description": "Node X position"
      },
      {
        "name": "posY",
        "type": "number",
        "required": false,
        "description": "Node Y position"
      },
      {
        "name": "save",
        "type": "boolean",
        "required": false,
        "description": "Save the asset after the edit (default true)"
      }
    ]
  },
  "add_socket": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StaticMesh, SkeletalMesh or Skeleton asset path",
        "role": "editTarget"
      },
      {
        "name": "socketName",
        "type": "string",
        "required": true,
        "description": "Socket name"
      },
      {
        "name": "boneName",
        "type": "string",
        "required": false,
        "description": "Bone to attach to (SkeletalMesh and Skeleton, default root)"
      },
      {
        "name": "relativeLocation",
        "type": "vec3",
        "required": false,
        "description": "Socket location relative to its parent"
      },
      {
        "name": "relativeRotation",
        "type": "rotator",
        "required": false,
        "description": "Socket rotation relative to its parent"
      },
      {
        "name": "relativeScale",
        "type": "vec3",
        "required": false,
        "description": "Socket scale"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "skip (default) | update | error"
      }
    ]
  },
  "append_asset_array_elements": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Asset path; a Blueprint path writes its generated-class CDO",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "propertyName",
        "type": "string",
        "required": true,
        "description": "Path of the TArray property"
      },
      {
        "name": "elements",
        "type": "array",
        "required": true,
        "description": "Values to append, validated before any is written",
        "minItems": 1
      },
      {
        "name": "save",
        "type": "boolean",
        "required": false,
        "description": "Save the package after the write (default true)"
      }
    ]
  },
  "apply_mesh_fracture": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StaticMesh to cut; it is never modified",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "pattern",
        "type": "string",
        "required": false,
        "description": "slice (default) | grid | random"
      },
      {
        "name": "axis",
        "type": "string",
        "required": false,
        "description": "x | y | z (default): the axis slice cuts run across"
      },
      {
        "name": "pieces",
        "type": "integer",
        "required": false,
        "description": "Pieces along axis for slice, at least 2 (default 4)"
      },
      {
        "name": "gridX",
        "type": "integer",
        "required": false,
        "description": "Pieces along X for grid (default 2)"
      },
      {
        "name": "gridY",
        "type": "integer",
        "required": false,
        "description": "Pieces along Y for grid (default 2)"
      },
      {
        "name": "gridZ",
        "type": "integer",
        "required": false,
        "description": "Pieces along Z for grid (default 1)"
      },
      {
        "name": "planeCount",
        "type": "integer",
        "required": false,
        "description": "Random planes to cut with (default 3)"
      },
      {
        "name": "seed",
        "type": "integer",
        "required": false,
        "description": "Seed for plane placement and jitter (default 0); the same seed gives the same pieces"
      },
      {
        "name": "jitter",
        "type": "number",
        "required": false,
        "description": "0 to 0.45, how far a slice or grid cut may wander from even spacing (default 0)"
      },
      {
        "name": "gapWidth",
        "type": "number",
        "required": false,
        "description": "How far apart the halves of each cut are pushed, in centimetres (default 0.01)"
      },
      {
        "name": "fillHoles",
        "type": "boolean",
        "required": false,
        "description": "Cap each piece where the plane cut it (default true)"
      },
      {
        "name": "minPieceTriangles",
        "type": "integer",
        "required": false,
        "description": "Discard pieces with fewer triangles than this (default 4)"
      },
      {
        "name": "outputBasePath",
        "type": "string",
        "required": false,
        "description": "Pieces are written as outputBasePath_00, _01 and on (default '<assetPath>_Piece')"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "When a piece asset exists: error (default) | replace"
      },
      {
        "name": "copyMaterialsFromSource",
        "type": "boolean",
        "required": false,
        "description": "Copy the source's material slots onto the written asset (default true)"
      },
      {
        "name": "nanite",
        "type": "string",
        "required": false,
        "description": "inherit (default, match the source) | enable | disable"
      },
      {
        "name": "recomputeNormals",
        "type": "boolean",
        "required": false,
        "description": "Recompute normals on the written mesh (default false)"
      },
      {
        "name": "recomputeTangents",
        "type": "boolean",
        "required": false,
        "description": "Recompute tangents on the written mesh (default false)"
      },
      {
        "name": "lodType",
        "type": "string",
        "required": false,
        "description": "MaxAvailable (default, ignores lodIndex) | HiResSourceModel | SourceModel | RenderData"
      },
      {
        "name": "lodIndex",
        "type": "integer",
        "required": false,
        "description": "LOD to read when lodType names one (default 0)",
        "min": 0
      },
      {
        "name": "save",
        "type": "boolean",
        "required": false,
        "description": "Save the written asset (default true)"
      },
      {
        "name": "dryRun",
        "type": "boolean",
        "required": false,
        "description": "Run the operation and report the result without writing (default false)"
      }
    ]
  },
  "apply_mesh_hole_fill": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StaticMesh to read",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "fillMethod",
        "type": "string",
        "required": false,
        "description": "Automatic (default) | MinimalFill | PolygonTriangulation | TriangleFan | PlanarProjection"
      },
      {
        "name": "weldFirst",
        "type": "boolean",
        "required": false,
        "description": "Weld coincident boundary edges before filling (default true)"
      },
      {
        "name": "weldTolerance",
        "type": "number",
        "required": false,
        "description": "How close two boundary edges must be to weld (default 1e-6)"
      },
      {
        "name": "removeDegenerateFirst",
        "type": "boolean",
        "required": false,
        "description": "Drop zero-area triangles before welding and filling (default false)"
      },
      {
        "name": "deleteIsolatedTriangles",
        "type": "boolean",
        "required": false,
        "description": "Delete floating disconnected triangles, whose holes no fill can close (default true)"
      },
      {
        "name": "outputPath",
        "type": "string",
        "required": false,
        "description": "Asset to write (default '<assetPath>_Filled')",
        "role": "editTarget"
      },
      {
        "name": "inPlace",
        "type": "boolean",
        "required": false,
        "description": "Overwrite assetPath instead of writing a separate asset (default false); not with outputPath, and the one form with no rollback"
      },
      {
        "name": "backupPath",
        "type": "string",
        "required": false,
        "description": "Copy the source here before an inPlace edit, the only way back to the original geometry",
        "role": "editTarget"
      },
      {
        "name": "lodType",
        "type": "string",
        "required": false,
        "description": "MaxAvailable (default, ignores lodIndex) | HiResSourceModel | SourceModel | RenderData"
      },
      {
        "name": "lodIndex",
        "type": "integer",
        "required": false,
        "description": "LOD to read when lodType names one (default 0)",
        "min": 0
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "When the output asset exists: error (default) | replace"
      },
      {
        "name": "copyMaterialsFromSource",
        "type": "boolean",
        "required": false,
        "description": "Copy the source's material slots onto the written asset (default true)"
      },
      {
        "name": "copyCollisionFromSource",
        "type": "boolean",
        "required": false,
        "description": "Copy the source's simple collision and trace flag onto the written asset (default true)"
      },
      {
        "name": "nanite",
        "type": "string",
        "required": false,
        "description": "inherit (default, match the source) | enable | disable"
      },
      {
        "name": "recomputeNormals",
        "type": "boolean",
        "required": false,
        "description": "Recompute normals on the written mesh (default false)"
      },
      {
        "name": "recomputeTangents",
        "type": "boolean",
        "required": false,
        "description": "Recompute tangents on the written mesh (default false)"
      },
      {
        "name": "removeDegenerates",
        "type": "boolean",
        "required": false,
        "description": "Drop degenerate triangles when writing back into an existing mesh (default false)"
      },
      {
        "name": "save",
        "type": "boolean",
        "required": false,
        "description": "Save the written asset (default true)"
      },
      {
        "name": "dryRun",
        "type": "boolean",
        "required": false,
        "description": "Run the operation and report the result without writing (default false)"
      }
    ]
  },
  "apply_mesh_mirror": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StaticMesh to read",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "axis",
        "type": "string",
        "required": false,
        "description": "x (default) | y | z | custom: the mirror plane's normal, through planeOrigin"
      },
      {
        "name": "planeOrigin",
        "type": "vec3",
        "required": false,
        "description": "A point on the mirror plane (default the mesh origin)"
      },
      {
        "name": "planeNormal",
        "type": "vec3",
        "required": false,
        "description": "The mirror plane's normal when axis is custom; a zero vector is refused"
      },
      {
        "name": "applyPlaneCut",
        "type": "boolean",
        "required": false,
        "description": "Remove the geometry on the far side of the plane before reflecting (default true)"
      },
      {
        "name": "flipCutSide",
        "type": "boolean",
        "required": false,
        "description": "Keep the other side of the plane instead (default false)"
      },
      {
        "name": "weldAlongPlane",
        "type": "boolean",
        "required": false,
        "description": "Weld the two halves along the plane (default true)"
      },
      {
        "name": "outputPath",
        "type": "string",
        "required": false,
        "description": "Asset to write (default '<assetPath>_Mirrored')",
        "role": "editTarget"
      },
      {
        "name": "inPlace",
        "type": "boolean",
        "required": false,
        "description": "Overwrite assetPath instead of writing a separate asset (default false); not with outputPath, and the one form with no rollback"
      },
      {
        "name": "backupPath",
        "type": "string",
        "required": false,
        "description": "Copy the source here before an inPlace edit, the only way back to the original geometry",
        "role": "editTarget"
      },
      {
        "name": "lodType",
        "type": "string",
        "required": false,
        "description": "MaxAvailable (default, ignores lodIndex) | HiResSourceModel | SourceModel | RenderData"
      },
      {
        "name": "lodIndex",
        "type": "integer",
        "required": false,
        "description": "LOD to read when lodType names one (default 0)",
        "min": 0
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "When the output asset exists: error (default) | replace"
      },
      {
        "name": "copyMaterialsFromSource",
        "type": "boolean",
        "required": false,
        "description": "Copy the source's material slots onto the written asset (default true)"
      },
      {
        "name": "copyCollisionFromSource",
        "type": "boolean",
        "required": false,
        "description": "Copy the source's simple collision and trace flag onto the written asset (default true)"
      },
      {
        "name": "nanite",
        "type": "string",
        "required": false,
        "description": "inherit (default, match the source) | enable | disable"
      },
      {
        "name": "recomputeNormals",
        "type": "boolean",
        "required": false,
        "description": "Recompute normals on the written mesh (default false)"
      },
      {
        "name": "recomputeTangents",
        "type": "boolean",
        "required": false,
        "description": "Recompute tangents on the written mesh (default false)"
      },
      {
        "name": "removeDegenerates",
        "type": "boolean",
        "required": false,
        "description": "Drop degenerate triangles when writing back into an existing mesh (default false)"
      },
      {
        "name": "save",
        "type": "boolean",
        "required": false,
        "description": "Save the written asset (default true)"
      },
      {
        "name": "dryRun",
        "type": "boolean",
        "required": false,
        "description": "Run the operation and report the result without writing (default false)"
      }
    ]
  },
  "apply_mesh_remesh": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StaticMesh to read",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "remeshMode",
        "type": "string",
        "required": false,
        "description": "uniform (default, even edge lengths) | adaptive (denser where the surface curves)"
      },
      {
        "name": "targetType",
        "type": "string",
        "required": false,
        "description": "TriangleCount (default) | TargetEdgeLength"
      },
      {
        "name": "targetTriangleCount",
        "type": "integer",
        "required": false,
        "description": "Approximate triangle count to aim for (targetType TriangleCount, default 1000)"
      },
      {
        "name": "targetEdgeLength",
        "type": "number",
        "required": false,
        "description": "Edge length in centimetres to aim for (targetType TargetEdgeLength, default 1)"
      },
      {
        "name": "smoothingType",
        "type": "string",
        "required": false,
        "description": "Uniform | UVPreserving | Mixed (default)"
      },
      {
        "name": "smoothingRate",
        "type": "number",
        "required": false,
        "description": "0 to 1, how far vertices move toward their neighbours each pass (default 0.25)"
      },
      {
        "name": "boundaryConstraint",
        "type": "string",
        "required": false,
        "description": "Fixed | Refine | Free (default) | Ignore"
      },
      {
        "name": "iterations",
        "type": "integer",
        "required": false,
        "description": "Remeshing passes (default: the engine's)"
      },
      {
        "name": "discardAttributes",
        "type": "boolean",
        "required": false,
        "description": "Throw away UVs, normals and material IDs so the remesher moves freely (default false)"
      },
      {
        "name": "reprojectToInputMesh",
        "type": "boolean",
        "required": false,
        "description": "Pull new vertices back onto the original surface each pass (default true)"
      },
      {
        "name": "relativeDensity",
        "type": "number",
        "required": false,
        "description": "-2 to 2, bias toward more or fewer triangles in curved regions (remeshMode adaptive, default 0)"
      },
      {
        "name": "outputPath",
        "type": "string",
        "required": false,
        "description": "Asset to write (default '<assetPath>_Remeshed')",
        "role": "editTarget"
      },
      {
        "name": "inPlace",
        "type": "boolean",
        "required": false,
        "description": "Overwrite assetPath instead of writing a separate asset (default false); not with outputPath, and the one form with no rollback"
      },
      {
        "name": "backupPath",
        "type": "string",
        "required": false,
        "description": "Copy the source here before an inPlace edit, the only way back to the original geometry",
        "role": "editTarget"
      },
      {
        "name": "lodType",
        "type": "string",
        "required": false,
        "description": "MaxAvailable (default, ignores lodIndex) | HiResSourceModel | SourceModel | RenderData"
      },
      {
        "name": "lodIndex",
        "type": "integer",
        "required": false,
        "description": "LOD to read when lodType names one (default 0)",
        "min": 0
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "When the output asset exists: error (default) | replace"
      },
      {
        "name": "copyMaterialsFromSource",
        "type": "boolean",
        "required": false,
        "description": "Copy the source's material slots onto the written asset (default true)"
      },
      {
        "name": "copyCollisionFromSource",
        "type": "boolean",
        "required": false,
        "description": "Copy the source's simple collision and trace flag onto the written asset (default true)"
      },
      {
        "name": "nanite",
        "type": "string",
        "required": false,
        "description": "inherit (default, match the source) | enable | disable"
      },
      {
        "name": "recomputeNormals",
        "type": "boolean",
        "required": false,
        "description": "Recompute normals on the written mesh (default false)"
      },
      {
        "name": "recomputeTangents",
        "type": "boolean",
        "required": false,
        "description": "Recompute tangents on the written mesh (default false)"
      },
      {
        "name": "removeDegenerates",
        "type": "boolean",
        "required": false,
        "description": "Drop degenerate triangles when writing back into an existing mesh (default false)"
      },
      {
        "name": "save",
        "type": "boolean",
        "required": false,
        "description": "Save the written asset (default true)"
      },
      {
        "name": "dryRun",
        "type": "boolean",
        "required": false,
        "description": "Run the operation and report the result without writing (default false)"
      }
    ]
  },
  "apply_mesh_simplify": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StaticMesh to read",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "simplifyMode",
        "type": "string",
        "required": false,
        "description": "triangleCount (default) | vertexCount | tolerance | edgeLength | clusterEdgeLength | planar | polygroup | editorTriangleCount | editorVertexCount"
      },
      {
        "name": "triangleCount",
        "type": "integer",
        "required": false,
        "description": "Triangle count to reduce to (simplifyMode triangleCount or editorTriangleCount)"
      },
      {
        "name": "vertexCount",
        "type": "integer",
        "required": false,
        "description": "Vertex count to reduce to, at least 4 (simplifyMode vertexCount or editorVertexCount)"
      },
      {
        "name": "tolerance",
        "type": "number",
        "required": false,
        "description": "Furthest in centimetres the surface may drift (simplifyMode tolerance)"
      },
      {
        "name": "edgeLength",
        "type": "number",
        "required": false,
        "description": "Target edge length in centimetres (simplifyMode edgeLength or clusterEdgeLength)"
      },
      {
        "name": "angleThreshold",
        "type": "number",
        "required": false,
        "description": "How far from coplanar still counts as coplanar (simplifyMode planar or polygroup, default 0.001)"
      },
      {
        "name": "method",
        "type": "string",
        "required": false,
        "description": "StandardQEM | VolumePreserving | AttributeAware (default) | AttributeAwareV2"
      },
      {
        "name": "allowSeamCollapse",
        "type": "boolean",
        "required": false,
        "description": "Let the simplifier collapse UV, normal and material seams (default true)"
      },
      {
        "name": "preserveVertexPositions",
        "type": "boolean",
        "required": false,
        "description": "Keep surviving vertices where they are (default false)"
      },
      {
        "name": "autoCompact",
        "type": "boolean",
        "required": false,
        "description": "Close the gaps the collapse leaves in the index space (default true)"
      },
      {
        "name": "outputPath",
        "type": "string",
        "required": false,
        "description": "Asset to write (default '<assetPath>_Simplified')",
        "role": "editTarget"
      },
      {
        "name": "inPlace",
        "type": "boolean",
        "required": false,
        "description": "Overwrite assetPath instead of writing a separate asset (default false); not with outputPath, and the one form with no rollback"
      },
      {
        "name": "backupPath",
        "type": "string",
        "required": false,
        "description": "Copy the source here before an inPlace edit, the only way back to the original geometry",
        "role": "editTarget"
      },
      {
        "name": "lodType",
        "type": "string",
        "required": false,
        "description": "MaxAvailable (default, ignores lodIndex) | HiResSourceModel | SourceModel | RenderData"
      },
      {
        "name": "lodIndex",
        "type": "integer",
        "required": false,
        "description": "LOD to read when lodType names one (default 0)",
        "min": 0
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "When the output asset exists: error (default) | replace"
      },
      {
        "name": "copyMaterialsFromSource",
        "type": "boolean",
        "required": false,
        "description": "Copy the source's material slots onto the written asset (default true)"
      },
      {
        "name": "copyCollisionFromSource",
        "type": "boolean",
        "required": false,
        "description": "Copy the source's simple collision and trace flag onto the written asset (default true)"
      },
      {
        "name": "nanite",
        "type": "string",
        "required": false,
        "description": "inherit (default, match the source) | enable | disable"
      },
      {
        "name": "recomputeNormals",
        "type": "boolean",
        "required": false,
        "description": "Recompute normals on the written mesh (default false)"
      },
      {
        "name": "recomputeTangents",
        "type": "boolean",
        "required": false,
        "description": "Recompute tangents on the written mesh (default false)"
      },
      {
        "name": "removeDegenerates",
        "type": "boolean",
        "required": false,
        "description": "Drop degenerate triangles when writing back into an existing mesh (default false)"
      },
      {
        "name": "save",
        "type": "boolean",
        "required": false,
        "description": "Save the written asset (default true)"
      },
      {
        "name": "dryRun",
        "type": "boolean",
        "required": false,
        "description": "Run the operation and report the result without writing (default false)"
      }
    ]
  },
  "asset_health_check": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Asset to check",
        "aliases": [
          "path"
        ]
      }
    ]
  },
  "audit_asset_hygiene": {
    "category": "asset",
    "params": [
      {
        "name": "directory",
        "type": "string",
        "required": false,
        "description": "Content path to sweep, added to directories (default /Game)"
      },
      {
        "name": "directories",
        "type": "array",
        "required": false,
        "description": "Content paths to sweep, mount-rooted such as /Game/Characters (default /Game)",
        "items": "string"
      },
      {
        "name": "recursive",
        "type": "boolean",
        "required": false,
        "description": "Include subfolders (default true)"
      },
      {
        "name": "maxAssets",
        "type": "integer",
        "required": false,
        "description": "Assets the sweep walks before it reports scanTruncated (default 20000, max 200000)"
      },
      {
        "name": "maxIssues",
        "type": "integer",
        "required": false,
        "description": "Findings of each kind to list (default 200); the counts are always complete"
      },
      {
        "name": "checks",
        "type": "array",
        "required": false,
        "description": "unreferenced | brokenReferences | duplicates | naming | redirectors (default all five)",
        "items": "string"
      },
      {
        "name": "classNames",
        "type": "array",
        "required": false,
        "description": "Only these asset class names",
        "items": "string"
      },
      {
        "name": "excludePaths",
        "type": "array",
        "required": false,
        "description": "Skip packages whose name starts with one of these",
        "items": "string"
      },
      {
        "name": "keepPaths",
        "type": "array",
        "required": false,
        "description": "Never report or touch packages whose name starts with one of these, such as a folder loaded by name from config",
        "items": "string"
      },
      {
        "name": "duplicateMethod",
        "type": "string",
        "required": false,
        "description": "content (same class, size and saved hash) | name (same name and class in several folders) | both (default)"
      },
      {
        "name": "namingRules",
        "type": "array",
        "required": false,
        "description": "Naming convention entries",
        "items": "object",
        "fields": [
          {
            "name": "class",
            "type": "string",
            "required": true,
            "description": "Asset class name the registry reports, such as StaticMesh or WidgetBlueprint, not a class path"
          },
          {
            "name": "prefix",
            "type": "string",
            "required": false,
            "description": "Name prefix the class requires"
          },
          {
            "name": "suffix",
            "type": "string",
            "required": false,
            "description": "Name suffix the class requires"
          }
        ]
      },
      {
        "name": "namingRuleMode",
        "type": "string",
        "required": false,
        "description": "merge (default, the caller's rules over the built-in table) | replace"
      },
      {
        "name": "includeWorlds",
        "type": "boolean",
        "required": false,
        "description": "Report maps as unreferenced too (default false)"
      },
      {
        "name": "ignoreRedirectorReferencers",
        "type": "boolean",
        "required": false,
        "description": "Do not count a redirector stub as a referencer (default true)"
      }
    ]
  },
  "bind_cloth_to_section": {
    "category": "asset",
    "params": [
      {
        "name": "skeletalMeshPath",
        "type": "string",
        "required": true,
        "description": "SkeletalMesh asset path",
        "aliases": [
          "assetPath"
        ],
        "role": "editTarget"
      },
      {
        "name": "lodIndex",
        "type": "integer",
        "required": true,
        "description": "Mesh LOD",
        "min": 0
      },
      {
        "name": "sectionIndex",
        "type": "integer",
        "required": true,
        "description": "Render section to bind",
        "min": 0
      },
      {
        "name": "clothingAsset",
        "type": "string",
        "required": false,
        "description": "Clothing asset by name (optional when the mesh has one)"
      },
      {
        "name": "assetLodIndex",
        "type": "integer",
        "required": false,
        "description": "LOD inside the clothing asset (default: lodIndex, clamped)",
        "min": 0
      }
    ]
  },
  "bulk_read_asset_properties": {
    "category": "asset",
    "params": [
      {
        "name": "propertyNames",
        "type": "array",
        "required": true,
        "description": "Property paths to read off every matched asset; dotted paths walk nested structs (max 32)",
        "items": "string"
      },
      {
        "name": "assetPaths",
        "type": "array",
        "required": false,
        "description": "The exact assets to read",
        "items": "string"
      },
      {
        "name": "directory",
        "type": "string",
        "required": false,
        "description": "Content folder to sweep, narrowed by classNames"
      },
      {
        "name": "recursive",
        "type": "boolean",
        "required": false,
        "description": "Include subfolders of directory (default true)"
      },
      {
        "name": "classNames",
        "type": "array",
        "required": false,
        "description": "Only assets of these classes",
        "items": "string"
      },
      {
        "name": "matchSubclasses",
        "type": "boolean",
        "required": false,
        "description": "Also match subclasses of classNames (default true)"
      },
      {
        "name": "where",
        "type": "array",
        "required": false,
        "description": "Predicates evaluated in the editor (max 24)",
        "items": "object",
        "fields": [
          {
            "name": "field",
            "type": "string",
            "required": true,
            "description": "Dot path into the row, e.g. props.CullDistance.Max, className or suspect"
          },
          {
            "name": "op",
            "type": "string",
            "required": false,
            "description": "eq (default), ne, lt, lte, gt, gte, contains, notContains, startsWith, endsWith, in, notIn, exists, notExists, isNull, isNotNull, isTrue, isFalse"
          },
          {
            "name": "value",
            "type": "any",
            "required": false,
            "description": "Operand the op compares against"
          }
        ]
      },
      {
        "name": "whereMode",
        "type": "string",
        "required": false,
        "description": "all (default) | any"
      },
      {
        "name": "suspectOnly",
        "type": "boolean",
        "required": false,
        "description": "Only rows where a requested property is absent or null"
      },
      {
        "name": "groupBy",
        "type": "string",
        "required": false,
        "description": "Dot path to group matches by (max 200 groups)"
      },
      {
        "name": "countBy",
        "type": "array",
        "required": false,
        "description": "Dot paths to build value histograms for (max 8)",
        "items": "string"
      },
      {
        "name": "sampleLimit",
        "type": "integer",
        "required": false,
        "description": "Sample asset names per group (default 5, max 25)"
      },
      {
        "name": "countOnly",
        "type": "boolean",
        "required": false,
        "description": "Return the aggregates without rows"
      },
      {
        "name": "limit",
        "type": "integer",
        "required": false,
        "description": "Rows to return (default 200, max 2000)"
      },
      {
        "name": "startIndex",
        "type": "integer",
        "required": false,
        "description": "First row index, for paging"
      },
      {
        "name": "maxAssets",
        "type": "integer",
        "required": false,
        "description": "Refuse to load more candidates than this (default 2000, max 20000)"
      },
      {
        "name": "outputPath",
        "type": "string",
        "required": false,
        "description": "Write every matched row to this JSON file and return the path instead of the rows"
      }
    ],
    "choices": [
      {
        "mode": "exactlyOne",
        "branches": [
          [
            "assetPaths"
          ],
          [
            "directory"
          ]
        ]
      }
    ]
  },
  "bulk_rename_assets": {
    "category": "asset",
    "params": [
      {
        "name": "renames",
        "type": "array",
        "required": true,
        "description": "Rename descriptors: {sourcePath, destinationPath}, {assetPath, newName} or {sourcePath, newPackagePath, newName}",
        "items": "object",
        "role": "editTarget",
        "roleKeys": [
          "sourcePath",
          "destinationPath",
          "assetPath"
        ]
      }
    ]
  },
  "bulk_restore_data_assets": {
    "category": "asset",
    "params": [
      {
        "name": "updatedItems",
        "type": "array",
        "required": false,
        "description": "{assetPath, properties} snapshots to write back",
        "items": "object",
        "role": "editTarget",
        "roleKeys": [
          "assetPath"
        ]
      },
      {
        "name": "createdAssetPaths",
        "type": "array",
        "required": false,
        "description": "Assets the upsert created, to delete",
        "items": "string",
        "role": "editTarget"
      },
      {
        "name": "save",
        "type": "boolean",
        "required": false,
        "description": "Save what was restored (default true)"
      }
    ],
    "contractExempt": "deletes and rewrites the assets its payload names"
  },
  "bulk_set_asset_properties": {
    "category": "asset",
    "params": [
      {
        "name": "items",
        "type": "array",
        "required": true,
        "description": "Asset updates: [{assetPath, properties}], max 500",
        "items": "object",
        "minItems": 1,
        "maxItems": 500,
        "role": "editTarget",
        "roleKeys": [
          "assetPath"
        ]
      },
      {
        "name": "save",
        "type": "boolean",
        "required": false,
        "description": "Save the changed packages (default true)"
      },
      {
        "name": "dryRun",
        "type": "boolean",
        "required": false,
        "description": "Preflight and report without writing (default false)"
      },
      {
        "name": "continueOnError",
        "type": "boolean",
        "required": false,
        "description": "Apply the items that passed preflight instead of aborting the batch (default false)"
      }
    ]
  },
  "bulk_upsert_data_assets": {
    "category": "asset",
    "params": [
      {
        "name": "items",
        "type": "array",
        "required": true,
        "description": "DataAsset descriptors: [{name, packagePath, className, properties?}], max 500",
        "items": "object",
        "minItems": 1,
        "maxItems": 500
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "update (default) | skip | error"
      },
      {
        "name": "dryRun",
        "type": "boolean",
        "required": false,
        "description": "Run the full preflight and report the planned statuses without writing (default false)"
      },
      {
        "name": "save",
        "type": "boolean",
        "required": false,
        "description": "Save the changed packages (default true)"
      }
    ]
  },
  "check_uvs": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StaticMesh or SkeletalMesh asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "lodIndex",
        "type": "integer",
        "required": false,
        "description": "Source LOD to act on (default 0)",
        "min": 0
      },
      {
        "name": "requireLightmapChannel",
        "type": "boolean",
        "required": false,
        "description": "Treat a missing lightmap channel as a fault (default true for StaticMesh)"
      },
      {
        "name": "maxOverlapFraction",
        "type": "number",
        "required": false,
        "description": "Overlap above this fraction of the lightmap channel is a fault (default 0.001)"
      },
      {
        "name": "rasterSize",
        "type": "number",
        "required": false,
        "description": "Raster resolution for the overlap estimate (default 512)"
      }
    ]
  },
  "compare_textures": {
    "category": "asset",
    "params": [
      {
        "name": "assetPathA",
        "type": "string",
        "required": true,
        "description": "First Texture2D",
        "aliases": [
          "a"
        ]
      },
      {
        "name": "assetPathB",
        "type": "string",
        "required": true,
        "description": "Second Texture2D",
        "aliases": [
          "b"
        ]
      }
    ]
  },
  "compile_customizable_object": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "CustomizableObject asset path",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "optimizationLevel",
        "type": "string",
        "required": false,
        "description": "Optimization level enum name, e.g. None or Maximum (default: the compile function's own)"
      },
      {
        "name": "textureCompression",
        "type": "string",
        "required": false,
        "description": "None | Fast | HighQuality"
      }
    ]
  },
  "connect_graph_pins": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "EdGraph-backed asset path",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "graphName",
        "type": "string",
        "required": false,
        "description": "Graph to author in, by name or unique substring (required when the asset has several graphs)"
      },
      {
        "name": "sourceNode",
        "type": "string",
        "required": false,
        "description": "Node of the source pin: nodeGuid, node path, name or unique title"
      },
      {
        "name": "sourcePinId",
        "type": "string",
        "required": false,
        "description": "pinId of the source pin (preferred)"
      },
      {
        "name": "sourcePin",
        "type": "string",
        "required": false,
        "description": "Source pin name, when no pinId is given"
      },
      {
        "name": "sourcePinDirection",
        "type": "string",
        "required": false,
        "description": "input | output, to disambiguate sourcePin"
      },
      {
        "name": "targetNode",
        "type": "string",
        "required": false,
        "description": "Node of the target pin"
      },
      {
        "name": "targetPinId",
        "type": "string",
        "required": false,
        "description": "pinId of the target pin"
      },
      {
        "name": "targetPin",
        "type": "string",
        "required": false,
        "description": "Target pin name, when no pinId is given"
      },
      {
        "name": "targetPinDirection",
        "type": "string",
        "required": false,
        "description": "input | output, to disambiguate targetPin"
      },
      {
        "name": "save",
        "type": "boolean",
        "required": false,
        "description": "Save the asset after the edit (default true)"
      }
    ]
  },
  "create_asset_by_class": {
    "category": "asset",
    "params": [
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "Asset name"
      },
      {
        "name": "className",
        "type": "string",
        "required": true,
        "description": "Concrete UObject class: class name with or without the C++ prefix, or a /Script/Module.Class path",
        "aliases": [
          "class"
        ]
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Destination folder (default /Game)"
      },
      {
        "name": "properties",
        "type": "object",
        "required": false,
        "description": "Property values keyed by property name"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "skip (default) returns an existing asset; error refuses"
      }
    ]
  },
  "create_curvetable": {
    "category": "asset",
    "params": [
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "Asset name"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Destination folder (default /Game/CurveTables)"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "skip (default) returns an existing table; error refuses"
      }
    ],
    "contractExempt": "Creates and saves a CurveTable from the contract name before anything fails"
  },
  "create_customizable_object": {
    "category": "asset",
    "params": [
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "Asset name"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Destination folder (default /Game)"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "skip (default) | error"
      },
      {
        "name": "save",
        "type": "boolean",
        "required": false,
        "description": "Save the new asset (default true)"
      }
    ]
  },
  "create_data_asset": {
    "category": "asset",
    "params": [
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "Asset name"
      },
      {
        "name": "className",
        "type": "string",
        "required": true,
        "description": "UDataAsset subclass: class name with or without the C++ prefix, or a /Script/Module.Class path",
        "aliases": [
          "class"
        ]
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Destination folder (default /Game)"
      },
      {
        "name": "properties",
        "type": "object",
        "required": false,
        "description": "Property values keyed by property name"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "skip (default) returns an existing asset; error refuses"
      }
    ]
  },
  "create_datatable": {
    "category": "asset",
    "params": [
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "Asset name"
      },
      {
        "name": "rowStruct",
        "type": "string",
        "required": true,
        "description": "Row struct, e.g. /Script/Module.MyRow or a UserDefinedStruct path"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Destination folder (default /Game/DataTables)"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "skip (default) returns an existing asset; error refuses"
      }
    ]
  },
  "create_folder": {
    "category": "asset",
    "params": [
      {
        "name": "path",
        "type": "string",
        "required": false,
        "description": "Content folder to create, e.g. /Game/Foo"
      },
      {
        "name": "paths",
        "type": "array",
        "required": false,
        "description": "Content folders to create; combined with path",
        "items": "string"
      }
    ],
    "choices": [
      {
        "mode": "atLeastOne",
        "branches": [
          [
            "path"
          ],
          [
            "paths"
          ]
        ]
      }
    ],
    "contractExempt": "Creates the folder the contract path names"
  },
  "create_interchange_pipeline": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": false,
        "description": "Object path of the new pipeline",
        "role": "editTarget"
      },
      {
        "name": "name",
        "type": "string",
        "required": false,
        "description": "Pipeline asset name, placed in packagePath"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Folder for name (default /Game/Import)"
      },
      {
        "name": "meshType",
        "type": "string",
        "required": false,
        "description": "skeletal (default) | static"
      },
      {
        "name": "options",
        "type": "object",
        "required": false,
        "description": "Dotted-path overrides on the new pipeline, e.g. {'MeshPipeline.bBuildNanite': true}"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "skip (default) returns an existing pipeline; error refuses"
      }
    ],
    "choices": [
      {
        "mode": "exactlyOne",
        "branches": [
          [
            "assetPath"
          ],
          [
            "name"
          ]
        ]
      }
    ],
    "contractExempt": "Creates and saves a pipeline asset from the contract path before anything fails"
  },
  "create_render_target_2d": {
    "category": "asset",
    "params": [
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "Asset name, without '/' or '.'"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Destination folder (default /Game)"
      },
      {
        "name": "width",
        "type": "integer",
        "required": false,
        "description": "Pixel width, 1-8192 (default 512)",
        "min": 1,
        "max": 8192
      },
      {
        "name": "height",
        "type": "integer",
        "required": false,
        "description": "Pixel height, 1-8192 (default 512)",
        "min": 1,
        "max": 8192
      },
      {
        "name": "format",
        "type": "string",
        "required": false,
        "description": "R8 | RG8 | RGBA8 | RGBA8_SRGB | R16F | RG16F | RGBA16F | R32F | RG32F | RGBA32F | RGB10A2 (default RGBA8_SRGB)"
      },
      {
        "name": "clearColor",
        "type": "object",
        "required": false,
        "description": "Linear clear color {r, g, b, a} (default transparent)"
      },
      {
        "name": "generateMips",
        "type": "boolean",
        "required": false,
        "description": "Generate mipmaps automatically (default false)"
      },
      {
        "name": "targetGamma",
        "type": "number",
        "required": false,
        "description": "Target gamma (default 0, the engine behavior)",
        "min": 0
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "skip (default) returns an existing asset; error refuses"
      }
    ]
  },
  "create_stringtable": {
    "category": "asset",
    "params": [
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "Asset name"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Destination folder (default /Game/StringTables)"
      },
      {
        "name": "namespace",
        "type": "string",
        "required": false,
        "description": "StringTable namespace"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "skip (default) returns an existing table; error refuses"
      }
    ],
    "contractExempt": "Creates and saves a StringTable from the contract name before anything fails"
  },
  "create_subobject": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Asset that owns the new subobject",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "className",
        "type": "string",
        "required": true,
        "description": "Class to instantiate, e.g. a /Script/Module.Class path"
      },
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "Subobject name"
      },
      {
        "name": "properties",
        "type": "object",
        "required": false,
        "description": "Property values, validated on a throwaway instance first"
      },
      {
        "name": "outer",
        "type": "string",
        "required": false,
        "description": "asset (default) | package"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "reuse (default) | error"
      },
      {
        "name": "save",
        "type": "boolean",
        "required": false,
        "description": "Save the owning package (default true)"
      }
    ]
  },
  "create_user_defined_enum": {
    "category": "asset",
    "params": [
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "Asset name"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Destination folder (default /Game)"
      },
      {
        "name": "values",
        "type": "array",
        "required": false,
        "description": "Initial enumerator display names",
        "items": "string"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "skip (default) returns an existing enum; error refuses"
      }
    ],
    "contractExempt": "Creates and saves an enum package from the contract name before anything fails"
  },
  "create_user_defined_struct": {
    "category": "asset",
    "params": [
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "Asset name"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Destination folder (default /Game)"
      },
      {
        "name": "structFields",
        "type": "array",
        "required": false,
        "description": "Initial members; with none the struct keeps its one default member",
        "items": "object",
        "fields": [
          {
            "name": "name",
            "type": "string",
            "required": true,
            "description": "Member display name"
          },
          {
            "name": "type",
            "type": "string",
            "required": false,
            "description": "MakePinType string: bool (default), int, int64, float, string, name, text, byte, a struct such as Vector, an enum, or an object class such as Actor"
          }
        ]
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "skip (default) returns an existing struct; error refuses"
      }
    ],
    "contractExempt": "Creates and saves a struct package from the contract name before anything fails"
  },
  "delete_asset": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Asset to delete",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "force",
        "type": "boolean",
        "required": false,
        "description": "Delete even when other packages reference it, closing open editors (default false)"
      }
    ]
  },
  "delete_asset_batch": {
    "category": "asset",
    "params": [
      {
        "name": "assetPaths",
        "type": "array",
        "required": true,
        "description": "Assets to delete",
        "aliases": [
          "paths"
        ],
        "items": "string",
        "role": "editTarget"
      },
      {
        "name": "force",
        "type": "boolean",
        "required": false,
        "description": "Delete referenced assets too, closing open editors (default false)"
      }
    ]
  },
  "delete_folder": {
    "category": "asset",
    "params": [
      {
        "name": "path",
        "type": "string",
        "required": false,
        "description": "Content folder to delete"
      },
      {
        "name": "paths",
        "type": "array",
        "required": false,
        "description": "Content folders to delete",
        "items": "string"
      },
      {
        "name": "force",
        "type": "boolean",
        "required": false,
        "description": "Also delete the assets inside (default false: only empty folders)"
      }
    ]
  },
  "diagnose_registry": {
    "category": "asset",
    "params": [
      {
        "name": "path",
        "type": "string",
        "required": true,
        "description": "Content path to diagnose"
      },
      {
        "name": "recursive",
        "type": "boolean",
        "required": false,
        "description": "Include subfolders (default true)"
      },
      {
        "name": "reconcile",
        "type": "boolean",
        "required": false,
        "description": "Force a synchronous rescan first, which evicts pending-kill ghosts"
      }
    ]
  },
  "diff_asset": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Blueprint, Skeleton or SkeletalMesh to diff from",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "otherPath",
        "type": "string",
        "required": true,
        "description": "Asset of the same class to compare against"
      }
    ]
  },
  "disconnect_graph_pins": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "EdGraph-backed asset path",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "graphName",
        "type": "string",
        "required": false,
        "description": "Graph to author in, by name or unique substring (required when the asset has several graphs)"
      },
      {
        "name": "sourceNode",
        "type": "string",
        "required": false,
        "description": "Node of the source pin: nodeGuid, node path, name or unique title"
      },
      {
        "name": "sourcePinId",
        "type": "string",
        "required": false,
        "description": "pinId of the source pin (preferred)"
      },
      {
        "name": "sourcePin",
        "type": "string",
        "required": false,
        "description": "Source pin name, when no pinId is given"
      },
      {
        "name": "sourcePinDirection",
        "type": "string",
        "required": false,
        "description": "input | output, to disambiguate sourcePin"
      },
      {
        "name": "targetNode",
        "type": "string",
        "required": false,
        "description": "Node of the target pin"
      },
      {
        "name": "targetPinId",
        "type": "string",
        "required": false,
        "description": "pinId of the target pin"
      },
      {
        "name": "targetPin",
        "type": "string",
        "required": false,
        "description": "Target pin name, when no pinId is given"
      },
      {
        "name": "targetPinDirection",
        "type": "string",
        "required": false,
        "description": "input | output, to disambiguate targetPin"
      },
      {
        "name": "save",
        "type": "boolean",
        "required": false,
        "description": "Save the asset after the edit (default true)"
      }
    ]
  },
  "duplicate_asset": {
    "category": "asset",
    "params": [
      {
        "name": "sourcePath",
        "type": "string",
        "required": true,
        "description": "Asset to duplicate"
      },
      {
        "name": "destinationPath",
        "type": "string",
        "required": true,
        "description": "Object path of the copy",
        "role": "editTarget"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "skip (default) returns an existing destination; error refuses"
      }
    ]
  },
  "edit_user_defined_enum": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "UserDefinedEnum asset path",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "op",
        "type": "string",
        "required": true,
        "description": "add_value | rename_value | remove_value"
      },
      {
        "name": "displayName",
        "type": "string",
        "required": false,
        "description": "Display text for the enumerator (add_value, rename_value)"
      },
      {
        "name": "name",
        "type": "string",
        "required": false,
        "description": "Enumerator to act on by short or display name; add_value uses it as the display name when displayName is omitted"
      },
      {
        "name": "index",
        "type": "number",
        "required": false,
        "description": "Enumerator index for rename_value and remove_value"
      }
    ]
  },
  "edit_user_defined_struct": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "UserDefinedStruct asset path",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "op",
        "type": "string",
        "required": true,
        "description": "add_field | rename_field | set_field_type | remove_field"
      },
      {
        "name": "fieldName",
        "type": "string",
        "required": false,
        "description": "Member by friendly or internal name; add_field names the new member with it"
      },
      {
        "name": "fieldGuid",
        "type": "string",
        "required": false,
        "description": "Member by GUID, stable across renames; wins over fieldName"
      },
      {
        "name": "newDisplayName",
        "type": "string",
        "required": false,
        "description": "New display name for rename_field; add_field falls back to it for the name"
      },
      {
        "name": "type",
        "type": "string",
        "required": false,
        "description": "MakePinType string for add_field (default bool) and set_field_type"
      }
    ]
  },
  "export_asset": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Asset to export",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "outputPath",
        "type": "string",
        "required": true,
        "description": "File to write; a relative path resolves against the project directory",
        "role": "outputPath"
      }
    ]
  },
  "export_texture": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Texture2D asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "outputPath",
        "type": "string",
        "required": true,
        "description": "PNG file to write; a relative path resolves against the project directory",
        "aliases": [
          "filePath"
        ],
        "role": "outputPath"
      }
    ]
  },
  "export_uv_layout": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StaticMesh or SkeletalMesh asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "lodIndex",
        "type": "integer",
        "required": false,
        "description": "Source LOD to act on (default 0)",
        "min": 0
      },
      {
        "name": "channel",
        "type": "number",
        "required": false,
        "description": "Channel to draw (default 0)"
      },
      {
        "name": "outputPath",
        "type": "string",
        "required": false,
        "description": "PNG to write (default under Saved/UVLayouts)",
        "role": "outputPath"
      },
      {
        "name": "imageSize",
        "type": "number",
        "required": false,
        "description": "PNG edge length (default 1024, max 4096)"
      },
      {
        "name": "showIslands",
        "type": "boolean",
        "required": false,
        "description": "Colour each island separately (default true)"
      },
      {
        "name": "showOverlaps",
        "type": "boolean",
        "required": false,
        "description": "Highlight overlapping texels (default true)"
      },
      {
        "name": "showGrid",
        "type": "boolean",
        "required": false,
        "description": "Draw the unit-square border (default true)"
      }
    ]
  },
  "fill_datatable_from_json": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "DataTable asset path",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "rows",
        "type": "object",
        "required": false,
        "description": "Rows to upsert: {rowName: {field: value}}"
      },
      {
        "name": "jsonString",
        "type": "string",
        "required": false,
        "description": "The same rows object as JSON text, used when rows is omitted"
      }
    ]
  },
  "fix_asset_hygiene": {
    "category": "asset",
    "params": [
      {
        "name": "fix",
        "type": "string",
        "required": true,
        "description": "naming (rename to the convention) | unreferenced (quarantine or delete)"
      },
      {
        "name": "assetPaths",
        "type": "array",
        "required": false,
        "description": "The exact assets to act on, the safest way to drive it",
        "items": "string",
        "role": "editTarget"
      },
      {
        "name": "directory",
        "type": "string",
        "required": false,
        "description": "Content path to sweep, added to directories (default /Game)"
      },
      {
        "name": "directories",
        "type": "array",
        "required": false,
        "description": "Content paths to sweep, mount-rooted such as /Game/Characters (default /Game)",
        "items": "string"
      },
      {
        "name": "recursive",
        "type": "boolean",
        "required": false,
        "description": "Include subfolders (default true)"
      },
      {
        "name": "maxAssets",
        "type": "integer",
        "required": false,
        "description": "Assets the sweep walks before it reports scanTruncated (default 20000, max 200000)"
      },
      {
        "name": "classNames",
        "type": "array",
        "required": false,
        "description": "Only these asset class names",
        "items": "string"
      },
      {
        "name": "excludePaths",
        "type": "array",
        "required": false,
        "description": "Skip packages whose name starts with one of these",
        "items": "string"
      },
      {
        "name": "keepPaths",
        "type": "array",
        "required": false,
        "description": "Never report or touch packages whose name starts with one of these, such as a folder loaded by name from config",
        "items": "string"
      },
      {
        "name": "namingRules",
        "type": "array",
        "required": false,
        "description": "Naming convention entries",
        "items": "object",
        "fields": [
          {
            "name": "class",
            "type": "string",
            "required": true,
            "description": "Asset class name the registry reports, such as StaticMesh or WidgetBlueprint, not a class path"
          },
          {
            "name": "prefix",
            "type": "string",
            "required": false,
            "description": "Name prefix the class requires"
          },
          {
            "name": "suffix",
            "type": "string",
            "required": false,
            "description": "Name suffix the class requires"
          }
        ]
      },
      {
        "name": "namingRuleMode",
        "type": "string",
        "required": false,
        "description": "merge (default, the caller's rules over the built-in table) | replace"
      },
      {
        "name": "unreferencedAction",
        "type": "string",
        "required": false,
        "description": "quarantine (default, undoable) | delete (permanent, no rollback)"
      },
      {
        "name": "quarantineFolder",
        "type": "string",
        "required": false,
        "description": "Where quarantined assets move, keeping their folder shape (default /Game/Quarantine)"
      },
      {
        "name": "ignoreRedirectorReferencers",
        "type": "boolean",
        "required": false,
        "description": "Do not count a redirector stub as a referencer (default true)"
      },
      {
        "name": "maxFixes",
        "type": "integer",
        "required": false,
        "description": "Refuse the call past this many assets (default 100)"
      },
      {
        "name": "continueOnError",
        "type": "boolean",
        "required": false,
        "description": "Apply the candidates that passed preflight instead of aborting (default false)"
      },
      {
        "name": "save",
        "type": "boolean",
        "required": false,
        "description": "Save what changed (default true)"
      },
      {
        "name": "dryRun",
        "type": "boolean",
        "required": false,
        "description": "Name every asset and destination without changing anything (default TRUE)"
      }
    ]
  },
  "fixup_redirectors": {
    "category": "asset",
    "params": [
      {
        "name": "paths",
        "type": "array",
        "required": true,
        "description": "Redirector packages, or the folders holding them",
        "items": "string",
        "role": "editTarget"
      },
      {
        "name": "dryRun",
        "type": "boolean",
        "required": false,
        "description": "Report the redirectors, referencers and packages it would load and save, and stop (default false)"
      },
      {
        "name": "save",
        "type": "boolean",
        "required": false,
        "description": "Run the explicit save pass (default true); the editor's own fix-up writes what it can regardless"
      },
      {
        "name": "allowProjectWide",
        "type": "boolean",
        "required": false,
        "description": "Permit a path naming a whole content root (default false)"
      }
    ]
  },
  "force_reload_asset": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Asset to reload from disk",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "discardUnsaved",
        "type": "boolean",
        "required": false,
        "description": "Reload even when the package has unsaved changes, discarding them (default false)"
      }
    ]
  },
  "generate_lightmap_uvs": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StaticMesh asset path",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "lodIndex",
        "type": "integer",
        "required": false,
        "description": "Source LOD to act on (default 0)",
        "min": 0
      },
      {
        "name": "enable",
        "type": "boolean",
        "required": false,
        "description": "Turn lightmap UV generation on (default) or off"
      },
      {
        "name": "sourceChannel",
        "type": "number",
        "required": false,
        "description": "Channel the generator reads (default: the current setting)"
      },
      {
        "name": "destinationChannel",
        "type": "number",
        "required": false,
        "description": "Channel it writes (default: the current setting, or the next free channel)"
      },
      {
        "name": "minLightmapResolution",
        "type": "number",
        "required": false,
        "description": "Packing resolution floor (default 64)"
      },
      {
        "name": "lightmapResolution",
        "type": "number",
        "required": false,
        "description": "The mesh's lightmap resolution"
      },
      {
        "name": "setLightmapCoordinateIndex",
        "type": "boolean",
        "required": false,
        "description": "Point LightMapCoordinateIndex at the destination channel (default true)"
      },
      {
        "name": "force",
        "type": "boolean",
        "required": false,
        "description": "Rebuild even when the settings already match"
      },
      {
        "name": "save",
        "type": "boolean",
        "required": false,
        "description": "Save the mesh (default true)"
      },
      {
        "name": "dryRun",
        "type": "boolean",
        "required": false,
        "description": "Report without writing"
      },
      {
        "name": "rasterSize",
        "type": "number",
        "required": false,
        "description": "Raster resolution for the result's overlap estimate (default 512)"
      }
    ]
  },
  "generate_mesh_collision": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StaticMesh whose collision to build or clear",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "op",
        "type": "string",
        "required": false,
        "description": "generate (default) | clear"
      },
      {
        "name": "method",
        "type": "string",
        "required": false,
        "description": "AlignedBoxes | OrientedBoxes | MinimalSpheres | Capsules | ConvexHulls (default) | SweptHulls | MinVolumeShapes | LevelSets"
      },
      {
        "name": "maxConvexHulls",
        "type": "integer",
        "required": false,
        "description": "Convex hulls the decomposition may produce (default 1)"
      },
      {
        "name": "hullTargetFaceCount",
        "type": "integer",
        "required": false,
        "description": "Faces to simplify each hull down to (default 25)"
      },
      {
        "name": "maxShapeCount",
        "type": "integer",
        "required": false,
        "description": "Cap on the shapes produced (default 0, uncapped)"
      },
      {
        "name": "minThickness",
        "type": "number",
        "required": false,
        "description": "Thinnest a shape may be, in centimetres (default 1)"
      },
      {
        "name": "autoDetectSpheres",
        "type": "boolean",
        "required": false,
        "description": "Use a sphere where one fits (default true)"
      },
      {
        "name": "autoDetectBoxes",
        "type": "boolean",
        "required": false,
        "description": "Use a box where one fits (default true)"
      },
      {
        "name": "autoDetectCapsules",
        "type": "boolean",
        "required": false,
        "description": "Use a capsule where one fits (default true)"
      },
      {
        "name": "simplifyHulls",
        "type": "boolean",
        "required": false,
        "description": "Simplify each hull down to hullTargetFaceCount (default true)"
      },
      {
        "name": "removeFullyContainedShapes",
        "type": "boolean",
        "required": false,
        "description": "Drop shapes entirely inside another (default true)"
      },
      {
        "name": "decompositionErrorTolerance",
        "type": "number",
        "required": false,
        "description": "Volume error the decomposition may accept before splitting further (default 0)"
      },
      {
        "name": "decompositionSearchFactor",
        "type": "number",
        "required": false,
        "description": "0 to 1, how hard the decomposition searches for a better split (default 0.5)"
      },
      {
        "name": "sweptHullAxis",
        "type": "string",
        "required": false,
        "description": "X | Y | Z (default) | SmallestBoxDimension | SmallestVolume (method SweptHulls)"
      },
      {
        "name": "markAsCustomized",
        "type": "boolean",
        "required": false,
        "description": "Mark the collision customized so a reimport keeps it (default true)"
      },
      {
        "name": "lodType",
        "type": "string",
        "required": false,
        "description": "MaxAvailable (default, ignores lodIndex) | HiResSourceModel | SourceModel | RenderData"
      },
      {
        "name": "lodIndex",
        "type": "integer",
        "required": false,
        "description": "LOD to read when lodType names one (default 0)",
        "min": 0
      },
      {
        "name": "save",
        "type": "boolean",
        "required": false,
        "description": "Save the written asset (default true)"
      },
      {
        "name": "dryRun",
        "type": "boolean",
        "required": false,
        "description": "Run the operation and report the result without writing (default false)"
      }
    ]
  },
  "get_asset_dependencies": {
    "category": "asset",
    "params": [
      {
        "name": "packages",
        "type": "array",
        "required": false,
        "description": "Package paths to look up",
        "items": "string"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "One package path, used when packages is omitted"
      },
      {
        "name": "hard",
        "type": "boolean",
        "required": false,
        "description": "Include hard dependencies (default true)"
      },
      {
        "name": "soft",
        "type": "boolean",
        "required": false,
        "description": "Include soft dependencies (default true)"
      }
    ]
  },
  "get_asset_properties": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Asset path; a Blueprint path reads its generated-class CDO",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "propertyName",
        "type": "string",
        "required": false,
        "description": "One property to read; dotted and indexed paths walk structs, arrays and instanced subobjects"
      },
      {
        "name": "includeValues",
        "type": "boolean",
        "required": false,
        "description": "Include each property's value (default true)"
      },
      {
        "name": "valueFormat",
        "type": "string",
        "required": false,
        "description": "text (default, Unreal export text) | json (structured values)"
      },
      {
        "name": "expandDepth",
        "type": "integer",
        "required": false,
        "description": "Inline the properties of subobjects the asset owns to this depth, 0-5 (default 0)",
        "min": 0,
        "max": 5
      },
      {
        "name": "expandExternal",
        "type": "boolean",
        "required": false,
        "description": "Also follow references to other assets when expanding (default false)"
      },
      {
        "name": "maxExpandedObjects",
        "type": "integer",
        "required": false,
        "description": "Cap on expanded objects (default 64)",
        "min": 1
      }
    ]
  },
  "get_asset_referencers": {
    "category": "asset",
    "params": [
      {
        "name": "packages",
        "type": "array",
        "required": false,
        "description": "Package paths to look up",
        "items": "string"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "One package path, used when packages is omitted"
      }
    ]
  },
  "get_curvetable_keys": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "CurveTable asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "rowName",
        "type": "string",
        "required": true,
        "description": "Row to read"
      }
    ]
  },
  "get_datatable_row": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "DataTable asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "rowName",
        "type": "string",
        "required": true,
        "description": "Row to read"
      }
    ]
  },
  "get_mesh_bounds": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StaticMesh or SkeletalMesh asset path"
      }
    ]
  },
  "get_mesh_collision": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StaticMesh asset path"
      }
    ]
  },
  "get_mesh_geometry": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StaticMesh or SkeletalMesh asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "lodIndex",
        "type": "integer",
        "required": false,
        "description": "LOD to read (default 0)",
        "min": 0
      },
      {
        "name": "sectionIndex",
        "type": "integer",
        "required": false,
        "description": "One render section (omit for every section)",
        "min": 0
      },
      {
        "name": "include",
        "type": "array",
        "required": false,
        "description": "positions | uvs | normals | triangles (omit for all four)",
        "items": "string"
      },
      {
        "name": "uvChannel",
        "type": "integer",
        "required": false,
        "description": "UV channel to return (default 0)",
        "min": 0
      },
      {
        "name": "dumpToFile",
        "type": "boolean",
        "required": false,
        "description": "Write the geometry JSON to a file instead of returning it"
      },
      {
        "name": "outputPath",
        "type": "string",
        "required": false,
        "description": "File for dumpToFile; relative paths resolve under Saved/"
      }
    ]
  },
  "get_mesh_info": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StaticMesh or SkeletalMesh asset path"
      }
    ]
  },
  "get_primary_asset_ids": {
    "category": "asset",
    "params": [
      {
        "name": "type",
        "type": "string",
        "required": false,
        "description": "FPrimaryAssetType to list (omit for every type)"
      },
      {
        "name": "maxResults",
        "type": "integer",
        "required": false,
        "description": "Most ids to return (default 1000)"
      }
    ]
  },
  "get_stringtable_entry": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StringTable asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "key",
        "type": "string",
        "required": true,
        "description": "Entry key"
      }
    ]
  },
  "get_texture_info": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Texture2D asset path",
        "aliases": [
          "path"
        ]
      }
    ]
  },
  "import_animation": {
    "category": "asset",
    "params": [
      {
        "name": "filePath",
        "type": "string",
        "required": true,
        "description": "Source file on disk",
        "aliases": [
          "filename"
        ]
      },
      {
        "name": "name",
        "type": "string",
        "required": false,
        "description": "Asset name (default: the file name)",
        "aliases": [
          "assetName"
        ]
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Destination folder (default /Game/Animations)",
        "aliases": [
          "destinationPath"
        ]
      },
      {
        "name": "skeletonPath",
        "type": "string",
        "required": true,
        "description": "Skeleton the animation targets"
      },
      {
        "name": "importCustomAttribute",
        "type": "boolean",
        "required": false,
        "description": "Import FBX custom attributes as curves (default true)"
      },
      {
        "name": "removeRedundantKeys",
        "type": "boolean",
        "required": false,
        "description": "Strip keys that do not change the value (default true)"
      },
      {
        "name": "importSettings",
        "type": "object",
        "required": false,
        "description": "FbxAnimSequenceImportData or FbxImportUI fields by UPROPERTY name or dotted path"
      }
    ]
  },
  "import_curvetable": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "CurveTable asset path",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "jsonString",
        "type": "string",
        "required": false,
        "description": "JSON rows to replace the table from"
      },
      {
        "name": "csvString",
        "type": "string",
        "required": false,
        "description": "CSV rows to replace the table from"
      },
      {
        "name": "filePath",
        "type": "string",
        "required": false,
        "description": "JSON or CSV file to replace the table from",
        "aliases": [
          "jsonPath",
          "csvPath"
        ]
      },
      {
        "name": "format",
        "type": "string",
        "required": false,
        "description": "json | csv (default: from the file extension)"
      },
      {
        "name": "interpMode",
        "type": "string",
        "required": false,
        "description": "linear (default) | constant | cubic | none"
      }
    ]
  },
  "import_file": {
    "category": "asset",
    "params": [
      {
        "name": "filePath",
        "type": "string",
        "required": true,
        "description": "Source file on disk",
        "aliases": [
          "filename"
        ]
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": true,
        "description": "Destination folder",
        "aliases": [
          "destinationPath"
        ]
      },
      {
        "name": "name",
        "type": "string",
        "required": false,
        "description": "Asset name (default: the file name)",
        "aliases": [
          "assetName"
        ]
      },
      {
        "name": "factoryClass",
        "type": "string",
        "required": false,
        "description": "UFactory subclass by name or /Script path (default: picked by extension)"
      },
      {
        "name": "factoryProperties",
        "type": "object",
        "required": false,
        "description": "Factory properties by UPROPERTY name or dotted path, set before the import"
      },
      {
        "name": "replaceExisting",
        "type": "boolean",
        "required": false,
        "description": "Replace an asset already at the destination (default true)"
      },
      {
        "name": "save",
        "type": "boolean",
        "required": false,
        "description": "Save the imported assets (default false)"
      },
      {
        "name": "automated",
        "type": "boolean",
        "required": false,
        "description": "Suppress interactive dialogs (default true)"
      }
    ]
  },
  "import_skeletal_mesh": {
    "category": "asset",
    "params": [
      {
        "name": "filePath",
        "type": "string",
        "required": true,
        "description": "Source file on disk",
        "aliases": [
          "filename"
        ]
      },
      {
        "name": "name",
        "type": "string",
        "required": false,
        "description": "Asset name (default: the file name)",
        "aliases": [
          "assetName"
        ]
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Destination folder (default /Game/Meshes)",
        "aliases": [
          "destinationPath"
        ]
      },
      {
        "name": "skeletonPath",
        "type": "string",
        "required": false,
        "description": "Existing Skeleton to import onto"
      },
      {
        "name": "importMaterials",
        "type": "boolean",
        "required": false,
        "description": "Import materials (default true)"
      },
      {
        "name": "importTextures",
        "type": "boolean",
        "required": false,
        "description": "Import textures (default true)"
      },
      {
        "name": "importUniformScale",
        "type": "number",
        "required": false,
        "description": "Uniform import scale (default 1.0); 100 fixes metre-authored FBX"
      },
      {
        "name": "importMorphTargets",
        "type": "boolean",
        "required": false,
        "description": "Import morph targets (default true)"
      },
      {
        "name": "createPhysicsAsset",
        "type": "boolean",
        "required": false,
        "description": "Create a PhysicsAsset (default false)"
      },
      {
        "name": "replaceExisting",
        "type": "boolean",
        "required": false,
        "description": "Replace an asset already at the destination (default true)"
      }
    ]
  },
  "import_static_mesh": {
    "category": "asset",
    "params": [
      {
        "name": "filePath",
        "type": "string",
        "required": true,
        "description": "Source file on disk",
        "aliases": [
          "filename"
        ]
      },
      {
        "name": "name",
        "type": "string",
        "required": false,
        "description": "Asset name (default: the file name)",
        "aliases": [
          "assetName"
        ]
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Destination folder (default /Game/Meshes)",
        "aliases": [
          "destinationPath"
        ]
      },
      {
        "name": "combineMeshes",
        "type": "boolean",
        "required": false,
        "description": "Combine every mesh in the file into one (default false)"
      },
      {
        "name": "importMaterials",
        "type": "boolean",
        "required": false,
        "description": "Import materials (default true)"
      },
      {
        "name": "importTextures",
        "type": "boolean",
        "required": false,
        "description": "Import textures (default true)"
      },
      {
        "name": "generateLightmapUVs",
        "type": "boolean",
        "required": false,
        "description": "Generate lightmap UVs (default true)"
      },
      {
        "name": "importUniformScale",
        "type": "number",
        "required": false,
        "description": "Uniform import scale; 100 fixes metre-authored FBX"
      }
    ]
  },
  "import_stringtable": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StringTable asset path",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "filePath",
        "type": "string",
        "required": true,
        "description": "CSV file to merge into the table",
        "aliases": [
          "filename",
          "csvPath"
        ]
      }
    ]
  },
  "import_stringtable_csv": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StringTable asset path",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "csvPath",
        "type": "string",
        "required": true,
        "description": "CSV file; a relative path resolves against the project directory",
        "aliases": [
          "filePath"
        ]
      },
      {
        "name": "expectedKeys",
        "type": "array",
        "required": false,
        "description": "Keys the CSV must carry, checked before the asset is touched",
        "items": "string"
      },
      {
        "name": "requireExactKeys",
        "type": "boolean",
        "required": false,
        "description": "Also fail when the CSV carries keys expectedKeys does not list (default false)"
      },
      {
        "name": "replaceExisting",
        "type": "boolean",
        "required": false,
        "description": "Remove entries the CSV does not carry (default false)"
      },
      {
        "name": "save",
        "type": "boolean",
        "required": false,
        "description": "Save the table (default true)"
      }
    ]
  },
  "import_texture": {
    "category": "asset",
    "params": [
      {
        "name": "filePath",
        "type": "string",
        "required": true,
        "description": "Source file on disk",
        "aliases": [
          "filename"
        ]
      },
      {
        "name": "name",
        "type": "string",
        "required": false,
        "description": "Asset name (default: the file name)",
        "aliases": [
          "assetName"
        ]
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Destination folder (default /Game/Textures)",
        "aliases": [
          "destinationPath"
        ]
      },
      {
        "name": "sRGB",
        "type": "boolean",
        "required": false,
        "description": "Applied to the imported texture"
      },
      {
        "name": "compressionSettings",
        "type": "string",
        "required": false,
        "description": "Compression setting such as Default, Normalmap, Grayscale, HDR or BC7, applied to the imported texture"
      },
      {
        "name": "lodGroup",
        "type": "string",
        "required": false,
        "description": "Texture LOD group, applied to the imported texture"
      },
      {
        "name": "neverStream",
        "type": "boolean",
        "required": false,
        "description": "Applied to the imported texture"
      },
      {
        "name": "noCompression",
        "type": "boolean",
        "required": false,
        "description": "Factory option: import uncompressed"
      },
      {
        "name": "noAlpha",
        "type": "boolean",
        "required": false,
        "description": "Factory option: drop the alpha channel"
      }
    ]
  },
  "import_texture_batch": {
    "category": "asset",
    "params": [
      {
        "name": "items",
        "type": "array",
        "required": true,
        "description": "Textures to import: [{filePath, packagePath?, name?, replaceExisting?}]",
        "items": "object",
        "minItems": 1,
        "maxItems": 500
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Folder for items that name none (default /Game/Textures)"
      },
      {
        "name": "save",
        "type": "boolean",
        "required": false,
        "description": "Save the imported textures (default true)"
      },
      {
        "name": "automated",
        "type": "boolean",
        "required": false,
        "description": "Suppress interactive dialogs (default true)"
      }
    ],
    "contractExempt": "Runs an import batch under the contract values; an empty batch is not refused"
  },
  "list_asset_sockets": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StaticMesh, SkeletalMesh or Skeleton asset path"
      }
    ]
  },
  "list_assets": {
    "category": "asset",
    "params": [
      {
        "name": "directory",
        "type": "string",
        "required": false,
        "description": "Content path to list (default /Game)"
      },
      {
        "name": "classFilter",
        "type": "string",
        "required": false,
        "description": "Asset class name, exact or substring",
        "aliases": [
          "typeFilter"
        ]
      },
      {
        "name": "recursive",
        "type": "boolean",
        "required": false,
        "description": "Include subfolders (default true)"
      },
      {
        "name": "cursor",
        "type": "string",
        "required": false,
        "description": "Resume a paged read: pass back the nextCursor from the previous page, unmodified"
      },
      {
        "name": "limit",
        "type": "integer",
        "required": false,
        "description": "Rows per page, 1 to 5000 (default 500)",
        "aliases": [
          "maxResults"
        ]
      },
      {
        "name": "offset",
        "type": "number",
        "required": false,
        "description": "Refused: the row offset was replaced by cursor paging, because a row number cannot report that the folder changed. Use cursor and limit"
      }
    ]
  },
  "list_curvetable_rows": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "CurveTable asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "rowFilter",
        "type": "string",
        "required": false,
        "description": "Case-insensitive substring filter on row names"
      }
    ]
  },
  "list_enum_values": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "UEnum or UserDefinedEnum asset path",
        "aliases": [
          "path"
        ]
      }
    ]
  },
  "list_locks": {
    "category": "asset",
    "params": []
  },
  "list_skeleton_bones": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "SkeletalMesh or Skeleton asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "includeTransforms",
        "type": "boolean",
        "required": false,
        "description": "Include rest-pose transforms (default true)"
      }
    ]
  },
  "list_sockets": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StaticMesh, SkeletalMesh or Skeleton asset path"
      }
    ]
  },
  "list_stringtable_keys": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StringTable asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "keyFilter",
        "type": "string",
        "required": false,
        "description": "Case-insensitive substring filter on keys"
      }
    ]
  },
  "list_struct_fields": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "UserDefinedStruct asset path",
        "aliases": [
          "path"
        ]
      }
    ]
  },
  "list_textures": {
    "category": "asset",
    "params": [
      {
        "name": "directory",
        "type": "string",
        "required": false,
        "description": "Only textures whose object path starts with this (default /Game/)"
      },
      {
        "name": "cursor",
        "type": "string",
        "required": false,
        "description": "Resume a paged read: pass back the nextCursor from the previous page, unmodified"
      },
      {
        "name": "limit",
        "type": "integer",
        "required": false,
        "description": "Rows per page, 1 to 2000 (default 50)",
        "aliases": [
          "maxResults"
        ]
      }
    ]
  },
  "measure_mesh_geometry": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StaticMesh or SkeletalMesh asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "lodIndex",
        "type": "integer",
        "required": false,
        "description": "LOD to measure (default 0)",
        "min": 0
      },
      {
        "name": "sectionIndex",
        "type": "integer",
        "required": false,
        "description": "One render section (omit to measure the whole LOD)",
        "min": 0
      }
    ]
  },
  "mesh_boolean": {
    "category": "asset",
    "params": [
      {
        "name": "operation",
        "type": "string",
        "required": true,
        "description": "union | subtract | intersect | trimInside | trimOutside | newPolyGroupInside | newPolyGroupOutside"
      },
      {
        "name": "targetPath",
        "type": "string",
        "required": true,
        "description": "StaticMesh being cut; the result inherits its transform, materials, collision and Nanite setting",
        "role": "editTarget"
      },
      {
        "name": "toolPath",
        "type": "string",
        "required": true,
        "description": "StaticMesh doing the cutting"
      },
      {
        "name": "outputPath",
        "type": "string",
        "required": false,
        "description": "Asset to write (default '<targetPath>_<Operation>')",
        "role": "editTarget"
      },
      {
        "name": "inPlace",
        "type": "boolean",
        "required": false,
        "description": "Overwrite targetPath instead (default false); not with outputPath, and the only destructive form"
      },
      {
        "name": "targetTransform",
        "type": "object",
        "required": false,
        "description": "Where the target sits for the boolean (default identity)",
        "fields": [
          {
            "name": "location",
            "type": "vec3",
            "required": false,
            "description": "Translation"
          },
          {
            "name": "rotation",
            "type": "rotator",
            "required": false,
            "description": "Rotation"
          },
          {
            "name": "scale",
            "type": "vec3",
            "required": false,
            "description": "Scale (default 1)"
          }
        ]
      },
      {
        "name": "toolTransform",
        "type": "object",
        "required": false,
        "description": "Where the tool sits for the boolean (default identity)",
        "fields": [
          {
            "name": "location",
            "type": "vec3",
            "required": false,
            "description": "Translation"
          },
          {
            "name": "rotation",
            "type": "rotator",
            "required": false,
            "description": "Rotation"
          },
          {
            "name": "scale",
            "type": "vec3",
            "required": false,
            "description": "Scale (default 1)"
          }
        ]
      },
      {
        "name": "lodType",
        "type": "string",
        "required": false,
        "description": "MaxAvailable (default, ignores lodIndex) | HiResSourceModel | SourceModel | RenderData"
      },
      {
        "name": "lodIndex",
        "type": "integer",
        "required": false,
        "description": "LOD of each input to read when lodType names one (default 0)",
        "min": 0
      },
      {
        "name": "fillHoles",
        "type": "boolean",
        "required": false,
        "description": "Close the holes the cut opens (default true)"
      },
      {
        "name": "simplifyOutput",
        "type": "boolean",
        "required": false,
        "description": "Collapse coplanar triangles the boolean introduced (default true)"
      },
      {
        "name": "simplifyPlanarTolerance",
        "type": "number",
        "required": false,
        "description": "How far from coplanar still counts as coplanar when simplifying (default 0.01)"
      },
      {
        "name": "allowEmptyResult",
        "type": "boolean",
        "required": false,
        "description": "Accept a result with no triangles (default false)"
      },
      {
        "name": "recomputeNormals",
        "type": "boolean",
        "required": false,
        "description": "Recompute normals on the written mesh (default false)"
      },
      {
        "name": "recomputeTangents",
        "type": "boolean",
        "required": false,
        "description": "Recompute tangents on the written mesh (default false)"
      },
      {
        "name": "removeDegenerates",
        "type": "boolean",
        "required": false,
        "description": "Drop degenerate triangles when writing back into an existing mesh (default false)"
      },
      {
        "name": "copyCollisionFromTarget",
        "type": "boolean",
        "required": false,
        "description": "Copy the target's simple collision onto the result (default true)"
      },
      {
        "name": "copyMaterialsFromTarget",
        "type": "boolean",
        "required": false,
        "description": "Copy the target's material slots onto the result (default true)"
      },
      {
        "name": "nanite",
        "type": "string",
        "required": false,
        "description": "inherit (default, match the target) | enable | disable"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "When outputPath exists: error (default) | replace"
      },
      {
        "name": "dryRun",
        "type": "boolean",
        "required": false,
        "description": "Run the boolean and report the counts without writing (default false)"
      },
      {
        "name": "save",
        "type": "boolean",
        "required": false,
        "description": "Save the written asset (default true)"
      }
    ]
  },
  "migrate": {
    "category": "asset",
    "params": [
      {
        "name": "destinationContentDir",
        "type": "string",
        "required": true,
        "description": "The Content folder of the TARGET project, not this one"
      },
      {
        "name": "assetPaths",
        "type": "array",
        "required": false,
        "description": "Assets to migrate",
        "items": "string"
      },
      {
        "name": "assetPath",
        "type": "string",
        "required": false,
        "description": "One asset to migrate"
      },
      {
        "name": "allowDirty",
        "type": "boolean",
        "required": false,
        "description": "Migrate the on-disk version of an asset with unsaved edits (default false)"
      },
      {
        "name": "includeDependencies",
        "type": "boolean",
        "required": false,
        "description": "Also copy the assets they reference (default true)"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "skip (default) | overwrite, for a file already at the destination"
      },
      {
        "name": "dryRun",
        "type": "boolean",
        "required": false,
        "description": "Report what would be copied without copying (default false)"
      }
    ],
    "choices": [
      {
        "mode": "atLeastOne",
        "branches": [
          [
            "assetPaths"
          ],
          [
            "assetPath"
          ]
        ]
      }
    ]
  },
  "move_asset": {
    "category": "asset",
    "params": [
      {
        "name": "sourcePath",
        "type": "string",
        "required": false,
        "description": "Asset to rename, together with destinationPath",
        "role": "editTarget"
      },
      {
        "name": "destinationPath",
        "type": "string",
        "required": false,
        "description": "New object path, together with sourcePath",
        "role": "editTarget"
      },
      {
        "name": "assetPath",
        "type": "string",
        "required": false,
        "description": "Asset to rename in its own folder, together with newName",
        "role": "editTarget"
      },
      {
        "name": "newName",
        "type": "string",
        "required": false,
        "description": "New asset name, together with assetPath"
      },
      {
        "name": "force",
        "type": "boolean",
        "required": false,
        "description": "World renames only: merge into a destination that already holds external packages (used by rollback)"
      }
    ]
  },
  "move_folder": {
    "category": "asset",
    "params": [
      {
        "name": "sourcePath",
        "type": "string",
        "required": true,
        "description": "Content folder to move"
      },
      {
        "name": "destinationPath",
        "type": "string",
        "required": true,
        "description": "Content folder to move it to"
      }
    ]
  },
  "read_asset": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Asset path",
        "aliases": [
          "path"
        ]
      }
    ]
  },
  "read_asset_graph": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "EdGraph-backed asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "graphName",
        "type": "string",
        "required": false,
        "description": "Only graphs whose name contains this"
      },
      {
        "name": "includePins",
        "type": "boolean",
        "required": false,
        "description": "Include each node's pins and links (default true)"
      },
      {
        "name": "maxNodes",
        "type": "number",
        "required": false,
        "description": "Nodes reported per graph (default 500, max 5000)"
      }
    ]
  },
  "read_asset_properties": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Asset path; a Blueprint path reads its generated-class CDO",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "propertyName",
        "type": "string",
        "required": false,
        "description": "One property to read; dotted and indexed paths walk structs, arrays and instanced subobjects"
      },
      {
        "name": "includeValues",
        "type": "boolean",
        "required": false,
        "description": "Include each property's value (default false)"
      },
      {
        "name": "valueFormat",
        "type": "string",
        "required": false,
        "description": "text (default, Unreal export text) | json (structured values)"
      },
      {
        "name": "expandDepth",
        "type": "integer",
        "required": false,
        "description": "Inline the properties of subobjects the asset owns to this depth, 0-5 (default 0)",
        "min": 0,
        "max": 5
      },
      {
        "name": "expandExternal",
        "type": "boolean",
        "required": false,
        "description": "Also follow references to other assets when expanding (default false)"
      },
      {
        "name": "maxExpandedObjects",
        "type": "integer",
        "required": false,
        "description": "Cap on expanded objects (default 64)",
        "min": 1
      }
    ]
  },
  "read_cloth_data": {
    "category": "asset",
    "params": [
      {
        "name": "skeletalMeshPath",
        "type": "string",
        "required": true,
        "description": "SkeletalMesh asset path",
        "aliases": [
          "assetPath"
        ]
      }
    ]
  },
  "read_curvetable": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "CurveTable asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "rowFilter",
        "type": "string",
        "required": false,
        "description": "Case-insensitive substring filter on row names"
      }
    ]
  },
  "read_datatable": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "DataTable asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "rowFilter",
        "type": "string",
        "required": false,
        "description": "Case-insensitive substring filter on row names"
      },
      {
        "name": "outputPath",
        "type": "string",
        "required": false,
        "description": "Write the rows to this JSON file instead of returning them; relative paths resolve under Saved/"
      }
    ]
  },
  "read_import_sources": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Imported asset path",
        "aliases": [
          "path"
        ]
      }
    ]
  },
  "read_skeletal_mesh_build_settings": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "SkeletalMesh asset path"
      },
      {
        "name": "lodIndex",
        "type": "integer",
        "required": false,
        "description": "LOD to target (default 0); not with allLods",
        "min": 0
      },
      {
        "name": "allLods",
        "type": "boolean",
        "required": false,
        "description": "Target every LOD instead of one; not with lodIndex"
      }
    ]
  },
  "read_skeletal_mesh_skin_weights": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "SkeletalMesh asset path"
      },
      {
        "name": "vertexIndices",
        "type": "array",
        "required": true,
        "description": "Source MeshDescription vertex IDs to read (1-256)",
        "items": "integer",
        "min": 0,
        "minItems": 1,
        "maxItems": 256
      },
      {
        "name": "lodIndex",
        "type": "integer",
        "required": false,
        "description": "Source LOD (default 0); generated LODs without source geometry are refused",
        "min": 0
      },
      {
        "name": "profileName",
        "type": "string",
        "required": false,
        "description": "Existing skin-weight profile; omit or pass 'default' for the default profile",
        "minLength": 1
      }
    ]
  },
  "read_stringtable": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StringTable asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "keyFilter",
        "type": "string",
        "required": false,
        "description": "Case-insensitive substring filter on keys"
      }
    ]
  },
  "read_uv_channels": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StaticMesh or SkeletalMesh asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "lodIndex",
        "type": "integer",
        "required": false,
        "description": "Source LOD to act on (default 0)",
        "min": 0
      },
      {
        "name": "channels",
        "type": "array",
        "required": false,
        "description": "Only these channels (omit for every channel)",
        "items": "number"
      },
      {
        "name": "includeIslands",
        "type": "boolean",
        "required": false,
        "description": "Include the per-island breakdown (default true)"
      },
      {
        "name": "includeOverlap",
        "type": "boolean",
        "required": false,
        "description": "Compute the overlapping-area fraction (default true)"
      },
      {
        "name": "rasterSize",
        "type": "number",
        "required": false,
        "description": "Raster resolution for the coverage and overlap estimate (default 512)"
      }
    ]
  },
  "recenter_pivot": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": false,
        "description": "StaticMesh to recenter",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "assetPaths",
        "type": "array",
        "required": false,
        "description": "StaticMeshes to recenter together; the first sets the reference pivot",
        "items": "string",
        "role": "editTarget"
      }
    ],
    "choices": [
      {
        "mode": "exactlyOne",
        "branches": [
          [
            "assetPath"
          ],
          [
            "assetPaths"
          ]
        ]
      }
    ]
  },
  "reimport_asset": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Imported asset to rebuild from its source file",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "filePath",
        "type": "string",
        "required": false,
        "description": "New source file to record and reimport from",
        "aliases": [
          "filename"
        ]
      }
    ]
  },
  "reimport_datatable": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "DataTable asset path",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "jsonPath",
        "type": "string",
        "required": false,
        "description": "JSON file to replace the table from"
      },
      {
        "name": "jsonString",
        "type": "string",
        "required": false,
        "description": "JSON text to replace the table from"
      }
    ]
  },
  "reindex_assets_fts": {
    "category": "asset",
    "params": [
      {
        "name": "directory",
        "type": "string",
        "required": false,
        "description": "Content path to rescan (default /Game)"
      }
    ]
  },
  "reload_package": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Asset whose package to reload",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      }
    ]
  },
  "remove_curvetable_row": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "CurveTable asset path",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "rowName",
        "type": "string",
        "required": true,
        "description": "Row to remove"
      }
    ]
  },
  "remove_datatable_row": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "DataTable asset path",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "rowName",
        "type": "string",
        "required": true,
        "description": "Row to remove"
      }
    ]
  },
  "remove_graph_node": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "EdGraph-backed asset path",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "graphName",
        "type": "string",
        "required": false,
        "description": "Graph to author in, by name or unique substring (required when the asset has several graphs)"
      },
      {
        "name": "node",
        "type": "string",
        "required": true,
        "description": "Node to remove: nodeGuid, path, name or unique title"
      },
      {
        "name": "save",
        "type": "boolean",
        "required": false,
        "description": "Save the asset after the edit (default true)"
      }
    ]
  },
  "remove_socket": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StaticMesh, SkeletalMesh or Skeleton asset path",
        "role": "editTarget"
      },
      {
        "name": "socketName",
        "type": "string",
        "required": true,
        "description": "Socket to remove"
      }
    ]
  },
  "remove_stringtable_entry": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StringTable asset path",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "key",
        "type": "string",
        "required": true,
        "description": "Entry key"
      }
    ]
  },
  "rename_asset": {
    "category": "asset",
    "params": [
      {
        "name": "sourcePath",
        "type": "string",
        "required": false,
        "description": "Asset to rename, together with destinationPath",
        "role": "editTarget"
      },
      {
        "name": "destinationPath",
        "type": "string",
        "required": false,
        "description": "New object path, together with sourcePath",
        "role": "editTarget"
      },
      {
        "name": "assetPath",
        "type": "string",
        "required": false,
        "description": "Asset to rename in its own folder, together with newName",
        "role": "editTarget"
      },
      {
        "name": "newName",
        "type": "string",
        "required": false,
        "description": "New asset name, together with assetPath"
      },
      {
        "name": "force",
        "type": "boolean",
        "required": false,
        "description": "World renames only: merge into a destination that already holds external packages (used by rollback)"
      }
    ]
  },
  "rename_curvetable_row": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "CurveTable asset path",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "oldName",
        "type": "string",
        "required": true,
        "description": "Row to rename",
        "aliases": [
          "rowName"
        ]
      },
      {
        "name": "newName",
        "type": "string",
        "required": true,
        "description": "New row name"
      }
    ]
  },
  "rename_datatable_row": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "DataTable asset path",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "oldName",
        "type": "string",
        "required": true,
        "description": "Row to rename",
        "aliases": [
          "rowName"
        ]
      },
      {
        "name": "newName",
        "type": "string",
        "required": true,
        "description": "New row name"
      }
    ]
  },
  "rename_struct_field": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "UserDefinedStruct asset path",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "fieldName",
        "type": "string",
        "required": false,
        "description": "Member by friendly or internal name"
      },
      {
        "name": "fieldGuid",
        "type": "string",
        "required": false,
        "description": "Member by GUID, stable across renames"
      },
      {
        "name": "newDisplayName",
        "type": "string",
        "required": true,
        "description": "New display name; the member GUID is kept"
      }
    ],
    "choices": [
      {
        "mode": "exactlyOne",
        "branches": [
          [
            "fieldName"
          ],
          [
            "fieldGuid"
          ]
        ]
      }
    ]
  },
  "save_all_dirty": {
    "category": "asset",
    "params": [
      {
        "name": "saveMapPackages",
        "type": "boolean",
        "required": false,
        "description": "Include map packages (default true)"
      },
      {
        "name": "saveContentPackages",
        "type": "boolean",
        "required": false,
        "description": "Include content packages (default true)"
      }
    ]
  },
  "save_asset": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": false,
        "description": "Asset to save; omit to save every dirty asset under /Game",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "force",
        "type": "boolean",
        "required": false,
        "description": "Write the package even when it is not dirty (needs assetPath)"
      }
    ]
  },
  "search_assets": {
    "category": "asset",
    "params": [
      {
        "name": "query",
        "type": "string",
        "required": false,
        "description": "Case-insensitive substring of the asset name or object path, or a wildcard pattern with * (default: every asset)"
      },
      {
        "name": "directory",
        "type": "string",
        "required": false,
        "description": "Content path to search under (default /Game/)"
      },
      {
        "name": "searchAll",
        "type": "boolean",
        "required": false,
        "description": "Search every mounted content root, plugins and engine included; with directory, only under it"
      },
      {
        "name": "cursor",
        "type": "string",
        "required": false,
        "description": "Resume a paged read: pass back the nextCursor from the previous page, unmodified"
      },
      {
        "name": "limit",
        "type": "integer",
        "required": false,
        "description": "Rows per page, 1 to 2000 (default 50)",
        "aliases": [
          "maxResults"
        ]
      }
    ]
  },
  "search_assets_fts": {
    "category": "asset",
    "params": [
      {
        "name": "query",
        "type": "string",
        "required": true,
        "description": "Words scored against each asset's name, class and path"
      },
      {
        "name": "classFilter",
        "type": "string",
        "required": false,
        "description": "Only assets whose class name contains this"
      },
      {
        "name": "cursor",
        "type": "string",
        "required": false,
        "description": "Resume a paged read: pass back the nextCursor from the previous page, unmodified"
      },
      {
        "name": "limit",
        "type": "integer",
        "required": false,
        "description": "Rows per page, 1 to 2000 (default 50)",
        "aliases": [
          "maxResults"
        ]
      }
    ]
  },
  "set_asset_property": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Asset path; a Blueprint path writes its generated-class CDO",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "propertyName",
        "type": "string",
        "required": true,
        "description": "Property path; dotted and indexed paths walk structs, arrays and instanced subobjects"
      },
      {
        "name": "value",
        "type": "any",
        "required": true,
        "description": "Value to write: scalar, object, array or asset path"
      },
      {
        "name": "save",
        "type": "boolean",
        "required": false,
        "description": "Save the package after the write (default true)"
      }
    ]
  },
  "set_cloth_config": {
    "category": "asset",
    "params": [
      {
        "name": "skeletalMeshPath",
        "type": "string",
        "required": true,
        "description": "SkeletalMesh asset path",
        "aliases": [
          "assetPath"
        ],
        "role": "editTarget"
      },
      {
        "name": "properties",
        "type": "object",
        "required": true,
        "description": "Config properties to set by reflection"
      },
      {
        "name": "clothingAsset",
        "type": "string",
        "required": false,
        "description": "Only the clothing asset with this name"
      },
      {
        "name": "configType",
        "type": "string",
        "required": false,
        "description": "Only configs whose class or key contains this"
      }
    ]
  },
  "set_curvetable_keys": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "CurveTable asset path",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "rowName",
        "type": "string",
        "required": true,
        "description": "Row whose keys to replace"
      },
      {
        "name": "keys",
        "type": "array",
        "required": true,
        "description": "Replacement keys: [{time, value, interpMode?, arriveTangent?, leaveTangent?}]",
        "items": "object"
      }
    ]
  },
  "set_datatable_cell": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "DataTable asset path",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "rowName",
        "type": "string",
        "required": true,
        "description": "Existing row to edit"
      },
      {
        "name": "fieldName",
        "type": "string",
        "required": true,
        "description": "Row-struct field to write"
      },
      {
        "name": "value",
        "type": "any",
        "required": true,
        "description": "Value to write"
      }
    ]
  },
  "set_datatable_row": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "DataTable asset path",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "rowName",
        "type": "string",
        "required": true,
        "description": "Row to append or overwrite"
      },
      {
        "name": "row",
        "type": "object",
        "required": true,
        "description": "Row-struct fields to write; fields not named keep their values",
        "aliases": [
          "fields",
          "data"
        ]
      }
    ]
  },
  "set_mesh_material": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StaticMesh asset path",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "materialPath",
        "type": "string",
        "required": true,
        "description": "Material to assign"
      },
      {
        "name": "slotIndex",
        "type": "number",
        "required": false,
        "description": "Material slot index (default 0)"
      }
    ]
  },
  "set_mesh_materials_batch": {
    "category": "asset",
    "params": [
      {
        "name": "assignments",
        "type": "array",
        "required": true,
        "description": "Slot assignments, max 500",
        "items": "object",
        "fields": [
          {
            "name": "assetPath",
            "type": "string",
            "required": true,
            "description": "StaticMesh or SkeletalMesh asset path",
            "minLength": 1
          },
          {
            "name": "materialPath",
            "type": "string",
            "required": true,
            "description": "Material to assign",
            "minLength": 1
          },
          {
            "name": "slotName",
            "type": "string",
            "required": false,
            "description": "Slot by name, which survives a reimport reordering slot indices"
          },
          {
            "name": "slotIndex",
            "type": "integer",
            "required": false,
            "description": "Slot by index (default 0); refused when it disagrees with slotName"
          }
        ],
        "minItems": 1,
        "maxItems": 500,
        "role": "editTarget",
        "roleKeys": [
          "assetPath"
        ]
      },
      {
        "name": "save",
        "type": "boolean",
        "required": false,
        "description": "Save each changed mesh (default true)"
      },
      {
        "name": "dryRun",
        "type": "boolean",
        "required": false,
        "description": "Preflight and report without writing (default false)"
      },
      {
        "name": "continueOnError",
        "type": "boolean",
        "required": false,
        "description": "Apply the assignments that passed preflight instead of aborting the batch (default false)"
      }
    ]
  },
  "set_mesh_nav": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StaticMesh asset path",
        "role": "editTarget"
      },
      {
        "name": "bHasNavigationData",
        "type": "boolean",
        "required": false,
        "description": "Whether the mesh generates navigation data"
      },
      {
        "name": "clearNavCollision",
        "type": "boolean",
        "required": false,
        "description": "Remove the mesh's NavCollision"
      }
    ]
  },
  "set_sk_material_slots": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "SkeletalMesh asset path",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "slots",
        "type": "array",
        "required": true,
        "description": "Slot assignments: [{slotName? | slotIndex?, materialPath}]",
        "items": "object"
      }
    ]
  },
  "set_skeletal_mesh_optimize_for_instancing": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "SkeletalMesh asset path",
        "role": "editTarget"
      },
      {
        "name": "enabled",
        "type": "boolean",
        "required": true,
        "description": "Value to write to bOptimizeForInstancing"
      },
      {
        "name": "lodIndex",
        "type": "integer",
        "required": false,
        "description": "LOD to target (default 0); not with allLods",
        "min": 0
      },
      {
        "name": "allLods",
        "type": "boolean",
        "required": false,
        "description": "Target every LOD instead of one; not with lodIndex"
      }
    ]
  },
  "set_skeletal_mesh_skin_weights": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "SkeletalMesh asset path",
        "role": "editTarget"
      },
      {
        "name": "edits",
        "type": "array",
        "required": true,
        "description": "Selected source vertices and their complete replacement influences (1-256)",
        "items": "object",
        "fields": [
          {
            "name": "vertexIndex",
            "type": "integer",
            "required": true,
            "description": "Source MeshDescription vertex ID",
            "min": 0
          },
          {
            "name": "influences",
            "type": "array",
            "required": true,
            "description": "1-64 entries of {boneName, weight? (0 to 1) | rawWeight? (1 to 65535)}",
            "items": "object",
            "minItems": 1,
            "maxItems": 64
          }
        ],
        "minItems": 1,
        "maxItems": 256
      },
      {
        "name": "lodIndex",
        "type": "integer",
        "required": false,
        "description": "Source LOD (default 0); generated LODs without source geometry are refused",
        "min": 0
      },
      {
        "name": "profileName",
        "type": "string",
        "required": false,
        "description": "Existing skin-weight profile; omit or pass 'default' for the default profile",
        "minLength": 1
      },
      {
        "name": "restoreRawWeights",
        "type": "boolean",
        "required": false,
        "description": "Rollback payloads only: restore the exact uint16 rawWeight values"
      }
    ]
  },
  "set_socket_transform": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StaticMesh or SkeletalMesh asset path",
        "role": "editTarget"
      },
      {
        "name": "socketName",
        "type": "string",
        "required": true,
        "description": "Socket to move"
      },
      {
        "name": "relativeLocation",
        "type": "vec3",
        "required": false,
        "description": "New relative location"
      },
      {
        "name": "relativeRotation",
        "type": "rotator",
        "required": false,
        "description": "New relative rotation"
      },
      {
        "name": "relativeScale",
        "type": "vec3",
        "required": false,
        "description": "New relative scale"
      }
    ]
  },
  "set_stringtable_entry": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StringTable asset path",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "key",
        "type": "string",
        "required": true,
        "description": "Entry key"
      },
      {
        "name": "sourceString",
        "type": "string",
        "required": false,
        "description": "Source string to write"
      },
      {
        "name": "value",
        "type": "any",
        "required": false,
        "description": "Source string to write, when sourceString is omitted"
      }
    ]
  },
  "set_texture_settings": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Texture2D asset path",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "settings",
        "type": "object",
        "required": false,
        "description": "The four settings below as one object; a key also given at the top level wins over it"
      },
      {
        "name": "compressionSettings",
        "type": "string",
        "required": false,
        "description": "Default, Normalmap, Grayscale, Displacementmap, VectorDisplacementmap, HDR, EditorIcon, Alpha, DistanceFieldFont, HDR_Compressed or BC7"
      },
      {
        "name": "lodGroup",
        "type": "string",
        "required": false,
        "description": "Texture LOD group: World, WorldNormalMap, Character, UI, Lightmap, Effects and the rest"
      },
      {
        "name": "sRGB",
        "type": "boolean",
        "required": false,
        "description": "Treat the texture as sRGB"
      },
      {
        "name": "neverStream",
        "type": "boolean",
        "required": false,
        "description": "Keep every mip resident"
      }
    ]
  },
  "set_texture_settings_by_type": {
    "category": "asset",
    "params": [
      {
        "name": "groups",
        "type": "object",
        "required": true,
        "description": "Texture paths per profile: {normal?, grayscale?, baseColor?, hdr?}"
      }
    ]
  },
  "set_uv_channel_count": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StaticMesh or SkeletalMesh asset path",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "lodIndex",
        "type": "integer",
        "required": false,
        "description": "Source LOD to act on (default 0)",
        "min": 0
      },
      {
        "name": "op",
        "type": "string",
        "required": false,
        "description": "set (default) | add | remove | copy"
      },
      {
        "name": "channelCount",
        "type": "number",
        "required": false,
        "description": "Target channel count (op=set)"
      },
      {
        "name": "count",
        "type": "number",
        "required": false,
        "description": "Channels to add (op=add, default 1)"
      },
      {
        "name": "channel",
        "type": "number",
        "required": false,
        "description": "Channel to remove (op=remove)"
      },
      {
        "name": "fromChannel",
        "type": "number",
        "required": false,
        "description": "Source channel (op=copy)"
      },
      {
        "name": "toChannel",
        "type": "number",
        "required": false,
        "description": "Destination channel (op=copy)"
      },
      {
        "name": "save",
        "type": "boolean",
        "required": false,
        "description": "Save the mesh (default true)"
      },
      {
        "name": "dryRun",
        "type": "boolean",
        "required": false,
        "description": "Report without writing"
      }
    ]
  },
  "transform_uvs": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StaticMesh or SkeletalMesh asset path",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "lodIndex",
        "type": "integer",
        "required": false,
        "description": "Source LOD to act on (default 0)",
        "min": 0
      },
      {
        "name": "channel",
        "type": "number",
        "required": false,
        "description": "Channel to transform (default 0)"
      },
      {
        "name": "translate",
        "type": "object",
        "required": false,
        "description": "UV-space offset {u, v}"
      },
      {
        "name": "scale",
        "type": "object",
        "required": false,
        "description": "UV-space scale {u, v}; a zero component is refused"
      },
      {
        "name": "rotate",
        "type": "number",
        "required": false,
        "description": "Rotation in degrees"
      },
      {
        "name": "origin",
        "type": "object",
        "required": false,
        "description": "Pivot for rotate and scale {u, v} (default 0.5, 0.5)"
      },
      {
        "name": "flipU",
        "type": "boolean",
        "required": false,
        "description": "Mirror across U"
      },
      {
        "name": "flipV",
        "type": "boolean",
        "required": false,
        "description": "Mirror across V"
      },
      {
        "name": "order",
        "type": "string",
        "required": false,
        "description": "flipScaleRotateTranslate (default) | translateRotateScaleFlip"
      },
      {
        "name": "selection",
        "type": "object",
        "required": false,
        "description": "What to transform: {mode: all|island|normal|polygonGroup, islandIndices?, normalDirection?, normalAngleTolerance?, polygonGroups?, materialSlotNames?}"
      },
      {
        "name": "save",
        "type": "boolean",
        "required": false,
        "description": "Save the mesh (default true)"
      },
      {
        "name": "dryRun",
        "type": "boolean",
        "required": false,
        "description": "Report without writing"
      }
    ]
  },
  "unbind_cloth_from_section": {
    "category": "asset",
    "params": [
      {
        "name": "skeletalMeshPath",
        "type": "string",
        "required": true,
        "description": "SkeletalMesh asset path",
        "aliases": [
          "assetPath"
        ],
        "role": "editTarget"
      },
      {
        "name": "lodIndex",
        "type": "integer",
        "required": true,
        "description": "Mesh LOD",
        "min": 0
      },
      {
        "name": "sectionIndex",
        "type": "integer",
        "required": true,
        "description": "Render section to unbind",
        "min": 0
      },
      {
        "name": "clothingAsset",
        "type": "string",
        "required": false,
        "description": "Refuse unless the section is bound to this clothing asset"
      }
    ]
  },
  "unwrap_uvs": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "StaticMesh or SkeletalMesh asset path",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "lodIndex",
        "type": "integer",
        "required": false,
        "description": "Source LOD to act on (default 0)",
        "min": 0
      },
      {
        "name": "channel",
        "type": "number",
        "required": false,
        "description": "Channel to write (default 0)"
      },
      {
        "name": "method",
        "type": "string",
        "required": false,
        "description": "xatlas (default) | patchBuilder | expMap | conformal | spectralConformal | planar | box | cylinder"
      },
      {
        "name": "pack",
        "type": "boolean",
        "required": false,
        "description": "Repack the islands after unwrapping (default true)"
      },
      {
        "name": "textureResolution",
        "type": "number",
        "required": false,
        "description": "Resolution the packer targets (default 1024)"
      },
      {
        "name": "maxIterations",
        "type": "number",
        "required": false,
        "description": "Solver iteration cap"
      },
      {
        "name": "initialPatchCount",
        "type": "number",
        "required": false,
        "description": "Starting patch count for patchBuilder"
      },
      {
        "name": "islandSource",
        "type": "string",
        "required": false,
        "description": "UVIslands (default) | PolyGroups"
      },
      {
        "name": "projectionTransform",
        "type": "object",
        "required": false,
        "description": "Transform for the planar, box and cylinder projections"
      },
      {
        "name": "preserveVertexOrder",
        "type": "boolean",
        "required": false,
        "description": "Keep the existing vertex order (default true)"
      },
      {
        "name": "backupToChannel",
        "type": "number",
        "required": false,
        "description": "Copy the existing UVs here first"
      },
      {
        "name": "save",
        "type": "boolean",
        "required": false,
        "description": "Save the mesh (default true)"
      },
      {
        "name": "dryRun",
        "type": "boolean",
        "required": false,
        "description": "Report without writing"
      },
      {
        "name": "rasterSize",
        "type": "number",
        "required": false,
        "description": "Raster resolution for the result's overlap estimate (default 512)"
      }
    ]
  },
  "update_datatable_row": {
    "category": "asset",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "DataTable asset path",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "rowName",
        "type": "string",
        "required": true,
        "description": "Row to append or overwrite"
      },
      {
        "name": "row",
        "type": "object",
        "required": true,
        "description": "Row-struct fields to write; fields not named keep their values",
        "aliases": [
          "fields",
          "data"
        ]
      }
    ]
  }
};

/** Every key the spec'd asset handlers declare, aliases included. */
export const schema = categorySchema(handlerSpecs);

/** Declare an action for a spec'd bridge method: effect, summary, method. */
export const specBp = makeSpecBp(handlerSpecs);
