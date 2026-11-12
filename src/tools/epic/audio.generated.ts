// GENERATED FILE - do not edit.
//
// Written by scripts/generate-epic-actions.mjs from tests/golden/epic-catalog/
// and src/tools/epic/effects.ts. To change what an action DOES, edit the
// effects file and regenerate; to pick up a new engine's toolsets, re-record
// the catalog and regenerate.
//
// These are ordinary actions. They carry a declared effect, real parameters and
// a Params: clause, they are in ALL_TOOLS, and they dispatch through the same
// task factory, guards and locks as every hand-written action in this package.
// Each also carries its input schema, which the compact signatures read (#1172).
import { z } from "zod";
import type { ActionSpec } from "../../core/types.js";
import { bp } from "../../surface/category-tool.js";

const S_epic_add_graph_inputs = {"properties":{"builderName":{"type":"string"},"inputs":{"type":"array","items":{"type":"object","properties":{"name":{"type":"string"},"dataType":{"type":"string"},"default":{"type":"string"},"bIsConstructor":{"type":"boolean"}}}},"page":{"type":"string"}},"required":["builderName","inputs"]} as const;
const S_epic_add_graph_outputs = {"properties":{"builderName":{"type":"string"},"outputs":{"type":"array","items":{"type":"object","properties":{"name":{"type":"string"},"dataType":{"type":"string"},"default":{"type":"string"},"bIsConstructor":{"type":"boolean"}}}},"page":{"type":"string"}},"required":["builderName","outputs"]} as const;
const S_epic_add_graph_page = {"properties":{"builderName":{"type":"string"},"page":{"type":"string"},"bDuplicateLastGraph":{"type":"boolean"},"bSetAsBuildPage":{"type":"boolean"}},"required":["builderName","page"]} as const;
const S_epic_add_graph_variables = {"properties":{"builderName":{"type":"string"},"variables":{"type":"array","items":{"type":"object","properties":{"name":{"type":"string"},"dataType":{"type":"string"},"default":{"type":"string"},"bIsConstructor":{"type":"boolean"}}}},"page":{"type":"string"}},"required":["builderName","variables"]} as const;
const S_epic_add_nodes = {"properties":{"builderName":{"type":"string"},"nodes":{"type":"array","items":{"type":"object","properties":{"className":{"type":"object","properties":{"namespace":{"type":"string"},"name":{"type":"string"},"variant":{"type":"string"}}}}}},"page":{"type":"string"}},"required":["builderName","nodes"]} as const;
const S_epic_add_variable_nodes = {"properties":{"builderName":{"type":"string"},"nodes":{"type":"array","items":{"type":"object","properties":{"variable":{"type":"string"},"kind":{"type":"string","enum":["Set","Get","GetDelayed"]}}}},"page":{"type":"string"}},"required":["builderName","nodes"]} as const;
const S_epic_attach_builder_to_asset = {"properties":{"assetPath":{"type":"string"},"builderName":{"type":"string"},"page":{"type":"string"}},"required":["assetPath","builderName","page"]} as const;
const S_epic_build_to_asset = {"properties":{"builderName":{"type":"string"},"assetName":{"type":"string"},"packagePath":{"type":"string"},"author":{"type":"string"}},"required":["builderName","assetName","packagePath","author"]} as const;
const S_epic_clear_pins = {"properties":{"builderName":{"type":"string"},"pins":{"type":"array","items":{"type":"object","properties":{"node":{"type":"string"},"pin":{"type":"string"},"direction":{"type":"string","enum":["Input","Output"]}}}},"page":{"type":"string"}},"required":["builderName","pins"]} as const;
const S_epic_commit_builder_to_asset = {"properties":{"builderName":{"type":"string"}},"required":["builderName"]} as const;
const S_epic_connect = {"properties":{"builderName":{"type":"string"},"connections":{"type":"array","items":{"type":"object","properties":{"fromNode":{"type":"string"},"fromPin":{"type":"string"},"toNode":{"type":"string"},"toPin":{"type":"string"}}}},"page":{"type":"string"}},"required":["builderName","connections"]} as const;
const S_epic_create_patch_builder = {"properties":{"builderName":{"type":"string"}},"required":["builderName"]} as const;
const S_epic_create_patch_preset_builder = {"properties":{"presetName":{"type":"string"},"referencedPatchPath":{"type":"string"}},"required":["presetName","referencedPatchPath"]} as const;
const S_epic_create_source_builder = {"properties":{"builderName":{"type":"string"},"outputFormat":{"type":"string","enum":["Mono","Stereo","Quad","FiveDotOne","SevenDotOne"]},"bIsOneShot":{"type":"boolean"}},"required":["builderName"]} as const;
const S_epic_create_source_preset_builder = {"properties":{"presetName":{"type":"string"},"referencedSourcePath":{"type":"string"}},"required":["presetName","referencedSourcePath"]} as const;
const S_epic_describe_graph = {"properties":{"builderName":{"type":"string"},"page":{"type":"string"}},"required":["builderName"]} as const;
const S_epic_describe_node = {"properties":{"builderName":{"type":"string"},"node":{"type":"string"},"page":{"type":"string"}},"required":["builderName","node"]} as const;
const S_epic_describe_node_class = {"properties":{"namespace":{"type":"string"},"name":{"type":"string"},"variant":{"type":"string"}},"required":["namespace","name","variant"]} as const;
const S_epic_detach_builder_from_asset = {"properties":{"builderName":{"type":"string"}},"required":["builderName"]} as const;
const S_epic_diff_pages = {"properties":{"builderName":{"type":"string"},"pageA":{"type":"string"},"pageB":{"type":"string"}},"required":["builderName","pageA","pageB"]} as const;
const S_epic_disconnect = {"properties":{"builderName":{"type":"string"},"connections":{"type":"array","items":{"type":"object","properties":{"fromNode":{"type":"string"},"fromPin":{"type":"string"},"toNode":{"type":"string"},"toPin":{"type":"string"}}}},"page":{"type":"string"}},"required":["builderName","connections"]} as const;
const S_epic_export_wav = {"properties":{"soundWave":{"type":"object","properties":{"refPath":{}}},"filePath":{"type":"string"}},"required":["soundWave","filePath"]} as const;
const S_epic_find_all_connections = {"properties":{"builderName":{"type":"string"},"page":{"type":"string"}},"required":["builderName"]} as const;
const S_epic_find_all_nodes = {"properties":{"builderName":{"type":"string"},"page":{"type":"string"}},"required":["builderName"]} as const;
const S_epic_find_builder_interfaces = {"properties":{"builderName":{"type":"string"}},"required":["builderName"]} as const;
const S_epic_find_data_types = {"properties":{"pattern":{"type":"string"}},"required":["pattern"]} as const;
const S_epic_find_graph_input_defaults = {"properties":{"builderName":{"type":"string"},"page":{"type":"string"}},"required":["builderName"]} as const;
const S_epic_find_graph_inputs = {"properties":{"builderName":{"type":"string"},"page":{"type":"string"}},"required":["builderName"]} as const;
const S_epic_find_graph_outputs = {"properties":{"builderName":{"type":"string"}},"required":["builderName"]} as const;
const S_epic_find_graph_pages = {"properties":{"builderName":{"type":"string"}},"required":["builderName"]} as const;
const S_epic_find_graph_variables = {"properties":{"builderName":{"type":"string"},"page":{"type":"string"}},"required":["builderName"]} as const;
const S_epic_find_interface_input_nodes = {"properties":{"builderName":{"type":"string"},"interfaceName":{"type":"string"}},"required":["builderName","interfaceName"]} as const;
const S_epic_find_interface_output_nodes = {"properties":{"builderName":{"type":"string"},"interfaceName":{"type":"string"}},"required":["builderName","interfaceName"]} as const;
const S_epic_find_interfaces = {"properties":{}} as const;
const S_epic_find_meta_sound_assets = {"properties":{"pattern":{"type":"string"},"maxResults":{"type":"integer"},"firstResult":{"type":"integer"}},"required":["pattern"]} as const;
const S_epic_find_node_classes = {"properties":{"pattern":{"type":"string"},"inputs":{"type":"array","items":{"type":"string"}},"outputs":{"type":"array","items":{"type":"string"}},"bIncludeNonAddable":{"type":"boolean"},"maxResults":{"type":"integer"},"firstResult":{"type":"integer"},"maxPerGroup":{"type":"integer"}},"required":["pattern","inputs","outputs"]} as const;
const S_epic_find_registered_pages = {"properties":{}} as const;
const S_epic_import_file = {"properties":{"folderPath":{"type":"string"},"assetName":{"type":"string"},"sourceFile":{"type":"string"},"bReplaceExisting":{"type":"boolean"},"bSave":{"type":"boolean"}},"required":["folderPath","assetName","sourceFile"]} as const;
const S_epic_layout_graph = {"properties":{"builderName":{"type":"string"},"page":{"type":"string"}},"required":["builderName"]} as const;
const S_epic_remove_graph_page = {"properties":{"builderName":{"type":"string"},"page":{"type":"string"}},"required":["builderName","page"]} as const;
const S_epic_remove_graph_variables = {"properties":{"builderName":{"type":"string"},"variables":{"type":"array","items":{"type":"string"}},"page":{"type":"string"}},"required":["builderName","variables"]} as const;
const S_epic_remove_nodes = {"properties":{"builderName":{"type":"string"},"nodes":{"type":"array","items":{"type":"string"}},"bRemoveUnusedDependencies":{"type":"boolean"},"page":{"type":"string"}},"required":["builderName","nodes"]} as const;
const S_epic_set_build_page = {"properties":{"builderName":{"type":"string"},"page":{"type":"string"}},"required":["builderName","page"]} as const;
const S_epic_set_defaults = {"properties":{"builderName":{"type":"string"},"defaults":{"type":"array","items":{"type":"object","properties":{"node":{"type":"string"},"pin":{"type":"string"},"value":{"type":"string"},"page":{"type":"string"}}}}},"required":["builderName","defaults"]} as const;

