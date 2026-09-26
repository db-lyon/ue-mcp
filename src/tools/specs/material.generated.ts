// GENERATED FILE - do not edit.
//
// Written by scripts/generate-handler-specs.mjs from tests/golden/handler-specs.json,
// which is recorded from the parameter specs the C++ handlers register with
// (npm run specs:record). To change a parameter, change its RegisterHandler
// spec, re-record, and regenerate (#1057).
import { z } from "zod";
import { makeSpecBp, type HandlerSpecs } from "../../surface/handler-spec.js";

/** The recorded contract of every spec'd material handler. */
export const handlerSpecs: HandlerSpecs = {
  "add_expression_in_function": {
    "category": "material",
    "params": [
      {
        "name": "functionPath",
        "type": "string",
        "required": true,
        "description": "MaterialFunction asset path (#463)",
        "aliases": [
          "materialFunctionPath"
        ]
      },
      {
        "name": "expressionType",
        "type": "string",
        "required": true,
        "description": "Expression type, e.g. Constant3Vector, FunctionInput, FunctionOutput, If"
      },
      {
        "name": "positionX",
        "type": "number",
        "required": false,
        "description": "Graph editor X position for a new node"
      },
      {
        "name": "positionY",
        "type": "number",
        "required": false,
        "description": "Graph editor Y position for a new node"
      },
      {
        "name": "inputName",
        "type": "string",
        "required": false,
        "description": "FunctionInput name (#463)"
      },
      {
        "name": "inputType",
        "type": "string",
        "required": false,
        "description": "FunctionInput type: Scalar|Vector2|Vector3|Vector4|Texture2D|TextureCube|StaticBool|MaterialAttributes (#463)"
      },
      {
        "name": "outputName",
        "type": "string",
        "required": false,
        "description": "FunctionOutput name"
      },
      {
        "name": "name",
        "type": "string",
        "required": false,
        "description": "Fallback for inputName on a FunctionInput and outputName on a FunctionOutput"
      }
    ]
  },
  "add_material_designer_layer": {
    "category": "material",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": false,
        "description": "A DynamicMaterialInstance, a DynamicMaterialModel, or an object path inside one"
      },
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Label of the actor whose component material to read; a label naming several actors is refused"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Full object path of that actor; the unambiguous selector"
      },
      {
        "name": "componentName",
        "type": "string",
        "required": false,
        "description": "Primitive component whose material slot to read (default: the first with material slots)"
      },
      {
        "name": "slotIndex",
        "type": "integer",
        "required": false,
        "description": "Material slot index on the component (default 0)"
      },
      {
        "name": "slotName",
        "type": "string",
        "required": false,
        "description": "Material slot name, instead of slotIndex"
      },
      {
        "name": "world",
        "type": "string",
        "required": false,
        "description": "World holding the actor: editor (default) | pie | auto"
      },
      {
        "name": "pieInstance",
        "type": "number",
        "required": false,
        "description": "Which PIE world when several run (0 = server/primary). See editor(list_pie_instances)"
      },
      {
        "name": "designerSlot",
        "type": "number",
        "required": false,
        "description": "Material Designer slot: index, or the material property it serves (BaseColor, or its short name Base)",
        "orTypes": [
          "string"
        ]
      },
      {
        "name": "materialProperty",
        "type": "string",
        "required": false,
        "description": "EDMMaterialPropertyType of the new layer, and the slot when designerSlot is omitted (default BaseColor)"
      },
      {
        "name": "layerName",
        "type": "string",
        "required": false,
        "description": "Name for the new layer"
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
            "actorLabel"
          ],
          [
            "actorPath"
          ]
        ]
      }
    ]
  },
  "add_material_expression": {
    "category": "material",
    "params": [
      {
        "name": "materialPath",
        "type": "string",
        "required": true,
        "description": "Material asset path",
        "aliases": [
          "path",
          "assetPath"
        ]
      },
      {
        "name": "expressionType",
        "type": "string",
        "required": true,
        "description": "Expression type: Constant, TextureSample, Multiply, Lerp, ScalarParameter, etc."
      },
      {
        "name": "name",
        "type": "string",
        "required": false,
        "description": "Node description",
        "aliases": [
          "expressionName"
        ]
      },
      {
        "name": "parameterName",
        "type": "string",
        "required": false,
        "description": "Parameter name, on a parameter node"
      },
      {
        "name": "group",
        "type": "string",
        "required": false,
        "description": "Parameter Group, on a parameter node (#318)"
      },
      {
        "name": "sortPriority",
        "type": "number",
        "required": false,
        "description": "SortPriority within the parameter Group"
      },
      {
        "name": "defaultValue",
        "type": "number",
        "required": false,
        "description": "ScalarParameter default (a number) or VectorParameter default ({r,g,b,a})",
        "orTypes": [
          "object"
        ]
      },
      {
        "name": "value",
        "type": "any",
        "required": false,
        "description": "A number for Constant, {r,g,b} for Constant3Vector, {x,y} for Constant2Vector"
      },
      {
        "name": "channels",
        "type": "object",
        "required": false,
        "description": "ComponentMask channels {r,g,b,a} as bools"
      },
      {
        "name": "positionX",
        "type": "number",
        "required": false,
        "description": "Graph editor X position for a new node"
      },
      {
        "name": "positionY",
        "type": "number",
        "required": false,
        "description": "Graph editor Y position for a new node"
      }
    ]
  },
  "add_rvt_output": {
    "category": "material",
    "params": [
      {
        "name": "materialPath",
        "type": "string",
        "required": true,
        "description": "Material asset path",
        "aliases": [
          "assetPath"
        ]
      },
      {
        "name": "expressionName",
        "type": "string",
        "required": false,
        "description": "Node name; an existing node of that name is reused"
      },
      {
        "name": "mirrorProperties",
        "type": "boolean",
        "required": false,
        "description": "Mirror the material's own property connections into the RVT output node (default true)"
      },
      {
        "name": "positionX",
        "type": "number",
        "required": false,
        "description": "Graph editor X position for a new node"
      },
      {
        "name": "positionY",
        "type": "number",
        "required": false,
        "description": "Graph editor Y position for a new node"
      },
      {
        "name": "recompile",
        "type": "boolean",
        "required": false,
        "description": "Recompile the material after the graph edit (default true)"
      }
    ]
  },
  "add_rvt_sampler": {
    "category": "material",
    "params": [
      {
        "name": "materialPath",
        "type": "string",
        "required": true,
        "description": "Material asset path",
        "aliases": [
          "assetPath"
        ]
      },
      {
        "name": "rvtPath",
        "type": "string",
        "required": true,
        "description": "RuntimeVirtualTexture asset path"
      },
      {
        "name": "expressionName",
        "type": "string",
        "required": false,
        "description": "Node name; an existing node of that name is reused"
      },
      {
        "name": "connectOutputs",
        "type": "boolean",
        "required": false,
        "description": "Connect each sample output to the material property of the same name (default true)"
      },
      {
        "name": "positionX",
        "type": "number",
        "required": false,
        "description": "Graph editor X position for a new node"
      },
      {
        "name": "positionY",
        "type": "number",
        "required": false,
        "description": "Graph editor Y position for a new node"
      },
      {
        "name": "recompile",
        "type": "boolean",
        "required": false,
        "description": "Recompile the material after the graph edit (default true)"
      }
    ]
  },
  "add_rvt_volume": {
    "category": "material",
    "params": [
      {
        "name": "rvtPath",
        "type": "string",
        "required": true,
        "description": "RuntimeVirtualTexture asset path",
        "aliases": [
          "assetPath"
        ]
      },
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Editor label for the new volume (default RVTVolume_<rvt name>)"
      },
      {
        "name": "boundsMode",
        "type": "string",
        "required": false,
        "description": "writers (cover every primitive writing into this RVT) | alignActor (match one actor's box and rotation)"
      },
      {
        "name": "boundsAlignActor",
        "type": "string",
        "required": false,
        "description": "Actor label or object path whose rotation and bounds the volume aligns to, typically the landscape"
      }
    ]
  },
  "assign_rvt_to_landscape": {
    "category": "material",
    "params": [
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Label of the landscape"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Full object path of the landscape"
      },
      {
        "name": "rvtPaths",
        "type": "array",
        "required": false,
        "description": "RuntimeVirtualTexture asset paths",
        "items": "string"
      },
      {
        "name": "rvtPath",
        "type": "string",
        "required": false,
        "description": "One RuntimeVirtualTexture asset path, instead of rvtPaths"
      },
      {
        "name": "assignMode",
        "type": "string",
        "required": false,
        "description": "set (default: replace the list) | add | remove"
      }
    ],
    "choices": [
      {
        "mode": "exactlyOne",
        "branches": [
          [
            "actorLabel"
          ],
          [
            "actorPath"
          ]
        ]
      }
    ]
  },
  "batch_set_material_instances": {
    "category": "material",
    "params": [
      {
        "name": "instances",
        "type": "array",
        "required": true,
        "description": "[{assetPath, parentPath?, parameters?:[{name, type (scalar|vector|texture), value}]}]. value: number (scalar), {r,g,b,a} (vector), or texture path (texture) (#594)",
        "items": "object"
      }
    ]
  },
  "begin_material_transaction": {
    "category": "material",
    "params": [
      {
        "name": "label",
        "type": "string",
        "required": false,
        "description": "Transaction label (default MCP Material Edit)"
      }
    ],
    "contractExempt": "Opens an editor transaction; nothing it reads fails first"
  },
  "build_material": {
    "category": "material",
    "params": [
      {
        "name": "materialPath",
        "type": "string",
        "required": false,
        "description": "Existing material to build into",
        "aliases": [
          "assetPath"
        ]
      },
      {
        "name": "name",
        "type": "string",
        "required": false,
        "description": "Name of a new material to create and build"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Folder for a new material (default /Game/Materials)"
      },
      {
        "name": "textures",
        "type": "object",
        "required": true,
        "description": "{materialProperty: textureAssetPath}: baseColor, normal, roughness, metallic, specular, emissive, opacity, opacityMask, ambientOcclusion, plus packed orm (R=AO,G=Roughness,B=Metallic) and rma (#946)"
      },
      {
        "name": "samplerTypes",
        "type": "object",
        "required": false,
        "description": "Per-texture EMaterialSamplerType override, e.g. {normal: 'VirtualNormal'}; omit to let the texture decide (#946)"
      },
      {
        "name": "clearExisting",
        "type": "boolean",
        "required": false,
        "description": "Delete every existing expression before building (#946)"
      },
      {
        "name": "assignToMesh",
        "type": "string",
        "required": false,
        "description": "StaticMesh or SkeletalMesh asset to put the finished material on (#946)",
        "aliases": [
          "meshPath"
        ]
      },
      {
        "name": "meshSlots",
        "type": "array",
        "required": false,
        "description": "Mesh material slot names or indices to assign (default every slot) (#946)"
      }
    ],
    "choices": [
      {
        "mode": "exactlyOne",
        "branches": [
          [
            "materialPath"
          ],
          [
            "name"
          ]
        ]
      }
    ]
  },
  "build_material_graph": {
    "category": "material",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Material asset path",
        "aliases": [
          "materialPath"
        ]
      },
      {
        "name": "nodes",
        "type": "array",
        "required": true,
        "description": "Graph spec: [{name,class,posX,posY,...}]",
        "items": "object"
      },
      {
        "name": "propertyConnections",
        "type": "array",
        "required": false,
        "description": "[{property,from,outputIndex}]",
        "items": "object"
      }
    ]
  },
  "clear_material_instance_parameters": {
    "category": "material",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "MaterialInstanceConstant asset path",
        "aliases": [
          "path",
          "materialPath"
        ]
      }
    ]
  },
  "connect_expressions_in_function": {
    "category": "material",
    "params": [
      {
        "name": "functionPath",
        "type": "string",
        "required": true,
        "description": "MaterialFunction asset path (#463)",
        "aliases": [
          "materialFunctionPath"
        ]
      },
      {
        "name": "sourceExpression",
        "type": "string",
        "required": true,
        "description": "Expression the wire leaves (name, or index inside a MaterialFunction)"
      },
      {
        "name": "sourceOutput",
        "type": "string",
        "required": false,
        "description": "Output of the source expression, by name or index (default: its first output)"
      },
      {
        "name": "targetExpression",
        "type": "string",
        "required": true,
        "description": "Expression the wire enters (name, or index inside a MaterialFunction)"
      },
      {
        "name": "targetInput",
        "type": "string",
        "required": false,
        "description": "Input of the target expression, by name or index (default: its first input)"
      }
    ]
  },
  "connect_material_expressions": {
    "category": "material",
    "params": [
      {
        "name": "materialPath",
        "type": "string",
        "required": true,
        "description": "Material asset path",
        "aliases": [
          "path",
          "assetPath"
        ]
      },
      {
        "name": "sourceExpression",
        "type": "string",
        "required": true,
        "description": "Expression the wire leaves (name, or index inside a MaterialFunction)"
      },
      {
        "name": "sourceOutput",
        "type": "string",
        "required": false,
        "description": "Output of the source expression, by name or index (default: its first output)"
      },
      {
        "name": "targetExpression",
        "type": "string",
        "required": true,
        "description": "Expression the wire enters (name, or index inside a MaterialFunction)"
      },
      {
        "name": "targetInput",
        "type": "string",
        "required": false,
        "description": "Input of the target expression, by name or index (default: its first input)"
      }
    ]
  },
  "connect_texture_to_material": {
    "category": "material",
    "params": [
      {
        "name": "materialPath",
        "type": "string",
        "required": true,
        "description": "Material asset path",
        "aliases": [
          "path",
          "assetPath"
        ]
      },
      {
        "name": "texturePath",
        "type": "string",
        "required": true,
        "description": "Texture asset path"
      },
      {
        "name": "property",
        "type": "string",
        "required": false,
        "description": "Material property: BaseColor, Normal, Roughness, Metallic, EmissiveColor, etc. (default BaseColor)",
        "aliases": [
          "materialProperty"
        ]
      }
    ]
  },
  "connect_to_material_property": {
    "category": "material",
    "params": [
      {
        "name": "materialPath",
        "type": "string",
        "required": true,
        "description": "Material asset path",
        "aliases": [
          "path",
          "assetPath"
        ]
      },
      {
        "name": "expressionName",
        "type": "string",
        "required": true,
        "description": "Expression to wire"
      },
      {
        "name": "outputName",
        "type": "string",
        "required": false,
        "description": "Output of the expression, by name or index (default: its first output)"
      },
      {
        "name": "property",
        "type": "string",
        "required": true,
        "description": "Material property: BaseColor, Normal, Roughness, Metallic, EmissiveColor, etc."
      }
    ]
  },
  "create_material": {
    "category": "material",
    "params": [
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "Material asset name"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Folder for the new material (default /Game/Materials)"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "When the asset exists: skip (default, report it) | error"
      }
    ],
    "contractExempt": "Creates and saves a material under the contract values; nothing it reads fails first"
  },
  "create_material_designer": {
    "category": "material",
    "params": [
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "Material asset name"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Folder for the new material (default /Game/Materials)"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "When the asset exists: skip (default, report it) | error"
      }
    ],
    "contractExempt": "Creates and saves a Material Designer material under the contract values; nothing it reads fails first"
  },
  "create_material_function": {
    "category": "material",
    "params": [
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "MaterialFunction asset name"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Folder for the new function (default /Game/Materials/Functions)"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "When the asset exists: skip (default, report it) | error"
      },
      {
        "name": "description",
        "type": "string",
        "required": false,
        "description": "MaterialFunction description (#463)"
      }
    ],
    "contractExempt": "Creates and saves a MaterialFunction under the contract values; nothing it reads fails first"
  },
  "create_material_instance": {
    "category": "material",
    "params": [
      {
        "name": "parentPath",
        "type": "string",
        "required": true,
        "description": "Parent material or material instance"
      },
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "Material instance asset name"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Folder for the new instance (default /Game/Materials)"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "When the asset exists: skip (default, report it) | error"
      }
    ]
  },
  "create_material_simple": {
    "category": "material",
    "params": [
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "Material asset name"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Folder for the new material (default /Game/Materials)"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "When the asset exists: skip (default, report it) | error"
      },
      {
        "name": "baseColor",
        "type": "color",
        "required": false,
        "description": "Base colour {r,g,b}"
      },
      {
        "name": "metallic",
        "type": "number",
        "required": false,
        "description": "Metallic constant"
      },
      {
        "name": "specular",
        "type": "number",
        "required": false,
        "description": "Specular constant"
      },
      {
        "name": "roughness",
        "type": "number",
        "required": false,
        "description": "Roughness constant"
      },
      {
        "name": "emissive",
        "type": "number",
        "required": false,
        "description": "Emissive strength"
      },
      {
        "name": "usages",
        "type": "array",
        "required": false,
        "description": "MATUSAGE flag names to turn on, e.g. InstancedStaticMeshes, Nanite, NiagaraSprites",
        "items": "string"
      }
    ],
    "contractExempt": "Creates and saves a material under the contract values; nothing it reads fails first"
  },
  "create_runtime_virtual_texture": {
    "category": "material",
    "params": [
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "RuntimeVirtualTexture asset name, with no '/' or '.'"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Folder for the new RVT (default /Game/Textures/RVT)"
      },
      {
        "name": "materialType",
        "type": "string",
        "required": false,
        "description": "RVT content layout: BaseColor | BaseColor_Normal_Roughness | BaseColor_Normal_Specular | BaseColor_Normal_Specular_YCoCg | BaseColor_Normal_Specular_Mask_YCoCg | Mask4 | WorldHeight | Displacement; one this project disables is refused"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "When the asset exists: skip (default, report it) | error"
      }
    ]
  },
  "delete_material_expression": {
    "category": "material",
    "params": [
      {
        "name": "materialPath",
        "type": "string",
        "required": false,
        "description": "Material asset path",
        "aliases": [
          "path",
          "assetPath"
        ]
      },
      {
        "name": "functionPath",
        "type": "string",
        "required": false,
        "description": "MaterialFunction asset path, instead of materialPath (#1138)",
        "aliases": [
          "materialFunctionPath"
        ]
      },
      {
        "name": "expressionName",
        "type": "string",
        "required": true,
        "description": "A description, class, parameter name, FunctionInput/FunctionOutput name, index or engine name"
      }
    ],
    "choices": [
      {
        "mode": "exactlyOne",
        "branches": [
          [
            "materialPath"
          ],
          [
            "functionPath"
          ]
        ]
      }
    ]
  },
  "disconnect_material_property": {
    "category": "material",
    "params": [
      {
        "name": "materialPath",
        "type": "string",
        "required": true,
        "description": "Material asset path",
        "aliases": [
          "assetPath"
        ]
      },
      {
        "name": "property",
        "type": "string",
        "required": true,
        "description": "Material property: BaseColor, Normal, Roughness, Metallic, EmissiveColor, etc."
      }
    ]
  },
  "duplicate_material": {
    "category": "material",
    "params": [
      {
        "name": "sourcePath",
        "type": "string",
        "required": true,
        "description": "Material asset to copy"
      },
      {
        "name": "destinationPath",
        "type": "string",
        "required": true,
        "description": "Package path of the copy, including its name; a missing folder is created"
      }
    ],
    "contractExempt": "Makes the destination folder under the contract values before the copy can fail"
  },
  "end_material_transaction": {
    "category": "material",
    "params": [],
    "contractExempt": "Commits whatever editor transaction is open"
  },
  "export_material_graph": {
    "category": "material",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Material asset path",
        "aliases": [
          "materialPath"
        ]
      }
    ]
  },
  "get_material_shader_stats": {
    "category": "material",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Material asset path",
        "aliases": [
          "materialPath"
        ]
      }
    ]
  },
  "get_material_usage": {
    "category": "material",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Material or MaterialInstance asset path",
        "aliases": [
          "path"
        ]
      }
    ]
  },
  "import_material_graph": {
    "category": "material",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Material asset path",
        "aliases": [
          "materialPath"
        ]
      },
      {
        "name": "nodes",
        "type": "array",
        "required": true,
        "description": "Graph spec: [{name,class,posX,posY,...}]",
        "items": "object"
      },
      {
        "name": "propertyConnections",
        "type": "array",
        "required": false,
        "description": "[{property,from,outputIndex}]",
        "items": "object"
      }
    ]
  },
  "list_expression_types": {
    "category": "material",
    "params": [
      {
        "name": "cursor",
        "type": "string",
        "required": false,
        "description": "Resume a paged read: pass back the 'nextCursor' from the previous page, unmodified"
      },
      {
        "name": "limit",
        "type": "integer",
        "required": false,
        "description": "Rows to return on this page"
      }
    ]
  },
  "list_expressions_in_function": {
    "category": "material",
    "params": [
      {
        "name": "functionPath",
        "type": "string",
        "required": true,
        "description": "MaterialFunction asset path (#463)",
        "aliases": [
          "materialFunctionPath"
        ]
      }
    ]
  },
  "list_material_expressions": {
    "category": "material",
    "params": [
      {
        "name": "materialPath",
        "type": "string",
        "required": true,
        "description": "Material asset path",
        "aliases": [
          "path",
          "assetPath"
        ]
      },
      {
        "name": "includeInputs",
        "type": "boolean",
        "required": false,
        "description": "Include each node's input pin wiring (default false)"
      },
      {
        "name": "cursor",
        "type": "string",
        "required": false,
        "description": "Resume a paged read: pass back the 'nextCursor' from the previous page, unmodified"
      },
      {
        "name": "limit",
        "type": "integer",
        "required": false,
        "description": "Rows to return on this page"
      }
    ]
  },
  "list_material_parameters": {
    "category": "material",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Material or MaterialInstance asset path",
        "aliases": [
          "path",
          "materialPath"
        ]
      }
    ]
  },
  "list_material_static_switches": {
    "category": "material",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Material or MaterialInstance asset path",
        "aliases": [
          "path",
          "materialPath"
        ]
      }
    ]
  },
  "read_material": {
    "category": "material",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Material or MaterialInstance asset path",
        "aliases": [
          "path",
          "materialPath"
        ]
      }
    ]
  },
  "read_material_designer": {
    "category": "material",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": false,
        "description": "A DynamicMaterialInstance, a DynamicMaterialModel, or an object path inside one"
      },
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Label of the actor whose component material to read; a label naming several actors is refused"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Full object path of that actor; the unambiguous selector"
      },
      {
        "name": "componentName",
        "type": "string",
        "required": false,
        "description": "Primitive component whose material slot to read (default: the first with material slots)"
      },
      {
        "name": "slotIndex",
        "type": "integer",
        "required": false,
        "description": "Material slot index on the component (default 0)"
      },
      {
        "name": "slotName",
        "type": "string",
        "required": false,
        "description": "Material slot name, instead of slotIndex"
      },
      {
        "name": "world",
        "type": "string",
        "required": false,
        "description": "World holding the actor: editor (default) | pie | auto"
      },
      {
        "name": "pieInstance",
        "type": "number",
        "required": false,
        "description": "Which PIE world when several run (0 = server/primary). See editor(list_pie_instances)"
      },
      {
        "name": "designerSlot",
        "type": "number",
        "required": false,
        "description": "Material Designer slot: index, or the material property it serves (BaseColor, or its short name Base)",
        "orTypes": [
          "string"
        ]
      },
      {
        "name": "layerIndex",
        "type": "integer",
        "required": false,
        "description": "Only this layer of the slot"
      },
      {
        "name": "maxDepth",
        "type": "integer",
        "required": false,
        "description": "How deep to describe nested stages and values (default 10)"
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
            "actorLabel"
          ],
          [
            "actorPath"
          ]
        ]
      }
    ]
  },
  "read_material_graph": {
    "category": "material",
    "params": [
      {
        "name": "materialPath",
        "type": "string",
        "required": true,
        "description": "Material asset path",
        "aliases": [
          "path",
          "assetPath"
        ]
      },
      {
        "name": "expressionIndex",
        "type": "number",
        "required": false,
        "description": "Read just this node (a nodeId) and the source expressions its inputs link to, unpaged"
      },
      {
        "name": "cursor",
        "type": "string",
        "required": false,
        "description": "Resume a paged read: pass back the 'nextCursor' from the previous page, unmodified"
      },
      {
        "name": "limit",
        "type": "integer",
        "required": false,
        "description": "Rows to return on this page"
      }
    ]
  },
  "read_material_instance": {
    "category": "material",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": false,
        "description": "MaterialInstance asset path",
        "aliases": [
          "path",
          "materialPath"
        ]
      },
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Label of the actor whose component material to read; a label naming several actors is refused"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Full object path of that actor; the unambiguous selector"
      },
      {
        "name": "componentName",
        "type": "string",
        "required": false,
        "description": "Primitive component whose material slot to read (default: the first with material slots)"
      },
      {
        "name": "slotIndex",
        "type": "integer",
        "required": false,
        "description": "Material slot index on the component (default 0)"
      },
      {
        "name": "slotName",
        "type": "string",
        "required": false,
        "description": "Material slot name, instead of slotIndex"
      },
      {
        "name": "world",
        "type": "string",
        "required": false,
        "description": "World holding the actor: editor | pie | auto (default auto: PIE when running, else editor)"
      },
      {
        "name": "pieInstance",
        "type": "number",
        "required": false,
        "description": "Which PIE world when several run (0 = server/primary). See editor(list_pie_instances)"
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
            "actorLabel"
          ],
          [
            "actorPath"
          ]
        ]
      }
    ]
  },
  "read_material_parameter_collection": {
    "category": "material",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "MaterialParameterCollection asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "world",
        "type": "string",
        "required": false,
        "description": "World whose live collection instance to read: editor, pie or auto. Omit for the stored defaults only"
      },
      {
        "name": "pieInstance",
        "type": "number",
        "required": false,
        "description": "Which PIE world when several run (0 = server/primary). See editor(list_pie_instances)"
      }
    ]
  },
  "read_runtime_virtual_texture": {
    "category": "material",
    "params": [
      {
        "name": "rvtPath",
        "type": "string",
        "required": true,
        "description": "RuntimeVirtualTexture asset path",
        "aliases": [
          "assetPath"
        ]
      }
    ]
  },
  "recompile_material": {
    "category": "material",
    "params": [
      {
        "name": "materialPath",
        "type": "string",
        "required": true,
        "description": "Material asset path",
        "aliases": [
          "path",
          "assetPath"
        ]
      },
      {
        "name": "recompileChildren",
        "type": "boolean",
        "required": false,
        "description": "Cascade to every MaterialInstanceConstant whose parent chain reaches this material (#421)"
      }
    ]
  },
  "remove_material_designer_layer": {
    "category": "material",
    "params": [
      {
        "name": "objectPath",
        "type": "string",
        "required": false,
        "description": "Full object path of the layer, as read_material_designer reports it"
      },
      {
        "name": "assetPath",
        "type": "string",
        "required": false,
        "description": "A DynamicMaterialInstance, a DynamicMaterialModel, or an object path inside one"
      },
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Label of the actor whose component material to read; a label naming several actors is refused"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Full object path of that actor; the unambiguous selector"
      },
      {
        "name": "componentName",
        "type": "string",
        "required": false,
        "description": "Primitive component whose material slot to read (default: the first with material slots)"
      },
      {
        "name": "slotIndex",
        "type": "integer",
        "required": false,
        "description": "Material slot index on the component (default 0)"
      },
      {
        "name": "slotName",
        "type": "string",
        "required": false,
        "description": "Material slot name, instead of slotIndex"
      },
      {
        "name": "world",
        "type": "string",
        "required": false,
        "description": "World holding the actor: editor (default) | pie | auto"
      },
      {
        "name": "pieInstance",
        "type": "number",
        "required": false,
        "description": "Which PIE world when several run (0 = server/primary). See editor(list_pie_instances)"
      },
      {
        "name": "designerSlot",
        "type": "number",
        "required": false,
        "description": "Material Designer slot: index, or the material property it serves (BaseColor, or its short name Base)",
        "orTypes": [
          "string"
        ]
      },
      {
        "name": "layerIndex",
        "type": "integer",
        "required": false,
        "description": "Material Designer layer index within the slot"
      },
      {
        "name": "layerName",
        "type": "string",
        "required": false,
        "description": "Material Designer layer name"
      }
    ],
    "choices": [
      {
        "mode": "exactlyOne",
        "branches": [
          [
            "objectPath"
          ],
          [
            "assetPath"
          ],
          [
            "actorLabel"
          ],
          [
            "actorPath"
          ]
        ]
      }
    ]
  },
  "render_material_preview": {
    "category": "material",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Material asset path",
        "aliases": [
          "materialPath"
        ]
      },
      {
        "name": "outputPath",
        "type": "string",
        "required": true,
        "description": "Absolute file path for the PNG output"
      },
      {
        "name": "width",
        "type": "number",
        "required": false,
        "description": "Image width in pixels (default 256)"
      },
      {
        "name": "height",
        "type": "number",
        "required": false,
        "description": "Image height in pixels (default 256)"
      }
    ]
  },
  "set_custom_expression": {
    "category": "material",
    "params": [
      {
        "name": "materialPath",
        "type": "string",
        "required": false,
        "description": "Material asset path",
        "aliases": [
          "path",
          "assetPath"
        ]
      },
      {
        "name": "functionPath",
        "type": "string",
        "required": false,
        "description": "MaterialFunction asset path, instead of materialPath (#1138)",
        "aliases": [
          "materialFunctionPath"
        ]
      },
      {
        "name": "expressionIndex",
        "type": "number",
        "required": true,
        "description": "Node position in the graph's expression list, as list_expressions reports it"
      },
      {
        "name": "code",
        "type": "string",
        "required": false,
        "description": "HLSL code body (#617)"
      },
      {
        "name": "inputs",
        "type": "array",
        "required": false,
        "description": "Named input pins; rebuilds the node's inputs (#617)",
        "items": "string"
      },
      {
        "name": "outputType",
        "type": "string",
        "required": false,
        "description": "float1 | float2 | float3 | float4 | materialAttributes (#617)"
      },
      {
        "name": "description",
        "type": "string",
        "required": false,
        "description": "Node description"
      }
    ],
    "choices": [
      {
        "mode": "exactlyOne",
        "branches": [
          [
            "materialPath"
          ],
          [
            "functionPath"
          ]
        ]
      }
    ]
  },
  "set_expression_value": {
    "category": "material",
    "params": [
      {
        "name": "materialPath",
        "type": "string",
        "required": false,
        "description": "Material asset path",
        "aliases": [
          "path",
          "assetPath"
        ]
      },
      {
        "name": "functionPath",
        "type": "string",
        "required": false,
        "description": "MaterialFunction asset path, instead of materialPath (#1138)",
        "aliases": [
          "materialFunctionPath"
        ]
      },
      {
        "name": "expressionIndex",
        "type": "number",
        "required": true,
        "description": "Node position in the graph's expression list, as list_expressions reports it"
      },
      {
        "name": "value",
        "type": "any",
        "required": false,
        "description": "Constant or parameter default: a number, or a colour/vector {r,g,b,a}, {x,y,z,w}, [r,g,b,a] or '(R=..,G=..)'; with propertyName, the property's value"
      },
      {
        "name": "color",
        "type": "color",
        "required": false,
        "description": "A colour or vector value, instead of value",
        "aliases": [
          "colour"
        ]
      },
      {
        "name": "x",
        "type": "number",
        "required": false,
        "description": "A vector component passed at the top level instead of inside value (#979)"
      },
      {
        "name": "y",
        "type": "number",
        "required": false,
        "description": "A vector component passed at the top level instead of inside value (#979)"
      },
      {
        "name": "parameterName",
        "type": "string",
        "required": false,
        "description": "Rename a scalar or vector parameter node"
      },
      {
        "name": "texturePath",
        "type": "string",
        "required": false,
        "description": "Default texture of a TextureSample node"
      },
      {
        "name": "uTiling",
        "type": "number",
        "required": false,
        "description": "U tiling multiplier of a TextureCoordinate node"
      },
      {
        "name": "vTiling",
        "type": "number",
        "required": false,
        "description": "V tiling multiplier of a TextureCoordinate node"
      },
      {
        "name": "coordinateIndex",
        "type": "integer",
        "required": false,
        "description": "UV channel a TextureCoordinate node reads"
      },
      {
        "name": "propertyName",
        "type": "string",
        "required": false,
        "description": "Any other UPROPERTY on the node, set from value by reflection, e.g. SamplerType (#663)"
      }
    ],
    "choices": [
      {
        "mode": "exactlyOne",
        "branches": [
          [
            "materialPath"
          ],
          [
            "functionPath"
          ]
        ]
      }
    ]
  },
  "set_material_base_color": {
    "category": "material",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Material asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "color",
        "type": "color",
        "required": true,
        "description": "Base colour {r, g, b, a?}; a channel left out is 1"
      }
    ]
  },
  "set_material_blend_mode": {
    "category": "material",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Material asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "blendMode",
        "type": "string",
        "required": true,
        "description": "Blend mode: Opaque, Masked, Translucent, Additive, Modulate, AlphaComposite, AlphaHoldout"
      }
    ]
  },
  "set_material_designer_value": {
    "category": "material",
    "params": [
      {
        "name": "propertyName",
        "type": "string",
        "required": true,
        "description": "Component property to write, e.g. Value, Offset, Tiling, Rotation, Text, bEnabled, LayerName"
      },
      {
        "name": "value",
        "type": "any",
        "required": true,
        "description": "The value to write"
      },
      {
        "name": "objectPath",
        "type": "string",
        "required": false,
        "description": "Full object path of the component, as read_material_designer reports it"
      },
      {
        "name": "componentPath",
        "type": "string",
        "required": false,
        "description": "Component path inside the target's model, as read_material_designer reports it"
      },
      {
        "name": "layerIndex",
        "type": "integer",
        "required": false,
        "description": "Material Designer layer index within the slot"
      },
      {
        "name": "layerName",
        "type": "string",
        "required": false,
        "description": "Material Designer layer name"
      },
      {
        "name": "stage",
        "type": "string",
        "required": false,
        "description": "Stage of the addressed layer: base | mask | index"
      },
      {
        "name": "assetPath",
        "type": "string",
        "required": false,
        "description": "A DynamicMaterialInstance, a DynamicMaterialModel, or an object path inside one"
      },
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Label of the actor whose component material to read; a label naming several actors is refused"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Full object path of that actor; the unambiguous selector"
      },
      {
        "name": "componentName",
        "type": "string",
        "required": false,
        "description": "Primitive component whose material slot to read (default: the first with material slots)"
      },
      {
        "name": "slotIndex",
        "type": "integer",
        "required": false,
        "description": "Material slot index on the component (default 0)"
      },
      {
        "name": "slotName",
        "type": "string",
        "required": false,
        "description": "Material slot name, instead of slotIndex"
      },
      {
        "name": "world",
        "type": "string",
        "required": false,
        "description": "World holding the actor: editor (default) | pie | auto"
      },
      {
        "name": "pieInstance",
        "type": "number",
        "required": false,
        "description": "Which PIE world when several run (0 = server/primary). See editor(list_pie_instances)"
      },
      {
        "name": "designerSlot",
        "type": "number",
        "required": false,
        "description": "Material Designer slot: index, or the material property it serves (BaseColor, or its short name Base)",
        "orTypes": [
          "string"
        ]
      },
      {
        "name": "rebuild",
        "type": "boolean",
        "required": false,
        "description": "Request a material build after the write (default true when no setter ran)"
      }
    ],
    "choices": [
      {
        "mode": "exactlyOne",
        "branches": [
          [
            "objectPath"
          ],
          [
            "componentPath"
          ],
          [
            "layerIndex"
          ],
          [
            "layerName"
          ]
        ]
      }
    ]
  },
  "set_material_domain": {
    "category": "material",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Material asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "materialDomain",
        "type": "string",
        "required": true,
        "description": "Material domain: Surface, DeferredDecal, LightFunction, Volume, PostProcess, UI, RuntimeVirtualTexture",
        "aliases": [
          "domain"
        ]
      }
    ]
  },
  "set_material_instance_parent": {
    "category": "material",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "MaterialInstanceConstant asset path",
        "aliases": [
          "path",
          "materialPath"
        ]
      },
      {
        "name": "newParentPath",
        "type": "string",
        "required": true,
        "description": "New parent material or material instance path",
        "aliases": [
          "parentPath"
        ]
      }
    ]
  },
  "set_material_parameter": {
    "category": "material",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "MaterialInstanceConstant asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "parameterName",
        "type": "string",
        "required": true,
        "description": "Material parameter name"
      },
      {
        "name": "parameterType",
        "type": "string",
        "required": false,
        "description": "scalar | vector (alias color) | texture; detected from the instance when omitted"
      },
      {
        "name": "value",
        "type": "any",
        "required": false,
        "description": "A number (scalar), a colour {r,g,b,a}, [r,g,b,a] or '(R=..,G=..,B=..,A=..)' (vector), or a texture asset path (texture)"
      },
      {
        "name": "color",
        "type": "color",
        "required": false,
        "description": "A colour or vector value, instead of value",
        "aliases": [
          "colour"
        ]
      },
      {
        "name": "texturePath",
        "type": "string",
        "required": false,
        "description": "Texture asset path, instead of value, for a texture parameter"
      },
      {
        "name": "association",
        "type": "string",
        "required": false,
        "description": "Material parameter association: Global (default), Layer, or Blend"
      }
    ],
    "choices": [
      {
        "mode": "atLeastOne",
        "branches": [
          [
            "value"
          ],
          [
            "color"
          ],
          [
            "texturePath"
          ]
        ]
      }
    ]
  },
  "set_material_shading_model": {
    "category": "material",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Material asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "shadingModel",
        "type": "string",
        "required": true,
        "description": "Shading model, e.g. DefaultLit, Unlit, Subsurface, ClearCoat"
      }
    ]
  },
  "set_material_static_switch": {
    "category": "material",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "MaterialInstanceConstant asset path",
        "aliases": [
          "path",
          "materialPath"
        ]
      },
      {
        "name": "parameterName",
        "type": "string",
        "required": true,
        "description": "Material parameter name"
      },
      {
        "name": "value",
        "type": "any",
        "required": true,
        "description": "Static switch value, a boolean"
      },
      {
        "name": "association",
        "type": "string",
        "required": false,
        "description": "Material parameter association: Global, Layer, or Blend"
      },
      {
        "name": "parameterIndex",
        "type": "number",
        "required": false,
        "description": "Material layer/blend parameter index"
      }
    ]
  },
  "set_material_usage": {
    "category": "material",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Material asset path",
        "aliases": [
          "path",
          "materialPath"
        ]
      },
      {
        "name": "usages",
        "type": "array",
        "required": false,
        "description": "MATUSAGE flag names (InstancedStaticMeshes, Nanite, VolumetricCloud, ...); get_usage lists every name this engine accepts",
        "items": "string"
      },
      {
        "name": "usage",
        "type": "string",
        "required": false,
        "description": "One MATUSAGE flag name"
      }
    ],
    "choices": [
      {
        "mode": "atLeastOne",
        "branches": [
          [
            "usages"
          ],
          [
            "usage"
          ]
        ]
      }
    ]
  },
  "set_rvt_volume_bounds": {
    "category": "material",
    "params": [
      {
        "name": "rvtPath",
        "type": "string",
        "required": false,
        "description": "RuntimeVirtualTexture whose one bound volume to refit",
        "aliases": [
          "assetPath"
        ]
      },
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Label of the RuntimeVirtualTextureVolume"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Full object path of the RuntimeVirtualTextureVolume"
      },
      {
        "name": "boundsMode",
        "type": "string",
        "required": false,
        "description": "writers (default: cover every primitive writing into this RVT) | alignActor (match one actor's box and rotation)"
      },
      {
        "name": "boundsAlignActor",
        "type": "string",
        "required": false,
        "description": "Actor label or object path whose rotation and bounds the volume aligns to, typically the landscape"
      }
    ],
    "choices": [
      {
        "mode": "exactlyOne",
        "branches": [
          [
            "rvtPath"
          ],
          [
            "actorLabel"
          ],
          [
            "actorPath"
          ]
        ]
      }
    ]
  },
  "validate_material": {
    "category": "material",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Material asset path",
        "aliases": [
          "materialPath"
        ]
      }
    ]
  }
};

