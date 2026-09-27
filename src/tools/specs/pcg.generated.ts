// GENERATED FILE - do not edit.
//
// Written by scripts/generate-handler-specs.mjs from tests/golden/handler-specs.json,
// which is recorded from the parameter specs the C++ handlers register with
// (npm run specs:record). To change a parameter, change its RegisterHandler
// spec, re-record, and regenerate (#1057).
import { categorySchema, makeSpecBp, type HandlerSpecs } from "../../surface/handler-spec.js";

/** The recorded contract of every spec'd pcg handler. */
export const handlerSpecs: HandlerSpecs = {
  "add_pcg_node": {
    "category": "pcg",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "PCGGraph asset path",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "nodeType",
        "type": "string",
        "required": true,
        "description": "PCG settings class of the node to add"
      },
      {
        "name": "posX",
        "type": "number",
        "required": false,
        "description": "Graph editor X position for the new node"
      },
      {
        "name": "posY",
        "type": "number",
        "required": false,
        "description": "Graph editor Y position for the new node"
      }
    ]
  },
  "add_pcg_volume": {
    "category": "pcg",
    "params": [
      {
        "name": "graphPath",
        "type": "string",
        "required": false,
        "description": "PCGGraph to assign to the volume's component"
      },
      {
        "name": "location",
        "type": "vec3",
        "required": false,
        "description": "World location {x,y,z} (default origin)"
      },
      {
        "name": "extent",
        "type": "vec3",
        "required": false,
        "description": "Half-size of the volume box {x,y,z} (default 500 on each axis)"
      },
      {
        "name": "label",
        "type": "string",
        "required": false,
        "description": "Editor label. Also the idempotency key: an existing actor with this label is reported rather than duplicated"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "When the label exists: skip (default, report it) | error"
      }
    ],
    "contractExempt": "Spawns a volume under the contract values; nothing it reads fails first"
  },
  "cleanup_pcg": {
    "category": "pcg",
    "params": [
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Editor label of the actor holding the PCG component; a label naming several actors is refused"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Full actor object path; the unambiguous selector"
      },
      {
        "name": "removeComponents",
        "type": "boolean",
        "required": false,
        "description": "Remove the managed spawned components too (default true)"
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
  "connect_pcg_nodes": {
    "category": "pcg",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "PCGGraph asset path",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "sourceNode",
        "type": "string",
        "required": true,
        "description": "Node the edge leaves",
        "aliases": [
          "sourceNodeName"
        ]
      },
      {
        "name": "sourcePin",
        "type": "string",
        "required": false,
        "description": "Output pin label. connect_nodes defaults to the first output pin, disconnect_nodes to any",
        "aliases": [
          "sourcePinLabel"
        ]
      },
      {
        "name": "targetNode",
        "type": "string",
        "required": true,
        "description": "Node the edge enters",
        "aliases": [
          "targetNodeName"
        ]
      },
      {
        "name": "targetPin",
        "type": "string",
        "required": false,
        "description": "Input pin label. connect_nodes defaults to the first input pin, disconnect_nodes to any",
        "aliases": [
          "targetPinLabel"
        ]
      }
    ]
  },
  "create_pcg_graph": {
    "category": "pcg",
    "params": [
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "Graph asset name"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Folder for the new graph (default /Game/PCG)"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "When the graph exists: skip (default, report it) | error"
      }
    ],
    "contractExempt": "Creates and saves a graph under the contract values; nothing it reads fails first"
  },
  "disconnect_pcg_nodes": {
    "category": "pcg",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "PCGGraph asset path",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "sourceNode",
        "type": "string",
        "required": true,
        "description": "Node the edge leaves",
        "aliases": [
          "sourceNodeName"
        ]
      },
      {
        "name": "targetNode",
        "type": "string",
        "required": true,
        "description": "Node the edge enters",
        "aliases": [
          "targetNodeName"
        ]
      },
      {
        "name": "sourcePin",
        "type": "string",
        "required": false,
        "description": "Output pin label. connect_nodes defaults to the first output pin, disconnect_nodes to any",
        "aliases": [
          "sourcePinLabel"
        ]
      },
      {
        "name": "targetPin",
        "type": "string",
        "required": false,
        "description": "Input pin label. connect_nodes defaults to the first input pin, disconnect_nodes to any",
        "aliases": [
          "targetPinLabel"
        ]
      }
    ]
  },
  "execute_pcg_graph": {
    "category": "pcg",
    "params": [
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Editor label of the actor holding the PCG component; a label naming several actors is refused"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Full actor object path; the unambiguous selector"
      },
      {
        "name": "seed",
        "type": "integer",
        "required": false,
        "description": "Write the component's Seed before generating; the old one is reported as previousSeed"
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
  "export_pcg_graph": {
    "category": "pcg",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "PCGGraph asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "includeSettings",
        "type": "boolean",
        "required": false,
        "description": "Include per-node editable settings in the response (default true)"
      }
    ]
  },
  "force_regenerate_pcg": {
    "category": "pcg",
    "params": [
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Editor label of the actor holding the PCG component; a label naming several actors is refused"
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
  "get_pcg_component_details": {
    "category": "pcg",
    "params": [
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Editor label of the actor holding the PCG component; a label naming several actors is refused"
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
  "get_pcg_components": {
    "category": "pcg",
    "params": []
  },
  "import_pcg_graph": {
    "category": "pcg",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "PCGGraph asset path",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "nodes",
        "type": "array",
        "required": true,
        "description": "[{name, class, posX?, posY?, settings?}]",
        "items": "object"
      },
      {
        "name": "connections",
        "type": "array",
        "required": false,
        "description": "[{from, fromPin?, to, toPin?}]",
        "items": "object"
      },
      {
        "name": "replace",
        "type": "boolean",
        "required": false,
        "description": "Wipe existing user nodes first (default false)"
      }
    ]
  },
  "list_pcg_graphs": {
    "category": "pcg",
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
  "read_pcg_graph": {
    "category": "pcg",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "PCGGraph asset path",
        "aliases": [
          "path"
        ]
      }
    ]
  },
  "read_pcg_node_settings": {
    "category": "pcg",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "PCGGraph asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "nodeName",
        "type": "string",
        "required": true,
        "description": "Engine name of the node, as read_graph reports it"
      }
    ]
  },
  "remove_pcg_node": {
    "category": "pcg",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "PCGGraph asset path",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "nodeName",
        "type": "string",
        "required": true,
        "description": "Engine name of the node, as read_graph reports it"
      }
    ]
  },
  "set_pcg_node_settings": {
    "category": "pcg",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "PCGGraph asset path",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "nodeName",
        "type": "string",
        "required": true,
        "description": "Engine name of the node, as read_graph reports it"
      },
      {
        "name": "settings",
        "type": "object",
        "required": false,
        "description": "{propertyPath: value}; dotted paths and nested structs supported"
      },
      {
        "name": "propertyName",
        "type": "string",
        "required": false,
        "description": "One property to write instead of a settings object"
      },
      {
        "name": "propertyValue",
        "type": "string",
        "required": false,
        "description": "The value for propertyName, as UE export text"
      }
    ],
    "choices": [
      {
        "mode": "exactlyOne",
        "branches": [
          [
            "settings"
          ],
          [
            "propertyName",
            "propertyValue"
          ]
        ]
      }
    ]
  },
  "set_static_mesh_spawner_meshes": {
    "category": "pcg",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "PCGGraph asset path",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "nodeName",
        "type": "string",
        "required": true,
        "description": "Engine name of the node, as read_graph reports it"
      },
      {
        "name": "entries",
        "type": "array",
        "required": true,
        "description": "Weighted mesh entries",
        "items": "object",
        "fields": [
          {
            "name": "mesh",
            "type": "string",
            "required": true,
            "description": "StaticMesh asset path; an entry without one is skipped"
          },
          {
            "name": "weight",
            "type": "number",
            "required": false,
            "description": "Relative pick weight (default 1), truncated to a whole number"
          }
        ]
      },
      {
        "name": "replace",
        "type": "boolean",
        "required": false,
        "description": "Overwrite existing MeshEntries (default true)"
      }
    ]
  },
  "toggle_pcg_graph": {
    "category": "pcg",
    "params": [
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Editor label of the actor holding the PCG component; a label naming several actors is refused"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Full actor object path; the unambiguous selector"
      },
      {
        "name": "graphPath",
        "type": "string",
        "required": false,
        "description": "PCGGraph to assign (default: re-apply the component's current graph)"
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
  "unwrap_pcg_instance_nodes": {
    "category": "pcg",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "PCGGraph asset path",
        "aliases": [
          "path"
        ],
        "role": "editTarget"
      },
      {
        "name": "nodeName",
        "type": "string",
        "required": false,
        "description": "Only this node (default: every node in the graph)"
      }
    ]
  }
};

/** Every key the spec'd pcg handlers declare, aliases included. */
export const schema = categorySchema(handlerSpecs);

/** Declare an action for a spec'd bridge method: effect, summary, method. */
export const specBp = makeSpecBp(handlerSpecs);
