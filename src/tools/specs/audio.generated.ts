// GENERATED FILE - do not edit.
//
// Written by scripts/generate-handler-specs.mjs from tests/golden/handler-specs.json,
// which is recorded from the parameter specs the C++ handlers register with
// (npm run specs:record). To change a parameter, change its RegisterHandler
// spec, re-record, and regenerate (#1057).
import { z } from "zod";
import { makeSpecBp, type HandlerSpecs } from "../../surface/handler-spec.js";

/** The recorded contract of every spec'd audio handler. */
export const handlerSpecs: HandlerSpecs = {
  "add_sound_submix_send": {
    "category": "audio",
    "params": [
      {
        "name": "soundPath",
        "type": "string",
        "required": true,
        "description": "Sound asset path",
        "aliases": [
          "assetPath"
        ]
      },
      {
        "name": "submixPath",
        "type": "string",
        "required": true,
        "description": "Submix to send to"
      },
      {
        "name": "sendLevel",
        "type": "number",
        "required": false,
        "description": "Send level (default 1.0)"
      }
    ]
  },
  "add_submix_effect": {
    "category": "audio",
    "params": [
      {
        "name": "submixPath",
        "type": "string",
        "required": true,
        "description": "SoundSubmix whose effect chain to append to"
      },
      {
        "name": "effectType",
        "type": "string",
        "required": true,
        "description": "reverb | eq | dynamics | filter | delay"
      },
      {
        "name": "name",
        "type": "string",
        "required": false,
        "description": "Preset asset name (default <submix>_<effectType>)"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Preset folder (default /Game/Audio/SubmixEffects)"
      },
      {
        "name": "settings",
        "type": "object",
        "required": false,
        "description": "Effect Settings struct as JSON"
      }
    ]
  },
  "create_attenuation": {
    "category": "audio",
    "params": [
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "SoundAttenuation asset name"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Destination folder (default /Game/Audio/Attenuation)"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "skip (default) returns the existing asset, error refuses; it never overwrites"
      },
      {
        "name": "settings",
        "type": "object",
        "required": false,
        "description": "FSoundAttenuationSettings as JSON, applied before the shortcuts"
      },
      {
        "name": "falloffDistance",
        "type": "number",
        "required": false,
        "description": "Falloff distance; also turns volume attenuation on"
      },
      {
        "name": "spatialize",
        "type": "boolean",
        "required": false,
        "description": "bSpatialize"
      },
      {
        "name": "enableOcclusion",
        "type": "boolean",
        "required": false,
        "description": "bEnableOcclusion"
      }
    ],
    "contractExempt": "Creates and saves a SoundAttenuation asset named by the contract values"
  },
  "create_concurrency": {
    "category": "audio",
    "params": [
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "SoundConcurrency asset name"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Destination folder (default /Game/Audio/Concurrency)"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "skip (default) returns the existing asset, error refuses; it never overwrites"
      },
      {
        "name": "maxCount",
        "type": "integer",
        "required": false,
        "description": "Most concurrent voices"
      },
      {
        "name": "limitToOwner",
        "type": "boolean",
        "required": false,
        "description": "Count voices per owning actor"
      },
      {
        "name": "volumeScale",
        "type": "number",
        "required": false,
        "description": "Volume scale applied to each new voice"
      },
      {
        "name": "resolutionRule",
        "type": "string",
        "required": false,
        "description": "EMaxConcurrentResolutionRule name, e.g. StopFarthestThenOldest"
      }
    ],
    "contractExempt": "Creates and saves a SoundConcurrency asset named by the contract values"
  },
  "create_metasound_source": {
    "category": "audio",
    "params": [
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "MetaSoundSource asset name"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Destination folder (default /Game/Audio/MetaSounds)"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "skip (default) returns the existing asset, error refuses; it never overwrites"
      },
      {
        "name": "format",
        "type": "string",
        "required": false,
        "description": "Output format: mono | stereo (default mono)"
      },
      {
        "name": "oneShot",
        "type": "boolean",
        "required": false,
        "description": "Declare the one-shot interface (default true)"
      }
    ],
    "contractExempt": "Creates and saves a MetaSoundSource asset named by the contract values"
  },
  "create_sound_class": {
    "category": "audio",
    "params": [
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "SoundClass asset name"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Destination folder (default /Game/Audio/SoundClasses)"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "skip (default) returns the existing asset, error refuses; it never overwrites"
      },
      {
        "name": "properties",
        "type": "object",
        "required": false,
        "description": "FSoundClassProperties as JSON: Volume, Pitch, bIsUISound, ..."
      },
      {
        "name": "parentPath",
        "type": "string",
        "required": false,
        "description": "Parent SoundClass (default none)"
      }
    ],
    "contractExempt": "Creates and saves a SoundClass asset named by the contract values"
  },
  "create_sound_cue": {
    "category": "audio",
    "params": [
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "SoundCue asset name"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Destination folder (default /Game/Audio/SoundCues)"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "skip (default) returns the existing asset, error refuses; it never overwrites"
      }
    ],
    "contractExempt": "Creates and saves a SoundCue asset named by the contract values"
  },
  "create_sound_mix": {
    "category": "audio",
    "params": [
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "SoundMix asset name"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Destination folder (default /Game/Audio/SoundMixes)"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "skip (default) returns the existing asset, error refuses; it never overwrites"
      },
      {
        "name": "fadeInTime",
        "type": "number",
        "required": false,
        "description": "Fade-in time in seconds"
      },
      {
        "name": "fadeOutTime",
        "type": "number",
        "required": false,
        "description": "Fade-out time in seconds"
      },
      {
        "name": "adjusters",
        "type": "array",
        "required": false,
        "description": "Per-SoundClass adjustments; an entry whose class does not load is skipped",
        "items": "object",
        "fields": [
          {
            "name": "soundClassPath",
            "type": "string",
            "required": true,
            "description": "SoundClass asset the adjustment applies to"
          },
          {
            "name": "volumeAdjuster",
            "type": "number",
            "required": false,
            "description": "Volume multiplier (default 1)"
          },
          {
            "name": "pitchAdjuster",
            "type": "number",
            "required": false,
            "description": "Pitch multiplier (default 1)"
          },
          {
            "name": "applyToChildren",
            "type": "boolean",
            "required": false,
            "description": "Apply to child sound classes (default false)"
          }
        ]
      }
    ],
    "contractExempt": "Creates and saves a SoundMix asset named by the contract values"
  },
  "create_submix": {
    "category": "audio",
    "params": [
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "SoundSubmix asset name"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Destination folder (default /Game/Audio/Submixes)"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "skip (default) returns the existing asset, error refuses; it never overwrites"
      },
      {
        "name": "parentPath",
        "type": "string",
        "required": false,
        "description": "Parent submix (default none)"
      },
      {
        "name": "outputVolume",
        "type": "number",
        "required": false,
        "description": "Output volume"
      },
      {
        "name": "wetLevel",
        "type": "number",
        "required": false,
        "description": "Wet level"
      },
      {
        "name": "dryLevel",
        "type": "number",
        "required": false,
        "description": "Dry level"
      }
    ],
    "contractExempt": "Creates and saves a SoundSubmix asset named by the contract values"
  },
  "extract_sound_wave_pcm": {
    "category": "audio",
    "params": [
      {
        "name": "soundPath",
        "type": "string",
        "required": true,
        "description": "SoundWave asset path",
        "aliases": [
          "assetPath"
        ]
      },
      {
        "name": "maxSeconds",
        "type": "number",
        "required": false,
        "description": "Cap the decoded window in seconds (default the full asset)"
      },
      {
        "name": "downmixMono",
        "type": "boolean",
        "required": false,
        "description": "Average the channels to mono (default false)"
      }
    ]
  },
  "import_audio": {
    "category": "audio",
    "params": [
      {
        "name": "filePath",
        "type": "string",
        "required": true,
        "description": "WAV, OGG or FLAC file on disk",
        "aliases": [
          "filename"
        ]
      },
      {
        "name": "name",
        "type": "string",
        "required": false,
        "description": "Asset name (default the file's base name)",
        "aliases": [
          "assetName"
        ]
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Destination folder (default /Game/Audio)",
        "aliases": [
          "destinationPath"
        ]
      },
      {
        "name": "looping",
        "type": "boolean",
        "required": false,
        "description": "Set bLooping on the imported SoundWave (omit to keep the importer's value)"
      },
      {
        "name": "replaceExisting",
        "type": "boolean",
        "required": false,
        "description": "Replace an existing asset of that name (default true)"
      }
    ]
  },
  "list_sound_assets": {
    "category": "audio",
    "params": [
      {
        "name": "directory",
        "type": "string",
        "required": false,
        "description": "Content directory to list (default /Game)"
      },
      {
        "name": "recursive",
        "type": "boolean",
        "required": false,
        "description": "Include subdirectories (default true)"
      },
      {
        "name": "offset",
        "type": "number",
        "required": false,
        "description": "Refused. The row offset was replaced by cursor paging (#730); pass cursor and limit instead"
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
        "description": "Rows per page, 1 to 5000 (default 1000)",
        "aliases": [
          "maxResults"
        ]
      }
    ]
  },
  "metasound_add_graph_input": {
    "category": "audio",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "MetaSoundSource asset path",
        "aliases": [
          "metasoundPath"
        ]
      },
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "Graph input name"
      },
      {
        "name": "dataType",
        "type": "string",
        "required": true,
        "description": "MetaSound data type: Float, Int32, Bool, String, Trigger, Audio, Time, ..."
      },
      {
        "name": "defaultValue",
        "type": "any",
        "required": false,
        "description": "Literal default for the input"
      }
    ]
  },
  "metasound_add_graph_output": {
    "category": "audio",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "MetaSoundSource asset path",
        "aliases": [
          "metasoundPath"
        ]
      },
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "Graph output name"
      },
      {
        "name": "dataType",
        "type": "string",
        "required": true,
        "description": "MetaSound data type: Float, Int32, Bool, String, Trigger, Audio, Time, ..."
      }
    ]
  },
  "metasound_add_node": {
    "category": "audio",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "MetaSoundSource asset path",
        "aliases": [
          "metasoundPath"
        ]
      },
      {
        "name": "nodeClassName",
        "type": "string",
        "required": true,
        "description": "Registered node class name, e.g. Sine"
      },
      {
        "name": "nodeNamespace",
        "type": "string",
        "required": false,
        "description": "Node class namespace (default UE)"
      },
      {
        "name": "nodeVariant",
        "type": "string",
        "required": false,
        "description": "Node class variant, e.g. Audio"
      },
      {
        "name": "majorVersion",
        "type": "integer",
        "required": false,
        "description": "Node class major version (default 1)"
      }
    ]
  },
  "metasound_author": {
    "category": "audio",
    "params": [
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "MetaSoundSource asset name"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Destination folder (default /Game/Audio/MetaSounds)"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "skip (default) returns the existing asset, error refuses; it never overwrites"
      },
      {
        "name": "format",
        "type": "string",
        "required": false,
        "description": "Output format: mono | stereo (default mono)"
      },
      {
        "name": "oneShot",
        "type": "boolean",
        "required": false,
        "description": "Declare the one-shot interface (default true)"
      },
      {
        "name": "inputs",
        "type": "array",
        "required": false,
        "description": "Graph inputs to add",
        "items": "object",
        "fields": [
          {
            "name": "name",
            "type": "string",
            "required": true,
            "description": "Graph input name"
          },
          {
            "name": "dataType",
            "type": "string",
            "required": true,
            "description": "MetaSound data type: Float, Int32, Bool, String, Trigger, Audio, Time, ..."
          },
          {
            "name": "default",
            "type": "any",
            "required": false,
            "description": "Literal default for the input"
          }
        ]
      },
      {
        "name": "outputs",
        "type": "array",
        "required": false,
        "description": "Graph outputs to add",
        "items": "object",
        "fields": [
          {
            "name": "name",
            "type": "string",
            "required": true,
            "description": "Graph output name"
          },
          {
            "name": "dataType",
            "type": "string",
            "required": true,
            "description": "MetaSound data type"
          }
        ]
      },
      {
        "name": "nodes",
        "type": "array",
        "required": false,
        "description": "Nodes to add, each {id, class, namespace?, variant?, majorVersion?, inputs?: {vertex: value}}; id is the local name connections use, namespace defaults to UE and majorVersion to 1",
        "items": "object"
      },
      {
        "name": "connections",
        "type": "array",
        "required": false,
        "description": "Edges, each {from, to}. Endpoints are 'nodeId:vertex', or the heads 'input:<name>', 'output:<name>' and 'audioOut:<channel>'",
        "items": "object"
      }
    ],
    "contractExempt": "Creates and saves a MetaSoundSource asset named by the contract values"
  },
  "metasound_build": {
    "category": "audio",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "MetaSoundSource asset path",
        "aliases": [
          "metasoundPath"
        ]
      }
    ]
  },
  "metasound_connect": {
    "category": "audio",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "MetaSoundSource asset path",
        "aliases": [
          "metasoundPath"
        ]
      },
      {
        "name": "fromNodeId",
        "type": "string",
        "required": true,
        "description": "Source node id"
      },
      {
        "name": "fromOutput",
        "type": "string",
        "required": true,
        "description": "Output vertex name on the source node"
      },
      {
        "name": "toNodeId",
        "type": "string",
        "required": true,
        "description": "Destination node id"
      },
      {
        "name": "toInput",
        "type": "string",
        "required": true,
        "description": "Input vertex name on the destination node"
      }
    ]
  },
  "metasound_connect_audio_out": {
    "category": "audio",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "MetaSoundSource asset path",
        "aliases": [
          "metasoundPath"
        ]
      },
      {
        "name": "fromNodeId",
        "type": "string",
        "required": true,
        "description": "Source node id"
      },
      {
        "name": "fromOutput",
        "type": "string",
        "required": true,
        "description": "Output vertex name on the source node, of Audio type"
      },
      {
        "name": "channel",
        "type": "integer",
        "required": false,
        "description": "Audio output channel: 0 left or mono, 1 right (default 0)"
      }
    ]
  },
  "metasound_connect_graph_input": {
    "category": "audio",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "MetaSoundSource asset path",
        "aliases": [
          "metasoundPath"
        ]
      },
      {
        "name": "graphInput",
        "type": "string",
        "required": true,
        "description": "Graph input name"
      },
      {
        "name": "toNodeId",
        "type": "string",
        "required": true,
        "description": "Destination node id"
      },
      {
        "name": "toInput",
        "type": "string",
        "required": true,
        "description": "Input vertex name on the destination node"
      }
    ]
  },
  "metasound_connect_graph_output": {
    "category": "audio",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "MetaSoundSource asset path",
        "aliases": [
          "metasoundPath"
        ]
      },
      {
        "name": "fromNodeId",
        "type": "string",
        "required": true,
        "description": "Source node id"
      },
      {
        "name": "fromOutput",
        "type": "string",
        "required": true,
        "description": "Output vertex name on the source node"
      },
      {
        "name": "graphOutput",
        "type": "string",
        "required": true,
        "description": "Graph output name"
      }
    ]
  },
  "metasound_disconnect": {
    "category": "audio",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "MetaSound asset path",
        "aliases": [
          "metasoundPath"
        ]
      },
      {
        "name": "fromNodeId",
        "type": "string",
        "required": false,
        "description": "Source node id, with fromOutput"
      },
      {
        "name": "fromOutput",
        "type": "string",
        "required": false,
        "description": "Output vertex name on fromNodeId"
      },
      {
        "name": "toNodeId",
        "type": "string",
        "required": false,
        "description": "Destination node id, with toInput"
      },
      {
        "name": "toInput",
        "type": "string",
        "required": false,
        "description": "Input vertex name on toNodeId"
      },
      {
        "name": "graphOutput",
        "type": "string",
        "required": false,
        "description": "Graph output to clear, audio outputs included (Out Mono, Out Left, Out Right)"
      }
    ]
  },
  "metasound_get_graph": {
    "category": "audio",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "MetaSoundSource asset path",
        "aliases": [
          "metasoundPath"
        ]
      }
    ]
  },
  "metasound_inspect_node": {
    "category": "audio",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "MetaSound asset path",
        "aliases": [
          "metasoundPath"
        ]
      },
      {
        "name": "nodeId",
        "type": "string",
        "required": true,
        "description": "Node id to inspect"
      },
      {
        "name": "pageId",
        "type": "string",
        "required": false,
        "description": "Graph page to read, for assets that declare more than one (default the default page)"
      }
    ]
  },
  "metasound_list_connections": {
    "category": "audio",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "MetaSound asset path",
        "aliases": [
          "metasoundPath"
        ]
      },
      {
        "name": "pageId",
        "type": "string",
        "required": false,
        "description": "Graph page to read, for assets that declare more than one (default the default page)"
      },
      {
        "name": "nodeId",
        "type": "string",
        "required": false,
        "description": "Narrow to edges touching this node"
      },
      {
        "name": "direction",
        "type": "string",
        "required": false,
        "description": "With nodeId: in | out | both (default both)"
      },
      {
        "name": "dataType",
        "type": "string",
        "required": false,
        "description": "Only edges carrying this data type"
      }
    ]
  },
  "metasound_list_node_classes": {
    "category": "audio",
    "params": [
      {
        "name": "filter",
        "type": "string",
        "required": false,
        "description": "Case-insensitive substring over the node class name"
      }
    ]
  },
  "metasound_list_node_pins": {
    "category": "audio",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "MetaSound asset path",
        "aliases": [
          "metasoundPath"
        ]
      },
      {
        "name": "nodeId",
        "type": "string",
        "required": true,
        "description": "Node id whose vertices to list"
      },
      {
        "name": "pageId",
        "type": "string",
        "required": false,
        "description": "Graph page to read, for assets that declare more than one (default the default page)"
      },
      {
        "name": "direction",
        "type": "string",
        "required": false,
        "description": "inputs | outputs | both (default both)"
      },
      {
        "name": "dataType",
        "type": "string",
        "required": false,
        "description": "Only vertices of this data type"
      }
    ]
  },
  "metasound_list_variables": {
    "category": "audio",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "MetaSound asset path",
        "aliases": [
          "metasoundPath"
        ]
      },
      {
        "name": "pageId",
        "type": "string",
        "required": false,
        "description": "Graph page to read, for assets that declare more than one (default the default page)"
      },
      {
        "name": "filter",
        "type": "string",
        "required": false,
        "description": "Case-insensitive substring over the variable name"
      }
    ]
  },
  "metasound_read_document": {
    "category": "audio",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "MetaSound asset path",
        "aliases": [
          "metasoundPath"
        ]
      },
      {
        "name": "pageId",
        "type": "string",
        "required": false,
        "description": "Graph page to read, for assets that declare more than one (default the default page)"
      },
      {
        "name": "includeNodes",
        "type": "boolean",
        "required": false,
        "description": "Include the node list (default true)"
      },
      {
        "name": "includeConnections",
        "type": "boolean",
        "required": false,
        "description": "Include the edge list (default true)"
      }
    ]
  },
  "metasound_remove_member": {
    "category": "audio",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "MetaSound asset path",
        "aliases": [
          "metasoundPath"
        ]
      },
      {
        "name": "memberKind",
        "type": "string",
        "required": true,
        "description": "input | output | variable"
      },
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "Member name to remove"
      }
    ]
  },
  "metasound_remove_node": {
    "category": "audio",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "MetaSound asset path",
        "aliases": [
          "metasoundPath"
        ]
      },
      {
        "name": "nodeId",
        "type": "string",
        "required": true,
        "description": "Node id to remove"
      },
      {
        "name": "removeUnusedDependencies",
        "type": "boolean",
        "required": false,
        "description": "Also drop node classes the graph no longer references (default true)"
      }
    ]
  },
  "metasound_rename_member": {
    "category": "audio",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "MetaSound asset path",
        "aliases": [
          "metasoundPath"
        ]
      },
      {
        "name": "memberKind",
        "type": "string",
        "required": true,
        "description": "input | output"
      },
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "Current graph input or output name"
      },
      {
        "name": "newName",
        "type": "string",
        "required": true,
        "description": "Name to rename it to"
      }
    ]
  },
  "metasound_search_nodes": {
    "category": "audio",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "MetaSound asset path",
        "aliases": [
          "metasoundPath"
        ]
      },
      {
        "name": "pageId",
        "type": "string",
        "required": false,
        "description": "Graph page to read, for assets that declare more than one (default the default page)"
      },
      {
        "name": "query",
        "type": "string",
        "required": false,
        "description": "Substring over node name, class name, namespace or variant"
      },
      {
        "name": "dataType",
        "type": "string",
        "required": false,
        "description": "Only nodes with a vertex of this data type"
      },
      {
        "name": "classType",
        "type": "string",
        "required": false,
        "description": "External | Input | Output | Variable | ..."
      },
      {
        "name": "limit",
        "type": "integer",
        "required": false,
        "description": "Most matches to return, 1 to 1000 (default 100)"
      }
    ]
  },
  "metasound_set_input_default": {
    "category": "audio",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "MetaSoundSource asset path",
        "aliases": [
          "metasoundPath"
        ]
      },
      {
        "name": "value",
        "type": "any",
        "required": true,
        "description": "Default value to set"
      },
      {
        "name": "dataType",
        "type": "string",
        "required": false,
        "description": "Literal type hint: Float | Int32 | Bool | String"
      },
      {
        "name": "nodeId",
        "type": "string",
        "required": false,
        "description": "Node whose input to set, with inputName. Pass either nodeId and inputName, or graphInput"
      },
      {
        "name": "inputName",
        "type": "string",
        "required": false,
        "description": "Input vertex name on nodeId"
      },
      {
        "name": "graphInput",
        "type": "string",
        "required": false,
        "description": "Graph input whose default to set, instead of nodeId and inputName"
      }
    ]
  },
  "metasound_validate": {
    "category": "audio",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "MetaSound asset path",
        "aliases": [
          "metasoundPath"
        ]
      },
      {
        "name": "pageId",
        "type": "string",
        "required": false,
        "description": "Graph page to read, for assets that declare more than one (default the default page)"
      }
    ]
  },
  "play_sound_at_location": {
    "category": "audio",
    "params": [
      {
        "name": "soundPath",
        "type": "string",
        "required": true,
        "description": "Sound asset to play (SoundWave, SoundCue or MetaSoundSource)",
        "aliases": [
          "assetPath",
          "path"
        ]
      },
      {
        "name": "location",
        "type": "vec3",
        "required": false,
        "description": "World location to play at (default origin)"
      },
      {
        "name": "volumeMultiplier",
        "type": "number",
        "required": false,
        "description": "Volume multiplier (default 1)",
        "aliases": [
          "volume"
        ]
      },
      {
        "name": "pitchMultiplier",
        "type": "number",
        "required": false,
        "description": "Pitch multiplier (default 1)",
        "aliases": [
          "pitch"
        ]
      }
    ]
  },
  "read_sound_routing": {
    "category": "audio",
    "params": [
      {
        "name": "soundPath",
        "type": "string",
        "required": true,
        "description": "Sound asset path",
        "aliases": [
          "assetPath"
        ]
      }
    ]
  },
  "set_audio_property": {
    "category": "audio",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Audio asset path"
      },
      {
        "name": "propertyName",
        "type": "string",
        "required": true,
        "description": "UPROPERTY name, dotted for nested structs"
      },
      {
        "name": "value",
        "type": "any",
        "required": true,
        "description": "Value as JSON: scalars, structs, arrays, object paths, or UE export text"
      }
    ]
  },
  "set_sound_attenuation": {
    "category": "audio",
    "params": [
      {
        "name": "soundPath",
        "type": "string",
        "required": true,
        "description": "Sound asset path",
        "aliases": [
          "assetPath"
        ]
      },
      {
        "name": "attenuationPath",
        "type": "string",
        "required": false,
        "description": "SoundAttenuation to attach (empty clears)"
      }
    ]
  },
  "set_sound_class": {
    "category": "audio",
    "params": [
      {
        "name": "soundPath",
        "type": "string",
        "required": true,
        "description": "Sound asset path",
        "aliases": [
          "assetPath"
        ]
      },
      {
        "name": "soundClassPath",
        "type": "string",
        "required": true,
        "description": "SoundClass to assign"
      }
    ]
  },
  "set_sound_class_parent": {
    "category": "audio",
    "params": [
      {
        "name": "soundClassPath",
        "type": "string",
        "required": true,
        "description": "SoundClass to reparent",
        "aliases": [
          "assetPath"
        ]
      },
      {
        "name": "parentPath",
        "type": "string",
        "required": false,
        "description": "New parent SoundClass (empty detaches to the root)"
      }
    ]
  },
  "set_sound_concurrency": {
    "category": "audio",
    "params": [
      {
        "name": "soundPath",
        "type": "string",
        "required": true,
        "description": "Sound asset path",
        "aliases": [
          "assetPath"
        ]
      },
      {
        "name": "concurrencyPath",
        "type": "string",
        "required": false,
        "description": "SoundConcurrency to attach (empty clears)"
      }
    ]
  },
  "set_sound_submix": {
    "category": "audio",
    "params": [
      {
        "name": "soundPath",
        "type": "string",
        "required": true,
        "description": "Sound asset path",
        "aliases": [
          "assetPath"
        ]
      },
      {
        "name": "submixPath",
        "type": "string",
        "required": false,
        "description": "Base submix to route to (empty detaches)"
      }
    ]
  },
  "set_submix_parent": {
    "category": "audio",
    "params": [
      {
        "name": "submixPath",
        "type": "string",
        "required": true,
        "description": "SoundSubmix to reparent"
      },
      {
        "name": "parentPath",
        "type": "string",
        "required": false,
        "description": "New parent submix (empty detaches to root)"
      }
    ]
  },
  "soundcue_add_node": {
    "category": "audio",
    "params": [
      {
        "name": "cuePath",
        "type": "string",
        "required": true,
        "description": "SoundCue asset path",
        "aliases": [
          "assetPath"
        ]
      },
      {
        "name": "nodeType",
        "type": "string",
        "required": true,
        "description": "wave_player | mixer | random | modulator | attenuation | looping | concatenator | delay | switch"
      },
      {
        "name": "soundWavePath",
        "type": "string",
        "required": false,
        "description": "SoundWave for a wave_player node"
      },
      {
        "name": "properties",
        "type": "object",
        "required": false,
        "description": "Node-specific fields to set, as {property: value}"
      }
    ]
  },
  "soundcue_author": {
    "category": "audio",
    "params": [
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "SoundCue asset name"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Destination folder (default /Game/Audio/SoundCues)"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "skip (default) returns the existing asset, error refuses; it never overwrites"
      },
      {
        "name": "nodes",
        "type": "array",
        "required": false,
        "description": "Nodes to add, each {id, type, soundWavePath?, properties?: {field: value}}; id is the local name connections use, type is a soundcue_add_node nodeType",
        "items": "object"
      },
      {
        "name": "connections",
        "type": "array",
        "required": false,
        "description": "Links, each {child, parent?, index?}; an omitted or 'root' parent makes the child the cue root, and index defaults to append",
        "items": "object"
      },
      {
        "name": "root",
        "type": "string",
        "required": false,
        "description": "Local id of the node to make the cue root, overriding the connections"
      }
    ],
    "contractExempt": "Creates and saves a SoundCue asset named by the contract values"
  },
  "soundcue_connect": {
    "category": "audio",
    "params": [
      {
        "name": "cuePath",
        "type": "string",
        "required": true,
        "description": "SoundCue asset path",
        "aliases": [
          "assetPath"
        ]
      },
      {
        "name": "childNodeId",
        "type": "string",
        "required": true,
        "description": "Node to attach"
      },
      {
        "name": "parentNodeId",
        "type": "string",
        "required": false,
        "description": "Parent node (omit to make the child the cue root)"
      },
      {
        "name": "childIndex",
        "type": "integer",
        "required": false,
        "description": "Slot under the parent (default append)"
      }
    ]
  },
  "soundcue_disconnect": {
    "category": "audio",
    "params": [
      {
        "name": "cuePath",
        "type": "string",
        "required": true,
        "description": "SoundCue asset path",
        "aliases": [
          "assetPath"
        ]
      },
      {
        "name": "childNodeId",
        "type": "string",
        "required": false,
        "description": "Child to detach"
      },
      {
        "name": "parentNodeId",
        "type": "string",
        "required": false,
        "description": "Detach from this parent only (default every parent)"
      },
      {
        "name": "clearRoot",
        "type": "boolean",
        "required": false,
        "description": "Unset the cue root instead of detaching a parent link"
      }
    ]
  },
  "soundcue_get_graph": {
    "category": "audio",
    "params": [
      {
        "name": "cuePath",
        "type": "string",
        "required": true,
        "description": "SoundCue asset path",
        "aliases": [
          "assetPath"
        ]
      }
    ]
  },
  "soundcue_remove_node": {
    "category": "audio",
    "params": [
      {
        "name": "cuePath",
        "type": "string",
        "required": true,
        "description": "SoundCue asset path",
        "aliases": [
          "assetPath"
        ]
      },
      {
        "name": "nodeId",
        "type": "string",
        "required": true,
        "description": "Node to remove"
      }
    ]
  },
  "spawn_ambient_sound": {
    "category": "audio",
    "params": [
      {
        "name": "soundPath",
        "type": "string",
        "required": true,
        "description": "Sound asset the AmbientSound plays",
        "aliases": [
          "assetPath",
          "path"
        ]
      },
      {
        "name": "location",
        "type": "vec3",
        "required": false,
        "description": "World location of the actor (default origin)"
      },
      {
        "name": "label",
        "type": "string",
        "required": false,
        "description": "Actor label"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "skip | error | rename when an actor with that label exists (default skip)"
      },
      {
        "name": "volumeMultiplier",
        "type": "number",
        "required": false,
        "description": "Volume multiplier on the audio component (default 1)",
        "aliases": [
          "volume"
        ]
      }
    ]
  }
};