/** The Params: clause of each spec'd bridge method. */
export const paramsClauses: Readonly<Record<string, string>> = {
  add_expression_in_function: "Params: functionPath (or materialFunctionPath), expressionType, positionX?, positionY?, inputName?, inputType?, outputName?, name?",
  add_material_designer_layer: "Params: assetPath OR actorLabel OR actorPath, componentName?, slotIndex?, slotName?, world?, pieInstance?, designerSlot?, materialProperty?, layerName?",
  add_material_expression: "Params: materialPath (or path, or assetPath), expressionType, name? (or expressionName), parameterName?, group?, sortPriority?, defaultValue?, value?, channels?, positionX?, positionY?",
  add_rvt_output: "Params: materialPath (or assetPath), expressionName?, mirrorProperties?, positionX?, positionY?, recompile?",
  add_rvt_sampler: "Params: materialPath (or assetPath), rvtPath, expressionName?, connectOutputs?, positionX?, positionY?, recompile?",
  add_rvt_volume: "Params: rvtPath (or assetPath), actorLabel?, boundsMode?, boundsAlignActor?",
  assign_rvt_to_landscape: "Params: actorLabel OR actorPath, rvtPaths?, rvtPath?, assignMode?",
  batch_set_material_instances: "Params: instances",
  begin_material_transaction: "Params: label?",
  build_material: "Params: materialPath (or assetPath) OR name, packagePath?, textures, samplerTypes?, clearExisting?, assignToMesh? (or meshPath), meshSlots?",
  build_material_graph: "Params: assetPath (or materialPath), nodes, propertyConnections?",
  clear_material_instance_parameters: "Params: assetPath (or path, or materialPath)",
  connect_expressions_in_function: "Params: functionPath (or materialFunctionPath), sourceExpression, sourceOutput?, targetExpression, targetInput?",
  connect_material_expressions: "Params: materialPath (or path, or assetPath), sourceExpression, sourceOutput?, targetExpression, targetInput?",
  connect_texture_to_material: "Params: materialPath (or path, or assetPath), texturePath, property? (or materialProperty)",
  connect_to_material_property: "Params: materialPath (or path, or assetPath), expressionName, outputName?, property",
  create_material: "Params: name, packagePath?, onConflict?",
  create_material_designer: "Params: name, packagePath?, onConflict?",
  create_material_function: "Params: name, packagePath?, onConflict?, description?",
  create_material_instance: "Params: parentPath, name, packagePath?, onConflict?",
  create_material_simple: "Params: name, packagePath?, onConflict?, baseColor?, metallic?, specular?, roughness?, emissive?, usages?",
  create_runtime_virtual_texture: "Params: name, packagePath?, materialType?, onConflict?",
  delete_material_expression: "Params: materialPath (or path, or assetPath) OR functionPath (or materialFunctionPath), expressionName",
  disconnect_material_property: "Params: materialPath (or assetPath), property",
  duplicate_material: "Params: sourcePath, destinationPath",
  end_material_transaction: "Params: none",
  export_material_graph: "Params: assetPath (or materialPath)",
  get_material_shader_stats: "Params: assetPath (or materialPath)",
  get_material_usage: "Params: assetPath (or path)",
  import_material_graph: "Params: assetPath (or materialPath), nodes, propertyConnections?",
  list_expression_types: "Params: cursor?, limit?",
  list_expressions_in_function: "Params: functionPath (or materialFunctionPath)",
  list_material_expressions: "Params: materialPath (or path, or assetPath), includeInputs?, cursor?, limit?",
  list_material_parameters: "Params: assetPath (or path, or materialPath)",
  list_material_static_switches: "Params: assetPath (or path, or materialPath)",
  read_material: "Params: assetPath (or path, or materialPath)",
  read_material_designer: "Params: assetPath OR actorLabel OR actorPath, componentName?, slotIndex?, slotName?, world?, pieInstance?, designerSlot?, layerIndex?, maxDepth?",
  read_material_graph: "Params: materialPath (or path, or assetPath), expressionIndex?, cursor?, limit?",
  read_material_instance: "Params: assetPath (or path, or materialPath) OR actorLabel OR actorPath, componentName?, slotIndex?, slotName?, world?, pieInstance?",
  read_material_parameter_collection: "Params: assetPath (or path), world?, pieInstance?",
  read_runtime_virtual_texture: "Params: rvtPath (or assetPath)",
  recompile_material: "Params: materialPath (or path, or assetPath), recompileChildren?",
  remove_material_designer_layer: "Params: objectPath OR assetPath OR actorLabel OR actorPath, componentName?, slotIndex?, slotName?, world?, pieInstance?, designerSlot?, layerIndex?, layerName?",
  render_material_preview: "Params: assetPath (or materialPath), outputPath, width?, height?",
  set_custom_expression: "Params: materialPath (or path, or assetPath) OR functionPath (or materialFunctionPath), expressionIndex, code?, inputs?, outputType?, description?",
  set_expression_value: "Params: materialPath (or path, or assetPath) OR functionPath (or materialFunctionPath), expressionIndex, value?, color? (or colour), x?, y?, parameterName?, texturePath?, uTiling?, vTiling?, coordinateIndex?, propertyName?",
  set_material_base_color: "Params: assetPath (or path), color",
  set_material_blend_mode: "Params: assetPath (or path), blendMode",
  set_material_designer_value: "Params: propertyName, value, objectPath OR componentPath OR layerIndex OR layerName, stage?, assetPath?, actorLabel?, actorPath?, componentName?, slotIndex?, slotName?, world?, pieInstance?, designerSlot?, rebuild?",
  set_material_domain: "Params: assetPath (or path), materialDomain (or domain)",
  set_material_instance_parent: "Params: assetPath (or path, or materialPath), newParentPath (or parentPath)",
  set_material_parameter: "Params: assetPath (or path), parameterName, parameterType?, at least one of value/color (or colour)/texturePath, association?",
  set_material_shading_model: "Params: assetPath (or path), shadingModel",
  set_material_static_switch: "Params: assetPath (or path, or materialPath), parameterName, value, association?, parameterIndex?",
  set_material_usage: "Params: assetPath (or path, or materialPath), at least one of usages/usage",
  set_rvt_volume_bounds: "Params: rvtPath (or assetPath) OR actorLabel OR actorPath, boundsMode?, boundsAlignActor?",
  validate_material: "Params: assetPath (or materialPath)",
};

