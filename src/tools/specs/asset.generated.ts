// GENERATED FILE - do not edit.
//
// Written by scripts/generate-handler-specs.mjs from tests/golden/handler-specs.json,
// which is recorded from the parameter specs the C++ handlers register with
// (npm run specs:record). To change a parameter, change its RegisterHandler
// spec, re-record, and regenerate (#1057).
import { z } from "zod";
import { makeSpecBp, type HandlerSpecs } from "../../surface/handler-spec.js";

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
        ]
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
        ]
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
        ]
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
        ]
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
        "description": "StaticMesh, SkeletalMesh or Skeleton asset path"
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
        ]
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
        "description": "Values to append, validated before any is written"
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
        "description": "LOD to read when lodType names one (default 0)"
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
        ]
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
        "description": "Asset to write (default '<assetPath>_Filled')"
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
        "description": "Copy the source here before an inPlace edit, the only way back to the original geometry"
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
        "description": "LOD to read when lodType names one (default 0)"
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
        ]
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
        "description": "Asset to write (default '<assetPath>_Mirrored')"
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
        "description": "Copy the source here before an inPlace edit, the only way back to the original geometry"
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
        "description": "LOD to read when lodType names one (default 0)"
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
        ]
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
        "description": "Asset to write (default '<assetPath>_Remeshed')"
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
        "description": "Copy the source here before an inPlace edit, the only way back to the original geometry"
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
        "description": "LOD to read when lodType names one (default 0)"
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
        ]
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
        "description": "Asset to write (default '<assetPath>_Simplified')"
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
        "description": "Copy the source here before an inPlace edit, the only way back to the original geometry"
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
        "description": "LOD to read when lodType names one (default 0)"
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
        ]
      },
      {
        "name": "lodIndex",
        "type": "integer",
        "required": true,
        "description": "Mesh LOD"
      },
      {
        "name": "sectionIndex",
        "type": "integer",
        "required": true,
        "description": "Render section to bind"
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
        "description": "LOD inside the clothing asset (default: lodIndex, clamped)"
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
        "items": "object"
      }
    ]
  },
  "bulk_set_asset_properties": {
    "category": "asset",
    "params": [
      {
        "name": "items",
        "type": "array",
        "required": true,
        "description": "Asset updates: [{assetPath, properties}], max 500",
        "items": "object"
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
        "items": "object"
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
        "description": "Source LOD to act on (default 0)"
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
        ]
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
        ]
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
        "description": "Object path of the new pipeline"
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
        "description": "Pixel width, 1-8192 (default 512)"
      },
      {
        "name": "height",
        "type": "integer",
        "required": false,
        "description": "Pixel height, 1-8192 (default 512)"
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
        "description": "Target gamma (default 0, the engine behavior)"
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
        ]
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
        ]
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
        "items": "string"
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
        ]
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
        "description": "Object path of the copy"
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
        ]
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
        ]
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
        "description": "File to write; a relative path resolves against the project directory"
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
        ]
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
        "description": "Source LOD to act on (default 0)"
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
        "description": "PNG to write (default under Saved/UVLayouts)"
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
        ]
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
        "items": "string"
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
        "items": "string"
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
        ]
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
        ]
      },
      {
        "name": "lodIndex",
        "type": "integer",
        "required": false,
        "description": "Source LOD to act on (default 0)"
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
        ]
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
        "description": "LOD to read when lodType names one (default 0)"
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
        "description": "Inline the properties of subobjects the asset owns to this depth, 0-5 (default 0)"
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
        "description": "Cap on expanded objects (default 64)"
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
        "description": "LOD to read (default 0)"
      },
      {
        "name": "sectionIndex",
        "type": "integer",
        "required": false,
        "description": "One render section (omit for every section)"
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
        "description": "UV channel to return (default 0)"
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
        ]
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
        ]
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
        ]
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
        "items": "object"
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
        "description": "LOD to measure (default 0)"
      },
      {
        "name": "sectionIndex",
        "type": "integer",
        "required": false,
        "description": "One render section (omit to measure the whole LOD)"
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
        "description": "StaticMesh being cut; the result inherits its transform, materials, collision and Nanite setting"
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
        "description": "Asset to write (default '<targetPath>_<Operation>')"
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
        "description": "LOD of each input to read when lodType names one (default 0)"
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
  "move_asset": {
    "category": "asset",
    "params": [
      {
        "name": "sourcePath",
        "type": "string",
        "required": false,
        "description": "Asset to rename, together with destinationPath"
      },
      {
        "name": "destinationPath",
        "type": "string",
        "required": false,
        "description": "New object path, together with sourcePath"
      },
      {
        "name": "assetPath",
        "type": "string",
        "required": false,
        "description": "Asset to rename in its own folder, together with newName"
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
        "description": "Inline the properties of subobjects the asset owns to this depth, 0-5 (default 0)"
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
        "description": "Cap on expanded objects (default 64)"
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
        "description": "LOD to target (default 0); not with allLods"
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
        "items": "integer"
      },
      {
        "name": "lodIndex",
        "type": "integer",
        "required": false,
        "description": "Source LOD (default 0); generated LODs without source geometry are refused"
      },
      {
        "name": "profileName",
        "type": "string",
        "required": false,
        "description": "Existing skin-weight profile; omit or pass 'default' for the default profile"
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
        "description": "Source LOD to act on (default 0)"
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
        ]
      },
      {
        "name": "assetPaths",
        "type": "array",
        "required": false,
        "description": "StaticMeshes to recenter together; the first sets the reference pivot",
        "items": "string"
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
        ]
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
        ]
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
        ]
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
        ]
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
        ]
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
        ]
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
        "description": "StaticMesh, SkeletalMesh or Skeleton asset path"
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
  "rename_asset": {
    "category": "asset",
    "params": [
      {
        "name": "sourcePath",
        "type": "string",
        "required": false,
        "description": "Asset to rename, together with destinationPath"
      },
      {
        "name": "destinationPath",
        "type": "string",
        "required": false,
        "description": "New object path, together with sourcePath"
      },
      {
        "name": "assetPath",
        "type": "string",
        "required": false,
        "description": "Asset to rename in its own folder, together with newName"
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
        ]
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
        ]
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
        ]
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
        ]
      },
      {
        "name": "force",
        "type": "boolean",
        "required": false,
        "description": "Write the package even when it is not dirty (needs assetPath)"
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
        ]
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
        ]
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
        ]
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
        ]
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
        ]
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
        ]
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
            "description": "StaticMesh or SkeletalMesh asset path"
          },
          {
            "name": "materialPath",
            "type": "string",
            "required": true,
            "description": "Material to assign"
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
        "description": "StaticMesh asset path"
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
        ]
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
        "description": "SkeletalMesh asset path"
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
        "description": "LOD to target (default 0); not with allLods"
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
        "description": "SkeletalMesh asset path"
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
            "description": "Source MeshDescription vertex ID"
          },
          {
            "name": "influences",
            "type": "array",
            "required": true,
            "description": "1-64 entries of {boneName, weight? (0 to 1) | rawWeight? (1 to 65535)}",
            "items": "object"
          }
        ]
      },
      {
        "name": "lodIndex",
        "type": "integer",
        "required": false,
        "description": "Source LOD (default 0); generated LODs without source geometry are refused"
      },
      {
        "name": "profileName",
        "type": "string",
        "required": false,
        "description": "Existing skin-weight profile; omit or pass 'default' for the default profile"
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
        "description": "StaticMesh or SkeletalMesh asset path"
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
        ]
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
        ]
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
        ]
      },
      {
        "name": "lodIndex",
        "type": "integer",
        "required": false,
        "description": "Source LOD to act on (default 0)"
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
        ]
      },
      {
        "name": "lodIndex",
        "type": "integer",
        "required": false,
        "description": "Source LOD to act on (default 0)"
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
        ]
      },
      {
        "name": "lodIndex",
        "type": "integer",
        "required": true,
        "description": "Mesh LOD"
      },
      {
        "name": "sectionIndex",
        "type": "integer",
        "required": true,
        "description": "Render section to unbind"
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
        ]
      },
      {
        "name": "lodIndex",
        "type": "integer",
        "required": false,
        "description": "Source LOD to act on (default 0)"
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
        ]
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

