// GENERATED FILE - do not edit.
//
// Written by scripts/generate-handler-specs.mjs from tests/golden/handler-specs.json,
// which is recorded from the parameter specs the C++ handlers register with
// (npm run specs:record). To change a parameter, change its RegisterHandler
// spec, re-record, and regenerate (#1057).
import { z } from "zod";
import { makeSpecBp, type HandlerSpecs } from "../../surface/handler-spec.js";

/** The recorded contract of every spec'd landscape handler. */
export const handlerSpecs: HandlerSpecs = {
  "add_landscape_layer_info": {
    "category": "landscape",
    "params": [
      {
        "name": "layerName",
        "type": "string",
        "required": true,
        "description": "Paint layer name"
      },
      {
        "name": "landscapeName",
        "type": "string",
        "required": false,
        "description": "Internal name of the landscape proxy, when the level has more than one"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Content folder for the LayerInfo asset (default /Game/Landscape/LayerInfos)"
      }
    ]
  },
  "analyze_landscape_terrain": {
    "category": "landscape",
    "params": [
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Landscape actor label, when the level has more than one"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Landscape actor object path; wins over actorLabel, which is not unique"
      },
      {
        "name": "region",
        "type": "object",
        "required": false,
        "description": "Rectangle {minX, minY, maxX, maxY} in landscape vertex indices, or centimetres with space world. Omit it for the whole landscape"
      },
      {
        "name": "space",
        "type": "string",
        "required": false,
        "description": "Units of region: quad (vertex indices, default) | world (centimetres)"
      },
      {
        "name": "center",
        "type": "object",
        "required": false,
        "description": "Brush centre {x, y} in world space"
      },
      {
        "name": "radius",
        "type": "number",
        "required": false,
        "description": "Brush radius in world centimetres (default 500)"
      },
      {
        "name": "maxVertices",
        "type": "integer",
        "required": false,
        "description": "Refuse an area covering more than this many vertices"
      },
      {
        "name": "histogramBins",
        "type": "integer",
        "required": false,
        "description": "Bins in the height histogram, 2..256 (default 16)"
      },
      {
        "name": "slopeThresholdDegrees",
        "type": "number",
        "required": false,
        "description": "At or below this slope a vertex counts as flat (default 10)"
      }
    ]
  },
  "apply_landscape_erosion": {
    "category": "landscape",
    "params": [
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Landscape actor label, when the level has more than one"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Landscape actor object path; wins over actorLabel, which is not unique"
      },
      {
        "name": "erosionType",
        "type": "string",
        "required": false,
        "description": "hydraulic (default) | thermal"
      },
      {
        "name": "region",
        "type": "object",
        "required": false,
        "description": "Rectangle {minX, minY, maxX, maxY} in landscape vertex indices, or centimetres with space world. Omit it for the whole landscape"
      },
      {
        "name": "space",
        "type": "string",
        "required": false,
        "description": "Units of region: quad (vertex indices, default) | world (centimetres)"
      },
      {
        "name": "center",
        "type": "object",
        "required": false,
        "description": "Brush centre {x, y} in world space"
      },
      {
        "name": "radius",
        "type": "number",
        "required": false,
        "description": "Brush radius in world centimetres (default 500)"
      },
      {
        "name": "maxVertices",
        "type": "integer",
        "required": false,
        "description": "Refuse an area covering more than this many vertices"
      },
      {
        "name": "iterations",
        "type": "integer",
        "required": false,
        "description": "Passes to run (default 20)"
      },
      {
        "name": "maxWork",
        "type": "integer",
        "required": false,
        "description": "Refuse more than this many vertex-iterations (default 40000000)"
      },
      {
        "name": "talusAngle",
        "type": "number",
        "required": false,
        "description": "Thermal: slope in degrees material stops slumping at (default 35)"
      },
      {
        "name": "strength",
        "type": "number",
        "required": false,
        "description": "Thermal: how much of the excess slumps per pass, 0..1 (default 0.5)"
      },
      {
        "name": "rainAmount",
        "type": "number",
        "required": false,
        "description": "Hydraulic: water added per vertex per pass, in world centimetres (default 0.5)"
      },
      {
        "name": "evaporation",
        "type": "number",
        "required": false,
        "description": "Hydraulic: fraction of water lost per pass, 0.01..1 (default 0.5)"
      },
      {
        "name": "sedimentCapacity",
        "type": "number",
        "required": false,
        "description": "Hydraulic: sediment a unit of water can hold (default 0.6)"
      },
      {
        "name": "erosionRate",
        "type": "number",
        "required": false,
        "description": "Hydraulic: how fast under-loaded water cuts in, 0..1 (default 0.3)"
      },
      {
        "name": "depositionRate",
        "type": "number",
        "required": false,
        "description": "Hydraulic: how fast over-loaded water drops sediment, 0..1 (default 0.3)"
      },
      {
        "name": "editLayer",
        "type": "string",
        "required": false,
        "description": "Edit layer name"
      },
      {
        "name": "editLayerIndex",
        "type": "integer",
        "required": false,
        "description": "Edit layer index when editLayer is not given (default 0)"
      },
      {
        "name": "rollbackMaxVertices",
        "type": "integer",
        "required": false,
        "description": "Carry the previous data as a rollback record only up to this many vertices (default 262144 for heights, 524288 for weights)"
      }
    ]
  },
  "apply_landscape_splines": {
    "category": "landscape",
    "params": [
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Landscape actor label, when the level has more than one"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Landscape actor path; wins over actorLabel"
      },
      {
        "name": "editLayer",
        "type": "string",
        "required": false,
        "description": "Edit layer to write into when the landscape has no reserved splines layer"
      },
      {
        "name": "editLayerIndex",
        "type": "integer",
        "required": false,
        "description": "Edit layer index when editLayer is not given (default 0)"
      },
      {
        "name": "updateNow",
        "type": "boolean",
        "required": false,
        "description": "Merge the edit layers now rather than on the next editor tick (default true)"
      }
    ]
  },
  "connect_landscape_spline_points": {
    "category": "landscape",
    "params": [
      {
        "name": "startPoint",
        "type": "string",
        "required": true,
        "description": "Control point the segment starts at, by name or path"
      },
      {
        "name": "endPoint",
        "type": "string",
        "required": true,
        "description": "Control point the segment ends at; must share the start point's spline owner"
      },
      {
        "name": "startTangentLength",
        "type": "number",
        "required": false,
        "description": "Tangent length at the start point (default the distance between the points)"
      },
      {
        "name": "endTangentLength",
        "type": "number",
        "required": false,
        "description": "Tangent length at the end point (default the distance between the points)"
      },
      {
        "name": "layerName",
        "type": "string",
        "required": false,
        "description": "Paint layer the segment paints when applied; empty clears it"
      },
      {
        "name": "raiseTerrain",
        "type": "boolean",
        "required": false,
        "description": "Raise terrain below the spline up to it when applied (engine default true)"
      },
      {
        "name": "lowerTerrain",
        "type": "boolean",
        "required": false,
        "description": "Lower terrain above the spline down to it when applied (engine default true)"
      },
      {
        "name": "meshes",
        "type": "array",
        "required": false,
        "description": "Meshes deformed along the segment, replacing the current list; an empty list clears it",
        "items": "object",
        "fields": [
          {
            "name": "mesh",
            "type": "string",
            "required": true,
            "description": "StaticMesh asset path"
          },
          {
            "name": "scale",
            "type": "vec3",
            "required": false,
            "description": "Mesh scale (default 1, 1, 1)"
          },
          {
            "name": "forwardAxis",
            "type": "string",
            "required": false,
            "description": "X (default) | Y | Z: the mesh axis laid along the spline"
          },
          {
            "name": "upAxis",
            "type": "string",
            "required": false,
            "description": "X | Y | Z (default): the mesh axis kept up"
          },
          {
            "name": "materialOverrides",
            "type": "array",
            "required": false,
            "description": "Material per slot; an empty string keeps the mesh's own",
            "items": "string"
          },
          {
            "name": "centerH",
            "type": "boolean",
            "required": false,
            "description": "Centre the mesh horizontally on the spline (default true)"
          },
          {
            "name": "scaleToWidth",
            "type": "boolean",
            "required": false,
            "description": "Scale the mesh to the spline width (default true)"
          }
        ]
      },
      {
        "name": "collisionProfile",
        "type": "string",
        "required": false,
        "description": "Collision profile of the segment's mesh components, e.g. BlockAll or NoCollision"
      },
      {
        "name": "castShadow",
        "type": "boolean",
        "required": false,
        "description": "Whether the segment's meshes cast shadows"
      },
      {
        "name": "autoRotate",
        "type": "boolean",
        "required": false,
        "description": "Rotate both points to follow the new segment (default true)"
      },
      {
        "name": "name",
        "type": "string",
        "required": false,
        "description": "Object name for the segment"
      }
    ]
  },
  "create_landscape": {
    "category": "landscape",
    "params": [
      {
        "name": "location",
        "type": "vec3",
        "required": false,
        "description": "Actor location"
      },
      {
        "name": "scale",
        "type": "vec3",
        "required": false,
        "description": "Actor scale (default 100, 100, 100)"
      },
      {
        "name": "componentCountX",
        "type": "integer",
        "required": false,
        "description": "Components along X (default 8)"
      },
      {
        "name": "componentCountY",
        "type": "integer",
        "required": false,
        "description": "Components along Y (default 8)"
      },
      {
        "name": "subsectionSizeQuads",
        "type": "integer",
        "required": false,
        "description": "7 | 15 | 31 | 63 | 127 | 255 (default 63)"
      },
      {
        "name": "numSubsections",
        "type": "integer",
        "required": false,
        "description": "1 | 2 (default 2)"
      },
      {
        "name": "heightOffset",
        "type": "integer",
        "required": false,
        "description": "Flat uint16 height (default 32768, the actor's own Z)"
      },
      {
        "name": "label",
        "type": "string",
        "required": false,
        "description": "Actor label; an existing landscape with this label is reported rather than duplicated"
      }
    ]
  },
  "create_landscape_layer_info": {
    "category": "landscape",
    "params": [
      {
        "name": "layerName",
        "type": "string",
        "required": true,
        "description": "Paint layer name"
      },
      {
        "name": "name",
        "type": "string",
        "required": false,
        "description": "LayerInfo asset name (default LI_<layerName>)"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Content folder for the LayerInfo asset (default /Game/Landscape/LayerInfos)"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "When the asset already exists: skip (default) | error"
      },
      {
        "name": "physMaterial",
        "type": "string",
        "required": false,
        "description": "PhysicalMaterial asset path"
      },
      {
        "name": "hardness",
        "type": "number",
        "required": false,
        "description": "Layer hardness"
      }
    ],
    "contractExempt": "Contract values reach CreatePackage before anything is loaded"
  },
  "create_landscape_spline_point": {
    "category": "landscape",
    "params": [
      {
        "name": "location",
        "type": "vec3",
        "required": true,
        "description": "World location of the control point"
      },
      {
        "name": "rotation",
        "type": "rotator",
        "required": false,
        "description": "World rotation of the tangent at the point"
      },
      {
        "name": "halfWidth",
        "type": "number",
        "required": false,
        "description": "Half-width of the spline at the point, in centimetres (engine default 1000)"
      },
      {
        "name": "sideFalloff",
        "type": "number",
        "required": false,
        "description": "Falloff beyond each side, in centimetres (engine default 1000)"
      },
      {
        "name": "endFalloff",
        "type": "number",
        "required": false,
        "description": "Falloff past an end point, in centimetres (engine default 2000)"
      },
      {
        "name": "layerName",
        "type": "string",
        "required": false,
        "description": "Paint layer applying the spline paints; empty clears it"
      },
      {
        "name": "raiseTerrain",
        "type": "boolean",
        "required": false,
        "description": "Raise terrain below the spline up to it when applied (engine default true)"
      },
      {
        "name": "lowerTerrain",
        "type": "boolean",
        "required": false,
        "description": "Lower terrain above the spline down to it when applied (engine default true)"
      },
      {
        "name": "name",
        "type": "string",
        "required": false,
        "description": "Object name for the point; an existing point of this name is reported rather than duplicated"
      },
      {
        "name": "ownerPath",
        "type": "string",
        "required": false,
        "description": "Existing spline owner to add the point to: a Landscape, LandscapeStreamingProxy or LandscapeSplineActor, by label or path"
      },
      {
        "name": "newSplineActor",
        "type": "boolean",
        "required": false,
        "description": "World Partition: put the point in a new LandscapeSplineActor (the default when ownerPath is not given)"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "When name is taken: skip (default) | error"
      },
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Landscape actor label, when the level has more than one"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Landscape actor path; wins over actorLabel"
      }
    ]
  },
  "delete_landscape_spline_point": {
    "category": "landscape",
    "params": [
      {
        "name": "controlPoint",
        "type": "string",
        "required": true,
        "description": "Control point object name or full path, as list_splines reports it"
      }
    ]
  },
  "delete_landscape_spline_segment": {
    "category": "landscape",
    "params": [
      {
        "name": "segment",
        "type": "string",
        "required": true,
        "description": "Segment object name or full path, as list_splines reports it"
      }
    ]
  },
  "export_landscape_heightmap": {
    "category": "landscape",
    "params": [
      {
        "name": "filePath",
        "type": "string",
        "required": true,
        "description": "Where to write the heightmap; a relative path resolves under the project Saved directory",
        "aliases": [
          "outputPath"
        ]
      },
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Landscape actor label, when the level has more than one"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Landscape actor object path; wins over actorLabel, which is not unique"
      },
      {
        "name": "format",
        "type": "string",
        "required": false,
        "description": "png16 | raw16; inferred from the file extension when omitted"
      },
      {
        "name": "region",
        "type": "object",
        "required": false,
        "description": "Rectangle {minX, minY, maxX, maxY} in landscape vertex indices, or centimetres with space world. Omit it for the whole landscape"
      },
      {
        "name": "space",
        "type": "string",
        "required": false,
        "description": "Units of region: quad (vertex indices, default) | world (centimetres)"
      },
      {
        "name": "center",
        "type": "object",
        "required": false,
        "description": "Brush centre {x, y} in world space"
      },
      {
        "name": "radius",
        "type": "number",
        "required": false,
        "description": "Brush radius in world centimetres (default 500)"
      },
      {
        "name": "maxVertices",
        "type": "integer",
        "required": false,
        "description": "Refuse an area covering more than this many vertices"
      },
      {
        "name": "editLayer",
        "type": "string",
        "required": false,
        "description": "Edit layer name"
      },
      {
        "name": "editLayerIndex",
        "type": "integer",
        "required": false,
        "description": "Edit layer index when editLayer is not given (default 0)"
      },
      {
        "name": "overwrite",
        "type": "boolean",
        "required": false,
        "description": "Allow replacing an existing file (default true)"
      }
    ]
  },
  "find_landscape_proxy_at": {
    "category": "landscape",
    "params": [
      {
        "name": "worldX",
        "type": "number",
        "required": true,
        "description": "World X of the position"
      },
      {
        "name": "worldY",
        "type": "number",
        "required": true,
        "description": "World Y of the position"
      }
    ]
  },
  "get_landscape_component": {
    "category": "landscape",
    "params": [
      {
        "name": "componentIndex",
        "type": "integer",
        "required": false,
        "description": "Index across every landscape component in the level (default 0)"
      }
    ]
  },
  "get_landscape_height_at_point": {
    "category": "landscape",
    "params": [
      {
        "name": "x",
        "type": "number",
        "required": false,
        "description": "World X of the position"
      },
      {
        "name": "y",
        "type": "number",
        "required": false,
        "description": "World Y of the position"
      },
      {
        "name": "point",
        "type": "object",
        "required": false,
        "description": "World position {x, y}"
      },
      {
        "name": "worldX",
        "type": "number",
        "required": false,
        "description": "World X of the position"
      },
      {
        "name": "worldY",
        "type": "number",
        "required": false,
        "description": "World Y of the position"
      },
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Landscape actor label, when the level has more than one"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Landscape actor object path; wins over actorLabel, which is not unique"
      },
      {
        "name": "editLayer",
        "type": "string",
        "required": false,
        "description": "Edit layer name"
      },
      {
        "name": "editLayerIndex",
        "type": "integer",
        "required": false,
        "description": "Edit layer index when editLayer is not given (default 0)"
      }
    ]
  },
  "get_landscape_height_region": {
    "category": "landscape",
    "params": [
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Landscape actor label, when the level has more than one"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Landscape actor object path; wins over actorLabel, which is not unique"
      },
      {
        "name": "region",
        "type": "object",
        "required": false,
        "description": "Rectangle {minX, minY, maxX, maxY} in landscape vertex indices, or centimetres with space world. Omit it for the whole landscape"
      },
      {
        "name": "space",
        "type": "string",
        "required": false,
        "description": "Units of region: quad (vertex indices, default) | world (centimetres)"
      },
      {
        "name": "center",
        "type": "object",
        "required": false,
        "description": "Brush centre {x, y} in world space"
      },
      {
        "name": "radius",
        "type": "number",
        "required": false,
        "description": "Brush radius in world centimetres (default 500)"
      },
      {
        "name": "maxVertices",
        "type": "integer",
        "required": false,
        "description": "Refuse an area covering more than this many vertices"
      },
      {
        "name": "editLayer",
        "type": "string",
        "required": false,
        "description": "Edit layer name"
      },
      {
        "name": "editLayerIndex",
        "type": "integer",
        "required": false,
        "description": "Edit layer index when editLayer is not given (default 0)"
      },
      {
        "name": "includeHeights",
        "type": "boolean",
        "required": false,
        "description": "Include the per-vertex heights (default true)"
      },
      {
        "name": "encoding",
        "type": "string",
        "required": false,
        "description": "array | base64 | auto (default, picks by arrayEncodingLimit)"
      },
      {
        "name": "arrayEncodingLimit",
        "type": "integer",
        "required": false,
        "description": "Return the per-vertex array only up to this many values (default 16384)"
      }
    ]
  },
  "get_landscape_holes": {
    "category": "landscape",
    "params": [
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Landscape actor label, when the level has more than one"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Landscape actor object path; wins over actorLabel, which is not unique"
      },
      {
        "name": "region",
        "type": "object",
        "required": false,
        "description": "Rectangle {minX, minY, maxX, maxY} in landscape vertex indices, or centimetres with space world. Omit it for the whole landscape"
      },
      {
        "name": "space",
        "type": "string",
        "required": false,
        "description": "Units of region: quad (vertex indices, default) | world (centimetres)"
      },
      {
        "name": "center",
        "type": "object",
        "required": false,
        "description": "Brush centre {x, y} in world space"
      },
      {
        "name": "radius",
        "type": "number",
        "required": false,
        "description": "Brush radius in world centimetres (default 500)"
      },
      {
        "name": "x",
        "type": "number",
        "required": false,
        "description": "World X of the position"
      },
      {
        "name": "y",
        "type": "number",
        "required": false,
        "description": "World Y of the position"
      },
      {
        "name": "point",
        "type": "object",
        "required": false,
        "description": "World position {x, y}"
      },
      {
        "name": "worldX",
        "type": "number",
        "required": false,
        "description": "World X of the position"
      },
      {
        "name": "worldY",
        "type": "number",
        "required": false,
        "description": "World Y of the position"
      },
      {
        "name": "maxVertices",
        "type": "integer",
        "required": false,
        "description": "Refuse an area covering more than this many vertices"
      },
      {
        "name": "includeMask",
        "type": "boolean",
        "required": false,
        "description": "Include the per-vertex hole mask (default true)"
      },
      {
        "name": "arrayEncodingLimit",
        "type": "integer",
        "required": false,
        "description": "Return the per-vertex array only up to this many values (default 16384)"
      },
      {
        "name": "editLayer",
        "type": "string",
        "required": false,
        "description": "Edit layer name"
      },
      {
        "name": "editLayerIndex",
        "type": "integer",
        "required": false,
        "description": "Edit layer index when editLayer is not given (default 0)"
      }
    ]
  },
  "get_landscape_info": {
    "category": "landscape",
    "params": []
  },
  "get_landscape_layer_weight_region": {
    "category": "landscape",
    "params": [
      {
        "name": "layerName",
        "type": "string",
        "required": true,
        "description": "Paint layer name"
      },
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Landscape actor label, when the level has more than one"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Landscape actor object path; wins over actorLabel, which is not unique"
      },
      {
        "name": "region",
        "type": "object",
        "required": false,
        "description": "Rectangle {minX, minY, maxX, maxY} in landscape vertex indices, or centimetres with space world. Omit it for the whole landscape"
      },
      {
        "name": "space",
        "type": "string",
        "required": false,
        "description": "Units of region: quad (vertex indices, default) | world (centimetres)"
      },
      {
        "name": "center",
        "type": "object",
        "required": false,
        "description": "Brush centre {x, y} in world space"
      },
      {
        "name": "radius",
        "type": "number",
        "required": false,
        "description": "Brush radius in world centimetres (default 500)"
      },
      {
        "name": "maxVertices",
        "type": "integer",
        "required": false,
        "description": "Refuse an area covering more than this many vertices"
      },
      {
        "name": "editLayer",
        "type": "string",
        "required": false,
        "description": "Edit layer name"
      },
      {
        "name": "editLayerIndex",
        "type": "integer",
        "required": false,
        "description": "Edit layer index when editLayer is not given (default 0)"
      },
      {
        "name": "includeWeights",
        "type": "boolean",
        "required": false,
        "description": "Include the per-vertex weights (default true)"
      },
      {
        "name": "encoding",
        "type": "string",
        "required": false,
        "description": "array | base64 | auto (default, picks by arrayEncodingLimit)"
      },
      {
        "name": "arrayEncodingLimit",
        "type": "integer",
        "required": false,
        "description": "Return the per-vertex array only up to this many values (default 16384)"
      }
    ]
  },
  "get_landscape_material_usage_summary": {
    "category": "landscape",
    "params": []
  },
  "get_landscape_normal_at_point": {
    "category": "landscape",
    "params": [
      {
        "name": "x",
        "type": "number",
        "required": false,
        "description": "World X of the position"
      },
      {
        "name": "y",
        "type": "number",
        "required": false,
        "description": "World Y of the position"
      },
      {
        "name": "point",
        "type": "object",
        "required": false,
        "description": "World position {x, y}"
      },
      {
        "name": "worldX",
        "type": "number",
        "required": false,
        "description": "World X of the position"
      },
      {
        "name": "worldY",
        "type": "number",
        "required": false,
        "description": "World Y of the position"
      },
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Landscape actor label, when the level has more than one"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Landscape actor object path; wins over actorLabel, which is not unique"
      }
    ]
  },
  "get_landscape_slope_at_point": {
    "category": "landscape",
    "params": [
      {
        "name": "x",
        "type": "number",
        "required": false,
        "description": "World X of the position"
      },
      {
        "name": "y",
        "type": "number",
        "required": false,
        "description": "World Y of the position"
      },
      {
        "name": "point",
        "type": "object",
        "required": false,
        "description": "World position {x, y}"
      },
      {
        "name": "worldX",
        "type": "number",
        "required": false,
        "description": "World X of the position"
      },
      {
        "name": "worldY",
        "type": "number",
        "required": false,
        "description": "World Y of the position"
      },
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Landscape actor label, when the level has more than one"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Landscape actor object path; wins over actorLabel, which is not unique"
      }
    ]
  },
  "get_landscape_slope_map": {
    "category": "landscape",
    "params": [
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Landscape actor label, when the level has more than one"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Landscape actor object path; wins over actorLabel, which is not unique"
      },
      {
        "name": "region",
        "type": "object",
        "required": false,
        "description": "Rectangle {minX, minY, maxX, maxY} in landscape vertex indices, or centimetres with space world. Omit it for the whole landscape"
      },
      {
        "name": "space",
        "type": "string",
        "required": false,
        "description": "Units of region: quad (vertex indices, default) | world (centimetres)"
      },
      {
        "name": "center",
        "type": "object",
        "required": false,
        "description": "Brush centre {x, y} in world space"
      },
      {
        "name": "radius",
        "type": "number",
        "required": false,
        "description": "Brush radius in world centimetres (default 500)"
      },
      {
        "name": "maxVertices",
        "type": "integer",
        "required": false,
        "description": "Refuse an area covering more than this many vertices"
      },
      {
        "name": "includeSlopes",
        "type": "boolean",
        "required": false,
        "description": "Include the per-vertex slope array (default true)"
      },
      {
        "name": "arrayEncodingLimit",
        "type": "integer",
        "required": false,
        "description": "Return the per-vertex array only up to this many values (default 16384)"
      }
    ]
  },
  "import_landscape_heightmap": {
    "category": "landscape",
    "params": [
      {
        "name": "filePath",
        "type": "string",
        "required": true,
        "description": "Heightmap file to read; a relative path resolves under the project Saved directory",
        "aliases": [
          "sourcePath"
        ]
      },
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Landscape actor label, when the level has more than one"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Landscape actor object path; wins over actorLabel, which is not unique"
      },
      {
        "name": "format",
        "type": "string",
        "required": false,
        "description": "png16 | raw16; inferred from the file extension when omitted"
      },
      {
        "name": "region",
        "type": "object",
        "required": false,
        "description": "Rectangle {minX, minY, maxX, maxY} in landscape vertex indices, or centimetres with space world. Omit it for the whole landscape"
      },
      {
        "name": "space",
        "type": "string",
        "required": false,
        "description": "Units of region: quad (vertex indices, default) | world (centimetres)"
      },
      {
        "name": "center",
        "type": "object",
        "required": false,
        "description": "Brush centre {x, y} in world space"
      },
      {
        "name": "radius",
        "type": "number",
        "required": false,
        "description": "Brush radius in world centimetres (default 500)"
      },
      {
        "name": "maxVertices",
        "type": "integer",
        "required": false,
        "description": "Refuse an area covering more than this many vertices"
      },
      {
        "name": "width",
        "type": "integer",
        "required": false,
        "description": "Source image width in pixels, required for raw16"
      },
      {
        "name": "height",
        "type": "number",
        "required": false,
        "description": "Source image height in pixels, required for raw16"
      },
      {
        "name": "resample",
        "type": "boolean",
        "required": false,
        "description": "Bilinearly fit an image whose size does not match the region (default false)"
      },
      {
        "name": "minHeight",
        "type": "number",
        "required": false,
        "description": "World Z the image value 0 maps to"
      },
      {
        "name": "maxHeight",
        "type": "number",
        "required": false,
        "description": "World Z the image value 65535 maps to"
      },
      {
        "name": "editLayer",
        "type": "string",
        "required": false,
        "description": "Edit layer name"
      },
      {
        "name": "editLayerIndex",
        "type": "integer",
        "required": false,
        "description": "Edit layer index when editLayer is not given (default 0)"
      },
      {
        "name": "rollbackMaxVertices",
        "type": "integer",
        "required": false,
        "description": "Carry the previous data as a rollback record only up to this many vertices (default 262144 for heights, 524288 for weights)"
      }
    ]
  },
  "landscape_layer_exists": {
    "category": "landscape",
    "params": [
      {
        "name": "layerName",
        "type": "string",
        "required": true,
        "description": "Paint layer name"
      },
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Landscape actor label, when the level has more than one"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Landscape actor object path; wins over actorLabel, which is not unique"
      }
    ]
  },
  "list_landscape_edit_layers": {
    "category": "landscape",
    "params": [
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Landscape actor label, when the level has more than one"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Landscape actor object path; wins over actorLabel, which is not unique"
      }
    ]
  },
  "list_landscape_layers": {
    "category": "landscape",
    "params": []
  },
  "list_landscape_proxies": {
    "category": "landscape",
    "params": [
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
        "description": "Rows on this page (default 200, max 2000)"
      }
    ]
  },
  "list_landscape_splines": {
    "category": "landscape",
    "params": []
  },
  "merge_landscape_edit_layers": {
    "category": "landscape",
    "params": [
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Landscape actor label, when the level has more than one"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Landscape actor object path; wins over actorLabel, which is not unique"
      },
      {
        "name": "updateNow",
        "type": "boolean",
        "required": false,
        "description": "Run the merge now rather than on the next editor tick (default true)"
      }
    ]
  },
  "paint_landscape_layer": {
    "category": "landscape",
    "params": [
      {
        "name": "layerName",
        "type": "string",
        "required": true,
        "description": "Paint layer name"
      },
      {
        "name": "center",
        "type": "object",
        "required": true,
        "description": "Brush centre {x, y} in world space"
      },
      {
        "name": "radius",
        "type": "number",
        "required": false,
        "description": "Brush radius in world centimetres (default 500)"
      },
      {
        "name": "strength",
        "type": "number",
        "required": false,
        "description": "Target weight at full brush strength, 0..1 (default 1)"
      },
      {
        "name": "falloff",
        "type": "number",
        "required": false,
        "description": "Soft edge fraction, 0..1 (default 0.5)"
      },
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Landscape actor label, when the level has more than one"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Landscape actor object path; wins over actorLabel, which is not unique"
      },
      {
        "name": "editLayer",
        "type": "string",
        "required": false,
        "description": "Edit layer name"
      },
      {
        "name": "editLayerIndex",
        "type": "integer",
        "required": false,
        "description": "Edit layer index when editLayer is not given (default 0)"
      },
      {
        "name": "maxVertices",
        "type": "integer",
        "required": false,
        "description": "Refuse a brush covering more than this many vertices (default 4000000)"
      },
      {
        "name": "rollbackMaxVertices",
        "type": "integer",
        "required": false,
        "description": "Carry the previous data as a rollback record only up to this many vertices (default 262144 for heights, 524288 for weights)"
      }
    ]
  },
  "plan_real_world_landscape": {
    "category": "landscape",
    "params": [
      {
        "name": "minElevationMeters",
        "type": "number",
        "required": true,
        "description": "Real-world elevation the heightmap's low value stands for"
      },
      {
        "name": "maxElevationMeters",
        "type": "number",
        "required": true,
        "description": "Real-world elevation the heightmap's high value stands for"
      },
      {
        "name": "realWorldSizeMeters",
        "type": "object",
        "required": false,
        "description": "Ground footprint {x, y} in metres"
      },
      {
        "name": "boundsLatLon",
        "type": "object",
        "required": false,
        "description": "Geographic box {minLat, minLon, maxLat, maxLon} in decimal degrees"
      },
      {
        "name": "sourcePath",
        "type": "string",
        "required": false,
        "description": "Heightmap file to measure; a relative path resolves under the project Saved directory",
        "aliases": [
          "filePath"
        ]
      },
      {
        "name": "format",
        "type": "string",
        "required": false,
        "description": "png16 | raw16; inferred from the file extension when omitted"
      },
      {
        "name": "width",
        "type": "integer",
        "required": false,
        "description": "Source image width in pixels"
      },
      {
        "name": "height",
        "type": "number",
        "required": false,
        "description": "Source image height in pixels"
      },
      {
        "name": "metersPerQuad",
        "type": "number",
        "required": false,
        "description": "Ground distance one quad covers; omit for one vertex per image sample"
      },
      {
        "name": "elevationEncoding",
        "type": "string",
        "required": false,
        "description": "full (default) | data (needs sourcePath)"
      },
      {
        "name": "verticalExaggeration",
        "type": "number",
        "required": false,
        "description": "Multiply real elevations by this, 0.01..100 (default 1)"
      },
      {
        "name": "maxComponents",
        "type": "integer",
        "required": false,
        "description": "Reject configurations needing more components than this (default 1024)"
      },
      {
        "name": "location",
        "type": "vec3",
        "required": false,
        "description": "Landscape origin; its Z is where elevation zero lands"
      }
    ]
  },
  "project_geo_coordinates": {
    "category": "landscape",
    "params": [
      {
        "name": "boundsLatLon",
        "type": "object",
        "required": true,
        "description": "The geographic box the landscape was planned for {minLat, minLon, maxLat, maxLon}"
      },
      {
        "name": "points",
        "type": "array",
        "required": true,
        "description": "Entries {lat, lon} to place, or {x, y} to convert back; an optional name is echoed",
        "items": "object"
      },
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Landscape actor label, when the level has more than one"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Landscape actor object path; wins over actorLabel, which is not unique"
      },
      {
        "name": "northAt",
        "type": "string",
        "required": false,
        "description": "Which end of the Y axis is north: minY (default) | maxY"
      },
      {
        "name": "sampleHeight",
        "type": "boolean",
        "required": false,
        "description": "Read the surface height at each projected point (default true)"
      }
    ]
  },
  "refresh_landscape_physical_material_collision": {
    "category": "landscape",
    "params": [
      {
        "name": "actorLabels",
        "type": "array",
        "required": false,
        "description": "Exact editor labels of loaded LandscapeStreamingProxy actors, 1 to 256",
        "items": "string"
      },
      {
        "name": "guids",
        "type": "array",
        "required": false,
        "description": "Actor GUIDs of loaded LandscapeStreamingProxy actors, 1 to 256",
        "items": "string"
      },
      {
        "name": "bounds",
        "type": "object",
        "required": false,
        "description": "World-space box; proxies intersecting it are refreshed",
        "fields": [
          {
            "name": "min",
            "type": "vec3",
            "required": true,
            "description": "Box minimum"
          },
          {
            "name": "max",
            "type": "vec3",
            "required": true,
            "description": "Box maximum"
          }
        ]
      },
      {
        "name": "maxActors",
        "type": "integer",
        "required": false,
        "description": "Refuse more matches than this, 1 to 1024 (default 256)"
      },
      {
        "name": "save",
        "type": "boolean",
        "required": false,
        "description": "Saving is not supported; the refresh is in memory only. Omit it or pass false",
        "literal": false
      }
    ]
  },
  "remove_landscape_layer": {
    "category": "landscape",
    "params": [
      {
        "name": "layerName",
        "type": "string",
        "required": true,
        "description": "Paint layer name"
      },
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Landscape actor label, when the level has more than one"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Landscape actor object path; wins over actorLabel, which is not unique"
      }
    ]
  },
  "sample_landscape": {
    "category": "landscape",
    "params": [
      {
        "name": "x",
        "type": "number",
        "required": false,
        "description": "World X of the position"
      },
      {
        "name": "y",
        "type": "number",
        "required": false,
        "description": "World Y of the position"
      },
      {
        "name": "point",
        "type": "object",
        "required": false,
        "description": "World position {x, y}"
      },
      {
        "name": "worldX",
        "type": "number",
        "required": false,
        "description": "World X of the position"
      },
      {
        "name": "worldY",
        "type": "number",
        "required": false,
        "description": "World Y of the position"
      },
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Landscape actor label, when the level has more than one"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Landscape actor object path; wins over actorLabel, which is not unique"
      },
      {
        "name": "layerName",
        "type": "string",
        "required": false,
        "description": "Report this paint layer only"
      },
      {
        "name": "includeLayers",
        "type": "boolean",
        "required": false,
        "description": "Include per-paint-layer weights (default true)"
      }
    ]
  },
  "sculpt_landscape": {
    "category": "landscape",
    "params": [
      {
        "name": "center",
        "type": "object",
        "required": true,
        "description": "Brush centre {x, y} in world space"
      },
      {
        "name": "radius",
        "type": "number",
        "required": false,
        "description": "Brush radius in world centimetres (default 500)"
      },
      {
        "name": "mode",
        "type": "string",
        "required": false,
        "description": "raise (default) | lower | flatten"
      },
      {
        "name": "amount",
        "type": "number",
        "required": false,
        "description": "World centimetres to move at full brush strength (default 100)"
      },
      {
        "name": "falloff",
        "type": "number",
        "required": false,
        "description": "Soft edge fraction, 0..1 (default 0.5)"
      },
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Landscape actor label, when the level has more than one"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Landscape actor object path; wins over actorLabel, which is not unique"
      },
      {
        "name": "editLayer",
        "type": "string",
        "required": false,
        "description": "Edit layer name"
      },
      {
        "name": "editLayerIndex",
        "type": "integer",
        "required": false,
        "description": "Edit layer index when editLayer is not given (default 0)"
      },
      {
        "name": "maxVertices",
        "type": "integer",
        "required": false,
        "description": "Refuse a brush covering more than this many vertices (default 4000000)"
      },
      {
        "name": "rollbackMaxVertices",
        "type": "integer",
        "required": false,
        "description": "Carry the previous data as a rollback record only up to this many vertices (default 262144 for heights, 524288 for weights)"
      }
    ]
  },
  "sculpt_landscape_region": {
    "category": "landscape",
    "params": [
      {
        "name": "operator",
        "type": "string",
        "required": true,
        "description": "raise | lower | flatten | smooth | mountain | valley | ridge | plateau | crater | terrace"
      },
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Landscape actor label, when the level has more than one"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Landscape actor object path; wins over actorLabel, which is not unique"
      },
      {
        "name": "region",
        "type": "object",
        "required": false,
        "description": "Rectangle {minX, minY, maxX, maxY} in landscape vertex indices, or centimetres with space world. Omit it for the whole landscape"
      },
      {
        "name": "space",
        "type": "string",
        "required": false,
        "description": "Units of region: quad (vertex indices, default) | world (centimetres)"
      },
      {
        "name": "center",
        "type": "object",
        "required": false,
        "description": "Brush centre {x, y} in world space"
      },
      {
        "name": "radius",
        "type": "number",
        "required": false,
        "description": "Brush radius in world centimetres (default 500)"
      },
      {
        "name": "maxVertices",
        "type": "integer",
        "required": false,
        "description": "Refuse an area covering more than this many vertices"
      },
      {
        "name": "amount",
        "type": "number",
        "required": false,
        "description": "World centimetres at full strength (default 500)"
      },
      {
        "name": "strength",
        "type": "number",
        "required": false,
        "description": "How hard the operator blends into what is there, 0..1 (default 1)"
      },
      {
        "name": "falloff",
        "type": "number",
        "required": false,
        "description": "Soft edge fraction, 0..1 (default 0.5)"
      },
      {
        "name": "sharpness",
        "type": "number",
        "required": false,
        "description": "How peaked the dome operators are, 0.1..8 (default 1.5)"
      },
      {
        "name": "shape",
        "type": "string",
        "required": false,
        "description": "ellipse (radial falloff, default) | rect (falloff toward the region edges)"
      },
      {
        "name": "targetHeight",
        "type": "number",
        "required": false,
        "description": "World Z that flatten and plateau pull toward"
      },
      {
        "name": "flattenTo",
        "type": "string",
        "required": false,
        "description": "mean (default) | center | min | max, when flatten has no targetHeight"
      },
      {
        "name": "iterations",
        "type": "integer",
        "required": false,
        "description": "Smooth passes (default 1)"
      },
      {
        "name": "steps",
        "type": "integer",
        "required": false,
        "description": "Terrace steps across the region's height range (default 8)"
      },
      {
        "name": "ridgeAngle",
        "type": "number",
        "required": false,
        "description": "Degrees the ridge crest runs at (default 0)"
      },
      {
        "name": "rimPosition",
        "type": "number",
        "required": false,
        "description": "Crater rim position as a fraction of the radius, 0.1..0.95 (default 0.75)"
      },
      {
        "name": "rimRatio",
        "type": "number",
        "required": false,
        "description": "Crater rim height as a fraction of the bowl depth (default 0.35)"
      },
      {
        "name": "editLayer",
        "type": "string",
        "required": false,
        "description": "Edit layer name"
      },
      {
        "name": "editLayerIndex",
        "type": "integer",
        "required": false,
        "description": "Edit layer index when editLayer is not given (default 0)"
      },
      {
        "name": "rollbackMaxVertices",
        "type": "integer",
        "required": false,
        "description": "Carry the previous data as a rollback record only up to this many vertices (default 262144 for heights, 524288 for weights)"
      }
    ]
  },
  "set_landscape_height_region": {
    "category": "landscape",
    "params": [
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Landscape actor label, when the level has more than one"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Landscape actor object path; wins over actorLabel, which is not unique"
      },
      {
        "name": "region",
        "type": "object",
        "required": false,
        "description": "Rectangle {minX, minY, maxX, maxY} in landscape vertex indices, or centimetres with space world. Omit it for the whole landscape"
      },
      {
        "name": "space",
        "type": "string",
        "required": false,
        "description": "Units of region: quad (vertex indices, default) | world (centimetres)"
      },
      {
        "name": "center",
        "type": "object",
        "required": false,
        "description": "Brush centre {x, y} in world space"
      },
      {
        "name": "radius",
        "type": "number",
        "required": false,
        "description": "Brush radius in world centimetres (default 500)"
      },
      {
        "name": "maxVertices",
        "type": "integer",
        "required": false,
        "description": "Refuse an area covering more than this many vertices"
      },
      {
        "name": "heightsBase64",
        "type": "string",
        "required": false,
        "description": "Little-endian uint16 blob, row-major, as get_landscape_height_region returns it"
      },
      {
        "name": "heights",
        "type": "array",
        "required": false,
        "description": "One height per vertex, row-major from (minX, minY)",
        "items": "number"
      },
      {
        "name": "height",
        "type": "number",
        "required": false,
        "description": "One world Z to fill the region with"
      },
      {
        "name": "rawHeight",
        "type": "number",
        "required": false,
        "description": "One raw uint16 height to fill the region with (32768 = the actor's own Z)"
      },
      {
        "name": "heightSpace",
        "type": "string",
        "required": false,
        "description": "raw (uint16, default) | world (centimetres)"
      },
      {
        "name": "editLayer",
        "type": "string",
        "required": false,
        "description": "Edit layer name"
      },
      {
        "name": "editLayerIndex",
        "type": "integer",
        "required": false,
        "description": "Edit layer index when editLayer is not given (default 0)"
      },
      {
        "name": "rollbackMaxVertices",
        "type": "integer",
        "required": false,
        "description": "Carry the previous data as a rollback record only up to this many vertices (default 262144 for heights, 524288 for weights)"
      }
    ]
  },
  "set_landscape_holes": {
    "category": "landscape",
    "params": [
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Landscape actor label, when the level has more than one"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Landscape actor object path; wins over actorLabel, which is not unique"
      },
      {
        "name": "region",
        "type": "object",
        "required": false,
        "description": "Rectangle {minX, minY, maxX, maxY} in landscape vertex indices, or centimetres with space world. Omit it for the whole landscape"
      },
      {
        "name": "space",
        "type": "string",
        "required": false,
        "description": "Units of region: quad (vertex indices, default) | world (centimetres)"
      },
      {
        "name": "center",
        "type": "object",
        "required": false,
        "description": "Brush centre {x, y} in world space"
      },
      {
        "name": "radius",
        "type": "number",
        "required": false,
        "description": "Brush radius in world centimetres (default 500)"
      },
      {
        "name": "x",
        "type": "number",
        "required": false,
        "description": "World X of the position"
      },
      {
        "name": "y",
        "type": "number",
        "required": false,
        "description": "World Y of the position"
      },
      {
        "name": "point",
        "type": "object",
        "required": false,
        "description": "World position {x, y}"
      },
      {
        "name": "worldX",
        "type": "number",
        "required": false,
        "description": "World X of the position"
      },
      {
        "name": "worldY",
        "type": "number",
        "required": false,
        "description": "World Y of the position"
      },
      {
        "name": "maxVertices",
        "type": "integer",
        "required": false,
        "description": "Refuse an area covering more than this many vertices"
      },
      {
        "name": "hole",
        "type": "boolean",
        "required": false,
        "description": "Punch (true, default) or fill (false) the whole target"
      },
      {
        "name": "holes",
        "type": "array",
        "required": false,
        "description": "One boolean per vertex, row-major from (minX, minY)",
        "items": "boolean"
      },
      {
        "name": "weightsBase64",
        "type": "string",
        "required": false,
        "description": "Exact previous visibility weights, one uint8 per vertex, as the rollback record carries them"
      },
      {
        "name": "editLayer",
        "type": "string",
        "required": false,
        "description": "Edit layer name"
      },
      {
        "name": "editLayerIndex",
        "type": "integer",
        "required": false,
        "description": "Edit layer index when editLayer is not given (default 0)"
      },
      {
        "name": "rollbackMaxVertices",
        "type": "integer",
        "required": false,
        "description": "Carry the previous data as a rollback record only up to this many vertices (default 262144 for heights, 524288 for weights)"
      }
    ]
  },
  "set_landscape_layer_weight_region": {
    "category": "landscape",
    "params": [
      {
        "name": "layerName",
        "type": "string",
        "required": true,
        "description": "Paint layer name"
      },
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Landscape actor label, when the level has more than one"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Landscape actor object path; wins over actorLabel, which is not unique"
      },
      {
        "name": "region",
        "type": "object",
        "required": false,
        "description": "Rectangle {minX, minY, maxX, maxY} in landscape vertex indices, or centimetres with space world. Omit it for the whole landscape"
      },
      {
        "name": "space",
        "type": "string",
        "required": false,
        "description": "Units of region: quad (vertex indices, default) | world (centimetres)"
      },
      {
        "name": "center",
        "type": "object",
        "required": false,
        "description": "Brush centre {x, y} in world space"
      },
      {
        "name": "radius",
        "type": "number",
        "required": false,
        "description": "Brush radius in world centimetres (default 500)"
      },
      {
        "name": "maxVertices",
        "type": "integer",
        "required": false,
        "description": "Refuse an area covering more than this many vertices"
      },
      {
        "name": "weightsBase64",
        "type": "string",
        "required": false,
        "description": "One uint8 per vertex, base64, as the getter returns it"
      },
      {
        "name": "weights",
        "type": "array",
        "required": false,
        "description": "One 0..255 weight per vertex, row-major from (minX, minY)",
        "items": "number"
      },
      {
        "name": "weight",
        "type": "number",
        "required": false,
        "description": "One value to fill the region with; 0..1 is a fraction, above that the raw 0..255"
      },
      {
        "name": "strength",
        "type": "number",
        "required": false,
        "description": "Fill fraction 0..1, used when weight is absent"
      },
      {
        "name": "editLayer",
        "type": "string",
        "required": false,
        "description": "Edit layer name"
      },
      {
        "name": "editLayerIndex",
        "type": "integer",
        "required": false,
        "description": "Edit layer index when editLayer is not given (default 0)"
      },
      {
        "name": "rollbackMaxVertices",
        "type": "integer",
        "required": false,
        "description": "Carry the previous data as a rollback record only up to this many vertices (default 262144 for heights, 524288 for weights)"
      }
    ]
  },
  "set_landscape_material": {
    "category": "landscape",
    "params": [
      {
        "name": "materialPath",
        "type": "string",
        "required": true,
        "description": "Material or material instance asset path",
        "aliases": [
          "path",
          "assetPath"
        ]
      },
      {
        "name": "landscapeName",
        "type": "string",
        "required": false,
        "description": "Internal name of the landscape proxy, when the level has more than one"
      }
    ]
  },
  "set_landscape_spline_segment_meshes": {
    "category": "landscape",
    "params": [
      {
        "name": "segment",
        "type": "string",
        "required": true,
        "description": "Segment object name or full path, as list_splines reports it"
      },
      {
        "name": "meshes",
        "type": "array",
        "required": false,
        "description": "Meshes deformed along the segment, replacing the current list; an empty list clears it",
        "items": "object",
        "fields": [
          {
            "name": "mesh",
            "type": "string",
            "required": true,
            "description": "StaticMesh asset path"
          },
          {
            "name": "scale",
            "type": "vec3",
            "required": false,
            "description": "Mesh scale (default 1, 1, 1)"
          },
          {
            "name": "forwardAxis",
            "type": "string",
            "required": false,
            "description": "X (default) | Y | Z: the mesh axis laid along the spline"
          },
          {
            "name": "upAxis",
            "type": "string",
            "required": false,
            "description": "X | Y | Z (default): the mesh axis kept up"
          },
          {
            "name": "materialOverrides",
            "type": "array",
            "required": false,
            "description": "Material per slot; an empty string keeps the mesh's own",
            "items": "string"
          },
          {
            "name": "centerH",
            "type": "boolean",
            "required": false,
            "description": "Centre the mesh horizontally on the spline (default true)"
          },
          {
            "name": "scaleToWidth",
            "type": "boolean",
            "required": false,
            "description": "Scale the mesh to the spline width (default true)"
          }
        ]
      },
      {
        "name": "collisionProfile",
        "type": "string",
        "required": false,
        "description": "Collision profile of the segment's mesh components, e.g. BlockAll or NoCollision"
      },
      {
        "name": "castShadow",
        "type": "boolean",
        "required": false,
        "description": "Whether the segment's meshes cast shadows"
      }
    ]
  },
  "update_landscape_spline_point": {
    "category": "landscape",
    "params": [
      {
        "name": "controlPoint",
        "type": "string",
        "required": true,
        "description": "Control point object name or full path, as list_splines reports it"
      },
      {
        "name": "location",
        "type": "vec3",
        "required": false,
        "description": "World location of the control point"
      },
      {
        "name": "rotation",
        "type": "rotator",
        "required": false,
        "description": "World rotation of the tangent at the point"
      },
      {
        "name": "halfWidth",
        "type": "number",
        "required": false,
        "description": "Half-width of the spline at the point, in centimetres (engine default 1000)"
      },
      {
        "name": "sideFalloff",
        "type": "number",
        "required": false,
        "description": "Falloff beyond each side, in centimetres (engine default 1000)"
      },
      {
        "name": "endFalloff",
        "type": "number",
        "required": false,
        "description": "Falloff past an end point, in centimetres (engine default 2000)"
      },
      {
        "name": "layerName",
        "type": "string",
        "required": false,
        "description": "Paint layer applying the spline paints; empty clears it"
      },
      {
        "name": "raiseTerrain",
        "type": "boolean",
        "required": false,
        "description": "Raise terrain below the spline up to it when applied (engine default true)"
      },
      {
        "name": "lowerTerrain",
        "type": "boolean",
        "required": false,
        "description": "Lower terrain above the spline down to it when applied (engine default true)"
      },
      {
        "name": "autoRotate",
        "type": "boolean",
        "required": false,
        "description": "Recompute the rotation from the connected segments when rotation is not given (default true when location moves)"
      }
    ]
  },
  "update_landscape_spline_segment": {
    "category": "landscape",
    "params": [
      {
        "name": "segment",
        "type": "string",
        "required": true,
        "description": "Segment object name or full path, as list_splines reports it"
      },
      {
        "name": "startTangentLength",
        "type": "number",
        "required": false,
        "description": "Tangent length at the start point (default the distance between the points)"
      },
      {
        "name": "endTangentLength",
        "type": "number",
        "required": false,
        "description": "Tangent length at the end point (default the distance between the points)"
      },
      {
        "name": "layerName",
        "type": "string",
        "required": false,
        "description": "Paint layer the segment paints when applied; empty clears it"
      },
      {
        "name": "raiseTerrain",
        "type": "boolean",
        "required": false,
        "description": "Raise terrain below the spline up to it when applied (engine default true)"
      },
      {
        "name": "lowerTerrain",
        "type": "boolean",
        "required": false,
        "description": "Lower terrain above the spline down to it when applied (engine default true)"
      }
    ]
  }
};

