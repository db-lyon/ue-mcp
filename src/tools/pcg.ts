import type { ToolDef } from "../types.js";
import { categoryTool } from "../category-tool.js";
import { actions as epicActions, schema as epicSchema } from "./epic/pcg.generated.js";
import { specBp, schema as specSchema } from "./specs/pcg.generated.js";

export const pcgTool: ToolDef = categoryTool(
  "pcg",
  "Procedural Content Generation: graphs, nodes, connections, execution, volumes.",
  {
    list_graphs:          specBp("read", "List PCG graphs, sorted by object path. Lists the whole project; page through it with cursor/limit.", "list_pcg_graphs"),
    read_graph:           specBp("read", "Read graph structure.", "read_pcg_graph"),
    read_node_settings:   specBp("read", "Read node settings.", "read_pcg_node_settings"),
    get_components:       specBp("read", "List PCG components in level.", "get_pcg_components"),
    get_component_details: specBp("read", "Inspect PCG component (#983).", "get_pcg_component_details"),
    create_graph:         specBp("mutate", "Create graph. Idempotent by path: an existing graph is reported rather than replaced.", "create_pcg_graph"),
    add_node:             specBp("mutate", "Add node. nodeName is a RESULT, not an input: the engine assigns the name and this action reports it back for connect_nodes and remove_node.", "add_pcg_node"),
    connect_nodes:        specBp("mutate", "Wire nodes. Returns edgeVerified=true after confirming the UPCGEdge persisted; surfaces an error if AddEdge succeeded but no edge object was instantiated (#304).", "connect_pcg_nodes"),
    disconnect_nodes:     specBp("mutate", "Remove a wired edge between two PCG nodes. Omitted pins match any pin. Returns removedEdges count (#346).", "disconnect_pcg_nodes"),
    set_node_settings:    specBp("mutate", "Set node params. Pass a settings object of {propertyPath: value} (dotted paths and nested structs supported), or propertyName + propertyValue for a single write. Reports previousProperties and rolls back to them.", "set_pcg_node_settings"),
    set_static_mesh_spawner_meshes: specBp("mutate", "Populate weighted MeshEntries on a PCGStaticMeshSpawner node (#145). entries=[{mesh, weight?}]; replace defaults to true.", "set_static_mesh_spawner_meshes"),
    remove_node:          specBp("mutate", "Remove node.", "remove_pcg_node"),
    unwrap_instance_nodes: specBp("mutate", "Give instance nodes (UPCGSettingsInstance wrappers, read-only in the PCG editor's details panel) their own settings object, keeping every value and edge. Settings from a shared asset are copied into the node. Nodes that already own their settings are left alone. Omit nodeName for every node in the graph (#1087).", "unwrap_pcg_instance_nodes"),
    execute:              specBp("mutate", "Regenerate PCG. A seed is written to the component before generating, and the old one reported back as previousSeed (#983).", "execute_pcg_graph"),
    force_regenerate:     specBp("mutate", "Force a stuck PCG component to regenerate (clears graph ref, re-sets, cleanup+generate) (#146/#983).", "force_regenerate_pcg"),
    cleanup:              specBp("mutate", "Cleanup a PCG component (remove spawned content) (#146).", "cleanup_pcg"),
    toggle_graph:         specBp("mutate", "Toggle a PCG component's graph assignment to force reinit (no generate) (#146).", "toggle_pcg_graph"),
    add_volume:           specBp("mutate", "Place PCG volume. Idempotent by editor label when one is given.", "add_pcg_volume"),
    import_graph:         specBp("mutate", "Bulk-author a PCG graph from JSON: nodes=[{name,class,posX?,posY?,settings?}], connections=[{from,fromPin?,to,toPin?}], replace defaults to false. One call replaces N add_node + M connect_nodes + K set_node_settings (#213).", "import_pcg_graph"),
    export_graph:         specBp("read", "Export a PCG graph as JSON; includeSettings defaults to true. Round-trip safe with import_graph (#213).", "export_pcg_graph"),
    ...epicActions,
  },
  {
    ...epicSchema,
    // #1057: every key a spec'd handler declares, generated from its C++
    // registration. A key listed again below is shared with hand-written
    // actions, and tests/unit/handler-specs.test.ts holds the two to one type.
    ...specSchema,
  },
);