/** The Params: clause of each spec'd bridge method. */
export const paramsClauses: Readonly<Record<string, string>> = {
  add_curvetable_key: "Params: assetPath (or path), rowName, time, value, interpMode?, keyTimeTolerance?",
  add_curvetable_row: "Params: assetPath (or path), rowName, curveType? (or mode), interpMode?",
  add_datatable_row: "Params: assetPath (or path), rowName, row (or fields, or data)",
  add_graph_node: "Params: assetPath (or path), graphName?, nodeClass?, actionName?, spawnMode?, posX?, posY?, save?",
  add_socket: "Params: assetPath, socketName, boneName?, relativeLocation?, relativeRotation?, relativeScale?, onConflict?",
  append_asset_array_elements: "Params: assetPath (or path), propertyName, elements, save?",
  apply_mesh_fracture: "Params: assetPath (or path), pattern?, axis?, pieces?, gridX?, gridY?, gridZ?, planeCount?, seed?, jitter?, gapWidth?, fillHoles?, minPieceTriangles?, outputBasePath?, onConflict?, copyMaterialsFromSource?, nanite?, recomputeNormals?, recomputeTangents?, lodType?, lodIndex?, save?, dryRun?",
  apply_mesh_hole_fill: "Params: assetPath (or path), fillMethod?, weldFirst?, weldTolerance?, removeDegenerateFirst?, deleteIsolatedTriangles?, outputPath?, inPlace?, backupPath?, lodType?, lodIndex?, onConflict?, copyMaterialsFromSource?, copyCollisionFromSource?, nanite?, recomputeNormals?, recomputeTangents?, removeDegenerates?, save?, dryRun?",
  apply_mesh_mirror: "Params: assetPath (or path), axis?, planeOrigin?, planeNormal?, applyPlaneCut?, flipCutSide?, weldAlongPlane?, outputPath?, inPlace?, backupPath?, lodType?, lodIndex?, onConflict?, copyMaterialsFromSource?, copyCollisionFromSource?, nanite?, recomputeNormals?, recomputeTangents?, removeDegenerates?, save?, dryRun?",
  apply_mesh_remesh: "Params: assetPath (or path), remeshMode?, targetType?, targetTriangleCount?, targetEdgeLength?, smoothingType?, smoothingRate?, boundaryConstraint?, iterations?, discardAttributes?, reprojectToInputMesh?, relativeDensity?, outputPath?, inPlace?, backupPath?, lodType?, lodIndex?, onConflict?, copyMaterialsFromSource?, copyCollisionFromSource?, nanite?, recomputeNormals?, recomputeTangents?, removeDegenerates?, save?, dryRun?",
  apply_mesh_simplify: "Params: assetPath (or path), simplifyMode?, triangleCount?, vertexCount?, tolerance?, edgeLength?, angleThreshold?, method?, allowSeamCollapse?, preserveVertexPositions?, autoCompact?, outputPath?, inPlace?, backupPath?, lodType?, lodIndex?, onConflict?, copyMaterialsFromSource?, copyCollisionFromSource?, nanite?, recomputeNormals?, recomputeTangents?, removeDegenerates?, save?, dryRun?",
  asset_health_check: "Params: assetPath (or path)",
  audit_asset_hygiene: "Params: directory?, directories?, recursive?, maxAssets?, maxIssues?, checks?, classNames?, excludePaths?, keepPaths?, duplicateMethod?, namingRules?, namingRuleMode?, includeWorlds?, ignoreRedirectorReferencers?",
  bind_cloth_to_section: "Params: skeletalMeshPath (or assetPath), lodIndex, sectionIndex, clothingAsset?, assetLodIndex?",
  bulk_read_asset_properties: "Params: propertyNames, assetPaths OR directory, recursive?, classNames?, matchSubclasses?, where?, whereMode?, suspectOnly?, groupBy?, countBy?, sampleLimit?, countOnly?, limit?, startIndex?, maxAssets?, outputPath?",
  bulk_rename_assets: "Params: renames",
  bulk_set_asset_properties: "Params: items, save?, dryRun?, continueOnError?",
  bulk_upsert_data_assets: "Params: items, onConflict?, dryRun?, save?",
  check_uvs: "Params: assetPath (or path), lodIndex?, requireLightmapChannel?, maxOverlapFraction?, rasterSize?",
  compare_textures: "Params: assetPathA (or a), assetPathB (or b)",
  compile_customizable_object: "Params: assetPath (or path), optimizationLevel?, textureCompression?",
  connect_graph_pins: "Params: assetPath (or path), graphName?, sourceNode?, sourcePinId?, sourcePin?, sourcePinDirection?, targetNode?, targetPinId?, targetPin?, targetPinDirection?, save?",
  create_asset_by_class: "Params: name, className (or class), packagePath?, properties?, onConflict?",
  create_curvetable: "Params: name, packagePath?, onConflict?",
  create_customizable_object: "Params: name, packagePath?, onConflict?, save?",
  create_data_asset: "Params: name, className (or class), packagePath?, properties?, onConflict?",
  create_datatable: "Params: name, rowStruct, packagePath?, onConflict?",
  create_folder: "Params: at least one of path/paths",
  create_interchange_pipeline: "Params: assetPath OR name, packagePath?, meshType?, options?, onConflict?",
  create_render_target_2d: "Params: name, packagePath?, width?, height?, format?, clearColor?, generateMips?, targetGamma?, onConflict?",
  create_stringtable: "Params: name, packagePath?, namespace?, onConflict?",
  create_subobject: "Params: assetPath (or path), className, name, properties?, outer?, onConflict?, save?",
  create_user_defined_enum: "Params: name, packagePath?, values?, onConflict?",
  create_user_defined_struct: "Params: name, packagePath?, structFields?, onConflict?",
  delete_asset: "Params: assetPath (or path), force?",
  delete_asset_batch: "Params: assetPaths (or paths), force?",
  delete_folder: "Params: path?, paths?, force?",
  diagnose_registry: "Params: path, recursive?, reconcile?",
  diff_asset: "Params: assetPath (or path), otherPath",
  disconnect_graph_pins: "Params: assetPath (or path), graphName?, sourceNode?, sourcePinId?, sourcePin?, sourcePinDirection?, targetNode?, targetPinId?, targetPin?, targetPinDirection?, save?",
  duplicate_asset: "Params: sourcePath, destinationPath, onConflict?",
  edit_user_defined_enum: "Params: assetPath (or path), op, displayName?, name?, index?",
  edit_user_defined_struct: "Params: assetPath (or path), op, fieldName?, fieldGuid?, newDisplayName?, type?",
  export_asset: "Params: assetPath (or path), outputPath",
  export_texture: "Params: assetPath (or path), outputPath (or filePath)",
  export_uv_layout: "Params: assetPath (or path), lodIndex?, channel?, outputPath?, imageSize?, showIslands?, showOverlaps?, showGrid?",
  fill_datatable_from_json: "Params: assetPath (or path), rows?, jsonString?",
  fix_asset_hygiene: "Params: fix, assetPaths?, directory?, directories?, recursive?, maxAssets?, classNames?, excludePaths?, keepPaths?, namingRules?, namingRuleMode?, unreferencedAction?, quarantineFolder?, ignoreRedirectorReferencers?, maxFixes?, continueOnError?, save?, dryRun?",
  fixup_redirectors: "Params: paths, dryRun?, save?, allowProjectWide?",
  force_reload_asset: "Params: assetPath (or path), discardUnsaved?",
  generate_lightmap_uvs: "Params: assetPath (or path), lodIndex?, enable?, sourceChannel?, destinationChannel?, minLightmapResolution?, lightmapResolution?, setLightmapCoordinateIndex?, force?, save?, dryRun?, rasterSize?",
  generate_mesh_collision: "Params: assetPath (or path), op?, method?, maxConvexHulls?, hullTargetFaceCount?, maxShapeCount?, minThickness?, autoDetectSpheres?, autoDetectBoxes?, autoDetectCapsules?, simplifyHulls?, removeFullyContainedShapes?, decompositionErrorTolerance?, decompositionSearchFactor?, sweptHullAxis?, markAsCustomized?, lodType?, lodIndex?, save?, dryRun?",
  get_asset_dependencies: "Params: packages?, packagePath?, hard?, soft?",
  get_asset_properties: "Params: assetPath (or path), propertyName?, includeValues?, valueFormat?, expandDepth?, expandExternal?, maxExpandedObjects?",
  get_asset_referencers: "Params: packages?, packagePath?",
  get_curvetable_keys: "Params: assetPath (or path), rowName",
  get_datatable_row: "Params: assetPath (or path), rowName",
  get_mesh_bounds: "Params: assetPath",
  get_mesh_collision: "Params: assetPath",
  get_mesh_geometry: "Params: assetPath (or path), lodIndex?, sectionIndex?, include?, uvChannel?, dumpToFile?, outputPath?",
  get_mesh_info: "Params: assetPath",
  get_primary_asset_ids: "Params: type?, maxResults?",
  get_stringtable_entry: "Params: assetPath (or path), key",
  get_texture_info: "Params: assetPath (or path)",
  import_animation: "Params: filePath (or filename), name? (or assetName), packagePath? (or destinationPath), skeletonPath, importCustomAttribute?, removeRedundantKeys?, importSettings?",
  import_curvetable: "Params: assetPath (or path), jsonString?, csvString?, filePath? (or jsonPath, or csvPath), format?, interpMode?",
  import_file: "Params: filePath (or filename), packagePath (or destinationPath), name? (or assetName), factoryClass?, factoryProperties?, replaceExisting?, save?, automated?",
  import_skeletal_mesh: "Params: filePath (or filename), name? (or assetName), packagePath? (or destinationPath), skeletonPath?, importMaterials?, importTextures?, importUniformScale?, importMorphTargets?, createPhysicsAsset?, replaceExisting?",
  import_static_mesh: "Params: filePath (or filename), name? (or assetName), packagePath? (or destinationPath), combineMeshes?, importMaterials?, importTextures?, generateLightmapUVs?, importUniformScale?",
  import_stringtable: "Params: assetPath (or path), filePath (or filename, or csvPath)",
  import_stringtable_csv: "Params: assetPath (or path), csvPath (or filePath), expectedKeys?, requireExactKeys?, replaceExisting?, save?",
  import_texture: "Params: filePath (or filename), name? (or assetName), packagePath? (or destinationPath), sRGB?, compressionSettings?, lodGroup?, neverStream?, noCompression?, noAlpha?",
  import_texture_batch: "Params: items, packagePath?, save?, automated?",
  list_asset_sockets: "Params: assetPath",
  list_assets: "Params: directory?, classFilter? (or typeFilter), recursive?, cursor?, limit? (or maxResults), offset?",
  list_curvetable_rows: "Params: assetPath (or path), rowFilter?",
  list_enum_values: "Params: assetPath (or path)",
  list_locks: "Params: none",
  list_skeleton_bones: "Params: assetPath (or path), includeTransforms?",
  list_sockets: "Params: assetPath",
  list_stringtable_keys: "Params: assetPath (or path), keyFilter?",
  list_struct_fields: "Params: assetPath (or path)",
  list_textures: "Params: directory?, cursor?, limit? (or maxResults)",
  measure_mesh_geometry: "Params: assetPath (or path), lodIndex?, sectionIndex?",
  mesh_boolean: "Params: operation, targetPath, toolPath, outputPath?, inPlace?, targetTransform?, toolTransform?, lodType?, lodIndex?, fillHoles?, simplifyOutput?, simplifyPlanarTolerance?, allowEmptyResult?, recomputeNormals?, recomputeTangents?, removeDegenerates?, copyCollisionFromTarget?, copyMaterialsFromTarget?, nanite?, onConflict?, dryRun?, save?",
  move_asset: "Params: sourcePath?, destinationPath?, assetPath?, newName?, force?",
  move_folder: "Params: sourcePath, destinationPath",
  read_asset: "Params: assetPath (or path)",
  read_asset_graph: "Params: assetPath (or path), graphName?, includePins?, maxNodes?",
  read_asset_properties: "Params: assetPath (or path), propertyName?, includeValues?, valueFormat?, expandDepth?, expandExternal?, maxExpandedObjects?",
  read_cloth_data: "Params: skeletalMeshPath (or assetPath)",
  read_curvetable: "Params: assetPath (or path), rowFilter?",
  read_datatable: "Params: assetPath (or path), rowFilter?, outputPath?",
  read_import_sources: "Params: assetPath (or path)",
  read_skeletal_mesh_build_settings: "Params: assetPath, lodIndex?, allLods?",
  read_skeletal_mesh_skin_weights: "Params: assetPath, vertexIndices, lodIndex?, profileName?",
  read_stringtable: "Params: assetPath (or path), keyFilter?",
  read_uv_channels: "Params: assetPath (or path), lodIndex?, channels?, includeIslands?, includeOverlap?, rasterSize?",
  recenter_pivot: "Params: assetPath (or path) OR assetPaths",
  reimport_asset: "Params: assetPath (or path), filePath? (or filename)",
  reimport_datatable: "Params: assetPath (or path), jsonPath?, jsonString?",
  reindex_assets_fts: "Params: directory?",
  reload_package: "Params: assetPath (or path)",
  remove_curvetable_row: "Params: assetPath (or path), rowName",
  remove_datatable_row: "Params: assetPath (or path), rowName",
  remove_graph_node: "Params: assetPath (or path), graphName?, node, save?",
  remove_socket: "Params: assetPath, socketName",
  remove_stringtable_entry: "Params: assetPath (or path), key",
  rename_asset: "Params: sourcePath?, destinationPath?, assetPath?, newName?, force?",
  rename_curvetable_row: "Params: assetPath (or path), oldName (or rowName), newName",
  rename_datatable_row: "Params: assetPath (or path), oldName (or rowName), newName",
  rename_struct_field: "Params: assetPath (or path), fieldName OR fieldGuid, newDisplayName",
  save_all_dirty: "Params: saveMapPackages?, saveContentPackages?",
  save_asset: "Params: assetPath? (or path), force?",
  search_assets_fts: "Params: query, classFilter?, cursor?, limit? (or maxResults)",
  set_asset_property: "Params: assetPath (or path), propertyName, value, save?",
  set_cloth_config: "Params: skeletalMeshPath (or assetPath), properties, clothingAsset?, configType?",
  set_curvetable_keys: "Params: assetPath (or path), rowName, keys",
  set_datatable_cell: "Params: assetPath (or path), rowName, fieldName, value",
  set_datatable_row: "Params: assetPath (or path), rowName, row (or fields, or data)",
  set_mesh_material: "Params: assetPath (or path), materialPath, slotIndex?",
  set_mesh_materials_batch: "Params: assignments, save?, dryRun?, continueOnError?",
  set_mesh_nav: "Params: assetPath, bHasNavigationData?, clearNavCollision?",
  set_sk_material_slots: "Params: assetPath (or path), slots",
  set_skeletal_mesh_optimize_for_instancing: "Params: assetPath, enabled, lodIndex?, allLods?",
  set_skeletal_mesh_skin_weights: "Params: assetPath, edits, lodIndex?, profileName?, restoreRawWeights?",
  set_socket_transform: "Params: assetPath, socketName, relativeLocation?, relativeRotation?, relativeScale?",
  set_stringtable_entry: "Params: assetPath (or path), key, sourceString?, value?",
  set_texture_settings: "Params: assetPath (or path), settings?, compressionSettings?, lodGroup?, sRGB?, neverStream?",
  set_texture_settings_by_type: "Params: groups",
  set_uv_channel_count: "Params: assetPath (or path), lodIndex?, op?, channelCount?, count?, channel?, fromChannel?, toChannel?, save?, dryRun?",
  transform_uvs: "Params: assetPath (or path), lodIndex?, channel?, translate?, scale?, rotate?, origin?, flipU?, flipV?, order?, selection?, save?, dryRun?",
  unbind_cloth_from_section: "Params: skeletalMeshPath (or assetPath), lodIndex, sectionIndex, clothingAsset?",
  unwrap_uvs: "Params: assetPath (or path), lodIndex?, channel?, method?, pack?, textureResolution?, maxIterations?, initialPatchCount?, islandSource?, projectionTransform?, preserveVertexOrder?, backupToChannel?, save?, dryRun?, rasterSize?",
  update_datatable_row: "Params: assetPath (or path), rowName, row (or fields, or data)",
};

