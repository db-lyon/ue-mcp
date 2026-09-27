// GENERATED FILE - do not edit.
//
// Written by scripts/generate-handler-specs.mjs from tests/golden/handler-specs.json,
// which is recorded from the parameter specs the C++ handlers register with
// (npm run specs:record). To change a parameter, change its RegisterHandler
// spec, re-record, and regenerate (#1057).
import { categorySchema, makeSpecBp, type HandlerSpecs } from "../../surface/handler-spec.js";

/** The recorded contract of every spec'd niagara handler. */
export const handlerSpecs: HandlerSpecs = {
  "add_emitter_renderer": {
    "category": "niagara",
    "params": [
      {
        "name": "systemPath",
        "type": "string",
        "required": true,
        "description": "NiagaraSystem asset path"
      },
      {
        "name": "rendererType",
        "type": "string",
        "required": true,
        "description": "sprite|mesh|ribbon or full class name"
      },
      {
        "name": "emitterName",
        "type": "string",
        "required": false,
        "description": "Emitter handle name. Omit to address the emitter by emitterIndex"
      },
      {
        "name": "emitterIndex",
        "type": "number",
        "required": false,
        "description": "Emitter handle index, used when emitterName is omitted"
      }
    ]
  },
  "add_emitter_to_system": {
    "category": "niagara",
    "params": [
      {
        "name": "systemPath",
        "type": "string",
        "required": true,
        "description": "NiagaraSystem asset path"
      },
      {
        "name": "emitterPath",
        "type": "string",
        "required": true,
        "description": "NiagaraEmitter asset to add"
      }
    ]
  },
  "add_niagara_event_handler": {
    "category": "niagara",
    "params": [
      {
        "name": "systemPath",
        "type": "string",
        "required": true,
        "description": "NiagaraSystem asset path"
      },
      {
        "name": "eventName",
        "type": "string",
        "required": true,
        "description": "Event handler name"
      },
      {
        "name": "emitterName",
        "type": "string",
        "required": false,
        "description": "Emitter handle name. Omit to address the emitter by emitterIndex"
      },
      {
        "name": "emitterIndex",
        "type": "number",
        "required": false,
        "description": "Emitter handle index, used when emitterName is omitted"
      },
      {
        "name": "sourceEmitterId",
        "type": "string",
        "required": false,
        "description": "Id of the emitter whose events this handler listens to (default: this emitter)"
      }
    ]
  },
  "add_niagara_module": {
    "category": "niagara",
    "params": [
      {
        "name": "systemPath",
        "type": "string",
        "required": true,
        "description": "NiagaraSystem asset path"
      },
      {
        "name": "moduleScript",
        "type": "string",
        "required": true,
        "description": "Stock module script path, e.g. /Niagara/Modules/Emitter/SpawnRate"
      },
      {
        "name": "stackContext",
        "type": "string",
        "required": true,
        "description": "ParticleSpawn|ParticleUpdate|EmitterSpawn|EmitterUpdate"
      },
      {
        "name": "emitterName",
        "type": "string",
        "required": false,
        "description": "Emitter handle name. Omit to address the emitter by emitterIndex"
      },
      {
        "name": "emitterIndex",
        "type": "number",
        "required": false,
        "description": "Emitter handle index, used when emitterName is omitted"
      },
      {
        "name": "targetIndex",
        "type": "integer",
        "required": false,
        "description": "Stack insert position; -1 (default) appends"
      }
    ]
  },
  "add_niagara_simulation_stage": {
    "category": "niagara",
    "params": [
      {
        "name": "systemPath",
        "type": "string",
        "required": true,
        "description": "NiagaraSystem asset path"
      },
      {
        "name": "stageName",
        "type": "string",
        "required": true,
        "description": "Simulation stage name"
      },
      {
        "name": "emitterName",
        "type": "string",
        "required": false,
        "description": "Emitter handle name. Omit to address the emitter by emitterIndex"
      },
      {
        "name": "emitterIndex",
        "type": "number",
        "required": false,
        "description": "Emitter handle index, used when emitterName is omitted"
      },
      {
        "name": "enabled",
        "type": "boolean",
        "required": false,
        "description": "Create the stage enabled (default true)"
      }
    ]
  },
  "compile_niagara_system": {
    "category": "niagara",
    "params": [
      {
        "name": "systemPath",
        "type": "string",
        "required": true,
        "description": "NiagaraSystem asset path"
      },
      {
        "name": "force",
        "type": "boolean",
        "required": false,
        "description": "Recompile even when nothing looks dirty (default true)"
      },
      {
        "name": "includeGpuShaders",
        "type": "boolean",
        "required": false,
        "description": "Also wait for GPU shader compilation to finish (default false)"
      }
    ]
  },
  "create_niagara_emitter": {
    "category": "niagara",
    "params": [
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "Emitter asset name"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Folder for the new emitter (default /Game/VFX)"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "When the asset exists: skip (default, report it) | error"
      },
      {
        "name": "templatePath",
        "type": "string",
        "required": false,
        "description": "Emitter asset to copy as the starting point (default: the empty emitter with the standard modules and a sprite renderer)"
      },
      {
        "name": "inherit",
        "type": "boolean",
        "required": false,
        "description": "Make a child that tracks templatePath instead of a copy (default false)"
      }
    ]
  },
  "create_niagara_module_from_hlsl": {
    "category": "niagara",
    "params": [
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "Module script asset name"
      },
      {
        "name": "hlsl",
        "type": "string",
        "required": true,
        "description": "Body of the module's CustomHLSL node; its pins are parsed from it"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Folder for the new module (default /Game/VFX/Modules)"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "When the asset exists: skip (default, report it) | error"
      },
      {
        "name": "inputs",
        "type": "array",
        "required": false,
        "description": "Requested inputs, counted back as requestedInputs; the pins come from the HLSL body",
        "items": "object",
        "fields": [
          {
            "name": "name",
            "type": "string",
            "required": true,
            "description": "Pin name"
          },
          {
            "name": "type",
            "type": "string",
            "required": false,
            "description": "HLSL type (default float)"
          }
        ]
      },
      {
        "name": "outputs",
        "type": "array",
        "required": false,
        "description": "Requested outputs, counted back as requestedOutputs; the pins come from the HLSL body",
        "items": "object",
        "fields": [
          {
            "name": "name",
            "type": "string",
            "required": true,
            "description": "Pin name"
          },
          {
            "name": "type",
            "type": "string",
            "required": false,
            "description": "HLSL type (default float)"
          }
        ]
      }
    ],
    "contractExempt": "Creates and saves a module script under the contract values; nothing it reads fails first"
  },
  "create_niagara_system": {
    "category": "niagara",
    "params": [
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "System asset name"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Folder for the new system (default /Game/VFX)"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "When the asset exists: skip (default, report it) | error"
      }
    ],
    "contractExempt": "Creates and saves a system under the contract values; nothing it reads fails first"
  },
  "create_niagara_system_from_spec": {
    "category": "niagara",
    "params": [
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "System asset name"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Folder for the new system (default /Game/VFX)"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "When the asset exists: skip (default, report it) | error"
      },
      {
        "name": "emitters",
        "type": "array",
        "required": false,
        "description": "Emitters to add; one that does not load is skipped",
        "items": "object",
        "fields": [
          {
            "name": "path",
            "type": "string",
            "required": true,
            "description": "NiagaraEmitter asset path, e.g. /Game/VFX/E_Fire"
          }
        ]
      }
    ],
    "contractExempt": "Creates and saves a system under the contract values; nothing it reads fails first"
  },
  "create_scratch_module": {
    "category": "niagara",
    "params": [
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "Module script asset name"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Folder for the new module (default /Game/VFX)"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "When the asset exists: skip (default, report it) | error"
      },
      {
        "name": "inputs",
        "type": "array",
        "required": false,
        "description": "Inputs declared on a pass-through CustomHLSL node",
        "items": "object",
        "fields": [
          {
            "name": "name",
            "type": "string",
            "required": true,
            "description": "Pin name"
          },
          {
            "name": "type",
            "type": "string",
            "required": false,
            "description": "HLSL type (default float)"
          }
        ]
      },
      {
        "name": "outputs",
        "type": "array",
        "required": false,
        "description": "Outputs declared on a pass-through CustomHLSL node",
        "items": "object",
        "fields": [
          {
            "name": "name",
            "type": "string",
            "required": true,
            "description": "Pin name"
          },
          {
            "name": "type",
            "type": "string",
            "required": false,
            "description": "HLSL type (default float)"
          }
        ]
      }
    ],
    "contractExempt": "Creates and saves a module script under the contract values; nothing it reads fails first"
  },
  "get_emitter_info": {
    "category": "niagara",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "NiagaraEmitter asset path"
      }
    ]
  },
  "get_niagara_compiled_hlsl": {
    "category": "niagara",
    "params": [
      {
        "name": "systemPath",
        "type": "string",
        "required": true,
        "description": "NiagaraSystem asset path"
      },
      {
        "name": "emitterName",
        "type": "string",
        "required": false,
        "description": "Emitter handle name. Omit to address the emitter by emitterIndex"
      },
      {
        "name": "emitterIndex",
        "type": "number",
        "required": false,
        "description": "Emitter handle index, used when emitterName is omitted"
      }
    ]
  },
  "get_niagara_custom_hlsl": {
    "category": "niagara",
    "params": [
      {
        "name": "scriptPath",
        "type": "string",
        "required": false,
        "description": "NiagaraScript asset holding the CustomHLSL node"
      },
      {
        "name": "systemPath",
        "type": "string",
        "required": false,
        "description": "NiagaraSystem whose emitter stack holds the CustomHLSL node"
      },
      {
        "name": "stackContext",
        "type": "string",
        "required": false,
        "description": "Stack of the addressed system graph: ParticleSpawn|ParticleUpdate|EmitterSpawn|EmitterUpdate (default ParticleUpdate)"
      },
      {
        "name": "emitterName",
        "type": "string",
        "required": false,
        "description": "Emitter handle name. Omit to address the emitter by emitterIndex"
      },
      {
        "name": "emitterIndex",
        "type": "number",
        "required": false,
        "description": "Emitter handle index, used when emitterName is omitted"
      },
      {
        "name": "nodeIndex",
        "type": "integer",
        "required": false,
        "description": "Only this CustomHLSL node (default: every one in the graph)"
      }
    ],
    "choices": [
      {
        "mode": "exactlyOne",
        "branches": [
          [
            "scriptPath"
          ],
          [
            "systemPath"
          ]
        ]
      }
    ]
  },
  "get_niagara_info": {
    "category": "niagara",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "NiagaraSystem asset path",
        "aliases": [
          "path"
        ]
      }
    ]
  },
  "inspect_data_interface": {
    "category": "niagara",
    "params": [
      {
        "name": "systemPath",
        "type": "string",
        "required": true,
        "description": "NiagaraSystem asset path"
      }
    ]
  },
  "list_emitter_renderers": {
    "category": "niagara",
    "params": [
      {
        "name": "systemPath",
        "type": "string",
        "required": true,
        "description": "NiagaraSystem asset path"
      },
      {
        "name": "emitterName",
        "type": "string",
        "required": false,
        "description": "Emitter handle name. Omit to address the emitter by emitterIndex"
      },
      {
        "name": "emitterIndex",
        "type": "number",
        "required": false,
        "description": "Emitter handle index, used when emitterName is omitted"
      }
    ]
  },
  "list_emitters_in_system": {
    "category": "niagara",
    "params": [
      {
        "name": "systemPath",
        "type": "string",
        "required": true,
        "description": "NiagaraSystem asset path"
      }
    ]
  },
  "list_niagara_dynamic_inputs": {
    "category": "niagara",
    "params": [
      {
        "name": "systemPath",
        "type": "string",
        "required": true,
        "description": "NiagaraSystem asset path"
      },
      {
        "name": "emitterName",
        "type": "string",
        "required": false,
        "description": "Emitter handle name. Omit to address the emitter by emitterIndex"
      },
      {
        "name": "emitterIndex",
        "type": "number",
        "required": false,
        "description": "Emitter handle index, used when emitterName is omitted"
      },
      {
        "name": "stackContext",
        "type": "string",
        "required": false,
        "description": "ParticleSpawn|ParticleUpdate|EmitterSpawn|EmitterUpdate|all (default all)"
      },
      {
        "name": "moduleName",
        "type": "string",
        "required": false,
        "description": "Only this module (default: every module)"
      }
    ]
  },
  "list_niagara_module_inputs": {
    "category": "niagara",
    "params": [
      {
        "name": "systemPath",
        "type": "string",
        "required": true,
        "description": "NiagaraSystem asset path"
      },
      {
        "name": "emitterName",
        "type": "string",
        "required": false,
        "description": "Emitter handle name. Omit to address the emitter by emitterIndex"
      },
      {
        "name": "emitterIndex",
        "type": "number",
        "required": false,
        "description": "Emitter handle index, used when emitterName is omitted"
      },
      {
        "name": "stackContext",
        "type": "string",
        "required": false,
        "description": "ParticleSpawn|ParticleUpdate|EmitterSpawn|EmitterUpdate|all (default all)"
      },
      {
        "name": "moduleName",
        "type": "string",
        "required": false,
        "description": "Only this module (default: every module)"
      }
    ]
  },
  "list_niagara_modules": {
    "category": "niagara",
    "params": [
      {
        "name": "pathFilter",
        "type": "string",
        "required": false,
        "description": "Case-sensitive substring of the module script object path, applied to the whole set before paging"
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
        "description": "Rows to return on this page (default 200, max 2000)"
      }
    ]
  },
  "list_niagara_static_switches": {
    "category": "niagara",
    "params": [
      {
        "name": "systemPath",
        "type": "string",
        "required": true,
        "description": "NiagaraSystem asset path"
      },
      {
        "name": "moduleName",
        "type": "string",
        "required": false,
        "description": "Only this module (default: every module)"
      },
      {
        "name": "emitterName",
        "type": "string",
        "required": false,
        "description": "Emitter handle name. Omit to address the emitter by emitterIndex"
      },
      {
        "name": "emitterIndex",
        "type": "number",
        "required": false,
        "description": "Emitter handle index, used when emitterName is omitted"
      },
      {
        "name": "stackContext",
        "type": "string",
        "required": false,
        "description": "ParticleSpawn|ParticleUpdate|EmitterSpawn|EmitterUpdate|all (default all)"
      }
    ]
  },
  "list_niagara_system_parameters": {
    "category": "niagara",
    "params": [
      {
        "name": "systemPath",
        "type": "string",
        "required": true,
        "description": "NiagaraSystem asset path"
      }
    ]
  },
  "list_niagara_systems": {
    "category": "niagara",
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
        "description": "Rows to return on this page (default 200, max 2000)"
      }
    ]
  },
  "reactivate_niagara": {
    "category": "niagara",
    "params": [
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Editor label of the actor holding the NiagaraComponent; a label naming several actors is refused"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Full actor object path; the unambiguous selector"
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
  "remove_emitter_from_system": {
    "category": "niagara",
    "params": [
      {
        "name": "systemPath",
        "type": "string",
        "required": true,
        "description": "NiagaraSystem asset path"
      },
      {
        "name": "emitterName",
        "type": "string",
        "required": false,
        "description": "Emitter handle name. Omit to address the emitter by emitterIndex"
      },
      {
        "name": "emitterIndex",
        "type": "number",
        "required": false,
        "description": "Emitter handle index, used when emitterName is omitted"
      }
    ]
  },
  "remove_emitter_renderer": {
    "category": "niagara",
    "params": [
      {
        "name": "systemPath",
        "type": "string",
        "required": true,
        "description": "NiagaraSystem asset path"
      },
      {
        "name": "rendererIndex",
        "type": "integer",
        "required": true,
        "description": "Index of the renderer on the emitter"
      },
      {
        "name": "emitterName",
        "type": "string",
        "required": false,
        "description": "Emitter handle name. Omit to address the emitter by emitterIndex"
      },
      {
        "name": "emitterIndex",
        "type": "number",
        "required": false,
        "description": "Emitter handle index, used when emitterName is omitted"
      }
    ]
  },
  "remove_niagara_dynamic_input": {
    "category": "niagara",
    "params": [
      {
        "name": "systemPath",
        "type": "string",
        "required": true,
        "description": "NiagaraSystem asset path"
      },
      {
        "name": "stackContext",
        "type": "string",
        "required": true,
        "description": "ParticleSpawn|ParticleUpdate|EmitterSpawn|EmitterUpdate"
      },
      {
        "name": "moduleName",
        "type": "string",
        "required": true,
        "description": "Name of the module function call node"
      },
      {
        "name": "inputName",
        "type": "string",
        "required": true,
        "description": "Module input pin name"
      },
      {
        "name": "emitterName",
        "type": "string",
        "required": false,
        "description": "Emitter handle name. Omit to address the emitter by emitterIndex"
      },
      {
        "name": "emitterIndex",
        "type": "number",
        "required": false,
        "description": "Emitter handle index, used when emitterName is omitted"
      }
    ]
  },
  "remove_niagara_event_handler": {
    "category": "niagara",
    "params": [
      {
        "name": "systemPath",
        "type": "string",
        "required": true,
        "description": "NiagaraSystem asset path"
      },
      {
        "name": "eventName",
        "type": "string",
        "required": true,
        "description": "Event handler name"
      },
      {
        "name": "emitterName",
        "type": "string",
        "required": false,
        "description": "Emitter handle name. Omit to address the emitter by emitterIndex"
      },
      {
        "name": "emitterIndex",
        "type": "number",
        "required": false,
        "description": "Emitter handle index, used when emitterName is omitted"
      }
    ]
  },
  "remove_niagara_module": {
    "category": "niagara",
    "params": [
      {
        "name": "systemPath",
        "type": "string",
        "required": true,
        "description": "NiagaraSystem asset path"
      },
      {
        "name": "stackContext",
        "type": "string",
        "required": true,
        "description": "ParticleSpawn|ParticleUpdate|EmitterSpawn|EmitterUpdate"
      },
      {
        "name": "moduleName",
        "type": "string",
        "required": true,
        "description": "Name of the module function call node"
      },
      {
        "name": "emitterName",
        "type": "string",
        "required": false,
        "description": "Emitter handle name. Omit to address the emitter by emitterIndex"
      },
      {
        "name": "emitterIndex",
        "type": "number",
        "required": false,
        "description": "Emitter handle index, used when emitterName is omitted"
      }
    ]
  },
  "remove_niagara_simulation_stage": {
    "category": "niagara",
    "params": [
      {
        "name": "systemPath",
        "type": "string",
        "required": true,
        "description": "NiagaraSystem asset path"
      },
      {
        "name": "stageName",
        "type": "string",
        "required": true,
        "description": "Simulation stage name"
      },
      {
        "name": "emitterName",
        "type": "string",
        "required": false,
        "description": "Emitter handle name. Omit to address the emitter by emitterIndex"
      },
      {
        "name": "emitterIndex",
        "type": "number",
        "required": false,
        "description": "Emitter handle index, used when emitterName is omitted"
      }
    ]
  },
  "set_emitter_property": {
    "category": "niagara",
    "params": [
      {
        "name": "systemPath",
        "type": "string",
        "required": true,
        "description": "NiagaraSystem asset path",
        "aliases": [
          "assetPath"
        ]
      },
      {
        "name": "emitterName",
        "type": "string",
        "required": false,
        "description": "Emitter handle name (default: the first emitter)"
      },
      {
        "name": "propertyName",
        "type": "string",
        "required": true,
        "description": "Emitter property to set"
      },
      {
        "name": "value",
        "type": "any",
        "required": true,
        "description": "New value, as a string"
      }
    ]
  },
  "set_niagara_custom_hlsl": {
    "category": "niagara",
    "params": [
      {
        "name": "hlsl",
        "type": "string",
        "required": true,
        "description": "The new HLSL body"
      },
      {
        "name": "scriptPath",
        "type": "string",
        "required": false,
        "description": "NiagaraScript asset holding the CustomHLSL node"
      },
      {
        "name": "systemPath",
        "type": "string",
        "required": false,
        "description": "NiagaraSystem whose emitter stack holds the CustomHLSL node"
      },
      {
        "name": "stackContext",
        "type": "string",
        "required": false,
        "description": "Stack of the addressed system graph: ParticleSpawn|ParticleUpdate|EmitterSpawn|EmitterUpdate (default ParticleUpdate)"
      },
      {
        "name": "emitterName",
        "type": "string",
        "required": false,
        "description": "Emitter handle name. Omit to address the emitter by emitterIndex"
      },
      {
        "name": "emitterIndex",
        "type": "number",
        "required": false,
        "description": "Emitter handle index, used when emitterName is omitted"
      },
      {
        "name": "nodeIndex",
        "type": "integer",
        "required": false,
        "description": "Which CustomHLSL node when the graph has several (default 0)"
      }
    ],
    "choices": [
      {
        "mode": "exactlyOne",
        "branches": [
          [
            "scriptPath"
          ],
          [
            "systemPath"
          ]
        ]
      }
    ]
  },
  "set_niagara_dynamic_input": {
    "category": "niagara",
    "params": [
      {
        "name": "systemPath",
        "type": "string",
        "required": true,
        "description": "NiagaraSystem asset path"
      },
      {
        "name": "stackContext",
        "type": "string",
        "required": true,
        "description": "ParticleSpawn|ParticleUpdate|EmitterSpawn|EmitterUpdate"
      },
      {
        "name": "moduleName",
        "type": "string",
        "required": true,
        "description": "Name of the module function call node"
      },
      {
        "name": "inputName",
        "type": "string",
        "required": true,
        "description": "Module input pin name"
      },
      {
        "name": "dynamicInputScript",
        "type": "string",
        "required": true,
        "description": "The dynamic-input NiagaraScript to wire in"
      },
      {
        "name": "emitterName",
        "type": "string",
        "required": false,
        "description": "Emitter handle name. Omit to address the emitter by emitterIndex"
      },
      {
        "name": "emitterIndex",
        "type": "number",
        "required": false,
        "description": "Emitter handle index, used when emitterName is omitted"
      }
    ]
  },
  "set_niagara_module_enabled": {
    "category": "niagara",
    "params": [
      {
        "name": "systemPath",
        "type": "string",
        "required": true,
        "description": "NiagaraSystem asset path"
      },
      {
        "name": "stackContext",
        "type": "string",
        "required": true,
        "description": "ParticleSpawn|ParticleUpdate|EmitterSpawn|EmitterUpdate"
      },
      {
        "name": "moduleName",
        "type": "string",
        "required": true,
        "description": "Name of the module function call node"
      },
      {
        "name": "enabled",
        "type": "boolean",
        "required": true,
        "description": "The state to set"
      },
      {
        "name": "emitterName",
        "type": "string",
        "required": false,
        "description": "Emitter handle name. Omit to address the emitter by emitterIndex"
      },
      {
        "name": "emitterIndex",
        "type": "number",
        "required": false,
        "description": "Emitter handle index, used when emitterName is omitted"
      }
    ]
  },
  "set_niagara_module_input": {
    "category": "niagara",
    "params": [
      {
        "name": "systemPath",
        "type": "string",
        "required": true,
        "description": "NiagaraSystem asset path"
      },
      {
        "name": "moduleName",
        "type": "string",
        "required": true,
        "description": "Name of the module function call node"
      },
      {
        "name": "inputName",
        "type": "string",
        "required": true,
        "description": "Module input pin name"
      },
      {
        "name": "value",
        "type": "any",
        "required": true,
        "description": "A scalar, [x,y,z], {x,y,z[,w]} or {r,g,b[,a]} (alpha defaults to 1); a gapped object, an empty array or a non-finite number is refused"
      },
      {
        "name": "emitterName",
        "type": "string",
        "required": false,
        "description": "Emitter handle name. Omit to address the emitter by emitterIndex"
      },
      {
        "name": "emitterIndex",
        "type": "number",
        "required": false,
        "description": "Emitter handle index, used when emitterName is omitted"
      },
      {
        "name": "stackContext",
        "type": "string",
        "required": false,
        "description": "ParticleSpawn|ParticleUpdate|EmitterSpawn|EmitterUpdate|all (default all)"
      }
    ]
  },
  "set_niagara_parameter": {
    "category": "niagara",
    "params": [
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Editor label of the actor holding the NiagaraComponent; a label naming several actors is refused"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Full actor object path; the unambiguous selector"
      },
      {
        "name": "parameterName",
        "type": "string",
        "required": true,
        "description": "User parameter name"
      },
      {
        "name": "parameterType",
        "type": "string",
        "required": false,
        "description": "float (default) | vector | bool | int"
      },
      {
        "name": "value",
        "type": "any",
        "required": false,
        "description": "The value of a float, int or bool parameter"
      },
      {
        "name": "valueX",
        "type": "number",
        "required": false,
        "description": "X component of a vector parameter"
      },
      {
        "name": "valueY",
        "type": "number",
        "required": false,
        "description": "Y component of a vector parameter"
      },
      {
        "name": "valueZ",
        "type": "number",
        "required": false,
        "description": "Z component of a vector parameter"
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
      },
      {
        "mode": "exactlyOne",
        "branches": [
          [
            "value"
          ],
          [
            "valueX",
            "valueY",
            "valueZ"
          ]
        ]
      }
    ]
  },
  "set_niagara_static_switch": {
    "category": "niagara",
    "params": [
      {
        "name": "systemPath",
        "type": "string",
        "required": true,
        "description": "NiagaraSystem asset path"
      },
      {
        "name": "moduleName",
        "type": "string",
        "required": true,
        "description": "Name of the module function call node"
      },
      {
        "name": "switchName",
        "type": "string",
        "required": true,
        "description": "Static switch input name"
      },
      {
        "name": "value",
        "type": "any",
        "required": true,
        "description": "Switch value, as a string"
      },
      {
        "name": "emitterName",
        "type": "string",
        "required": false,
        "description": "Emitter handle name. Omit to address the emitter by emitterIndex"
      },
      {
        "name": "emitterIndex",
        "type": "number",
        "required": false,
        "description": "Emitter handle index, used when emitterName is omitted"
      },
      {
        "name": "stackContext",
        "type": "string",
        "required": false,
        "description": "ParticleSpawn|ParticleUpdate|EmitterSpawn|EmitterUpdate|all (default all)"
      }
    ]
  },
  "set_renderer_property": {
    "category": "niagara",
    "params": [
      {
        "name": "systemPath",
        "type": "string",
        "required": true,
        "description": "NiagaraSystem asset path"
      },
      {
        "name": "rendererIndex",
        "type": "integer",
        "required": false,
        "description": "Index of the renderer on the emitter (default 0)"
      },
      {
        "name": "propertyName",
        "type": "string",
        "required": true,
        "description": "Renderer property to set"
      },
      {
        "name": "value",
        "type": "any",
        "required": true,
        "description": "New value: a bool, number or string, an asset path for an object property, or JSON for a struct, enum, name or array"
      },
      {
        "name": "emitterName",
        "type": "string",
        "required": false,
        "description": "Emitter handle name. Omit to address the emitter by emitterIndex"
      },
      {
        "name": "emitterIndex",
        "type": "number",
        "required": false,
        "description": "Emitter handle index, used when emitterName is omitted"
      }
    ]
  },
  "spawn_niagara_actor": {
    "category": "niagara",
    "params": [
      {
        "name": "systemPath",
        "type": "string",
        "required": true,
        "description": "NiagaraSystem asset path"
      },
      {
        "name": "location",
        "type": "vec3",
        "required": false,
        "description": "World location {x,y,z} (default origin)"
      },
      {
        "name": "rotation",
        "type": "rotator",
        "required": false,
        "description": "World rotation {pitch,yaw,roll}"
      },
      {
        "name": "label",
        "type": "string",
        "required": false,
        "description": "Editor label. Also the idempotency key: an existing actor with this label is reported rather than duplicated"
      },
      {
        "name": "activate",
        "type": "boolean",
        "required": false,
        "description": "Activate the system on spawn (default true) (#537)"
      }
    ]
  },
  "spawn_niagara_at_location": {
    "category": "niagara",
    "params": [
      {
        "name": "systemPath",
        "type": "string",
        "required": true,
        "description": "NiagaraSystem asset path"
      },
      {
        "name": "location",
        "type": "vec3",
        "required": false,
        "description": "World location {x,y,z} (default origin)"
      },
      {
        "name": "rotation",
        "type": "rotator",
        "required": false,
        "description": "World rotation {pitch,yaw,roll}"
      },
      {
        "name": "label",
        "type": "string",
        "required": false,
        "description": "Editor label. Also the idempotency key: an existing actor with this label is reported rather than duplicated"
      },
      {
        "name": "scaleX",
        "type": "number",
        "required": false,
        "description": "X scale of the spawned component (default 1)"
      },
      {
        "name": "scaleY",
        "type": "number",
        "required": false,
        "description": "Y scale of the spawned component (default 1)"
      },
      {
        "name": "scaleZ",
        "type": "number",
        "required": false,
        "description": "Z scale of the spawned component (default 1)"
      },
      {
        "name": "autoDestroy",
        "type": "boolean",
        "required": false,
        "description": "Destroy the component once the system finishes (default false)"
      }
    ]
  },
  "validate_niagara_system": {
    "category": "niagara",
    "params": [
      {
        "name": "systemPath",
        "type": "string",
        "required": true,
        "description": "NiagaraSystem asset path"
      }
    ]
  }
};

/** Every key the spec'd niagara handlers declare, aliases included. */
export const schema = categorySchema(handlerSpecs);

/** Declare an action for a spec'd bridge method: effect, summary, method. */
export const specBp = makeSpecBp(handlerSpecs);