/** 44 wrapped engine tools routed to the `audio` category. */
export const actions: Record<string, ActionSpec> = {
  epic_add_graph_inputs: {
    epicSchema: S_epic_add_graph_inputs,
    epicTool: { toolset: "MetaSoundToolset.MetaSoundAuthoringToolset", name: "MetaSoundToolset.MetaSoundAuthoringToolset.AddGraphInputs", engines: ["5.9"] },
    ...bp("mutate", "[Epic MetaSoundToolset.MetaSoundAuthoringToolset] Declares graph inputs. Each becomes a node whose ref is the input's own name. Available on UE 5.9; absent from UE 5.8. Params: builderName, inputs, page?", "epic_call_tool"),
  },
  epic_add_graph_outputs: {
    epicSchema: S_epic_add_graph_outputs,
    epicTool: { toolset: "MetaSoundToolset.MetaSoundAuthoringToolset", name: "MetaSoundToolset.MetaSoundAuthoringToolset.AddGraphOutputs", engines: ["5.9"] },
    ...bp("mutate", "[Epic MetaSoundToolset.MetaSoundAuthoringToolset] Declares graph outputs. Each becomes a node whose ref is the output's own name. A Source's audio output is already an interface member. Available on UE 5.9; absent from UE 5.8. Params: builderName, outputs, page?", "epic_call_tool"),
  },
  epic_add_graph_page: {
    epicSchema: S_epic_add_graph_page,
    epicTool: { toolset: "MetaSoundToolset.MetaSoundAuthoringToolset", name: "MetaSoundToolset.MetaSoundAuthoringToolset.AddGraphPage", engines: ["5.9"] },
    ...bp("mutate", "[Epic MetaSoundToolset.MetaSoundAuthoringToolset] Adds a paged graph. Page must be declared (FindRegisteredPages) and not already carried (the record's Pages). Duplicating copies topology but not graph-input defaults. Available on UE 5.9; absent from UE 5.8. Params: builderName, page, bDuplicateLastGraph?, bSetAsBuildPage?", "epic_call_tool"),
  },
  epic_add_graph_variables: {
    epicSchema: S_epic_add_graph_variables,
    epicTool: { toolset: "MetaSoundToolset.MetaSoundAuthoringToolset", name: "MetaSoundToolset.MetaSoundAuthoringToolset.AddGraphVariables", engines: ["5.9"] },
    ...bp("mutate", "[Epic MetaSoundToolset.MetaSoundAuthoringToolset] Declares graph variables, which carry state across blocks. Referenced by name thereafter. Available on UE 5.9; absent from UE 5.8. Params: builderName, variables, page?", "epic_call_tool"),
  },
  epic_add_nodes: {
    epicSchema: S_epic_add_nodes,
    epicTool: { toolset: "MetaSoundToolset.MetaSoundAuthoringToolset", name: "MetaSoundToolset.MetaSoundAuthoringToolset.AddNodes", engines: ["5.9"] },
    ...bp("mutate", "[Epic MetaSoundToolset.MetaSoundAuthoringToolset] Adds nodes by registered class. Returns each new node's ref in the report's Nodes. Available on UE 5.9; absent from UE 5.8. Params: builderName, nodes, page?", "epic_call_tool"),
  },
  epic_add_variable_nodes: {
    epicSchema: S_epic_add_variable_nodes,
    epicTool: { toolset: "MetaSoundToolset.MetaSoundAuthoringToolset", name: "MetaSoundToolset.MetaSoundAuthoringToolset.AddVariableNodes", engines: ["5.9"] },
    ...bp("mutate", "[Epic MetaSoundToolset.MetaSoundAuthoringToolset] Adds Set / Get / Delayed-Get nodes for already-declared graph variables. Available on UE 5.9; absent from UE 5.8. Params: builderName, nodes, page?", "epic_call_tool"),
  },
  epic_attach_builder_to_asset: {
    epicSchema: S_epic_attach_builder_to_asset,
    epicTool: { toolset: "MetaSoundToolset.MetaSoundAuthoringToolset", name: "MetaSoundToolset.MetaSoundAuthoringToolset.AttachBuilderToAsset", engines: ["5.9"] },
    ...bp("mutate", "[Epic MetaSoundToolset.MetaSoundAuthoringToolset] Attaches an editable builder to an existing MetaSound under BuilderName, targeting Page. The builder is the engine's singleton for that asset, so anything else editing it in this editor shares the state. BuilderName is independent of the asset; any unused name works. Page is stated, not inherited: the page a builder arrives on differs by whether the asset is open, was opened earlier, or never has been, and those are indistinguishable from here. Pass \"Default\" unless the asset is paged. Fails without attaching on an unknown page, or one the asset does not carry -- AddGraphPage adopts it. Available on UE 5.9; absent from UE 5.8. Params: assetPath, builderName, page", "epic_call_tool"),
  },
  epic_build_to_asset: {
    epicSchema: S_epic_build_to_asset,
    epicTool: { toolset: "MetaSoundToolset.MetaSoundAuthoringToolset", name: "MetaSoundToolset.MetaSoundAuthoringToolset.BuildToAsset", engines: ["5.9"] },
    ...bp("mutate", "[Epic MetaSoundToolset.MetaSoundAuthoringToolset] Writes the builder to a NEW asset on disk. Returns the new asset's object path. On an attached builder this forks a duplicate rather than saving it. Available on UE 5.9; absent from UE 5.8. Params: builderName, assetName, packagePath, author", "epic_call_tool"),
  },
  epic_clear_pins: {
    epicSchema: S_epic_clear_pins,
    epicTool: { toolset: "MetaSoundToolset.MetaSoundAuthoringToolset", name: "MetaSoundToolset.MetaSoundAuthoringToolset.ClearPins", engines: ["5.9"] },
    ...bp("mutate", "[Epic MetaSoundToolset.MetaSoundAuthoringToolset] Removes every edge touching the named pins, without knowing what is wired to them. An input takes at most one edge; clearing an output severs every consumer it fans out to. Available on UE 5.9; absent from UE 5.8. Params: builderName, pins, page?", "epic_call_tool"),
  },
  epic_commit_builder_to_asset: {
    epicSchema: S_epic_commit_builder_to_asset,
    epicTool: { toolset: "MetaSoundToolset.MetaSoundAuthoringToolset", name: "MetaSoundToolset.MetaSoundAuthoringToolset.CommitBuilderToAsset", engines: ["5.9"] },
    ...bp("mutate", "[Epic MetaSoundToolset.MetaSoundAuthoringToolset] Saves an attached builder's edits back to its source asset and re-registers its graph, so the next play of the asset hears them. Returns the asset's path. Only valid for an attached builder. Available on UE 5.9; absent from UE 5.8. Params: builderName", "epic_call_tool"),
  },
  epic_connect: {
    epicSchema: S_epic_connect,
    epicTool: { toolset: "MetaSoundToolset.MetaSoundAuthoringToolset", name: "MetaSoundToolset.MetaSoundAuthoringToolset.Connect", engines: ["5.9"] },
    ...bp("mutate", "[Epic MetaSoundToolset.MetaSoundAuthoringToolset] Wires outputs to inputs. Every edge is resolved before any is made. Direction is not checked beyond type: same-typed pins wired backwards connect cleanly and produce wrong audio. Available on UE 5.9; absent from UE 5.8. Params: builderName, connections, page?", "epic_call_tool"),
  },
  epic_create_patch_builder: {
    epicSchema: S_epic_create_patch_builder,
    epicTool: { toolset: "MetaSoundToolset.MetaSoundAuthoringToolset", name: "MetaSoundToolset.MetaSoundAuthoringToolset.CreatePatchBuilder", engines: ["5.9"] },
    ...bp("mutate", "[Epic MetaSoundToolset.MetaSoundAuthoringToolset] Creates a Patch builder registered under BuilderName. Persists for the session. No built-in interface; its graph I/O must be declared explicitly. Available on UE 5.9; absent from UE 5.8. Params: builderName", "epic_call_tool"),
  },
  epic_create_patch_preset_builder: {
    epicSchema: S_epic_create_patch_preset_builder,
    epicTool: { toolset: "MetaSoundToolset.MetaSoundAuthoringToolset", name: "MetaSoundToolset.MetaSoundAuthoringToolset.CreatePatchPresetBuilder", engines: ["5.9"] },
    ...bp("mutate", "[Epic MetaSoundToolset.MetaSoundAuthoringToolset] Same contract as CreateSourcePresetBuilder, for MetaSoundPatch parents. Available on UE 5.9; absent from UE 5.8. Params: presetName, referencedPatchPath", "epic_call_tool"),
  },
  epic_create_source_builder: {
    epicSchema: S_epic_create_source_builder,
    epicTool: { toolset: "MetaSoundToolset.MetaSoundAuthoringToolset", name: "MetaSoundToolset.MetaSoundAuthoringToolset.CreateSourceBuilder", engines: ["5.9"] },
    ...bp("mutate", "[Epic MetaSoundToolset.MetaSoundAuthoringToolset] Creates a Source builder registered under BuilderName. Persists for the session. The returned record already holds the interface members: an audio output per channel of OutputFormat, OnPlay, and -- when bIsOneShot -- OnFinished and the UE.Source.OneShot interface. Available on UE 5.9; absent from UE 5.8. Params: builderName, outputFormat?, bIsOneShot?", "epic_call_tool"),
  },
  epic_create_source_preset_builder: {
    epicSchema: S_epic_create_source_preset_builder,
    epicTool: { toolset: "MetaSoundToolset.MetaSoundAuthoringToolset", name: "MetaSoundToolset.MetaSoundAuthoringToolset.CreateSourcePresetBuilder", engines: ["5.9"] },
    ...bp("mutate", "[Epic MetaSoundToolset.MetaSoundAuthoringToolset] Creates a Source preset over an existing MetaSoundSource. The returned record's inputs are the whole overridable set; topology tools reject this builder. A restricted editor refuses a parent FindMetaSoundAssets would not list. Available on UE 5.9; absent from UE 5.8. Params: presetName, referencedSourcePath", "epic_call_tool"),
  },
  epic_describe_graph: {
    epicSchema: S_epic_describe_graph,
    epicTool: { toolset: "MetaSoundToolset.MetaSoundInspectionToolset", name: "MetaSoundToolset.MetaSoundInspectionToolset.DescribeGraph", engines: ["5.9"] },
    ...bp("read", "[Epic MetaSoundToolset.MetaSoundInspectionToolset] The whole document in one call: interfaces, graph I/O with defaults, variables, nodes with their pins and authored values, positions, and edges. The detailed read -- creating or attaching a builder reports the same document with its nodes named but not detailed. Available on UE 5.9; absent from UE 5.8. Params: builderName, page?", "epic_call_tool"),
  },
  epic_describe_node: {
    epicSchema: S_epic_describe_node,
    epicTool: { toolset: "MetaSoundToolset.MetaSoundInspectionToolset", name: "MetaSoundToolset.MetaSoundInspectionToolset.DescribeNode", engines: ["5.9"] },
    ...bp("read", "[Epic MetaSoundToolset.MetaSoundInspectionToolset] One node with its pins and the values authored onto them -- the record a document read reports. Errors when the ref names no node; a node that resolves but declares no pins reads back empty. A pin's ValueType is None when the node authored nothing, so the class default applies; DescribeNodeClass reports it. A connected pin uses neither -- the edge feeding it wins. Re-read refs per page: a duplicated page keeps the ids it was copied from. Available on UE 5.9; absent from UE 5.8. Params: builderName, node, page?", "epic_call_tool"),
  },
  epic_describe_node_class: {
    epicSchema: S_epic_describe_node_class,
    epicTool: { toolset: "MetaSoundToolset.MetaSoundDiscoveryToolset", name: "MetaSoundToolset.MetaSoundDiscoveryToolset.DescribeNodeClass", engines: ["5.9"] },
    ...bp("read", "[Epic MetaSoundToolset.MetaSoundDiscoveryToolset] A registered class's default pins, without adding a node. Resolves the highest version. Errors when no class has that name. Available on UE 5.9; absent from UE 5.8. Params: namespace, name, variant", "epic_call_tool"),
  },
  epic_detach_builder_from_asset: {
    epicSchema: S_epic_detach_builder_from_asset,
    epicTool: { toolset: "MetaSoundToolset.MetaSoundAuthoringToolset", name: "MetaSoundToolset.MetaSoundAuthoringToolset.DetachBuilderFromAsset", engines: ["5.9"] },
    ...bp("mutate", "[Epic MetaSoundToolset.MetaSoundAuthoringToolset] Unregisters an attached builder and drops its attachment record. Returns BuilderName. Uncommitted edits are lost. Safe to call when the builder was already unregistered externally. Available on UE 5.9; absent from UE 5.8. Params: builderName", "epic_call_tool"),
  },
  epic_diff_pages: {
    epicSchema: S_epic_diff_pages,
    epicTool: { toolset: "MetaSoundToolset.MetaSoundInspectionToolset", name: "MetaSoundToolset.MetaSoundInspectionToolset.DiffPages", engines: ["5.9"] },
    ...bp("read", "[Epic MetaSoundToolset.MetaSoundInspectionToolset] Compares two pages across topology and paged input defaults. Every array empty means they match. Matches by class, not id, so independently built pages compare usefully. Counts are significant: ten wave players against four reports six extras -- how many differ, not which. Available on UE 5.9; absent from UE 5.8. Params: builderName, pageA, pageB", "epic_call_tool"),
  },
  epic_disconnect: {
    epicSchema: S_epic_disconnect,
    epicTool: { toolset: "MetaSoundToolset.MetaSoundAuthoringToolset", name: "MetaSoundToolset.MetaSoundAuthoringToolset.Disconnect", engines: ["5.9"] },
    ...bp("mutate", "[Epic MetaSoundToolset.MetaSoundAuthoringToolset] Removes specific edges. Takes the same records a read reports. Available on UE 5.9; absent from UE 5.8. Params: builderName, connections, page?", "epic_call_tool"),
  },
  epic_export_wav: {
    epicSchema: S_epic_export_wav,
    epicTool: { toolset: "EditorToolset.SoundWaveToolset", name: "EditorToolset.SoundWaveToolset.ExportWav", engines: ["5.9"] },
    ...bp("mutate", "[Epic EditorToolset.SoundWaveToolset] Exports a SoundWave to a .wav file on disk. Available on UE 5.9; absent from UE 5.8. Params: soundWave, filePath", "epic_call_tool"),
  },
  epic_find_all_connections: {
    epicSchema: S_epic_find_all_connections,
    epicTool: { toolset: "MetaSoundToolset.MetaSoundInspectionToolset", name: "MetaSoundToolset.MetaSoundInspectionToolset.FindAllConnections", engines: ["5.9"] },
    ...bp("read", "[Epic MetaSoundToolset.MetaSoundInspectionToolset] Every edge on Page, addressed by node ref. Edges whose pin names do not resolve against the document are dropped, not reported. Available on UE 5.9; absent from UE 5.8. Params: builderName, page?", "epic_call_tool"),
  },
  epic_find_all_nodes: {
    epicSchema: S_epic_find_all_nodes,
    epicTool: { toolset: "MetaSoundToolset.MetaSoundInspectionToolset", name: "MetaSoundToolset.MetaSoundInspectionToolset.FindAllNodes", engines: ["5.9"] },
    ...bp("read", "[Epic MetaSoundToolset.MetaSoundInspectionToolset] Every node on Page, interface and graph-I/O nodes included. Read DisplayName and AssetPath to tell what a node is: a nested patch or source registers its class name off the asset's GUID, so ClassName alone does not name it. Available on UE 5.9; absent from UE 5.8. Params: builderName, page?", "epic_call_tool"),
  },
  epic_find_builder_interfaces: {
    epicSchema: S_epic_find_builder_interfaces,
    epicTool: { toolset: "MetaSoundToolset.MetaSoundInspectionToolset", name: "MetaSoundToolset.MetaSoundInspectionToolset.FindBuilderInterfaces", engines: ["5.9"] },
    ...bp("read", "[Epic MetaSoundToolset.MetaSoundInspectionToolset] Interfaces declared on this builder. Available on UE 5.9; absent from UE 5.8. Params: builderName", "epic_call_tool"),
  },
  epic_find_data_types: {
    epicSchema: S_epic_find_data_types,
    epicTool: { toolset: "MetaSoundToolset.MetaSoundDiscoveryToolset", name: "MetaSoundToolset.MetaSoundDiscoveryToolset.FindDataTypes", engines: ["5.9"] },
    ...bp("read", "[Epic MetaSoundToolset.MetaSoundDiscoveryToolset] Registered MetaSound data types matching Pattern, exact name first. TypeName is the spelling every data-type argument on this surface expects. Available on UE 5.9; absent from UE 5.8. Params: pattern", "epic_call_tool"),
  },
  epic_find_graph_input_defaults: {
    epicSchema: S_epic_find_graph_input_defaults,
    epicTool: { toolset: "MetaSoundToolset.MetaSoundInspectionToolset", name: "MetaSoundToolset.MetaSoundInspectionToolset.FindGraphInputDefaults", engines: ["5.9"] },
    ...bp("read", "[Epic MetaSoundToolset.MetaSoundInspectionToolset] Graph-input defaults on Page. Inputs with no default there are omitted. Available on UE 5.9; absent from UE 5.8. Params: builderName, page?", "epic_call_tool"),
  },
  epic_find_graph_inputs: {
    epicSchema: S_epic_find_graph_inputs,
    epicTool: { toolset: "MetaSoundToolset.MetaSoundInspectionToolset", name: "MetaSoundToolset.MetaSoundInspectionToolset.FindGraphInputs", engines: ["5.9"] },
    ...bp("read", "[Epic MetaSoundToolset.MetaSoundInspectionToolset] Graph inputs as Name / DataType, with the default set on Page. Available on UE 5.9; absent from UE 5.8. Params: builderName, page?", "epic_call_tool"),
  },
  epic_find_graph_outputs: {
    epicSchema: S_epic_find_graph_outputs,
    epicTool: { toolset: "MetaSoundToolset.MetaSoundInspectionToolset", name: "MetaSoundToolset.MetaSoundInspectionToolset.FindGraphOutputs", engines: ["5.9"] },
    ...bp("read", "[Epic MetaSoundToolset.MetaSoundInspectionToolset] Graph outputs as Name / DataType. Outputs carry no default, so Default is empty. Available on UE 5.9; absent from UE 5.8. Params: builderName", "epic_call_tool"),
  },
  epic_find_graph_pages: {
    epicSchema: S_epic_find_graph_pages,
    epicTool: { toolset: "MetaSoundToolset.MetaSoundInspectionToolset", name: "MetaSoundToolset.MetaSoundInspectionToolset.FindGraphPages", engines: ["5.9"] },
    ...bp("read", "[Epic MetaSoundToolset.MetaSoundInspectionToolset] Pages this asset carries, naming which one mutations target. For the wider set a page can be added from, call FindRegisteredPages. Available on UE 5.9; absent from UE 5.8. Params: builderName", "epic_call_tool"),
  },
  epic_find_graph_variables: {
    epicSchema: S_epic_find_graph_variables,
    epicTool: { toolset: "MetaSoundToolset.MetaSoundInspectionToolset", name: "MetaSoundToolset.MetaSoundInspectionToolset.FindGraphVariables", engines: ["5.9"] },
    ...bp("read", "[Epic MetaSoundToolset.MetaSoundInspectionToolset] Graph variables on Page as Name / DataType / Default. Their Set and Get nodes are in the node listing, named \"Variable.Get.<Name>_<id>\". Declaring a variable also adds a \"Variable.<Name>_<id>\" node holding its value, which performs no access. Available on UE 5.9; absent from UE 5.8. Params: builderName, page?", "epic_call_tool"),
  },
  epic_find_interface_input_nodes: {
    epicSchema: S_epic_find_interface_input_nodes,
    epicTool: { toolset: "MetaSoundToolset.MetaSoundInspectionToolset", name: "MetaSoundToolset.MetaSoundInspectionToolset.FindInterfaceInputNodes", engines: ["5.9"] },
    ...bp("read", "[Epic MetaSoundToolset.MetaSoundInspectionToolset] Ids of the input nodes an interface contributes on the build page; empty when undeclared. Available on UE 5.9; absent from UE 5.8. Params: builderName, interfaceName", "epic_call_tool"),
  },
  epic_find_interface_output_nodes: {
    epicSchema: S_epic_find_interface_output_nodes,
    epicTool: { toolset: "MetaSoundToolset.MetaSoundInspectionToolset", name: "MetaSoundToolset.MetaSoundInspectionToolset.FindInterfaceOutputNodes", engines: ["5.9"] },
    ...bp("read", "[Epic MetaSoundToolset.MetaSoundInspectionToolset] Ids of the output nodes an interface contributes on the build page; empty when undeclared. Available on UE 5.9; absent from UE 5.8. Params: builderName, interfaceName", "epic_call_tool"),
  },
  epic_find_interfaces: {
    epicSchema: S_epic_find_interfaces,
    epicTool: { toolset: "MetaSoundToolset.MetaSoundDiscoveryToolset", name: "MetaSoundToolset.MetaSoundDiscoveryToolset.FindInterfaces", engines: ["5.9"] },
    ...bp("read", "[Epic MetaSoundToolset.MetaSoundDiscoveryToolset] Every registered MetaSound interface, at its highest registered version. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_find_meta_sound_assets: {
    epicSchema: S_epic_find_meta_sound_assets,
    epicTool: { toolset: "MetaSoundToolset.MetaSoundAuthoringToolset", name: "MetaSoundToolset.MetaSoundAuthoringToolset.FindMetaSoundAssets", engines: ["5.9"] },
    ...bp("read", "[Epic MetaSoundToolset.MetaSoundAuthoringToolset] MetaSound assets in content, ordered by path. Where preset authoring starts: each can parent a preset, and a preset that is not read-only can be attached and retuned. Lists only what this editor lets the user see. A restricted editor also drops any asset a preset would fail validation from: a preset's chain must end at a built-in MetaSound, not a project's own. The preset creators and AttachBuilderToAsset refuse the same assets there. Available on UE 5.9; absent from UE 5.8. Params: pattern, maxResults?, firstResult?", "epic_call_tool"),
  },
  epic_find_node_classes: {
    epicSchema: S_epic_find_node_classes,
    epicTool: { toolset: "MetaSoundToolset.MetaSoundDiscoveryToolset", name: "MetaSoundToolset.MetaSoundDiscoveryToolset.FindNodeClasses", engines: ["5.9"] },
    ...bp("read", "[Epic MetaSoundToolset.MetaSoundDiscoveryToolset] Registered node classes matching Pattern, best first, one group per node. Highest versions only; no deprecated classes. No pins: describe the chosen class before wiring it. The session's first call waits on the asset scan, so an empty result means nothing matched. Every term must match the class triple, display name, description, keywords or category. Ranked by match strength, an exact name first; ties rank UE before Experimental. Asset MetaSounds are listed loaded or not, and match by asset name. Available on UE 5.9; absent from UE 5.8. Params: pattern, inputs, outputs, bIncludeNonAddable?, maxResults?, firstResult?, maxPerGroup?", "epic_call_tool"),
  },
  epic_find_registered_pages: {
    epicSchema: S_epic_find_registered_pages,
    epicTool: { toolset: "MetaSoundToolset.MetaSoundDiscoveryToolset", name: "MetaSoundToolset.MetaSoundDiscoveryToolset.FindRegisteredPages", engines: ["5.9"] },
    ...bp("read", "[Epic MetaSoundToolset.MetaSoundDiscoveryToolset] Page names the project declares, Default first -- the set AddGraphPage draws from. A name means the same page on every MetaSound. For the pages one asset carries, read Pages off a graph record. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_import_file: {
    epicSchema: S_epic_import_file,
    epicTool: { toolset: "EditorToolset.SoundWaveToolset", name: "EditorToolset.SoundWaveToolset.ImportFile", engines: ["5.9"] },
    ...bp("mutate", "[Epic EditorToolset.SoundWaveToolset] Imports an audio file from disk as a SoundWave asset. Accepts the extensions SoundFactory declares: wav, aif, aiff, ogg, flac, opus, mp3. Available on UE 5.9; absent from UE 5.8. Params: folderPath, assetName, sourceFile, bReplaceExisting?, bSave?", "epic_call_tool"),
  },
  epic_layout_graph: {
    epicSchema: S_epic_layout_graph,
    epicTool: { toolset: "MetaSoundToolset.MetaSoundAuthoringToolset", name: "MetaSoundToolset.MetaSoundAuthoringToolset.LayoutGraph", engines: ["5.9"] },
    ...bp("mutate", "[Epic MetaSoundToolset.MetaSoundAuthoringToolset] Arranges the targeted page left to right by connection depth, discarding manual arrangement. Applied counts the nodes positioned, which is every node on the page, so zero with bOk set means the page held no nodes rather than that layout declined to run. Available on UE 5.9; absent from UE 5.8. Params: builderName, page?", "epic_call_tool"),
  },
  epic_remove_graph_page: {
    epicSchema: S_epic_remove_graph_page,
    epicTool: { toolset: "MetaSoundToolset.MetaSoundAuthoringToolset", name: "MetaSoundToolset.MetaSoundAuthoringToolset.RemoveGraphPage", engines: ["5.9"] },
    ...bp("mutate", "[Epic MetaSoundToolset.MetaSoundAuthoringToolset] Removes a paged graph. Removing the targeted page moves the builder to an adjacent one unreported -- read the next result's BuildPage. Default cannot be removed. Available on UE 5.9; absent from UE 5.8. Params: builderName, page", "epic_call_tool"),
  },
  epic_remove_graph_variables: {
    epicSchema: S_epic_remove_graph_variables,
    epicTool: { toolset: "MetaSoundToolset.MetaSoundAuthoringToolset", name: "MetaSoundToolset.MetaSoundAuthoringToolset.RemoveGraphVariables", engines: ["5.9"] },
    ...bp("mutate", "[Epic MetaSoundToolset.MetaSoundAuthoringToolset] Removes graph variables, every Set and Get node bound to them, and the edges those carried. Refs to those nodes are stale afterwards. Available on UE 5.9; absent from UE 5.8. Params: builderName, variables, page?", "epic_call_tool"),
  },
  epic_remove_nodes: {
    epicSchema: S_epic_remove_nodes,
    epicTool: { toolset: "MetaSoundToolset.MetaSoundAuthoringToolset", name: "MetaSoundToolset.MetaSoundAuthoringToolset.RemoveNodes", engines: ["5.9"] },
    ...bp("mutate", "[Epic MetaSoundToolset.MetaSoundAuthoringToolset] Removes nodes and every edge touching them. Refs are stale afterwards. Removing an interface-provided node leaves the interface inconsistent. Available on UE 5.9; absent from UE 5.8. Params: builderName, nodes, bRemoveUnusedDependencies?, page?", "epic_call_tool"),
  },
  epic_set_build_page: {
    epicSchema: S_epic_set_build_page,
    epicTool: { toolset: "MetaSoundToolset.MetaSoundAuthoringToolset", name: "MetaSoundToolset.MetaSoundAuthoringToolset.SetBuildPage", engines: ["5.9"] },
    ...bp("mutate", "[Epic MetaSoundToolset.MetaSoundAuthoringToolset] Moves the builder to another page the asset carries. Every later edit lands there. Shared with the MetaSound editor, so an open editor follows. Transacted. Available on UE 5.9; absent from UE 5.8. Params: builderName, page", "epic_call_tool"),
  },
  epic_set_defaults: {
    epicSchema: S_epic_set_defaults,
    epicTool: { toolset: "MetaSoundToolset.MetaSoundAuthoringToolset", name: "MetaSoundToolset.MetaSoundAuthoringToolset.SetDefaults", engines: ["5.9"] },
    ...bp("mutate", "[Epic MetaSoundToolset.MetaSoundAuthoringToolset] Sets default values on node pins and graph inputs. A node pin on a preset aborts the whole call rather than failing that one item. A value carries no type; each is parsed against the data type of the pin it lands on. Scalars go bare -- \"true\", \"440\", \"0.5\", an object path -- and arrays as JSON. An empty value clears the default rather than setting one. Array values on a node pin do not render in the MetaSound editor. A graph-input default is page-scoped. Each entry's Page names where it lands, empty meaning the targeted page. Unlike the mutators a named page here writes there rather than failing -- the write is addressed, not applied in place. Node-pin defaults ignore Page; there is no paged form. Available on UE 5.9; absent from UE 5.8. Params: builderName, defaults", "epic_call_tool"),
  },
};

/** The parameters those actions accept, declared so the MCP layer stops stripping them. */
export const schema: Record<string, z.ZodType> = {
  assetName: z.string().optional().describe("The name of the new asset."),
  assetPath: z.string().optional(),
  author: z.string().optional(),
  bDuplicateLastGraph: z.boolean().optional().describe("Copy the most-recently-added page rather than start empty."),
  bIncludeNonAddable: z.boolean().optional().describe("Include what cannot be added to a graph: Input / Output / Literal / Variable / Template scaffolding, non-Referenceable MetaSounds."),
  bIsOneShot: z.boolean().optional(),
  bRemoveUnusedDependencies: z.boolean().optional().describe("Also drop class dependencies the document no longer uses."),
  bReplaceExisting: z.boolean().optional().describe("Overwrite an existing SoundWave of the same name instead of failing."),
  bSave: z.boolean().optional().describe("Save the imported asset; otherwise it is left unsaved."),
  bSetAsBuildPage: z.boolean().optional().describe("Target the new page on success."),
  builderName: z.string().optional(),
  connections: z.array(z.unknown()).optional(),
  defaults: z.array(z.unknown()).optional(),
  filePath: z.string().optional().describe("The absolute path of the .wav file to write. Missing directories are created."),
  firstResult: z.number().optional().describe("How many to skip."),
  folderPath: z.string().optional().describe("The content-browser folder to create the asset in."),
  inputs: z.array(z.unknown()).optional().describe("Keep classes with these input types. Entries AND; \"|\" ORs within one, e.g. [\"Audio\", \"Float|Int32\"]. Exact, case-insensitive: \"Float\" misses \"Float:Array\"."),
  interfaceName: z.string().optional(),
  maxPerGroup: z.number().optional().describe("Variants or suffixes to list per group, applied only where it at least halves the list. 0 or less lists all."),
  maxResults: z.number().optional().describe("How many to return. 0 or less returns everything from FirstResult on."),
  name: z.string().optional(),
  namespace: z.string().optional(),
  node: z.string().optional(),
  nodes: z.array(z.unknown()).optional(),
  outputFormat: z.string().optional().describe("Value display names: FiveDotOne = \"5.1\", SevenDotOne = \"7.1\"."),
  outputs: z.array(z.unknown()).optional().describe("Same, for outputs."),
  packagePath: z.string().optional(),
  page: z.string().optional().describe("Fails the call unless it names the targeted page. Empty skips the check."),
  pageA: z.string().optional(),
  pageB: z.string().optional(),
  pattern: z.string().optional().describe("Whitespace-separated terms, each a case-insensitive substring of the asset path or of the path the Content Browser shows for it. Terms AND. Empty matches every…"),
  pins: z.array(z.unknown()).optional(),
  presetName: z.string().optional(),
  referencedPatchPath: z.string().optional(),
  referencedSourcePath: z.string().optional(),
  soundWave: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The SoundWave to export."),
  sourceFile: z.string().optional().describe("The absolute path to the source audio file on disk."),
  variables: z.array(z.unknown()).optional(),
  variant: z.string().optional(),
};