/** The Params: clause of each spec'd bridge method. */
export const paramsClauses: Readonly<Record<string, string>> = {
  add_landscape_layer_info: "Params: layerName, landscapeName?, packagePath?",
  analyze_landscape_terrain: "Params: actorLabel?, actorPath?, region?, space?, center?, radius?, maxVertices?, histogramBins?, slopeThresholdDegrees?",
  apply_landscape_erosion: "Params: actorLabel?, actorPath?, erosionType?, region?, space?, center?, radius?, maxVertices?, iterations?, maxWork?, talusAngle?, strength?, rainAmount?, evaporation?, sedimentCapacity?, erosionRate?, depositionRate?, editLayer?, editLayerIndex?, rollbackMaxVertices?",
  apply_landscape_splines: "Params: actorLabel?, actorPath?, editLayer?, editLayerIndex?, updateNow?",
  connect_landscape_spline_points: "Params: startPoint, endPoint, startTangentLength?, endTangentLength?, layerName?, raiseTerrain?, lowerTerrain?, meshes?, collisionProfile?, castShadow?, autoRotate?, name?",
  create_landscape: "Params: location?, scale?, componentCountX?, componentCountY?, subsectionSizeQuads?, numSubsections?, heightOffset?, label?",
  create_landscape_layer_info: "Params: layerName, name?, packagePath?, onConflict?, physMaterial?, hardness?",
  create_landscape_spline_point: "Params: location, rotation?, halfWidth?, sideFalloff?, endFalloff?, layerName?, raiseTerrain?, lowerTerrain?, name?, ownerPath?, newSplineActor?, onConflict?, actorLabel?, actorPath?",
  delete_landscape_spline_point: "Params: controlPoint",
  delete_landscape_spline_segment: "Params: segment",
  export_landscape_heightmap: "Params: filePath (or outputPath), actorLabel?, actorPath?, format?, region?, space?, center?, radius?, maxVertices?, editLayer?, editLayerIndex?, overwrite?",
  find_landscape_proxy_at: "Params: worldX, worldY",
  get_landscape_component: "Params: componentIndex?",
  get_landscape_height_at_point: "Params: x?, y?, point?, worldX?, worldY?, actorLabel?, actorPath?, editLayer?, editLayerIndex?",
  get_landscape_height_region: "Params: actorLabel?, actorPath?, region?, space?, center?, radius?, maxVertices?, editLayer?, editLayerIndex?, includeHeights?, encoding?, arrayEncodingLimit?",
  get_landscape_holes: "Params: actorLabel?, actorPath?, region?, space?, center?, radius?, x?, y?, point?, worldX?, worldY?, maxVertices?, includeMask?, arrayEncodingLimit?, editLayer?, editLayerIndex?",
  get_landscape_info: "Params: none",
  get_landscape_layer_weight_region: "Params: layerName, actorLabel?, actorPath?, region?, space?, center?, radius?, maxVertices?, editLayer?, editLayerIndex?, includeWeights?, encoding?, arrayEncodingLimit?",
  get_landscape_material_usage_summary: "Params: none",
  get_landscape_normal_at_point: "Params: x?, y?, point?, worldX?, worldY?, actorLabel?, actorPath?",
  get_landscape_slope_at_point: "Params: x?, y?, point?, worldX?, worldY?, actorLabel?, actorPath?",
  get_landscape_slope_map: "Params: actorLabel?, actorPath?, region?, space?, center?, radius?, maxVertices?, includeSlopes?, arrayEncodingLimit?",
  import_landscape_heightmap: "Params: filePath (or sourcePath), actorLabel?, actorPath?, format?, region?, space?, center?, radius?, maxVertices?, width?, height?, resample?, minHeight?, maxHeight?, editLayer?, editLayerIndex?, rollbackMaxVertices?",
  landscape_layer_exists: "Params: layerName, actorLabel?, actorPath?",
  list_landscape_edit_layers: "Params: actorLabel?, actorPath?",
  list_landscape_layers: "Params: none",
  list_landscape_proxies: "Params: cursor?, limit?",
  list_landscape_splines: "Params: none",
  merge_landscape_edit_layers: "Params: actorLabel?, actorPath?, updateNow?",
  paint_landscape_layer: "Params: layerName, center, radius?, strength?, falloff?, actorLabel?, actorPath?, editLayer?, editLayerIndex?, maxVertices?, rollbackMaxVertices?",
  plan_real_world_landscape: "Params: minElevationMeters, maxElevationMeters, realWorldSizeMeters?, boundsLatLon?, sourcePath? (or filePath), format?, width?, height?, metersPerQuad?, elevationEncoding?, verticalExaggeration?, maxComponents?, location?",
  project_geo_coordinates: "Params: boundsLatLon, points, actorLabel?, actorPath?, northAt?, sampleHeight?",
  refresh_landscape_physical_material_collision: "Params: actorLabels?, guids?, bounds?, maxActors?, save?",
  remove_landscape_layer: "Params: layerName, actorLabel?, actorPath?",
  sample_landscape: "Params: x?, y?, point?, worldX?, worldY?, actorLabel?, actorPath?, layerName?, includeLayers?",
  sculpt_landscape: "Params: center, radius?, mode?, amount?, falloff?, actorLabel?, actorPath?, editLayer?, editLayerIndex?, maxVertices?, rollbackMaxVertices?",
  sculpt_landscape_region: "Params: operator, actorLabel?, actorPath?, region?, space?, center?, radius?, maxVertices?, amount?, strength?, falloff?, sharpness?, shape?, targetHeight?, flattenTo?, iterations?, steps?, ridgeAngle?, rimPosition?, rimRatio?, editLayer?, editLayerIndex?, rollbackMaxVertices?",
  set_landscape_height_region: "Params: actorLabel?, actorPath?, region?, space?, center?, radius?, maxVertices?, heightsBase64?, heights?, height?, rawHeight?, heightSpace?, editLayer?, editLayerIndex?, rollbackMaxVertices?",
  set_landscape_holes: "Params: actorLabel?, actorPath?, region?, space?, center?, radius?, x?, y?, point?, worldX?, worldY?, maxVertices?, hole?, holes?, weightsBase64?, editLayer?, editLayerIndex?, rollbackMaxVertices?",
  set_landscape_layer_weight_region: "Params: layerName, actorLabel?, actorPath?, region?, space?, center?, radius?, maxVertices?, weightsBase64?, weights?, weight?, strength?, editLayer?, editLayerIndex?, rollbackMaxVertices?",
  set_landscape_material: "Params: materialPath (or path, or assetPath), landscapeName?",
  set_landscape_spline_segment_meshes: "Params: segment, meshes?, collisionProfile?, castShadow?",
  update_landscape_spline_point: "Params: controlPoint, location?, rotation?, halfWidth?, sideFalloff?, endFalloff?, layerName?, raiseTerrain?, lowerTerrain?, autoRotate?",
  update_landscape_spline_segment: "Params: segment, startTangentLength?, endTangentLength?, layerName?, raiseTerrain?, lowerTerrain?",
};

