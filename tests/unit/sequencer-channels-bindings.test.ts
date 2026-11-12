import { describe, expect, it } from "vitest";
import { editorTool } from "../../src/tools/editor.js";
import { classifyAction } from "../../src/dispatch/locking.js";
import { readHandlerFile } from "../../scripts/lib/cpp-registrations.mjs";

const channels = readHandlerFile("SequencerHandlers_Channels.cpp");
const bindings = readHandlerFile("SequencerHandlers_Bindings.cpp");
const sequencer = readHandlerFile("SequencerHandlers.cpp");

function body(source: string, fn: string, next?: string): string {
  const start = source.indexOf(`FSequencerHandlers::${fn}(`);
  expect(start).toBeGreaterThan(-1);
  const end = next ? source.indexOf(`FSequencerHandlers::${next}(`, start) : source.length;
  return source.slice(start, end > start ? end : source.length);
}

describe("sequencer channel actions (#1407)", () => {
  it("advertises the channel actions with their C++ parameters", () => {
    const read = editorTool.actions.get_sequence_channel_keys;
    expect(read.description).toContain("Params: sequencePath (or assetPath, or path), trackType?, trackName?, trackIndex?");
    expect(read.description).toContain("sampleStartFrame?");
    const write = editorTool.actions.set_sequence_channel_keys;
    expect(write.description).toContain("channel, keyframes?, interpolation?, clearExisting?, defaultValue?, clearDefault?, parameterType?");
    expect(editorTool.schema.parameterType.safeParse("color").success).toBe(true);
    expect(editorTool.schema.materialParameterCollection.safeParse("/Game/MPC_Ocean").success).toBe(true);
  });

  it("classifies the reader as a read and the writer as a mutation", () => {
    expect(classifyAction("editor.get_sequence_channel_keys", { sequencePath: "/Game/S", trackType: "MPC" }).mutates).toBe(false);
    expect(classifyAction("editor.set_sequence_channel_keys", { sequencePath: "/Game/S", trackType: "MPC", channel: "Intensity" }).mutates).toBe(true);
    expect(classifyAction("editor.remove_sequence_section", { sequencePath: "/Game/S", trackType: "Transform", sectionIndex: 1 }).mutates).toBe(true);
  });

  it("reads keys without writing to the section", () => {
    const get = body(channels, "GetChannelKeys", "SetChannelKeys");
    for (const write of ["Modify()", "MarkPackageDirty", "WriteKey(", "ClearKeys("]) expect(get).not.toContain(write);
    expect(get).toContain("EvaluateChannel(");
  });

  it("validates every key before it touches the section, and reads the channel back", () => {
    const set = body(channels, "SetChannelKeys", "RemoveSection");
    const firstWrite = set.indexOf("R.Section->Modify()");
    for (const guard of ["needs a time", "must be a number or a boolean", "is not cubic, linear or constant"]) {
      expect(set.indexOf(guard)).toBeGreaterThan(-1);
      expect(set.indexOf(guard)).toBeLessThan(firstWrite);
    }
    expect(set).toContain("keysVerified");
    expect(set).toContain('MCPSetRollback(Result, TEXT("set_sequence_channel_keys")');
  });

  it("names parameter-section channels by parameter, so MPC curves are addressable", () => {
    const header = readHandlerFile("HandlerSequencerTracks.h");
    for (const name of ["GetScalarParameterNamesAndCurves", "GetColorParameterNamesAndCurves", 'TEXT(".R")', "MaterialParameterCollection"]) {
      expect(header).toContain(name);
    }
  });

  it("keys with constant interpolation in set_sequence_keyframes (#1232)", () => {
    const keys = body(sequencer, "SetKeyframes");
    expect(keys).toContain("AddConstantKey");
    expect(keys).toContain("Unknown interpolation");
  });

  it("removes a section inside a transaction whose undo is the rollback (#1232)", () => {
    const remove = body(channels, "RemoveSection");
    expect(remove).toContain("FScopedTransaction");
    expect(remove).toContain('MCPSetRollback(Result, TEXT("undo_redo_steps")');
    expect(remove).toContain("Pass sectionIndex");
  });
});

describe("sequencer binding actions (#1356, #1232)", () => {
  it("advertises rebind, resolve and runtime bindings", () => {
    expect(editorTool.actions.rebind_possessable.description).toContain("bindingGuid?, bindingName?, actorLabel?, actorPath?, includeChildren?, componentMap?, renameBinding?");
    expect(classifyAction("editor.resolve_sequence_bindings", { sequencePath: "/Game/S" }).mutates).toBe(false);
    expect(classifyAction("editor.get_sequence_runtime_bindings", {}).mutates).toBe(false);
    expect(classifyAction("editor.rebind_possessable", { sequencePath: "/Game/S", bindingName: "Cam", actorLabel: "Cam" }).mutates).toBe(true);
  });

  it("keeps the binding guid and verifies the retarget by resolving it again", () => {
    const rebind = body(bindings, "RebindPossessable", "GetRuntimeBindings");
    expect(rebind).toContain("UnbindPossessableObjects(Guid)");
    expect(rebind).toContain("BindPossessableObject(Guid, *Actor, World)");
    expect(rebind).not.toContain("AddPossessable");
    expect(rebind).not.toContain("RemovePossessable");
    const bind = rebind.indexOf("BindPossessableObject(Guid, *Actor, World)");
    const verify = rebind.indexOf("const TArray<UObject*> AfterObjects = LocateInWorld(");
    expect(verify).toBeGreaterThan(bind);
    expect(rebind).toContain("AfterObjects[0] == Actor");
  });

  it("asks each runtime player, not the editor, what a binding resolved to", () => {
    const runtime = body(bindings, "GetRuntimeBindings", "GetBindingComponents");
    expect(runtime).toContain("Player->GetBoundObjects(");
    expect(runtime).toContain("TransformOrigin");
  });
});

describe("sequence binding components (#1640)", () => {
  it("advertises a read action with its C++ parameters", () => {
    expect(editorTool.actions.get_sequence_binding_components.description).toContain("Params: sequencePath (or assetPath, or path), bindingGuid?, bindingName?, componentClass?, propertyNames?");
    expect(classifyAction("editor.get_sequence_binding_components", { sequencePath: "/Game/S" }).mutates).toBe(false);
  });

  it("reads spawnable templates and possessed actors without opening or evaluating the sequence", () => {
    const read = body(bindings, "GetBindingComponents");
    expect(read).toContain("Spawnable->GetObjectTemplate()");
    expect(read).toContain("ActorTemplate");
    expect(read).toContain("LocateInWorld(Sequence, MovieScene, Guid, World)");
    expect(read).not.toContain("OpenEditorForAsset");
    expect(read).not.toContain("ForceUpdate");
    expect(bindings).toContain('TEXT("bHoldout")');
    expect(bindings).toContain("GetActualComponentTemplate(ActualClass)");
  });
});