/** The Params: clause of each spec'd bridge method. */
export const paramsClauses: Readonly<Record<string, string>> = {
  add_sound_submix_send: "Params: soundPath (or assetPath), submixPath, sendLevel?",
  add_submix_effect: "Params: submixPath, effectType, name?, packagePath?, settings?",
  create_attenuation: "Params: name, packagePath?, onConflict?, settings?, falloffDistance?, spatialize?, enableOcclusion?",
  create_concurrency: "Params: name, packagePath?, onConflict?, maxCount?, limitToOwner?, volumeScale?, resolutionRule?",
  create_metasound_source: "Params: name, packagePath?, onConflict?, format?, oneShot?",
  create_sound_class: "Params: name, packagePath?, onConflict?, properties?, parentPath?",
  create_sound_cue: "Params: name, packagePath?, onConflict?",
  create_sound_mix: "Params: name, packagePath?, onConflict?, fadeInTime?, fadeOutTime?, adjusters?",
  create_submix: "Params: name, packagePath?, onConflict?, parentPath?, outputVolume?, wetLevel?, dryLevel?",
  extract_sound_wave_pcm: "Params: soundPath (or assetPath), maxSeconds?, downmixMono?",
  import_audio: "Params: filePath (or filename), name? (or assetName), packagePath? (or destinationPath), looping?, replaceExisting?",
  list_sound_assets: "Params: directory?, recursive?, offset?, cursor?, limit? (or maxResults)",
  metasound_add_graph_input: "Params: assetPath (or metasoundPath), name, dataType, defaultValue?",
  metasound_add_graph_output: "Params: assetPath (or metasoundPath), name, dataType",
  metasound_add_node: "Params: assetPath (or metasoundPath), nodeClassName, nodeNamespace?, nodeVariant?, majorVersion?",
  metasound_author: "Params: name, packagePath?, onConflict?, format?, oneShot?, inputs?, outputs?, nodes?, connections?",
  metasound_build: "Params: assetPath (or metasoundPath)",
  metasound_connect: "Params: assetPath (or metasoundPath), fromNodeId, fromOutput, toNodeId, toInput",
  metasound_connect_audio_out: "Params: assetPath (or metasoundPath), fromNodeId, fromOutput, channel?",
  metasound_connect_graph_input: "Params: assetPath (or metasoundPath), graphInput, toNodeId, toInput",
  metasound_connect_graph_output: "Params: assetPath (or metasoundPath), fromNodeId, fromOutput, graphOutput",
  metasound_disconnect: "Params: assetPath (or metasoundPath), fromNodeId?, fromOutput?, toNodeId?, toInput?, graphOutput?",
  metasound_get_graph: "Params: assetPath (or metasoundPath)",
  metasound_inspect_node: "Params: assetPath (or metasoundPath), nodeId, pageId?",
  metasound_list_connections: "Params: assetPath (or metasoundPath), pageId?, nodeId?, direction?, dataType?",
  metasound_list_node_classes: "Params: filter?",
  metasound_list_node_pins: "Params: assetPath (or metasoundPath), nodeId, pageId?, direction?, dataType?",
  metasound_list_variables: "Params: assetPath (or metasoundPath), pageId?, filter?",
  metasound_read_document: "Params: assetPath (or metasoundPath), pageId?, includeNodes?, includeConnections?",
  metasound_remove_member: "Params: assetPath (or metasoundPath), memberKind, name",
  metasound_remove_node: "Params: assetPath (or metasoundPath), nodeId, removeUnusedDependencies?",
  metasound_rename_member: "Params: assetPath (or metasoundPath), memberKind, name, newName",
  metasound_search_nodes: "Params: assetPath (or metasoundPath), pageId?, query?, dataType?, classType?, limit?",
  metasound_set_input_default: "Params: assetPath (or metasoundPath), value, dataType?, nodeId?, inputName?, graphInput?",
  metasound_validate: "Params: assetPath (or metasoundPath), pageId?",
  play_sound_at_location: "Params: soundPath (or assetPath, or path), location?, volumeMultiplier? (or volume), pitchMultiplier? (or pitch)",
  read_sound_routing: "Params: soundPath (or assetPath)",
  set_audio_property: "Params: assetPath, propertyName, value",
  set_sound_attenuation: "Params: soundPath (or assetPath), attenuationPath?",
  set_sound_class: "Params: soundPath (or assetPath), soundClassPath",
  set_sound_class_parent: "Params: soundClassPath (or assetPath), parentPath?",
  set_sound_concurrency: "Params: soundPath (or assetPath), concurrencyPath?",
  set_sound_submix: "Params: soundPath (or assetPath), submixPath?",
  set_submix_parent: "Params: submixPath, parentPath?",
  soundcue_add_node: "Params: cuePath (or assetPath), nodeType, soundWavePath?, properties?",
  soundcue_author: "Params: name, packagePath?, onConflict?, nodes?, connections?, root?",
  soundcue_connect: "Params: cuePath (or assetPath), childNodeId, parentNodeId?, childIndex?",
  soundcue_disconnect: "Params: cuePath (or assetPath), childNodeId?, parentNodeId?, clearRoot?",
  soundcue_get_graph: "Params: cuePath (or assetPath)",
  soundcue_remove_node: "Params: cuePath (or assetPath), nodeId",
  spawn_ambient_sound: "Params: soundPath (or assetPath, or path), location?, label?, onConflict?, volumeMultiplier? (or volume)",
};