/** Every key the spec'd landscape handlers declare, aliases included. */
export const schema: Record<string, z.ZodType> = {
  actorLabel: z.string().optional().describe("Landscape actor label, when the level has more than one"),
  actorLabels: z.array(z.string()).optional().describe("Exact editor labels of loaded LandscapeStreamingProxy actors, 1 to 256"),
  actorPath: z.string().optional().describe("Landscape actor object path; wins over actorLabel, which is not unique (analyze_landscape_terrain, apply_landscape_erosion, export_landscape_heightmap, get_landscape_height_at_point, get_landscape_height_region, get_landscape_holes, get_landscape_layer_weight_region, get_landscape_normal_at_point, get_landscape_slope_at_point, get_landscape_slope_map, import_landscape_heightmap, landscape_layer_exists, list_landscape_edit_layers, merge_landscape_edit_layers, paint_landscape_layer, project_geo_coordinates, remove_landscape_layer, sample_landscape, sculpt_landscape, sculpt_landscape_region, set_landscape_height_region, set_landscape_holes, set_landscape_layer_weight_region). Landscape actor path; wins over actorLabel (apply_landscape_splines, create_landscape_spline_point)"),
  amount: z.number().optional().describe("World centimetres to move at full brush strength (default 100) (sculpt_landscape). World centimetres at full strength (default 500) (sculpt_landscape_region)"),
  arrayEncodingLimit: z.number().int().optional().describe("Return the per-vertex array only up to this many values (default 16384)"),
  assetPath: z.string().optional().describe("Alias for materialPath"),
  autoRotate: z.boolean().optional().describe("Rotate both points to follow the new segment (default true) (connect_landscape_spline_points). Recompute the rotation from the connected segments when rotation is not given (default true when location moves) (update_landscape_spline_point)"),
  bounds: z.object({ min: z.object({ x: z.number(), y: z.number(), z: z.number() }).describe("Box minimum"), max: z.object({ x: z.number(), y: z.number(), z: z.number() }).describe("Box maximum") }).optional().describe("World-space box; proxies intersecting it are refreshed"),
  boundsLatLon: z.record(z.string(), z.unknown()).optional().describe("Geographic box {minLat, minLon, maxLat, maxLon} in decimal degrees (plan_real_world_landscape). The geographic box the landscape was planned for {minLat, minLon, maxLat, maxLon} (project_geo_coordinates)"),
  castShadow: z.boolean().optional().describe("Whether the segment's meshes cast shadows"),
  center: z.record(z.string(), z.unknown()).optional().describe("Brush centre {x, y} in world space"),
  collisionProfile: z.string().optional().describe("Collision profile of the segment's mesh components, e.g. BlockAll or NoCollision"),
  componentCountX: z.number().int().optional().describe("Components along X (default 8)"),
  componentCountY: z.number().int().optional().describe("Components along Y (default 8)"),
  componentIndex: z.number().int().optional().describe("Index across every landscape component in the level (default 0)"),
  controlPoint: z.string().optional().describe("Control point object name or full path, as list_splines reports it"),
  cursor: z.string().optional().describe("Resume a paged read: pass back the nextCursor from the previous page, unmodified"),
  depositionRate: z.number().optional().describe("Hydraulic: how fast over-loaded water drops sediment, 0..1 (default 0.3)"),
  editLayer: z.string().optional().describe("Edit layer name (apply_landscape_erosion, export_landscape_heightmap, get_landscape_height_at_point, get_landscape_height_region, get_landscape_holes, get_landscape_layer_weight_region, import_landscape_heightmap, paint_landscape_layer, sculpt_landscape, sculpt_landscape_region, set_landscape_height_region, set_landscape_holes, set_landscape_layer_weight_region). Edit layer to write into when the landscape has no reserved splines layer (apply_landscape_splines)"),
  editLayerIndex: z.number().int().optional().describe("Edit layer index when editLayer is not given (default 0)"),
  elevationEncoding: z.string().optional().describe("full (default) | data (needs sourcePath)"),
  encoding: z.string().optional().describe("array | base64 | auto (default, picks by arrayEncodingLimit)"),
  endFalloff: z.number().optional().describe("Falloff past an end point, in centimetres (engine default 2000)"),
  endPoint: z.string().optional().describe("Control point the segment ends at; must share the start point's spline owner"),
  endTangentLength: z.number().optional().describe("Tangent length at the end point (default the distance between the points)"),
  erosionRate: z.number().optional().describe("Hydraulic: how fast under-loaded water cuts in, 0..1 (default 0.3)"),
  erosionType: z.string().optional().describe("hydraulic (default) | thermal"),
  evaporation: z.number().optional().describe("Hydraulic: fraction of water lost per pass, 0.01..1 (default 0.5)"),
  falloff: z.number().optional().describe("Soft edge fraction, 0..1 (default 0.5)"),
  filePath: z.string().optional().describe("Where to write the heightmap; a relative path resolves under the project Saved directory (export_landscape_heightmap). Heightmap file to read; a relative path resolves under the project Saved directory (import_landscape_heightmap). Alias for sourcePath (plan_real_world_landscape)"),
  flattenTo: z.string().optional().describe("mean (default) | center | min | max, when flatten has no targetHeight"),
  format: z.string().optional().describe("png16 | raw16; inferred from the file extension when omitted"),
  guids: z.array(z.string()).optional().describe("Actor GUIDs of loaded LandscapeStreamingProxy actors, 1 to 256"),
  halfWidth: z.number().optional().describe("Half-width of the spline at the point, in centimetres (engine default 1000)"),
  hardness: z.number().optional().describe("Layer hardness"),
  height: z.number().optional().describe("Source image height in pixels, required for raw16 (import_landscape_heightmap). Source image height in pixels (plan_real_world_landscape). One world Z to fill the region with (set_landscape_height_region)"),
  heightOffset: z.number().int().optional().describe("Flat uint16 height (default 32768, the actor's own Z)"),
  heights: z.array(z.number()).optional().describe("One height per vertex, row-major from (minX, minY)"),
  heightsBase64: z.string().optional().describe("Little-endian uint16 blob, row-major, as get_landscape_height_region returns it"),
  heightSpace: z.string().optional().describe("raw (uint16, default) | world (centimetres)"),
  histogramBins: z.number().int().optional().describe("Bins in the height histogram, 2..256 (default 16)"),
  hole: z.boolean().optional().describe("Punch (true, default) or fill (false) the whole target"),
  holes: z.array(z.boolean()).optional().describe("One boolean per vertex, row-major from (minX, minY)"),
  includeHeights: z.boolean().optional().describe("Include the per-vertex heights (default true)"),
  includeLayers: z.boolean().optional().describe("Include per-paint-layer weights (default true)"),
  includeMask: z.boolean().optional().describe("Include the per-vertex hole mask (default true)"),
  includeSlopes: z.boolean().optional().describe("Include the per-vertex slope array (default true)"),
  includeWeights: z.boolean().optional().describe("Include the per-vertex weights (default true)"),
  iterations: z.number().int().optional().describe("Passes to run (default 20) (apply_landscape_erosion). Smooth passes (default 1) (sculpt_landscape_region)"),
  label: z.string().optional().describe("Actor label; an existing landscape with this label is reported rather than duplicated"),
  landscapeName: z.string().optional().describe("Internal name of the landscape proxy, when the level has more than one"),
  layerName: z.string().optional().describe("Paint layer name (add_landscape_layer_info, create_landscape_layer_info, get_landscape_layer_weight_region, landscape_layer_exists, paint_landscape_layer, remove_landscape_layer, set_landscape_layer_weight_region). Paint layer the segment paints when applied; empty clears it (connect_landscape_spline_points, update_landscape_spline_segment). Paint layer applying the spline paints; empty clears it (create_landscape_spline_point, update_landscape_spline_point). Report this paint layer only (sample_landscape)"),
  limit: z.number().int().optional().describe("Rows on this page (default 200, max 2000)"),
  location: z.object({ x: z.number(), y: z.number(), z: z.number() }).optional().describe("Actor location (create_landscape). World location of the control point (create_landscape_spline_point, update_landscape_spline_point). Landscape origin; its Z is where elevation zero lands (plan_real_world_landscape)"),
  lowerTerrain: z.boolean().optional().describe("Lower terrain above the spline down to it when applied (engine default true)"),
  materialPath: z.string().optional().describe("Material or material instance asset path"),
  maxActors: z.number().int().optional().describe("Refuse more matches than this, 1 to 1024 (default 256)"),
  maxComponents: z.number().int().optional().describe("Reject configurations needing more components than this (default 1024)"),
  maxElevationMeters: z.number().optional().describe("Real-world elevation the heightmap's high value stands for"),
  maxHeight: z.number().optional().describe("World Z the image value 65535 maps to"),
  maxVertices: z.number().int().optional().describe("Refuse an area covering more than this many vertices (analyze_landscape_terrain, apply_landscape_erosion, export_landscape_heightmap, get_landscape_height_region, get_landscape_holes, get_landscape_layer_weight_region, get_landscape_slope_map, import_landscape_heightmap, sculpt_landscape_region, set_landscape_height_region, set_landscape_holes, set_landscape_layer_weight_region). Refuse a brush covering more than this many vertices (default 4000000) (paint_landscape_layer, sculpt_landscape)"),
  maxWork: z.number().int().optional().describe("Refuse more than this many vertex-iterations (default 40000000)"),
  meshes: z.array(z.object({ mesh: z.string().describe("StaticMesh asset path"), scale: z.object({ x: z.number(), y: z.number(), z: z.number() }).optional().describe("Mesh scale (default 1, 1, 1)"), forwardAxis: z.string().optional().describe("X (default) | Y | Z: the mesh axis laid along the spline"), upAxis: z.string().optional().describe("X | Y | Z (default): the mesh axis kept up"), materialOverrides: z.array(z.string()).optional().describe("Material per slot; an empty string keeps the mesh's own"), centerH: z.boolean().optional().describe("Centre the mesh horizontally on the spline (default true)"), scaleToWidth: z.boolean().optional().describe("Scale the mesh to the spline width (default true)") })).optional().describe("Meshes deformed along the segment, replacing the current list; an empty list clears it"),
  metersPerQuad: z.number().optional().describe("Ground distance one quad covers; omit for one vertex per image sample"),
  minElevationMeters: z.number().optional().describe("Real-world elevation the heightmap's low value stands for"),
  minHeight: z.number().optional().describe("World Z the image value 0 maps to"),
  mode: z.string().optional().describe("raise (default) | lower | flatten"),
  name: z.string().optional().describe("Object name for the segment (connect_landscape_spline_points). LayerInfo asset name (default LI_<layerName>) (create_landscape_layer_info). Object name for the point; an existing point of this name is reported rather than duplicated (create_landscape_spline_point)"),
  newSplineActor: z.boolean().optional().describe("World Partition: put the point in a new LandscapeSplineActor (the default when ownerPath is not given)"),
  northAt: z.string().optional().describe("Which end of the Y axis is north: minY (default) | maxY"),
  numSubsections: z.number().int().optional().describe("1 | 2 (default 2)"),
  onConflict: z.string().optional().describe("When the asset already exists: skip (default) | error (create_landscape_layer_info). When name is taken: skip (default) | error (create_landscape_spline_point)"),
  operator: z.string().optional().describe("raise | lower | flatten | smooth | mountain | valley | ridge | plateau | crater | terrace"),
  outputPath: z.string().optional().describe("Alias for filePath"),
  overwrite: z.boolean().optional().describe("Allow replacing an existing file (default true)"),
  ownerPath: z.string().optional().describe("Existing spline owner to add the point to: a Landscape, LandscapeStreamingProxy or LandscapeSplineActor, by label or path"),
  packagePath: z.string().optional().describe("Content folder for the LayerInfo asset (default /Game/Landscape/LayerInfos)"),
  path: z.string().optional().describe("Alias for materialPath"),
  physMaterial: z.string().optional().describe("PhysicalMaterial asset path"),
  point: z.record(z.string(), z.unknown()).optional().describe("World position {x, y}"),
  points: z.array(z.record(z.string(), z.unknown())).optional().describe("Entries {lat, lon} to place, or {x, y} to convert back; an optional name is echoed"),
  radius: z.number().optional().describe("Brush radius in world centimetres (default 500)"),
  rainAmount: z.number().optional().describe("Hydraulic: water added per vertex per pass, in world centimetres (default 0.5)"),
  raiseTerrain: z.boolean().optional().describe("Raise terrain below the spline up to it when applied (engine default true)"),
  rawHeight: z.number().optional().describe("One raw uint16 height to fill the region with (32768 = the actor's own Z)"),
  realWorldSizeMeters: z.record(z.string(), z.unknown()).optional().describe("Ground footprint {x, y} in metres"),
  region: z.record(z.string(), z.unknown()).optional().describe("Rectangle {minX, minY, maxX, maxY} in landscape vertex indices, or centimetres with space world. Omit it for the whole landscape"),
  resample: z.boolean().optional().describe("Bilinearly fit an image whose size does not match the region (default false)"),
  ridgeAngle: z.number().optional().describe("Degrees the ridge crest runs at (default 0)"),
  rimPosition: z.number().optional().describe("Crater rim position as a fraction of the radius, 0.1..0.95 (default 0.75)"),
  rimRatio: z.number().optional().describe("Crater rim height as a fraction of the bowl depth (default 0.35)"),
  rollbackMaxVertices: z.number().int().optional().describe("Carry the previous data as a rollback record only up to this many vertices (default 262144 for heights, 524288 for weights)"),
  rotation: z.object({ pitch: z.number(), yaw: z.number(), roll: z.number() }).optional().describe("World rotation of the tangent at the point"),
  sampleHeight: z.boolean().optional().describe("Read the surface height at each projected point (default true)"),
  save: z.literal(false).optional().describe("Saving is not supported; the refresh is in memory only. Omit it or pass false"),
  scale: z.object({ x: z.number(), y: z.number(), z: z.number() }).optional().describe("Actor scale (default 100, 100, 100)"),
  sedimentCapacity: z.number().optional().describe("Hydraulic: sediment a unit of water can hold (default 0.6)"),
  segment: z.string().optional().describe("Segment object name or full path, as list_splines reports it"),
  shape: z.string().optional().describe("ellipse (radial falloff, default) | rect (falloff toward the region edges)"),
  sharpness: z.number().optional().describe("How peaked the dome operators are, 0.1..8 (default 1.5)"),
  sideFalloff: z.number().optional().describe("Falloff beyond each side, in centimetres (engine default 1000)"),
  slopeThresholdDegrees: z.number().optional().describe("At or below this slope a vertex counts as flat (default 10)"),
  sourcePath: z.string().optional().describe("Alias for filePath (import_landscape_heightmap). Heightmap file to measure; a relative path resolves under the project Saved directory (plan_real_world_landscape)"),
  space: z.string().optional().describe("Units of region: quad (vertex indices, default) | world (centimetres)"),
  startPoint: z.string().optional().describe("Control point the segment starts at, by name or path"),
  startTangentLength: z.number().optional().describe("Tangent length at the start point (default the distance between the points)"),
  steps: z.number().int().optional().describe("Terrace steps across the region's height range (default 8)"),
  strength: z.number().optional().describe("Thermal: how much of the excess slumps per pass, 0..1 (default 0.5) (apply_landscape_erosion). Target weight at full brush strength, 0..1 (default 1) (paint_landscape_layer). How hard the operator blends into what is there, 0..1 (default 1) (sculpt_landscape_region). Fill fraction 0..1, used when weight is absent (set_landscape_layer_weight_region)"),
  subsectionSizeQuads: z.number().int().optional().describe("7 | 15 | 31 | 63 | 127 | 255 (default 63)"),
  talusAngle: z.number().optional().describe("Thermal: slope in degrees material stops slumping at (default 35)"),
  targetHeight: z.number().optional().describe("World Z that flatten and plateau pull toward"),
  updateNow: z.boolean().optional().describe("Merge the edit layers now rather than on the next editor tick (default true) (apply_landscape_splines). Run the merge now rather than on the next editor tick (default true) (merge_landscape_edit_layers)"),
  verticalExaggeration: z.number().optional().describe("Multiply real elevations by this, 0.01..100 (default 1)"),
  weight: z.number().optional().describe("One value to fill the region with; 0..1 is a fraction, above that the raw 0..255"),
  weights: z.array(z.number()).optional().describe("One 0..255 weight per vertex, row-major from (minX, minY)"),
  weightsBase64: z.string().optional().describe("Exact previous visibility weights, one uint8 per vertex, as the rollback record carries them (set_landscape_holes). One uint8 per vertex, base64, as the getter returns it (set_landscape_layer_weight_region)"),
  width: z.number().int().optional().describe("Source image width in pixels, required for raw16 (import_landscape_heightmap). Source image width in pixels (plan_real_world_landscape)"),
  worldX: z.number().optional().describe("World X of the position"),
  worldY: z.number().optional().describe("World Y of the position"),
  x: z.number().optional().describe("World X of the position"),
  y: z.number().optional().describe("World Y of the position"),
};

/** Declare an action for a spec'd bridge method: effect, summary, method. */
export const specBp = makeSpecBp(paramsClauses, handlerSpecs);