/** Every key the spec'd asset handlers declare, aliases included. */
export const schema: Record<string, z.ZodType> = {
  a: z.string().optional().describe("Alias for assetPathA"),
  actionName: z.string().optional().describe("Schema menu entry name, or 'category|name'"),
  allLods: z.boolean().optional().describe("Target every LOD instead of one; not with lodIndex"),
  allowEmptyResult: z.boolean().optional().describe("Accept a result with no triangles (default false)"),
  allowProjectWide: z.boolean().optional().describe("Permit a path naming a whole content root (default false)"),
  allowSeamCollapse: z.boolean().optional().describe("Let the simplifier collapse UV, normal and material seams (default true)"),
  angleThreshold: z.number().optional().describe("How far from coplanar still counts as coplanar (simplifyMode planar or polygroup, default 0.001)"),
  applyPlaneCut: z.boolean().optional().describe("Remove the geometry on the far side of the plane before reflecting (default true)"),
  assetLodIndex: z.number().int().optional().describe("LOD inside the clothing asset (default: lodIndex, clamped)"),
  assetName: z.string().optional().describe("Alias for name"),
  assetPath: z.string().optional().describe("CurveTable asset path (add_curvetable_key, add_curvetable_row, get_curvetable_keys, import_curvetable, list_curvetable_rows, read_curvetable, remove_curvetable_row, rename_curvetable_row, set_curvetable_keys). DataTable asset path (add_datatable_row, fill_datatable_from_json, get_datatable_row, read_datatable, reimport_datatable, remove_datatable_row, rename_datatable_row, set_datatable_cell, set_datatable_row, update_datatable_row). EdGraph-backed asset path (add_graph_node, connect_graph_pins, disconnect_graph_pins, read_asset_graph, remove_graph_node). StaticMesh, SkeletalMesh or Skeleton asset path (add_socket, list_asset_sockets, list_sockets, remove_socket). Asset path; a Blueprint path writes its generated-class CDO (append_asset_array_elements, set_asset_property). StaticMesh to cut; it is never modified (apply_mesh_fracture). StaticMesh to read (apply_mesh_hole_fill, apply_mesh_mirror, apply_mesh_remesh, apply_mesh_simplify). Asset to check (asset_health_check). Alias for skeletalMeshPath (bind_cloth_to_section, read_cloth_data, set_cloth_config, unbind_cloth_from_section). StaticMesh or SkeletalMesh asset path (check_uvs, export_uv_layout, get_mesh_bounds, get_mesh_geometry, get_mesh_info, measure_mesh_geometry, read_uv_channels, set_socket_transform, set_uv_channel_count, transform_uvs, unwrap_uvs). CustomizableObject asset path (compile_customizable_object). Object path of the new pipeline (create_interchange_pipeline). Asset that owns the new subobject (create_subobject). Asset to delete (delete_asset). Blueprint, Skeleton or SkeletalMesh to diff from (diff_asset). UserDefinedEnum asset path (edit_user_defined_enum). UserDefinedStruct asset path (edit_user_defined_struct, list_struct_fields, rename_struct_field). Asset to export (export_asset). Texture2D asset path (export_texture, get_texture_info, set_texture_settings). Asset to reload from disk (force_reload_asset). StaticMesh asset path (generate_lightmap_uvs, get_mesh_collision, set_mesh_material, set_mesh_nav). StaticMesh whose collision to build or clear (generate_mesh_collision). Asset path; a Blueprint path reads its generated-class CDO (get_asset_properties, read_asset_properties). StringTable asset path (get_stringtable_entry, import_stringtable, import_stringtable_csv, list_stringtable_keys, read_stringtable, remove_stringtable_entry, set_stringtable_entry). UEnum or UserDefinedEnum asset path (list_enum_values). SkeletalMesh or Skeleton asset path (list_skeleton_bones). Asset to rename in its own folder, together with newName (move_asset, rename_asset). Asset path (read_asset). Imported asset path (read_import_sources). SkeletalMesh asset path (read_skeletal_mesh_build_settings, read_skeletal_mesh_skin_weights, set_sk_material_slots, set_skeletal_mesh_optimize_for_instancing, set_skeletal_mesh_skin_weights). StaticMesh to recenter (recenter_pivot). Imported asset to rebuild from its source file (reimport_asset). Asset whose package to reload (reload_package). Asset to save; omit to save every dirty asset under /Game (save_asset)"),
  assetPathA: z.string().optional().describe("First Texture2D"),
  assetPathB: z.string().optional().describe("Second Texture2D"),
  assetPaths: z.array(z.string()).optional().describe("The exact assets to read (bulk_read_asset_properties). Assets to delete (delete_asset_batch). The exact assets to act on, the safest way to drive it (fix_asset_hygiene). StaticMeshes to recenter together; the first sets the reference pivot (recenter_pivot)"),
  assignments: z.array(z.object({ assetPath: z.string().describe("StaticMesh or SkeletalMesh asset path"), materialPath: z.string().describe("Material to assign"), slotName: z.string().optional().describe("Slot by name, which survives a reimport reordering slot indices"), slotIndex: z.number().int().optional().describe("Slot by index (default 0); refused when it disagrees with slotName") })).optional().describe("Slot assignments, max 500"),
  autoCompact: z.boolean().optional().describe("Close the gaps the collapse leaves in the index space (default true)"),
  autoDetectBoxes: z.boolean().optional().describe("Use a box where one fits (default true)"),
  autoDetectCapsules: z.boolean().optional().describe("Use a capsule where one fits (default true)"),
  autoDetectSpheres: z.boolean().optional().describe("Use a sphere where one fits (default true)"),
  automated: z.boolean().optional().describe("Suppress interactive dialogs (default true)"),
  axis: z.string().optional().describe("x | y | z (default): the axis slice cuts run across (apply_mesh_fracture). x (default) | y | z | custom: the mirror plane's normal, through planeOrigin (apply_mesh_mirror)"),
  b: z.string().optional().describe("Alias for assetPathB"),
  backupPath: z.string().optional().describe("Copy the source here before an inPlace edit, the only way back to the original geometry"),
  backupToChannel: z.number().optional().describe("Copy the existing UVs here first"),
  bHasNavigationData: z.boolean().optional().describe("Whether the mesh generates navigation data"),
  boneName: z.string().optional().describe("Bone to attach to (SkeletalMesh and Skeleton, default root)"),
  boundaryConstraint: z.string().optional().describe("Fixed | Refine | Free (default) | Ignore"),
  channel: z.number().optional().describe("Channel to draw (default 0) (export_uv_layout). Channel to remove (op=remove) (set_uv_channel_count). Channel to transform (default 0) (transform_uvs). Channel to write (default 0) (unwrap_uvs)"),
  channelCount: z.number().optional().describe("Target channel count (op=set)"),
  channels: z.array(z.number()).optional().describe("Only these channels (omit for every channel)"),
  checks: z.array(z.string()).optional().describe("unreferenced | brokenReferences | duplicates | naming | redirectors (default all five)"),
  class: z.string().optional().describe("Alias for className"),
  classFilter: z.string().optional().describe("Asset class name, exact or substring (list_assets). Only assets whose class name contains this (search_assets_fts)"),
  className: z.string().optional().describe("Concrete UObject class: class name with or without the C++ prefix, or a /Script/Module.Class path (create_asset_by_class). UDataAsset subclass: class name with or without the C++ prefix, or a /Script/Module.Class path (create_data_asset). Class to instantiate, e.g. a /Script/Module.Class path (create_subobject)"),
  classNames: z.array(z.string()).optional().describe("Only these asset class names (audit_asset_hygiene, fix_asset_hygiene). Only assets of these classes (bulk_read_asset_properties)"),
  clearColor: z.record(z.unknown()).optional().describe("Linear clear color {r, g, b, a} (default transparent)"),
  clearNavCollision: z.boolean().optional().describe("Remove the mesh's NavCollision"),
  clothingAsset: z.string().optional().describe("Clothing asset by name (optional when the mesh has one) (bind_cloth_to_section). Only the clothing asset with this name (set_cloth_config). Refuse unless the section is bound to this clothing asset (unbind_cloth_from_section)"),
  combineMeshes: z.boolean().optional().describe("Combine every mesh in the file into one (default false)"),
  compressionSettings: z.string().optional().describe("Compression setting such as Default, Normalmap, Grayscale, HDR or BC7, applied to the imported texture (import_texture). Default, Normalmap, Grayscale, Displacementmap, VectorDisplacementmap, HDR, EditorIcon, Alpha, DistanceFieldFont, HDR_Compressed or BC7 (set_texture_settings)"),
  configType: z.string().optional().describe("Only configs whose class or key contains this"),
  continueOnError: z.boolean().optional().describe("Apply the items that passed preflight instead of aborting the batch (default false) (bulk_set_asset_properties). Apply the candidates that passed preflight instead of aborting (default false) (fix_asset_hygiene). Apply the assignments that passed preflight instead of aborting the batch (default false) (set_mesh_materials_batch)"),
  copyCollisionFromSource: z.boolean().optional().describe("Copy the source's simple collision and trace flag onto the written asset (default true)"),
  copyCollisionFromTarget: z.boolean().optional().describe("Copy the target's simple collision onto the result (default true)"),
  copyMaterialsFromSource: z.boolean().optional().describe("Copy the source's material slots onto the written asset (default true)"),
  copyMaterialsFromTarget: z.boolean().optional().describe("Copy the target's material slots onto the result (default true)"),
  count: z.number().optional().describe("Channels to add (op=add, default 1)"),
  countBy: z.array(z.string()).optional().describe("Dot paths to build value histograms for (max 8)"),
  countOnly: z.boolean().optional().describe("Return the aggregates without rows"),
  createPhysicsAsset: z.boolean().optional().describe("Create a PhysicsAsset (default false)"),
  csvPath: z.string().optional().describe("Alias for filePath (import_curvetable, import_stringtable). CSV file; a relative path resolves against the project directory (import_stringtable_csv)"),
  csvString: z.string().optional().describe("CSV rows to replace the table from"),
  cursor: z.string().optional().describe("Resume a paged read: pass back the nextCursor from the previous page, unmodified"),
  curveType: z.string().optional().describe("simple | rich (default: the table's type, or rich for cubic)"),
  data: z.record(z.unknown()).optional().describe("Alias for row"),
  decompositionErrorTolerance: z.number().optional().describe("Volume error the decomposition may accept before splitting further (default 0)"),
  decompositionSearchFactor: z.number().optional().describe("0 to 1, how hard the decomposition searches for a better split (default 0.5)"),
  deleteIsolatedTriangles: z.boolean().optional().describe("Delete floating disconnected triangles, whose holes no fill can close (default true)"),
  destinationChannel: z.number().optional().describe("Channel it writes (default: the current setting, or the next free channel)"),
  destinationPath: z.string().optional().describe("Object path of the copy (duplicate_asset). Alias for packagePath (import_animation, import_file, import_skeletal_mesh, import_static_mesh, import_texture). New object path, together with sourcePath (move_asset, rename_asset). Content folder to move it to (move_folder)"),
  directories: z.array(z.string()).optional().describe("Content paths to sweep, mount-rooted such as /Game/Characters (default /Game)"),
  directory: z.string().optional().describe("Content path to sweep, added to directories (default /Game) (audit_asset_hygiene, fix_asset_hygiene). Content folder to sweep, narrowed by classNames (bulk_read_asset_properties). Content path to list (default /Game) (list_assets). Only textures whose object path starts with this (default /Game/) (list_textures). Content path to rescan (default /Game) (reindex_assets_fts)"),
  discardAttributes: z.boolean().optional().describe("Throw away UVs, normals and material IDs so the remesher moves freely (default false)"),
  discardUnsaved: z.boolean().optional().describe("Reload even when the package has unsaved changes, discarding them (default false)"),
  displayName: z.string().optional().describe("Display text for the enumerator (add_value, rename_value)"),
  dryRun: z.boolean().optional().describe("Run the operation and report the result without writing (default false) (apply_mesh_fracture, apply_mesh_hole_fill, apply_mesh_mirror, apply_mesh_remesh, apply_mesh_simplify, generate_mesh_collision). Preflight and report without writing (default false) (bulk_set_asset_properties, set_mesh_materials_batch). Run the full preflight and report the planned statuses without writing (default false) (bulk_upsert_data_assets). Name every asset and destination without changing anything (default TRUE) (fix_asset_hygiene). Report the redirectors, referencers and packages it would load and save, and stop (default false) (fixup_redirectors). Report without writing (generate_lightmap_uvs, set_uv_channel_count, transform_uvs, unwrap_uvs). Run the boolean and report the counts without writing (default false) (mesh_boolean)"),
  dumpToFile: z.boolean().optional().describe("Write the geometry JSON to a file instead of returning it"),
  duplicateMethod: z.string().optional().describe("content (same class, size and saved hash) | name (same name and class in several folders) | both (default)"),
  edgeLength: z.number().optional().describe("Target edge length in centimetres (simplifyMode edgeLength or clusterEdgeLength)"),
  edits: z.array(z.object({ vertexIndex: z.number().int().describe("Source MeshDescription vertex ID"), influences: z.array(z.record(z.unknown())).describe("1-64 entries of {boneName, weight? (0 to 1) | rawWeight? (1 to 65535)}") })).optional().describe("Selected source vertices and their complete replacement influences (1-256)"),
  elements: z.array(z.unknown()).optional().describe("Values to append, validated before any is written"),
  enable: z.boolean().optional().describe("Turn lightmap UV generation on (default) or off"),
  enabled: z.boolean().optional().describe("Value to write to bOptimizeForInstancing"),
  excludePaths: z.array(z.string()).optional().describe("Skip packages whose name starts with one of these"),
  expandDepth: z.number().int().optional().describe("Inline the properties of subobjects the asset owns to this depth, 0-5 (default 0)"),
  expandExternal: z.boolean().optional().describe("Also follow references to other assets when expanding (default false)"),
  expectedKeys: z.array(z.string()).optional().describe("Keys the CSV must carry, checked before the asset is touched"),
  factoryClass: z.string().optional().describe("UFactory subclass by name or /Script path (default: picked by extension)"),
  factoryProperties: z.record(z.unknown()).optional().describe("Factory properties by UPROPERTY name or dotted path, set before the import"),
  fieldGuid: z.string().optional().describe("Member by GUID, stable across renames; wins over fieldName (edit_user_defined_struct). Member by GUID, stable across renames (rename_struct_field)"),
  fieldName: z.string().optional().describe("Member by friendly or internal name; add_field names the new member with it (edit_user_defined_struct). Member by friendly or internal name (rename_struct_field). Row-struct field to write (set_datatable_cell)"),
  fields: z.record(z.unknown()).optional().describe("Alias for row"),
  filename: z.string().optional().describe("Alias for filePath"),
  filePath: z.string().optional().describe("Alias for outputPath (export_texture). Source file on disk (import_animation, import_file, import_skeletal_mesh, import_static_mesh, import_texture). JSON or CSV file to replace the table from (import_curvetable). CSV file to merge into the table (import_stringtable). Alias for csvPath (import_stringtable_csv). New source file to record and reimport from (reimport_asset)"),
  fillHoles: z.boolean().optional().describe("Cap each piece where the plane cut it (default true) (apply_mesh_fracture). Close the holes the cut opens (default true) (mesh_boolean)"),
  fillMethod: z.string().optional().describe("Automatic (default) | MinimalFill | PolygonTriangulation | TriangleFan | PlanarProjection"),
  fix: z.string().optional().describe("naming (rename to the convention) | unreferenced (quarantine or delete)"),
  flipCutSide: z.boolean().optional().describe("Keep the other side of the plane instead (default false)"),
  flipU: z.boolean().optional().describe("Mirror across U"),
  flipV: z.boolean().optional().describe("Mirror across V"),
  force: z.boolean().optional().describe("Delete even when other packages reference it, closing open editors (default false) (delete_asset). Delete referenced assets too, closing open editors (default false) (delete_asset_batch). Also delete the assets inside (default false: only empty folders) (delete_folder). Rebuild even when the settings already match (generate_lightmap_uvs). World renames only: merge into a destination that already holds external packages (used by rollback) (move_asset, rename_asset). Write the package even when it is not dirty (needs assetPath) (save_asset)"),
  format: z.string().optional().describe("R8 | RG8 | RGBA8 | RGBA8_SRGB | R16F | RG16F | RGBA16F | R32F | RG32F | RGBA32F | RGB10A2 (default RGBA8_SRGB) (create_render_target_2d). json | csv (default: from the file extension) (import_curvetable)"),
  fromChannel: z.number().optional().describe("Source channel (op=copy)"),
  gapWidth: z.number().optional().describe("How far apart the halves of each cut are pushed, in centimetres (default 0.01)"),
  generateLightmapUVs: z.boolean().optional().describe("Generate lightmap UVs (default true)"),
  generateMips: z.boolean().optional().describe("Generate mipmaps automatically (default false)"),
  graphName: z.string().optional().describe("Graph to author in, by name or unique substring (required when the asset has several graphs) (add_graph_node, connect_graph_pins, disconnect_graph_pins, remove_graph_node). Only graphs whose name contains this (read_asset_graph)"),
  gridX: z.number().int().optional().describe("Pieces along X for grid (default 2)"),
  gridY: z.number().int().optional().describe("Pieces along Y for grid (default 2)"),
  gridZ: z.number().int().optional().describe("Pieces along Z for grid (default 1)"),
  groupBy: z.string().optional().describe("Dot path to group matches by (max 200 groups)"),
  groups: z.record(z.unknown()).optional().describe("Texture paths per profile: {normal?, grayscale?, baseColor?, hdr?}"),
  hard: z.boolean().optional().describe("Include hard dependencies (default true)"),
  height: z.number().int().optional().describe("Pixel height, 1-8192 (default 512)"),
  hullTargetFaceCount: z.number().int().optional().describe("Faces to simplify each hull down to (default 25)"),
  ignoreRedirectorReferencers: z.boolean().optional().describe("Do not count a redirector stub as a referencer (default true)"),
  imageSize: z.number().optional().describe("PNG edge length (default 1024, max 4096)"),
  importCustomAttribute: z.boolean().optional().describe("Import FBX custom attributes as curves (default true)"),
  importMaterials: z.boolean().optional().describe("Import materials (default true)"),
  importMorphTargets: z.boolean().optional().describe("Import morph targets (default true)"),
  importSettings: z.record(z.unknown()).optional().describe("FbxAnimSequenceImportData or FbxImportUI fields by UPROPERTY name or dotted path"),
  importTextures: z.boolean().optional().describe("Import textures (default true)"),
  importUniformScale: z.number().optional().describe("Uniform import scale (default 1.0); 100 fixes metre-authored FBX (import_skeletal_mesh). Uniform import scale; 100 fixes metre-authored FBX (import_static_mesh)"),
  include: z.array(z.string()).optional().describe("positions | uvs | normals | triangles (omit for all four)"),
  includeIslands: z.boolean().optional().describe("Include the per-island breakdown (default true)"),
  includeOverlap: z.boolean().optional().describe("Compute the overlapping-area fraction (default true)"),
  includePins: z.boolean().optional().describe("Include each node's pins and links (default true)"),
  includeTransforms: z.boolean().optional().describe("Include rest-pose transforms (default true)"),
  includeValues: z.boolean().optional().describe("Include each property's value (default true) (get_asset_properties). Include each property's value (default false) (read_asset_properties)"),
  includeWorlds: z.boolean().optional().describe("Report maps as unreferenced too (default false)"),
  index: z.number().optional().describe("Enumerator index for rename_value and remove_value"),
  initialPatchCount: z.number().optional().describe("Starting patch count for patchBuilder"),
  inPlace: z.boolean().optional().describe("Overwrite assetPath instead of writing a separate asset (default false); not with outputPath, and the one form with no rollback (apply_mesh_hole_fill, apply_mesh_mirror, apply_mesh_remesh, apply_mesh_simplify). Overwrite targetPath instead (default false); not with outputPath, and the only destructive form (mesh_boolean)"),
  interpMode: z.string().optional().describe("linear (default) | constant | cubic | none"),
  islandSource: z.string().optional().describe("UVIslands (default) | PolyGroups"),
  items: z.array(z.record(z.unknown())).optional().describe("Asset updates: [{assetPath, properties}], max 500 (bulk_set_asset_properties). DataAsset descriptors: [{name, packagePath, className, properties?}], max 500 (bulk_upsert_data_assets). Textures to import: [{filePath, packagePath?, name?, replaceExisting?}] (import_texture_batch)"),
  iterations: z.number().int().optional().describe("Remeshing passes (default: the engine's)"),
  jitter: z.number().optional().describe("0 to 0.45, how far a slice or grid cut may wander from even spacing (default 0)"),
  jsonPath: z.string().optional().describe("Alias for filePath (import_curvetable). JSON file to replace the table from (reimport_datatable)"),
  jsonString: z.string().optional().describe("The same rows object as JSON text, used when rows is omitted (fill_datatable_from_json). JSON rows to replace the table from (import_curvetable). JSON text to replace the table from (reimport_datatable)"),
  keepPaths: z.array(z.string()).optional().describe("Never report or touch packages whose name starts with one of these, such as a folder loaded by name from config"),
  key: z.string().optional().describe("Entry key"),
  keyFilter: z.string().optional().describe("Case-insensitive substring filter on keys"),
  keys: z.array(z.record(z.unknown())).optional().describe("Replacement keys: [{time, value, interpMode?, arriveTangent?, leaveTangent?}]"),
  keyTimeTolerance: z.number().optional().describe("How close an existing key must be to be updated rather than added"),
  lightmapResolution: z.number().optional().describe("The mesh's lightmap resolution"),
  limit: z.number().int().optional().describe("Rows to return (default 200, max 2000) (bulk_read_asset_properties). Rows per page, 1 to 5000 (default 500) (list_assets). Rows per page, 1 to 2000 (default 50) (list_textures, search_assets_fts)"),
  lodGroup: z.string().optional().describe("Texture LOD group, applied to the imported texture (import_texture). Texture LOD group: World, WorldNormalMap, Character, UI, Lightmap, Effects and the rest (set_texture_settings)"),
  lodIndex: z.number().int().optional().describe("LOD to read when lodType names one (default 0) (apply_mesh_fracture, apply_mesh_hole_fill, apply_mesh_mirror, apply_mesh_remesh, apply_mesh_simplify, generate_mesh_collision). Mesh LOD (bind_cloth_to_section, unbind_cloth_from_section). Source LOD to act on (default 0) (check_uvs, export_uv_layout, generate_lightmap_uvs, read_uv_channels, set_uv_channel_count, transform_uvs, unwrap_uvs). LOD to read (default 0) (get_mesh_geometry). LOD to measure (default 0) (measure_mesh_geometry). LOD of each input to read when lodType names one (default 0) (mesh_boolean). LOD to target (default 0); not with allLods (read_skeletal_mesh_build_settings, set_skeletal_mesh_optimize_for_instancing). Source LOD (default 0); generated LODs without source geometry are refused (read_skeletal_mesh_skin_weights, set_skeletal_mesh_skin_weights)"),
  lodType: z.string().optional().describe("MaxAvailable (default, ignores lodIndex) | HiResSourceModel | SourceModel | RenderData"),
  markAsCustomized: z.boolean().optional().describe("Mark the collision customized so a reimport keeps it (default true)"),
  matchSubclasses: z.boolean().optional().describe("Also match subclasses of classNames (default true)"),
  materialPath: z.string().optional().describe("Material to assign"),
  maxAssets: z.number().int().optional().describe("Assets the sweep walks before it reports scanTruncated (default 20000, max 200000) (audit_asset_hygiene, fix_asset_hygiene). Refuse to load more candidates than this (default 2000, max 20000) (bulk_read_asset_properties)"),
  maxConvexHulls: z.number().int().optional().describe("Convex hulls the decomposition may produce (default 1)"),
  maxExpandedObjects: z.number().int().optional().describe("Cap on expanded objects (default 64)"),
  maxFixes: z.number().int().optional().describe("Refuse the call past this many assets (default 100)"),
  maxIssues: z.number().int().optional().describe("Findings of each kind to list (default 200); the counts are always complete"),
  maxIterations: z.number().optional().describe("Solver iteration cap"),
  maxNodes: z.number().optional().describe("Nodes reported per graph (default 500, max 5000)"),
  maxOverlapFraction: z.number().optional().describe("Overlap above this fraction of the lightmap channel is a fault (default 0.001)"),
  maxResults: z.number().int().optional().describe("Most ids to return (default 1000) (get_primary_asset_ids). Alias for limit (list_assets, list_textures, search_assets_fts)"),
  maxShapeCount: z.number().int().optional().describe("Cap on the shapes produced (default 0, uncapped)"),
  meshType: z.string().optional().describe("skeletal (default) | static"),
  method: z.string().optional().describe("StandardQEM | VolumePreserving | AttributeAware (default) | AttributeAwareV2 (apply_mesh_simplify). AlignedBoxes | OrientedBoxes | MinimalSpheres | Capsules | ConvexHulls (default) | SweptHulls | MinVolumeShapes | LevelSets (generate_mesh_collision). xatlas (default) | patchBuilder | expMap | conformal | spectralConformal | planar | box | cylinder (unwrap_uvs)"),
  minLightmapResolution: z.number().optional().describe("Packing resolution floor (default 64)"),
  minPieceTriangles: z.number().int().optional().describe("Discard pieces with fewer triangles than this (default 4)"),
  minThickness: z.number().optional().describe("Thinnest a shape may be, in centimetres (default 1)"),
  mode: z.string().optional().describe("Alias for curveType"),
  name: z.string().optional().describe("Asset name (create_asset_by_class, create_curvetable, create_customizable_object, create_data_asset, create_datatable, create_stringtable, create_user_defined_enum, create_user_defined_struct). Pipeline asset name, placed in packagePath (create_interchange_pipeline). Asset name, without '/' or '.' (create_render_target_2d). Subobject name (create_subobject). Enumerator to act on by short or display name; add_value uses it as the display name when displayName is omitted (edit_user_defined_enum). Asset name (default: the file name) (import_animation, import_file, import_skeletal_mesh, import_static_mesh, import_texture)"),
  namespace: z.string().optional().describe("StringTable namespace"),
  namingRuleMode: z.string().optional().describe("merge (default, the caller's rules over the built-in table) | replace"),
  namingRules: z.array(z.object({ class: z.string().describe("Asset class name the registry reports, such as StaticMesh or WidgetBlueprint, not a class path"), prefix: z.string().optional().describe("Name prefix the class requires"), suffix: z.string().optional().describe("Name suffix the class requires") })).optional().describe("Naming convention entries"),
  nanite: z.string().optional().describe("inherit (default, match the source) | enable | disable (apply_mesh_fracture, apply_mesh_hole_fill, apply_mesh_mirror, apply_mesh_remesh, apply_mesh_simplify). inherit (default, match the target) | enable | disable (mesh_boolean)"),
  neverStream: z.boolean().optional().describe("Applied to the imported texture (import_texture). Keep every mip resident (set_texture_settings)"),
  newDisplayName: z.string().optional().describe("New display name for rename_field; add_field falls back to it for the name (edit_user_defined_struct). New display name; the member GUID is kept (rename_struct_field)"),
  newName: z.string().optional().describe("New asset name, together with assetPath (move_asset, rename_asset). New row name (rename_curvetable_row, rename_datatable_row)"),
  noAlpha: z.boolean().optional().describe("Factory option: drop the alpha channel"),
  noCompression: z.boolean().optional().describe("Factory option: import uncompressed"),
  node: z.string().optional().describe("Node to remove: nodeGuid, path, name or unique title"),
  nodeClass: z.string().optional().describe("Class the node action spawns"),
  offset: z.number().optional().describe("Refused: the row offset was replaced by cursor paging, because a row number cannot report that the folder changed. Use cursor and limit"),
  oldName: z.string().optional().describe("Row to rename"),
  onConflict: z.string().optional().describe("skip (default) | update | error (add_socket). When a piece asset exists: error (default) | replace (apply_mesh_fracture). When the output asset exists: error (default) | replace (apply_mesh_hole_fill, apply_mesh_mirror, apply_mesh_remesh, apply_mesh_simplify). update (default) | skip | error (bulk_upsert_data_assets). skip (default) returns an existing asset; error refuses (create_asset_by_class, create_data_asset, create_datatable, create_render_target_2d). skip (default) returns an existing table; error refuses (create_curvetable, create_stringtable). skip (default) | error (create_customizable_object). skip (default) returns an existing pipeline; error refuses (create_interchange_pipeline). reuse (default) | error (create_subobject). skip (default) returns an existing enum; error refuses (create_user_defined_enum). skip (default) returns an existing struct; error refuses (create_user_defined_struct). skip (default) returns an existing destination; error refuses (duplicate_asset). When outputPath exists: error (default) | replace (mesh_boolean)"),
  op: z.string().optional().describe("add_value | rename_value | remove_value (edit_user_defined_enum). add_field | rename_field | set_field_type | remove_field (edit_user_defined_struct). generate (default) | clear (generate_mesh_collision). set (default) | add | remove | copy (set_uv_channel_count)"),
  operation: z.string().optional().describe("union | subtract | intersect | trimInside | trimOutside | newPolyGroupInside | newPolyGroupOutside"),
  optimizationLevel: z.string().optional().describe("Optimization level enum name, e.g. None or Maximum (default: the compile function's own)"),
  options: z.record(z.unknown()).optional().describe("Dotted-path overrides on the new pipeline, e.g. {'MeshPipeline.bBuildNanite': true}"),
  order: z.string().optional().describe("flipScaleRotateTranslate (default) | translateRotateScaleFlip"),
  origin: z.record(z.unknown()).optional().describe("Pivot for rotate and scale {u, v} (default 0.5, 0.5)"),
  otherPath: z.string().optional().describe("Asset of the same class to compare against"),
  outer: z.string().optional().describe("asset (default) | package"),
  outputBasePath: z.string().optional().describe("Pieces are written as outputBasePath_00, _01 and on (default '<assetPath>_Piece')"),
  outputPath: z.string().optional().describe("Asset to write (default '<assetPath>_Filled') (apply_mesh_hole_fill). Asset to write (default '<assetPath>_Mirrored') (apply_mesh_mirror). Asset to write (default '<assetPath>_Remeshed') (apply_mesh_remesh). Asset to write (default '<assetPath>_Simplified') (apply_mesh_simplify). Write every matched row to this JSON file and return the path instead of the rows (bulk_read_asset_properties). File to write; a relative path resolves against the project directory (export_asset). PNG file to write; a relative path resolves against the project directory (export_texture). PNG to write (default under Saved/UVLayouts) (export_uv_layout). File for dumpToFile; relative paths resolve under Saved/ (get_mesh_geometry). Asset to write (default '<targetPath>_<Operation>') (mesh_boolean). Write the rows to this JSON file instead of returning them; relative paths resolve under Saved/ (read_datatable)"),
  pack: z.boolean().optional().describe("Repack the islands after unwrapping (default true)"),
  packagePath: z.string().optional().describe("Destination folder (default /Game) (create_asset_by_class, create_customizable_object, create_data_asset, create_render_target_2d, create_user_defined_enum, create_user_defined_struct). Destination folder (default /Game/CurveTables) (create_curvetable). Destination folder (default /Game/DataTables) (create_datatable). Folder for name (default /Game/Import) (create_interchange_pipeline). Destination folder (default /Game/StringTables) (create_stringtable). One package path, used when packages is omitted (get_asset_dependencies, get_asset_referencers). Destination folder (default /Game/Animations) (import_animation). Destination folder (import_file). Destination folder (default /Game/Meshes) (import_skeletal_mesh, import_static_mesh). Destination folder (default /Game/Textures) (import_texture). Folder for items that name none (default /Game/Textures) (import_texture_batch)"),
  packages: z.array(z.string()).optional().describe("Package paths to look up"),
  path: z.string().optional().describe("Alias for assetPath (add_curvetable_key, add_curvetable_row, add_datatable_row, add_graph_node, append_asset_array_elements, apply_mesh_fracture, apply_mesh_hole_fill, apply_mesh_mirror, apply_mesh_remesh, apply_mesh_simplify, asset_health_check, check_uvs, compile_customizable_object, connect_graph_pins, create_subobject, delete_asset, diff_asset, disconnect_graph_pins, edit_user_defined_enum, edit_user_defined_struct, export_asset, export_texture, export_uv_layout, fill_datatable_from_json, force_reload_asset, generate_lightmap_uvs, generate_mesh_collision, get_asset_properties, get_curvetable_keys, get_datatable_row, get_mesh_geometry, get_stringtable_entry, get_texture_info, import_curvetable, import_stringtable, import_stringtable_csv, list_curvetable_rows, list_enum_values, list_skeleton_bones, list_stringtable_keys, list_struct_fields, measure_mesh_geometry, read_asset, read_asset_graph, read_asset_properties, read_curvetable, read_datatable, read_import_sources, read_stringtable, read_uv_channels, recenter_pivot, reimport_asset, reimport_datatable, reload_package, remove_curvetable_row, remove_datatable_row, remove_graph_node, remove_stringtable_entry, rename_curvetable_row, rename_datatable_row, rename_struct_field, save_asset, set_asset_property, set_curvetable_keys, set_datatable_cell, set_datatable_row, set_mesh_material, set_sk_material_slots, set_stringtable_entry, set_texture_settings, set_uv_channel_count, transform_uvs, unwrap_uvs, update_datatable_row). Content folder to create, e.g. /Game/Foo (create_folder). Content folder to delete (delete_folder). Content path to diagnose (diagnose_registry)"),
  paths: z.array(z.string()).optional().describe("Content folders to create; combined with path (create_folder). Alias for assetPaths (delete_asset_batch). Content folders to delete (delete_folder). Redirector packages, or the folders holding them (fixup_redirectors)"),
  pattern: z.string().optional().describe("slice (default) | grid | random"),
  pieces: z.number().int().optional().describe("Pieces along axis for slice, at least 2 (default 4)"),
  planeCount: z.number().int().optional().describe("Random planes to cut with (default 3)"),
  planeNormal: z.object({ x: z.number(), y: z.number(), z: z.number() }).optional().describe("The mirror plane's normal when axis is custom; a zero vector is refused"),
  planeOrigin: z.object({ x: z.number(), y: z.number(), z: z.number() }).optional().describe("A point on the mirror plane (default the mesh origin)"),
  posX: z.number().optional().describe("Node X position"),
  posY: z.number().optional().describe("Node Y position"),
  preserveVertexOrder: z.boolean().optional().describe("Keep the existing vertex order (default true)"),
  preserveVertexPositions: z.boolean().optional().describe("Keep surviving vertices where they are (default false)"),
  profileName: z.string().optional().describe("Existing skin-weight profile; omit or pass 'default' for the default profile"),
  projectionTransform: z.record(z.unknown()).optional().describe("Transform for the planar, box and cylinder projections"),
  properties: z.record(z.unknown()).optional().describe("Property values keyed by property name (create_asset_by_class, create_data_asset). Property values, validated on a throwaway instance first (create_subobject). Config properties to set by reflection (set_cloth_config)"),
  propertyName: z.string().optional().describe("Path of the TArray property (append_asset_array_elements). One property to read; dotted and indexed paths walk structs, arrays and instanced subobjects (get_asset_properties, read_asset_properties). Property path; dotted and indexed paths walk structs, arrays and instanced subobjects (set_asset_property)"),
  propertyNames: z.array(z.string()).optional().describe("Property paths to read off every matched asset; dotted paths walk nested structs (max 32)"),
  quarantineFolder: z.string().optional().describe("Where quarantined assets move, keeping their folder shape (default /Game/Quarantine)"),
  query: z.string().optional().describe("Words scored against each asset's name, class and path"),
  rasterSize: z.number().optional().describe("Raster resolution for the overlap estimate (default 512) (check_uvs). Raster resolution for the result's overlap estimate (default 512) (generate_lightmap_uvs, unwrap_uvs). Raster resolution for the coverage and overlap estimate (default 512) (read_uv_channels)"),
  recomputeNormals: z.boolean().optional().describe("Recompute normals on the written mesh (default false)"),
  recomputeTangents: z.boolean().optional().describe("Recompute tangents on the written mesh (default false)"),
  reconcile: z.boolean().optional().describe("Force a synchronous rescan first, which evicts pending-kill ghosts"),
  recursive: z.boolean().optional().describe("Include subfolders (default true) (audit_asset_hygiene, diagnose_registry, fix_asset_hygiene, list_assets). Include subfolders of directory (default true) (bulk_read_asset_properties)"),
  relativeDensity: z.number().optional().describe("-2 to 2, bias toward more or fewer triangles in curved regions (remeshMode adaptive, default 0)"),
  relativeLocation: z.object({ x: z.number(), y: z.number(), z: z.number() }).optional().describe("Socket location relative to its parent (add_socket). New relative location (set_socket_transform)"),
  relativeRotation: z.object({ pitch: z.number(), yaw: z.number(), roll: z.number() }).optional().describe("Socket rotation relative to its parent (add_socket). New relative rotation (set_socket_transform)"),
  relativeScale: z.object({ x: z.number(), y: z.number(), z: z.number() }).optional().describe("Socket scale (add_socket). New relative scale (set_socket_transform)"),
  remeshMode: z.string().optional().describe("uniform (default, even edge lengths) | adaptive (denser where the surface curves)"),
  removeDegenerateFirst: z.boolean().optional().describe("Drop zero-area triangles before welding and filling (default false)"),
  removeDegenerates: z.boolean().optional().describe("Drop degenerate triangles when writing back into an existing mesh (default false)"),
  removeFullyContainedShapes: z.boolean().optional().describe("Drop shapes entirely inside another (default true)"),
  removeRedundantKeys: z.boolean().optional().describe("Strip keys that do not change the value (default true)"),
  renames: z.array(z.record(z.unknown())).optional().describe("Rename descriptors: {sourcePath, destinationPath}, {assetPath, newName} or {sourcePath, newPackagePath, newName}"),
  replaceExisting: z.boolean().optional().describe("Replace an asset already at the destination (default true) (import_file, import_skeletal_mesh). Remove entries the CSV does not carry (default false) (import_stringtable_csv)"),
  reprojectToInputMesh: z.boolean().optional().describe("Pull new vertices back onto the original surface each pass (default true)"),
  requireExactKeys: z.boolean().optional().describe("Also fail when the CSV carries keys expectedKeys does not list (default false)"),
  requireLightmapChannel: z.boolean().optional().describe("Treat a missing lightmap channel as a fault (default true for StaticMesh)"),
  restoreRawWeights: z.boolean().optional().describe("Rollback payloads only: restore the exact uint16 rawWeight values"),
  rotate: z.number().optional().describe("Rotation in degrees"),
  row: z.record(z.unknown()).optional().describe("Row-struct fields to write; fields not named keep their values"),
  rowFilter: z.string().optional().describe("Case-insensitive substring filter on row names"),
  rowName: z.string().optional().describe("Row to key (add_curvetable_key). Row to add (add_curvetable_row). Row to append or overwrite (add_datatable_row, set_datatable_row, update_datatable_row). Row to read (get_curvetable_keys, get_datatable_row). Row to remove (remove_curvetable_row, remove_datatable_row). Alias for oldName (rename_curvetable_row, rename_datatable_row). Row whose keys to replace (set_curvetable_keys). Existing row to edit (set_datatable_cell)"),
  rows: z.record(z.unknown()).optional().describe("Rows to upsert: {rowName: {field: value}}"),
  rowStruct: z.string().optional().describe("Row struct, e.g. /Script/Module.MyRow or a UserDefinedStruct path"),
  sampleLimit: z.number().int().optional().describe("Sample asset names per group (default 5, max 25)"),
  save: z.boolean().optional().describe("Save the asset after the edit (default true) (add_graph_node, connect_graph_pins, disconnect_graph_pins, remove_graph_node). Save the package after the write (default true) (append_asset_array_elements, set_asset_property). Save the written asset (default true) (apply_mesh_fracture, apply_mesh_hole_fill, apply_mesh_mirror, apply_mesh_remesh, apply_mesh_simplify, generate_mesh_collision, mesh_boolean). Save the changed packages (default true) (bulk_set_asset_properties, bulk_upsert_data_assets). Save the new asset (default true) (create_customizable_object). Save the owning package (default true) (create_subobject). Save what changed (default true) (fix_asset_hygiene). Run the explicit save pass (default true); the editor's own fix-up writes what it can regardless (fixup_redirectors). Save the mesh (default true) (generate_lightmap_uvs, set_uv_channel_count, transform_uvs, unwrap_uvs). Save the imported assets (default false) (import_file). Save the table (default true) (import_stringtable_csv). Save the imported textures (default true) (import_texture_batch). Save each changed mesh (default true) (set_mesh_materials_batch)"),
  saveContentPackages: z.boolean().optional().describe("Include content packages (default true)"),
  saveMapPackages: z.boolean().optional().describe("Include map packages (default true)"),
  scale: z.record(z.unknown()).optional().describe("UV-space scale {u, v}; a zero component is refused"),
  sectionIndex: z.number().int().optional().describe("Render section to bind (bind_cloth_to_section). One render section (omit for every section) (get_mesh_geometry). One render section (omit to measure the whole LOD) (measure_mesh_geometry). Render section to unbind (unbind_cloth_from_section)"),
  seed: z.number().int().optional().describe("Seed for plane placement and jitter (default 0); the same seed gives the same pieces"),
  selection: z.record(z.unknown()).optional().describe("What to transform: {mode: all|island|normal|polygonGroup, islandIndices?, normalDirection?, normalAngleTolerance?, polygonGroups?, materialSlotNames?}"),
  setLightmapCoordinateIndex: z.boolean().optional().describe("Point LightMapCoordinateIndex at the destination channel (default true)"),
  settings: z.record(z.unknown()).optional().describe("The four settings below as one object; a key also given at the top level wins over it"),
  showGrid: z.boolean().optional().describe("Draw the unit-square border (default true)"),
  showIslands: z.boolean().optional().describe("Colour each island separately (default true)"),
  showOverlaps: z.boolean().optional().describe("Highlight overlapping texels (default true)"),
  simplifyHulls: z.boolean().optional().describe("Simplify each hull down to hullTargetFaceCount (default true)"),
  simplifyMode: z.string().optional().describe("triangleCount (default) | vertexCount | tolerance | edgeLength | clusterEdgeLength | planar | polygroup | editorTriangleCount | editorVertexCount"),
  simplifyOutput: z.boolean().optional().describe("Collapse coplanar triangles the boolean introduced (default true)"),
  simplifyPlanarTolerance: z.number().optional().describe("How far from coplanar still counts as coplanar when simplifying (default 0.01)"),
  skeletalMeshPath: z.string().optional().describe("SkeletalMesh asset path"),
  skeletonPath: z.string().optional().describe("Skeleton the animation targets (import_animation). Existing Skeleton to import onto (import_skeletal_mesh)"),
  slotIndex: z.number().optional().describe("Material slot index (default 0)"),
  slots: z.array(z.record(z.unknown())).optional().describe("Slot assignments: [{slotName? | slotIndex?, materialPath}]"),
  smoothingRate: z.number().optional().describe("0 to 1, how far vertices move toward their neighbours each pass (default 0.25)"),
  smoothingType: z.string().optional().describe("Uniform | UVPreserving | Mixed (default)"),
  socketName: z.string().optional().describe("Socket name (add_socket). Socket to remove (remove_socket). Socket to move (set_socket_transform)"),
  soft: z.boolean().optional().describe("Include soft dependencies (default true)"),
  sourceChannel: z.number().optional().describe("Channel the generator reads (default: the current setting)"),
  sourceNode: z.string().optional().describe("Node of the source pin: nodeGuid, node path, name or unique title"),
  sourcePath: z.string().optional().describe("Asset to duplicate (duplicate_asset). Asset to rename, together with destinationPath (move_asset, rename_asset). Content folder to move (move_folder)"),
  sourcePin: z.string().optional().describe("Source pin name, when no pinId is given"),
  sourcePinDirection: z.string().optional().describe("input | output, to disambiguate sourcePin"),
  sourcePinId: z.string().optional().describe("pinId of the source pin (preferred)"),
  sourceString: z.string().optional().describe("Source string to write"),
  spawnMode: z.string().optional().describe("auto (default) | action | direct"),
  sRGB: z.boolean().optional().describe("Applied to the imported texture (import_texture). Treat the texture as sRGB (set_texture_settings)"),
  startIndex: z.number().int().optional().describe("First row index, for paging"),
  structFields: z.array(z.object({ name: z.string().describe("Member display name"), type: z.string().optional().describe("MakePinType string: bool (default), int, int64, float, string, name, text, byte, a struct such as Vector, an enum, or an object class such as Actor") })).optional().describe("Initial members; with none the struct keeps its one default member"),
  suspectOnly: z.boolean().optional().describe("Only rows where a requested property is absent or null"),
  sweptHullAxis: z.string().optional().describe("X | Y | Z (default) | SmallestBoxDimension | SmallestVolume (method SweptHulls)"),
  targetEdgeLength: z.number().optional().describe("Edge length in centimetres to aim for (targetType TargetEdgeLength, default 1)"),
  targetGamma: z.number().optional().describe("Target gamma (default 0, the engine behavior)"),
  targetNode: z.string().optional().describe("Node of the target pin"),
  targetPath: z.string().optional().describe("StaticMesh being cut; the result inherits its transform, materials, collision and Nanite setting"),
  targetPin: z.string().optional().describe("Target pin name, when no pinId is given"),
  targetPinDirection: z.string().optional().describe("input | output, to disambiguate targetPin"),
  targetPinId: z.string().optional().describe("pinId of the target pin"),
  targetTransform: z.object({ location: z.object({ x: z.number(), y: z.number(), z: z.number() }).optional().describe("Translation"), rotation: z.object({ pitch: z.number(), yaw: z.number(), roll: z.number() }).optional().describe("Rotation"), scale: z.object({ x: z.number(), y: z.number(), z: z.number() }).optional().describe("Scale (default 1)") }).optional().describe("Where the target sits for the boolean (default identity)"),
  targetTriangleCount: z.number().int().optional().describe("Approximate triangle count to aim for (targetType TriangleCount, default 1000)"),
  targetType: z.string().optional().describe("TriangleCount (default) | TargetEdgeLength"),
  textureCompression: z.string().optional().describe("None | Fast | HighQuality"),
  textureResolution: z.number().optional().describe("Resolution the packer targets (default 1024)"),
  time: z.number().optional().describe("Key time"),
  toChannel: z.number().optional().describe("Destination channel (op=copy)"),
  tolerance: z.number().optional().describe("Furthest in centimetres the surface may drift (simplifyMode tolerance)"),
  toolPath: z.string().optional().describe("StaticMesh doing the cutting"),
  toolTransform: z.object({ location: z.object({ x: z.number(), y: z.number(), z: z.number() }).optional().describe("Translation"), rotation: z.object({ pitch: z.number(), yaw: z.number(), roll: z.number() }).optional().describe("Rotation"), scale: z.object({ x: z.number(), y: z.number(), z: z.number() }).optional().describe("Scale (default 1)") }).optional().describe("Where the tool sits for the boolean (default identity)"),
  translate: z.record(z.unknown()).optional().describe("UV-space offset {u, v}"),
  triangleCount: z.number().int().optional().describe("Triangle count to reduce to (simplifyMode triangleCount or editorTriangleCount)"),
  type: z.string().optional().describe("MakePinType string for add_field (default bool) and set_field_type (edit_user_defined_struct). FPrimaryAssetType to list (omit for every type) (get_primary_asset_ids)"),
  typeFilter: z.string().optional().describe("Alias for classFilter"),
  unreferencedAction: z.string().optional().describe("quarantine (default, undoable) | delete (permanent, no rollback)"),
  uvChannel: z.number().int().optional().describe("UV channel to return (default 0)"),
  value: z.unknown().optional().describe("Key value (a number) (add_curvetable_key). Value to write: scalar, object, array or asset path (set_asset_property). Value to write (set_datatable_cell). Source string to write, when sourceString is omitted (set_stringtable_entry)"),
  valueFormat: z.string().optional().describe("text (default, Unreal export text) | json (structured values)"),
  values: z.array(z.string()).optional().describe("Initial enumerator display names"),
  vertexCount: z.number().int().optional().describe("Vertex count to reduce to, at least 4 (simplifyMode vertexCount or editorVertexCount)"),
  vertexIndices: z.array(z.number().int()).optional().describe("Source MeshDescription vertex IDs to read (1-256)"),
  weldAlongPlane: z.boolean().optional().describe("Weld the two halves along the plane (default true)"),
  weldFirst: z.boolean().optional().describe("Weld coincident boundary edges before filling (default true)"),
  weldTolerance: z.number().optional().describe("How close two boundary edges must be to weld (default 1e-6)"),
  where: z.array(z.object({ field: z.string().describe("Dot path into the row, e.g. props.CullDistance.Max, className or suspect"), op: z.string().optional().describe("eq (default), ne, lt, lte, gt, gte, contains, notContains, startsWith, endsWith, in, notIn, exists, notExists, isNull, isNotNull, isTrue, isFalse"), value: z.unknown().optional().describe("Operand the op compares against") })).optional().describe("Predicates evaluated in the editor (max 24)"),
  whereMode: z.string().optional().describe("all (default) | any"),
  width: z.number().int().optional().describe("Pixel width, 1-8192 (default 512)"),
};

/** Declare an action for a spec'd bridge method: effect, summary, method. */
export const specBp = makeSpecBp(paramsClauses, handlerSpecs);