/** Every key the spec'd material handlers declare, aliases included. */
export const schema: Record<string, z.ZodType> = {
  actorLabel: z.string().optional().describe("Label of the actor whose component material to read; a label naming several actors is refused (add_material_designer_layer, read_material_designer, read_material_instance, remove_material_designer_layer, set_material_designer_value). Editor label for the new volume (default RVTVolume_<rvt name>) (add_rvt_volume). Label of the landscape (assign_rvt_to_landscape). Label of the RuntimeVirtualTextureVolume (set_rvt_volume_bounds)"),
  actorPath: z.string().optional().describe("Full object path of that actor; the unambiguous selector (add_material_designer_layer, read_material_designer, read_material_instance, remove_material_designer_layer, set_material_designer_value). Full object path of the landscape (assign_rvt_to_landscape). Full object path of the RuntimeVirtualTextureVolume (set_rvt_volume_bounds)"),
  assetPath: z.string().optional().describe("A DynamicMaterialInstance, a DynamicMaterialModel, or an object path inside one (add_material_designer_layer, read_material_designer, remove_material_designer_layer, set_material_designer_value). Alias for materialPath (add_material_expression, add_rvt_output, add_rvt_sampler, build_material, connect_material_expressions, connect_texture_to_material, connect_to_material_property, delete_material_expression, disconnect_material_property, list_material_expressions, read_material_graph, recompile_material, set_custom_expression, set_expression_value). Alias for rvtPath (add_rvt_volume, read_runtime_virtual_texture, set_rvt_volume_bounds). Material asset path (build_material_graph, export_material_graph, get_material_shader_stats, import_material_graph, render_material_preview, set_material_base_color, set_material_blend_mode, set_material_domain, set_material_shading_model, set_material_usage, validate_material). MaterialInstanceConstant asset path (clear_material_instance_parameters, set_material_instance_parent, set_material_parameter, set_material_static_switch). Material or MaterialInstance asset path (get_material_usage, list_material_parameters, list_material_static_switches, read_material). MaterialInstance asset path (read_material_instance). MaterialParameterCollection asset path (read_material_parameter_collection)"),
  assignMode: z.string().optional().describe("set (default: replace the list) | add | remove"),
  assignToMesh: z.string().optional().describe("StaticMesh or SkeletalMesh asset to put the finished material on (#946)"),
  association: z.string().optional().describe("Material parameter association: Global (default), Layer, or Blend (set_material_parameter). Material parameter association: Global, Layer, or Blend (set_material_static_switch)"),
  baseColor: z.object({ r: z.number(), g: z.number(), b: z.number(), a: z.number().optional() }).optional().describe("Base colour {r,g,b}"),
  blendMode: z.string().optional().describe("Blend mode: Opaque, Masked, Translucent, Additive, Modulate, AlphaComposite, AlphaHoldout"),
  boundsAlignActor: z.string().optional().describe("Actor label or object path whose rotation and bounds the volume aligns to, typically the landscape"),
  boundsMode: z.string().optional().describe("writers (cover every primitive writing into this RVT) | alignActor (match one actor's box and rotation) (add_rvt_volume). writers (default: cover every primitive writing into this RVT) | alignActor (match one actor's box and rotation) (set_rvt_volume_bounds)"),
  channels: z.record(z.unknown()).optional().describe("ComponentMask channels {r,g,b,a} as bools"),
  clearExisting: z.boolean().optional().describe("Delete every existing expression before building (#946)"),
  code: z.string().optional().describe("HLSL code body (#617)"),
  color: z.object({ r: z.number(), g: z.number(), b: z.number(), a: z.number().optional() }).optional().describe("A colour or vector value, instead of value (set_expression_value, set_material_parameter). Base colour {r, g, b, a?}; a channel left out is 1 (set_material_base_color)"),
  colour: z.object({ r: z.number(), g: z.number(), b: z.number(), a: z.number().optional() }).optional().describe("Alias for color"),
  componentName: z.string().optional().describe("Primitive component whose material slot to read (default: the first with material slots)"),
  componentPath: z.string().optional().describe("Component path inside the target's model, as read_material_designer reports it"),
  connectOutputs: z.boolean().optional().describe("Connect each sample output to the material property of the same name (default true)"),
  coordinateIndex: z.number().int().optional().describe("UV channel a TextureCoordinate node reads"),
  cursor: z.string().optional().describe("Resume a paged read: pass back the 'nextCursor' from the previous page, unmodified"),
  defaultValue: z.union([z.number(), z.record(z.unknown())]).optional().describe("ScalarParameter default (a number) or VectorParameter default ({r,g,b,a})"),
  description: z.string().optional().describe("MaterialFunction description (#463) (create_material_function). Node description (set_custom_expression)"),
  designerSlot: z.union([z.number(), z.string()]).optional().describe("Material Designer slot: index, or the material property it serves (BaseColor, or its short name Base)"),
  destinationPath: z.string().optional().describe("Package path of the copy, including its name; a missing folder is created"),
  domain: z.string().optional().describe("Alias for materialDomain"),
  emissive: z.number().optional().describe("Emissive strength"),
  expressionIndex: z.number().optional().describe("Read just this node (a nodeId) and the source expressions its inputs link to, unpaged (read_material_graph). Node position in the graph's expression list, as list_expressions reports it (set_custom_expression, set_expression_value)"),
  expressionName: z.string().optional().describe("Alias for name (add_material_expression). Node name; an existing node of that name is reused (add_rvt_output, add_rvt_sampler). Expression to wire (connect_to_material_property). A description, class, parameter name, FunctionInput/FunctionOutput name, index or engine name (delete_material_expression)"),
  expressionType: z.string().optional().describe("Expression type, e.g. Constant3Vector, FunctionInput, FunctionOutput, If (add_expression_in_function). Expression type: Constant, TextureSample, Multiply, Lerp, ScalarParameter, etc. (add_material_expression)"),
  functionPath: z.string().optional().describe("MaterialFunction asset path (#463) (add_expression_in_function, connect_expressions_in_function, list_expressions_in_function). MaterialFunction asset path, instead of materialPath (#1138) (delete_material_expression, set_custom_expression, set_expression_value)"),
  group: z.string().optional().describe("Parameter Group, on a parameter node (#318)"),
  height: z.number().optional().describe("Image height in pixels (default 256)"),
  includeInputs: z.boolean().optional().describe("Include each node's input pin wiring (default false)"),
  inputName: z.string().optional().describe("FunctionInput name (#463)"),
  inputs: z.array(z.string()).optional().describe("Named input pins; rebuilds the node's inputs (#617)"),
  inputType: z.string().optional().describe("FunctionInput type: Scalar|Vector2|Vector3|Vector4|Texture2D|TextureCube|StaticBool|MaterialAttributes (#463)"),
  instances: z.array(z.record(z.unknown())).optional().describe("[{assetPath, parentPath?, parameters?:[{name, type (scalar|vector|texture), value}]}]. value: number (scalar), {r,g,b,a} (vector), or texture path (texture) (#594)"),
  label: z.string().optional().describe("Transaction label (default MCP Material Edit)"),
  layerIndex: z.number().int().optional().describe("Only this layer of the slot (read_material_designer). Material Designer layer index within the slot (remove_material_designer_layer, set_material_designer_value)"),
  layerName: z.string().optional().describe("Name for the new layer (add_material_designer_layer). Material Designer layer name (remove_material_designer_layer, set_material_designer_value)"),
  limit: z.number().int().optional().describe("Rows to return on this page"),
  materialDomain: z.string().optional().describe("Material domain: Surface, DeferredDecal, LightFunction, Volume, PostProcess, UI, RuntimeVirtualTexture"),
  materialFunctionPath: z.string().optional().describe("Alias for functionPath"),
  materialPath: z.string().optional().describe("Material asset path (add_material_expression, add_rvt_output, add_rvt_sampler, connect_material_expressions, connect_texture_to_material, connect_to_material_property, delete_material_expression, disconnect_material_property, list_material_expressions, read_material_graph, recompile_material, set_custom_expression, set_expression_value). Existing material to build into (build_material). Alias for assetPath (build_material_graph, clear_material_instance_parameters, export_material_graph, get_material_shader_stats, import_material_graph, list_material_parameters, list_material_static_switches, read_material, read_material_instance, render_material_preview, set_material_instance_parent, set_material_static_switch, set_material_usage, validate_material)"),
  materialProperty: z.string().optional().describe("EDMMaterialPropertyType of the new layer, and the slot when designerSlot is omitted (default BaseColor) (add_material_designer_layer). Alias for property (connect_texture_to_material)"),
  materialType: z.string().optional().describe("RVT content layout: BaseColor | BaseColor_Normal_Roughness | BaseColor_Normal_Specular | BaseColor_Normal_Specular_YCoCg | BaseColor_Normal_Specular_Mask_YCoCg | Mask4 | WorldHeight | Displacement; one this project disables is refused"),
  maxDepth: z.number().int().optional().describe("How deep to describe nested stages and values (default 10)"),
  meshPath: z.string().optional().describe("Alias for assignToMesh"),
  meshSlots: z.array(z.unknown()).optional().describe("Mesh material slot names or indices to assign (default every slot) (#946)"),
  metallic: z.number().optional().describe("Metallic constant"),
  mirrorProperties: z.boolean().optional().describe("Mirror the material's own property connections into the RVT output node (default true)"),
  name: z.string().optional().describe("Fallback for inputName on a FunctionInput and outputName on a FunctionOutput (add_expression_in_function). Node description (add_material_expression). Name of a new material to create and build (build_material). Material asset name (create_material, create_material_designer, create_material_simple). MaterialFunction asset name (create_material_function). Material instance asset name (create_material_instance). RuntimeVirtualTexture asset name, with no '/' or '.' (create_runtime_virtual_texture)"),
  newParentPath: z.string().optional().describe("New parent material or material instance path"),
  nodes: z.array(z.record(z.unknown())).optional().describe("Graph spec: [{name,class,posX,posY,...}]"),
  objectPath: z.string().optional().describe("Full object path of the layer, as read_material_designer reports it (remove_material_designer_layer). Full object path of the component, as read_material_designer reports it (set_material_designer_value)"),
  onConflict: z.string().optional().describe("When the asset exists: skip (default, report it) | error"),
  outputName: z.string().optional().describe("FunctionOutput name (add_expression_in_function). Output of the expression, by name or index (default: its first output) (connect_to_material_property)"),
  outputPath: z.string().optional().describe("Absolute file path for the PNG output"),
  outputType: z.string().optional().describe("float1 | float2 | float3 | float4 | materialAttributes (#617)"),
  packagePath: z.string().optional().describe("Folder for a new material (default /Game/Materials) (build_material). Folder for the new material (default /Game/Materials) (create_material, create_material_designer, create_material_simple). Folder for the new function (default /Game/Materials/Functions) (create_material_function). Folder for the new instance (default /Game/Materials) (create_material_instance). Folder for the new RVT (default /Game/Textures/RVT) (create_runtime_virtual_texture)"),
  parameterIndex: z.number().optional().describe("Material layer/blend parameter index"),
  parameterName: z.string().optional().describe("Parameter name, on a parameter node (add_material_expression). Rename a scalar or vector parameter node (set_expression_value). Material parameter name (set_material_parameter, set_material_static_switch)"),
  parameterType: z.string().optional().describe("scalar | vector (alias color) | texture; detected from the instance when omitted"),
  parentPath: z.string().optional().describe("Parent material or material instance (create_material_instance). Alias for newParentPath (set_material_instance_parent)"),
  path: z.string().optional().describe("Alias for materialPath (add_material_expression, connect_material_expressions, connect_texture_to_material, connect_to_material_property, delete_material_expression, list_material_expressions, read_material_graph, recompile_material, set_custom_expression, set_expression_value). Alias for assetPath (clear_material_instance_parameters, get_material_usage, list_material_parameters, list_material_static_switches, read_material, read_material_instance, read_material_parameter_collection, set_material_base_color, set_material_blend_mode, set_material_domain, set_material_instance_parent, set_material_parameter, set_material_shading_model, set_material_static_switch, set_material_usage)"),
  pieInstance: z.number().optional().describe("Which PIE world when several run (0 = server/primary). See editor(list_pie_instances)"),
  positionX: z.number().optional().describe("Graph editor X position for a new node"),
  positionY: z.number().optional().describe("Graph editor Y position for a new node"),
  property: z.string().optional().describe("Material property: BaseColor, Normal, Roughness, Metallic, EmissiveColor, etc. (default BaseColor) (connect_texture_to_material). Material property: BaseColor, Normal, Roughness, Metallic, EmissiveColor, etc. (connect_to_material_property, disconnect_material_property)"),
  propertyConnections: z.array(z.record(z.unknown())).optional().describe("[{property,from,outputIndex}]"),
  propertyName: z.string().optional().describe("Any other UPROPERTY on the node, set from value by reflection, e.g. SamplerType (#663) (set_expression_value). Component property to write, e.g. Value, Offset, Tiling, Rotation, Text, bEnabled, LayerName (set_material_designer_value)"),
  rebuild: z.boolean().optional().describe("Request a material build after the write (default true when no setter ran)"),
  recompile: z.boolean().optional().describe("Recompile the material after the graph edit (default true)"),
  recompileChildren: z.boolean().optional().describe("Cascade to every MaterialInstanceConstant whose parent chain reaches this material (#421)"),
  roughness: z.number().optional().describe("Roughness constant"),
  rvtPath: z.string().optional().describe("RuntimeVirtualTexture asset path (add_rvt_sampler, add_rvt_volume, read_runtime_virtual_texture). One RuntimeVirtualTexture asset path, instead of rvtPaths (assign_rvt_to_landscape). RuntimeVirtualTexture whose one bound volume to refit (set_rvt_volume_bounds)"),
  rvtPaths: z.array(z.string()).optional().describe("RuntimeVirtualTexture asset paths"),
  samplerTypes: z.record(z.unknown()).optional().describe("Per-texture EMaterialSamplerType override, e.g. {normal: 'VirtualNormal'}; omit to let the texture decide (#946)"),
  shadingModel: z.string().optional().describe("Shading model, e.g. DefaultLit, Unlit, Subsurface, ClearCoat"),
  slotIndex: z.number().int().optional().describe("Material slot index on the component (default 0)"),
  slotName: z.string().optional().describe("Material slot name, instead of slotIndex"),
  sortPriority: z.number().optional().describe("SortPriority within the parameter Group"),
  sourceExpression: z.string().optional().describe("Expression the wire leaves (name, or index inside a MaterialFunction)"),
  sourceOutput: z.string().optional().describe("Output of the source expression, by name or index (default: its first output)"),
  sourcePath: z.string().optional().describe("Material asset to copy"),
  specular: z.number().optional().describe("Specular constant"),
  stage: z.string().optional().describe("Stage of the addressed layer: base | mask | index"),
  targetExpression: z.string().optional().describe("Expression the wire enters (name, or index inside a MaterialFunction)"),
  targetInput: z.string().optional().describe("Input of the target expression, by name or index (default: its first input)"),
  texturePath: z.string().optional().describe("Texture asset path (connect_texture_to_material). Default texture of a TextureSample node (set_expression_value). Texture asset path, instead of value, for a texture parameter (set_material_parameter)"),
  textures: z.record(z.unknown()).optional().describe("{materialProperty: textureAssetPath}: baseColor, normal, roughness, metallic, specular, emissive, opacity, opacityMask, ambientOcclusion, plus packed orm (R=AO,G=Roughness,B=Metallic) and rma (#946)"),
  usage: z.string().optional().describe("One MATUSAGE flag name"),
  usages: z.array(z.string()).optional().describe("MATUSAGE flag names to turn on, e.g. InstancedStaticMeshes, Nanite, NiagaraSprites (create_material_simple). MATUSAGE flag names (InstancedStaticMeshes, Nanite, VolumetricCloud, ...); get_usage lists every name this engine accepts (set_material_usage)"),
  uTiling: z.number().optional().describe("U tiling multiplier of a TextureCoordinate node"),
  value: z.unknown().optional().describe("A number for Constant, {r,g,b} for Constant3Vector, {x,y} for Constant2Vector (add_material_expression). Constant or parameter default: a number, or a colour/vector {r,g,b,a}, {x,y,z,w}, [r,g,b,a] or '(R=..,G=..)'; with propertyName, the property's value (set_expression_value). The value to write (set_material_designer_value). A number (scalar), a colour {r,g,b,a}, [r,g,b,a] or '(R=..,G=..,B=..,A=..)' (vector), or a texture asset path (texture) (set_material_parameter). Static switch value, a boolean (set_material_static_switch)"),
  vTiling: z.number().optional().describe("V tiling multiplier of a TextureCoordinate node"),
  width: z.number().optional().describe("Image width in pixels (default 256)"),
  world: z.string().optional().describe("World holding the actor: editor (default) | pie | auto (add_material_designer_layer, read_material_designer, remove_material_designer_layer, set_material_designer_value). World holding the actor: editor | pie | auto (default auto: PIE when running, else editor) (read_material_instance). World whose live collection instance to read: editor, pie or auto. Omit for the stored defaults only (read_material_parameter_collection)"),
  x: z.number().optional().describe("A vector component passed at the top level instead of inside value (#979)"),
  y: z.number().optional().describe("A vector component passed at the top level instead of inside value (#979)"),
};

/** Declare an action for a spec'd bridge method: effect, summary, method. */
export const specBp = makeSpecBp(paramsClauses, handlerSpecs);