/** Every key the spec'd audio handlers declare, aliases included. */
export const schema: Record<string, z.ZodType> = {
  adjusters: z.array(z.object({ soundClassPath: z.string().describe("SoundClass asset the adjustment applies to"), volumeAdjuster: z.number().optional().describe("Volume multiplier (default 1)"), pitchAdjuster: z.number().optional().describe("Pitch multiplier (default 1)"), applyToChildren: z.boolean().optional().describe("Apply to child sound classes (default false)") })).optional().describe("Per-SoundClass adjustments; an entry whose class does not load is skipped"),
  assetName: z.string().optional().describe("Alias for name"),
  assetPath: z.string().optional().describe("Alias for soundPath (add_sound_submix_send, extract_sound_wave_pcm, play_sound_at_location, read_sound_routing, set_sound_attenuation, set_sound_class, set_sound_concurrency, set_sound_submix, spawn_ambient_sound). MetaSoundSource asset path (metasound_add_graph_input, metasound_add_graph_output, metasound_add_node, metasound_build, metasound_connect, metasound_connect_audio_out, metasound_connect_graph_input, metasound_connect_graph_output, metasound_get_graph, metasound_set_input_default). MetaSound asset path (metasound_disconnect, metasound_inspect_node, metasound_list_connections, metasound_list_node_pins, metasound_list_variables, metasound_read_document, metasound_remove_member, metasound_remove_node, metasound_rename_member, metasound_search_nodes, metasound_validate). Audio asset path (set_audio_property). Alias for soundClassPath (set_sound_class_parent). Alias for cuePath (soundcue_add_node, soundcue_connect, soundcue_disconnect, soundcue_get_graph, soundcue_remove_node)"),
  attenuationPath: z.string().optional().describe("SoundAttenuation to attach (empty clears)"),
  channel: z.number().int().optional().describe("Audio output channel: 0 left or mono, 1 right (default 0)"),
  childIndex: z.number().int().optional().describe("Slot under the parent (default append)"),
  childNodeId: z.string().optional().describe("Node to attach (soundcue_connect). Child to detach (soundcue_disconnect)"),
  classType: z.string().optional().describe("External | Input | Output | Variable | ..."),
  clearRoot: z.boolean().optional().describe("Unset the cue root instead of detaching a parent link"),
  concurrencyPath: z.string().optional().describe("SoundConcurrency to attach (empty clears)"),
  connections: z.array(z.record(z.unknown())).optional().describe("Edges, each {from, to}. Endpoints are 'nodeId:vertex', or the heads 'input:<name>', 'output:<name>' and 'audioOut:<channel>' (metasound_author). Links, each {child, parent?, index?}; an omitted or 'root' parent makes the child the cue root, and index defaults to append (soundcue_author)"),
  cuePath: z.string().optional().describe("SoundCue asset path"),
  cursor: z.string().optional().describe("Resume a paged read: pass back the nextCursor from the previous page, unmodified"),
  dataType: z.string().optional().describe("MetaSound data type: Float, Int32, Bool, String, Trigger, Audio, Time, ... (metasound_add_graph_input, metasound_add_graph_output). Only edges carrying this data type (metasound_list_connections). Only vertices of this data type (metasound_list_node_pins). Only nodes with a vertex of this data type (metasound_search_nodes). Literal type hint: Float | Int32 | Bool | String (metasound_set_input_default)"),
  defaultValue: z.unknown().optional().describe("Literal default for the input"),
  destinationPath: z.string().optional().describe("Alias for packagePath"),
  direction: z.string().optional().describe("With nodeId: in | out | both (default both) (metasound_list_connections). inputs | outputs | both (default both) (metasound_list_node_pins)"),
  directory: z.string().optional().describe("Content directory to list (default /Game)"),
  downmixMono: z.boolean().optional().describe("Average the channels to mono (default false)"),
  dryLevel: z.number().optional().describe("Dry level"),
  effectType: z.string().optional().describe("reverb | eq | dynamics | filter | delay"),
  enableOcclusion: z.boolean().optional().describe("bEnableOcclusion"),
  fadeInTime: z.number().optional().describe("Fade-in time in seconds"),
  fadeOutTime: z.number().optional().describe("Fade-out time in seconds"),
  falloffDistance: z.number().optional().describe("Falloff distance; also turns volume attenuation on"),
  filename: z.string().optional().describe("Alias for filePath"),
  filePath: z.string().optional().describe("WAV, OGG or FLAC file on disk"),
  filter: z.string().optional().describe("Case-insensitive substring over the node class name (metasound_list_node_classes). Case-insensitive substring over the variable name (metasound_list_variables)"),
  format: z.string().optional().describe("Output format: mono | stereo (default mono)"),
  fromNodeId: z.string().optional().describe("Source node id (metasound_connect, metasound_connect_audio_out, metasound_connect_graph_output). Source node id, with fromOutput (metasound_disconnect)"),
  fromOutput: z.string().optional().describe("Output vertex name on the source node (metasound_connect, metasound_connect_graph_output). Output vertex name on the source node, of Audio type (metasound_connect_audio_out). Output vertex name on fromNodeId (metasound_disconnect)"),
  graphInput: z.string().optional().describe("Graph input name (metasound_connect_graph_input). Graph input whose default to set, instead of nodeId and inputName (metasound_set_input_default)"),
  graphOutput: z.string().optional().describe("Graph output name (metasound_connect_graph_output). Graph output to clear, audio outputs included (Out Mono, Out Left, Out Right) (metasound_disconnect)"),
  includeConnections: z.boolean().optional().describe("Include the edge list (default true)"),
  includeNodes: z.boolean().optional().describe("Include the node list (default true)"),
  inputName: z.string().optional().describe("Input vertex name on nodeId"),
  inputs: z.array(z.object({ name: z.string().describe("Graph input name"), dataType: z.string().describe("MetaSound data type: Float, Int32, Bool, String, Trigger, Audio, Time, ..."), default: z.unknown().optional().describe("Literal default for the input") })).optional().describe("Graph inputs to add"),
  label: z.string().optional().describe("Actor label"),
  limit: z.number().int().optional().describe("Rows per page, 1 to 5000 (default 1000) (list_sound_assets). Most matches to return, 1 to 1000 (default 100) (metasound_search_nodes)"),
  limitToOwner: z.boolean().optional().describe("Count voices per owning actor"),
  location: z.object({ x: z.number(), y: z.number(), z: z.number() }).optional().describe("World location to play at (default origin) (play_sound_at_location). World location of the actor (default origin) (spawn_ambient_sound)"),
  looping: z.boolean().optional().describe("Set bLooping on the imported SoundWave (omit to keep the importer's value)"),
  majorVersion: z.number().int().optional().describe("Node class major version (default 1)"),
  maxCount: z.number().int().optional().describe("Most concurrent voices"),
  maxResults: z.number().int().optional().describe("Alias for limit"),
  maxSeconds: z.number().optional().describe("Cap the decoded window in seconds (default the full asset)"),
  memberKind: z.string().optional().describe("input | output | variable (metasound_remove_member). input | output (metasound_rename_member)"),
  metasoundPath: z.string().optional().describe("Alias for assetPath"),
  name: z.string().optional().describe("Preset asset name (default <submix>_<effectType>) (add_submix_effect). SoundAttenuation asset name (create_attenuation). SoundConcurrency asset name (create_concurrency). MetaSoundSource asset name (create_metasound_source, metasound_author). SoundClass asset name (create_sound_class). SoundCue asset name (create_sound_cue, soundcue_author). SoundMix asset name (create_sound_mix). SoundSubmix asset name (create_submix). Asset name (default the file's base name) (import_audio). Graph input name (metasound_add_graph_input). Graph output name (metasound_add_graph_output). Member name to remove (metasound_remove_member). Current graph input or output name (metasound_rename_member)"),
  newName: z.string().optional().describe("Name to rename it to"),
  nodeClassName: z.string().optional().describe("Registered node class name, e.g. Sine"),
  nodeId: z.string().optional().describe("Node id to inspect (metasound_inspect_node). Narrow to edges touching this node (metasound_list_connections). Node id whose vertices to list (metasound_list_node_pins). Node id to remove (metasound_remove_node). Node whose input to set, with inputName. Pass either nodeId and inputName, or graphInput (metasound_set_input_default). Node to remove (soundcue_remove_node)"),
  nodeNamespace: z.string().optional().describe("Node class namespace (default UE)"),
  nodes: z.array(z.record(z.unknown())).optional().describe("Nodes to add, each {id, class, namespace?, variant?, majorVersion?, inputs?: {vertex: value}}; id is the local name connections use, namespace defaults to UE and majorVersion to 1 (metasound_author). Nodes to add, each {id, type, soundWavePath?, properties?: {field: value}}; id is the local name connections use, type is a soundcue_add_node nodeType (soundcue_author)"),
  nodeType: z.string().optional().describe("wave_player | mixer | random | modulator | attenuation | looping | concatenator | delay | switch"),
  nodeVariant: z.string().optional().describe("Node class variant, e.g. Audio"),
  offset: z.number().optional().describe("Refused. The row offset was replaced by cursor paging (#730); pass cursor and limit instead"),
  onConflict: z.string().optional().describe("skip (default) returns the existing asset, error refuses; it never overwrites (create_attenuation, create_concurrency, create_metasound_source, create_sound_class, create_sound_cue, create_sound_mix, create_submix, metasound_author, soundcue_author). skip | error | rename when an actor with that label exists (default skip) (spawn_ambient_sound)"),
  oneShot: z.boolean().optional().describe("Declare the one-shot interface (default true)"),
  outputs: z.array(z.object({ name: z.string().describe("Graph output name"), dataType: z.string().describe("MetaSound data type") })).optional().describe("Graph outputs to add"),
  outputVolume: z.number().optional().describe("Output volume"),
  packagePath: z.string().optional().describe("Preset folder (default /Game/Audio/SubmixEffects) (add_submix_effect). Destination folder (default /Game/Audio/Attenuation) (create_attenuation). Destination folder (default /Game/Audio/Concurrency) (create_concurrency). Destination folder (default /Game/Audio/MetaSounds) (create_metasound_source, metasound_author). Destination folder (default /Game/Audio/SoundClasses) (create_sound_class). Destination folder (default /Game/Audio/SoundCues) (create_sound_cue, soundcue_author). Destination folder (default /Game/Audio/SoundMixes) (create_sound_mix). Destination folder (default /Game/Audio/Submixes) (create_submix). Destination folder (default /Game/Audio) (import_audio)"),
  pageId: z.string().optional().describe("Graph page to read, for assets that declare more than one (default the default page)"),
  parentNodeId: z.string().optional().describe("Parent node (omit to make the child the cue root) (soundcue_connect). Detach from this parent only (default every parent) (soundcue_disconnect)"),
  parentPath: z.string().optional().describe("Parent SoundClass (default none) (create_sound_class). Parent submix (default none) (create_submix). New parent SoundClass (empty detaches to the root) (set_sound_class_parent). New parent submix (empty detaches to root) (set_submix_parent)"),
  path: z.string().optional().describe("Alias for soundPath"),
  pitch: z.number().optional().describe("Alias for pitchMultiplier"),
  pitchMultiplier: z.number().optional().describe("Pitch multiplier (default 1)"),
  properties: z.record(z.unknown()).optional().describe("FSoundClassProperties as JSON: Volume, Pitch, bIsUISound, ... (create_sound_class). Node-specific fields to set, as {property: value} (soundcue_add_node)"),
  propertyName: z.string().optional().describe("UPROPERTY name, dotted for nested structs"),
  query: z.string().optional().describe("Substring over node name, class name, namespace or variant"),
  recursive: z.boolean().optional().describe("Include subdirectories (default true)"),
  removeUnusedDependencies: z.boolean().optional().describe("Also drop node classes the graph no longer references (default true)"),
  replaceExisting: z.boolean().optional().describe("Replace an existing asset of that name (default true)"),
  resolutionRule: z.string().optional().describe("EMaxConcurrentResolutionRule name, e.g. StopFarthestThenOldest"),
  root: z.string().optional().describe("Local id of the node to make the cue root, overriding the connections"),
  sendLevel: z.number().optional().describe("Send level (default 1.0)"),
  settings: z.record(z.unknown()).optional().describe("Effect Settings struct as JSON (add_submix_effect). FSoundAttenuationSettings as JSON, applied before the shortcuts (create_attenuation)"),
  soundClassPath: z.string().optional().describe("SoundClass to assign (set_sound_class). SoundClass to reparent (set_sound_class_parent)"),
  soundPath: z.string().optional().describe("Sound asset path (add_sound_submix_send, read_sound_routing, set_sound_attenuation, set_sound_class, set_sound_concurrency, set_sound_submix). SoundWave asset path (extract_sound_wave_pcm). Sound asset to play (SoundWave, SoundCue or MetaSoundSource) (play_sound_at_location). Sound asset the AmbientSound plays (spawn_ambient_sound)"),
  soundWavePath: z.string().optional().describe("SoundWave for a wave_player node"),
  spatialize: z.boolean().optional().describe("bSpatialize"),
  submixPath: z.string().optional().describe("Submix to send to (add_sound_submix_send). SoundSubmix whose effect chain to append to (add_submix_effect). Base submix to route to (empty detaches) (set_sound_submix). SoundSubmix to reparent (set_submix_parent)"),
  toInput: z.string().optional().describe("Input vertex name on the destination node (metasound_connect, metasound_connect_graph_input). Input vertex name on toNodeId (metasound_disconnect)"),
  toNodeId: z.string().optional().describe("Destination node id (metasound_connect, metasound_connect_graph_input). Destination node id, with toInput (metasound_disconnect)"),
  value: z.unknown().optional().describe("Default value to set (metasound_set_input_default). Value as JSON: scalars, structs, arrays, object paths, or UE export text (set_audio_property)"),
  volume: z.number().optional().describe("Alias for volumeMultiplier"),
  volumeMultiplier: z.number().optional().describe("Volume multiplier (default 1) (play_sound_at_location). Volume multiplier on the audio component (default 1) (spawn_ambient_sound)"),
  volumeScale: z.number().optional().describe("Volume scale applied to each new voice"),
  wetLevel: z.number().optional().describe("Wet level"),
};

/** Declare an action for a spec'd bridge method: effect, summary, method. */
export const specBp = makeSpecBp(paramsClauses, handlerSpecs);
