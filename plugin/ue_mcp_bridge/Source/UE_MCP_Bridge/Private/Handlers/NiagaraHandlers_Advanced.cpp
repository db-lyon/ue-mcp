// Niagara authoring depth the stack editor has and the bridge did not:
// dynamic inputs, simulation stages, event handlers, custom HLSL bodies, and
// the missing half of module-stack CRUD (remove / enable).
//
// Split out of NiagaraHandlers.cpp on purpose. Every helper in here carries a
// NiaAdv prefix because the module is a unity build: a second copy of
// ResolveEmitter, GraphOfScript or UsageOfContext in a file that shares a blob
// with NiagaraHandlers.cpp is a redefinition (C2084), and the grouping shifts
// with file count and order, so it would build clean here and break elsewhere.
//
// Deliberately NOT here: configure_simulation_stage / configure_event_handler.
// ExecuteBehavior, NumIterations, IterationSource, SpawnNumber, ExecutionMode
// and SourceEventName are plain UPROPERTYs on objects that already have an
// address, so every add and read below reports the objectPath (and, for the
// event-handler struct, the indexed property path) that asset(set_property)
// writes through. A handler earns its place only when it must call an engine
// function.

#include "NiagaraHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "UObject/UObjectGlobals.h"
#include "EditorScriptingUtilities/Public/EditorAssetLibrary.h"

#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"
#include "NiagaraScript.h"
#include "NiagaraScriptSource.h"
#include "NiagaraGraph.h"
#include "NiagaraTypes.h"
#include "NiagaraCommon.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraSimulationStageBase.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraNodeCustomHlsl.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraNodeInput.h"
#include "EdGraphSchema_Niagara.h"
#include "NiagaraEditorUtilities.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"
#include "ViewModels/Stack/NiagaraParameterHandle.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"

namespace
{
	// ── Addressing ───────────────────────────────────────────────────────────

	/** The four emitter stack contexts a module can live in, spelled once. */
	const TCHAR* NiaAdvValidContexts = TEXT("ParticleSpawn|ParticleUpdate|EmitterSpawn|EmitterUpdate");

	FVersionedNiagaraEmitterData* NiaAdvResolveEmitter(
		UNiagaraSystem* System, const FString& EmitterName, int32 EmitterIndex,
		UNiagaraEmitter*& OutEmitter, FGuid& OutVersion, FString& OutError)
	{
		OutEmitter = nullptr;
		if (!System) { OutError = TEXT("System is null"); return nullptr; }

		const TArray<FNiagaraEmitterHandle>& Handles = System->GetEmitterHandles();
		TArray<FString> Names;
		for (const FNiagaraEmitterHandle& H : Handles) Names.Add(H.GetName().ToString());

		int32 TargetIdx = -1;
		if (!EmitterName.IsEmpty())
		{
			for (int32 i = 0; i < Handles.Num(); ++i)
			{
				if (Names[i].Equals(EmitterName, ESearchCase::IgnoreCase)) { TargetIdx = i; break; }
			}
			if (TargetIdx < 0)
			{
				OutError = FString::Printf(
					TEXT("No emitter named '%s' in '%s'. Emitters present: [%s]. Address one by 'emitterName' or by 'emitterIndex' (0-%d)."),
					*EmitterName, *System->GetPathName(),
					Names.Num() ? *FString::Join(Names, TEXT(", ")) : TEXT("none"),
					FMath::Max(0, Names.Num() - 1));
				return nullptr;
			}
		}
		else if (EmitterIndex >= 0 && EmitterIndex < Handles.Num())
		{
			TargetIdx = EmitterIndex;
		}
		else
		{
			OutError = FString::Printf(
				TEXT("emitterIndex %d is out of range for '%s', which has %d emitter(s): [%s]. Pass 'emitterName' instead when you know it."),
				EmitterIndex, *System->GetPathName(), Names.Num(),
				Names.Num() ? *FString::Join(Names, TEXT(", ")) : TEXT("none"));
			return nullptr;
		}

		FVersionedNiagaraEmitter VE = Handles[TargetIdx].GetInstance();
		OutEmitter = VE.Emitter;
		OutVersion = VE.Version;
		FVersionedNiagaraEmitterData* Data = VE.GetEmitterData();
		if (!Data)
		{
			OutError = FString::Printf(
				TEXT("Emitter '%s' resolved but carries no version data for version %s. The emitter asset may be a stale or unmigrated version."),
				*Names[TargetIdx], *OutVersion.ToString());
		}
		return Data;
	}

	/** Index of the emitter's version-data entry that matches Version. This is
	 *  the array index asset(set_property) needs to reach anything that lives on
	 *  FVersionedNiagaraEmitterData rather than on the UObject. */
	int32 NiaAdvVersionDataIndex(UNiagaraEmitter* Emitter, const FGuid& Version)
	{
		if (!Emitter) return 0;
		const TArray<FNiagaraAssetVersion> Versions = Emitter->GetAllAvailableVersions();
		for (int32 i = 0; i < Versions.Num(); ++i)
		{
			if (Versions[i].VersionGuid == Version) return i;
		}
		return 0;
	}

	UNiagaraGraph* NiaAdvGraphOfScript(UNiagaraScript* Script)
	{
		if (!Script) return nullptr;
		UNiagaraScriptSource* Src = Cast<UNiagaraScriptSource>(Script->GetLatestSource());
		return Src ? Src->NodeGraph : nullptr;
	}

	/** The one graph every script on an emitter shares. Output nodes, not
	 *  graphs, are what separate a spawn script from a sim stage. */
	UNiagaraGraph* NiaAdvEmitterGraph(FVersionedNiagaraEmitterData* Data)
	{
		if (!Data) return nullptr;
		UNiagaraScriptSource* Src = Cast<UNiagaraScriptSource>(Data->GraphSource);
		return Src ? Src->NodeGraph : nullptr;
	}

	struct FNiaAdvStackSlot
	{
		FString Context;
		UNiagaraScript* Script = nullptr;
		ENiagaraScriptUsage Usage = ENiagaraScriptUsage::ParticleSpawnScript;
	};

	bool NiaAdvResolveContext(FVersionedNiagaraEmitterData* Data, const FString& Ctx, FNiaAdvStackSlot& Out)
	{
		if (!Data) return false;
		if (Ctx.Equals(TEXT("ParticleSpawn"), ESearchCase::IgnoreCase))
		{ Out = { TEXT("ParticleSpawn"), Data->SpawnScriptProps.Script, ENiagaraScriptUsage::ParticleSpawnScript }; return true; }
		if (Ctx.Equals(TEXT("ParticleUpdate"), ESearchCase::IgnoreCase))
		{ Out = { TEXT("ParticleUpdate"), Data->UpdateScriptProps.Script, ENiagaraScriptUsage::ParticleUpdateScript }; return true; }
		if (Ctx.Equals(TEXT("EmitterSpawn"), ESearchCase::IgnoreCase))
		{ Out = { TEXT("EmitterSpawn"), Data->EmitterSpawnScriptProps.Script, ENiagaraScriptUsage::EmitterSpawnScript }; return true; }
		if (Ctx.Equals(TEXT("EmitterUpdate"), ESearchCase::IgnoreCase))
		{ Out = { TEXT("EmitterUpdate"), Data->EmitterUpdateScriptProps.Script, ENiagaraScriptUsage::EmitterUpdateScript }; return true; }
		return false;
	}

	void NiaAdvAllContexts(FVersionedNiagaraEmitterData* Data, const FString& Filter, TArray<FNiaAdvStackSlot>& Out)
	{
		static const TCHAR* All[] = { TEXT("ParticleSpawn"), TEXT("ParticleUpdate"), TEXT("EmitterSpawn"), TEXT("EmitterUpdate") };
		const bool bAll = Filter.IsEmpty() || Filter.Equals(TEXT("all"), ESearchCase::IgnoreCase);
		for (const TCHAR* Name : All)
		{
			if (!bAll && !Filter.Equals(Name, ESearchCase::IgnoreCase)) continue;
			FNiaAdvStackSlot Slot;
			if (NiaAdvResolveContext(Data, Name, Slot) && Slot.Script) Out.Add(Slot);
		}
	}

	// ── Graph shape ──────────────────────────────────────────────────────────

	bool NiaAdvIsParameterMapPin(const UEdGraphPin* Pin)
	{
		if (!Pin) return false;
		return UEdGraphSchema_Niagara::PinToTypeDefinition(Pin) == FNiagaraTypeDefinition::GetParameterMapDef();
	}

	UEdGraphPin* NiaAdvMapPin(UEdGraphNode* Node, EEdGraphPinDirection Direction)
	{
		if (!Node) return nullptr;
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->Direction == Direction && NiaAdvIsParameterMapPin(Pin)) return Pin;
		}
		return nullptr;
	}

	/** UNiagaraNodeParameterMapSet lives in NiagaraEditor/Private, so its header
	 *  cannot be included from a plugin. The class is still reachable by name
	 *  through the UClass chain, which is all the override-map walk needs. */
	bool NiaAdvIsMapSetNode(const UEdGraphNode* Node)
	{
		if (!Node) return false;
		for (const UClass* C = Node->GetClass(); C; C = C->GetSuperClass())
		{
			if (C->GetName() == TEXT("NiagaraNodeParameterMapSet")) return true;
		}
		return false;
	}

	/** The aliased override-pin name for one module input: an input named
	 *  "Module.SpawnRate" on a function call named "SpawnRate" is overridden
	 *  through a pin called "SpawnRate.SpawnRate". */
	FName NiaAdvAliasedHandle(const FString& FullInputName, const UNiagaraNodeFunctionCall& FC)
	{
		return FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(
			FName(*FullInputName), FName(*FC.GetFunctionName())).GetParameterHandleString();
	}

	/** Every override pin currently authored for one function-call node, found
	 *  by walking the graph's map-set nodes rather than through
	 *  GetStackFunctionInputOverridePin, which NiagaraEditor does not export. */
	void NiaAdvCollectOverridePins(UNiagaraGraph* Graph, const UNiagaraNodeFunctionCall& FC, TArray<UEdGraphPin*>& Out)
	{
		if (!Graph) return;
		const FString Prefix = FC.GetFunctionName() + TEXT(".");
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!NiaAdvIsMapSetNode(Node)) continue;
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (!Pin || Pin->Direction != EGPD_Input) continue;
				if (Pin->PinName.ToString().StartsWith(Prefix, ESearchCase::IgnoreCase)) Out.Add(Pin);
			}
		}
	}

	UEdGraphPin* NiaAdvFindOverridePin(UNiagaraGraph* Graph, const FName& AliasedName)
	{
		if (!Graph) return nullptr;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!NiaAdvIsMapSetNode(Node)) continue;
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (Pin && Pin->Direction == EGPD_Input && Pin->PinName == AliasedName) return Pin;
			}
		}
		return nullptr;
	}

	UNiagaraNodeFunctionCall* NiaAdvFindModule(UNiagaraGraph* Graph, const FString& ModuleName, TArray<FString>& OutSeen)
	{
		if (!Graph) return nullptr;
		UNiagaraNodeFunctionCall* Match = nullptr;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			UNiagaraNodeFunctionCall* FC = Cast<UNiagaraNodeFunctionCall>(Node);
			if (!FC) continue;
			OutSeen.AddUnique(FC->GetFunctionName());
			if (!Match && FC->GetFunctionName().Equals(ModuleName, ESearchCase::IgnoreCase)) Match = FC;
		}
		return Match;
	}

	/** The module nodes wired into one output node, in stack order. Walks the
	 *  parameter-map chain backwards from the output node, because
	 *  GetOrderedModuleNodes is not exported. */
	void NiaAdvOrderedModules(UNiagaraNodeOutput* OutputNode, TArray<UNiagaraNodeFunctionCall*>& Out)
	{
		if (!OutputNode) return;
		TArray<UNiagaraNodeFunctionCall*> Reversed;
		UEdGraphPin* Cursor = NiaAdvMapPin(OutputNode, EGPD_Input);
		int32 Guard = 0;
		while (Cursor && Cursor->LinkedTo.Num() > 0 && Guard++ < 512)
		{
			UEdGraphPin* Upstream = Cursor->LinkedTo[0];
			UEdGraphNode* Node = Upstream ? Upstream->GetOwningNode() : nullptr;
			if (!Node) break;
			if (UNiagaraNodeFunctionCall* FC = Cast<UNiagaraNodeFunctionCall>(Node)) Reversed.Add(FC);
			Cursor = NiaAdvMapPin(Node, EGPD_Input);
		}
		for (int32 i = Reversed.Num() - 1; i >= 0; --i) Out.Add(Reversed[i]);
	}

	/** The nodes that feed Root and nothing else. Used to delete a dynamic-input
	 *  subtree or a whole simulation-stage chain without touching a node the
	 *  rest of the graph still reads. */
	void NiaAdvCollectExclusiveUpstream(UEdGraphNode* Root, TSet<UEdGraphNode*>& Out)
	{
		if (!Root) return;
		TSet<UEdGraphNode*> Reachable;
		TArray<UEdGraphNode*> Frontier;
		Frontier.Add(Root);
		int32 Guard = 0;
		while (Frontier.Num() > 0 && Guard++ < 4096)
		{
			UEdGraphNode* Node = Frontier.Pop();
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (!Pin || Pin->Direction != EGPD_Input) continue;
				for (UEdGraphPin* Linked : Pin->LinkedTo)
				{
					UEdGraphNode* Upstream = Linked ? Linked->GetOwningNode() : nullptr;
					if (Upstream && Upstream != Root && !Reachable.Contains(Upstream))
					{
						Reachable.Add(Upstream);
						Frontier.Add(Upstream);
					}
				}
			}
		}
		// Keep only nodes whose every consumer is inside the set. A node the
		// wider stack still reads is shared, and deleting it would silently
		// break an unrelated module.
		for (UEdGraphNode* Candidate : Reachable)
		{
			bool bExclusive = true;
			for (UEdGraphPin* Pin : Candidate->Pins)
			{
				if (!Pin || Pin->Direction != EGPD_Output) continue;
				for (UEdGraphPin* Linked : Pin->LinkedTo)
				{
					UEdGraphNode* Consumer = Linked ? Linked->GetOwningNode() : nullptr;
					if (Consumer && Consumer != Root && !Reachable.Contains(Consumer)) { bExclusive = false; break; }
				}
				if (!bExclusive) break;
			}
			if (bExclusive) Out.Add(Candidate);
		}
	}

	// ── Custom HLSL ──────────────────────────────────────────────────────────
	//
	// UNiagaraNodeCustomHlsl is UCLASS(MinimalAPI) and Get/SetCustomHlsl are not
	// exported, and the CustomHlsl UPROPERTY itself is private. Reflection is
	// the only route from a plugin, and it is the same route the existing
	// create_niagara_module_from_hlsl write already takes.

	FStrProperty* NiaAdvHlslProperty(UNiagaraNodeCustomHlsl* Node)
	{
		if (!Node) return nullptr;
		return CastField<FStrProperty>(Node->GetClass()->FindPropertyByName(TEXT("CustomHlsl")));
	}

	FString NiaAdvReadHlsl(UNiagaraNodeCustomHlsl* Node)
	{
		FStrProperty* Prop = NiaAdvHlslProperty(Node);
		return Prop ? Prop->GetPropertyValue(Prop->ContainerPtrToValuePtr<void>(Node)) : FString();
	}

	void NiaAdvCollectHlslNodes(UNiagaraGraph* Graph, TArray<UNiagaraNodeCustomHlsl*>& Out)
	{
		if (!Graph) return;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (UNiagaraNodeCustomHlsl* Custom = Cast<UNiagaraNodeCustomHlsl>(Node)) Out.Add(Custom);
		}
	}

	// ── Reporting ────────────────────────────────────────────────────────────

	TSharedPtr<FJsonObject> NiaAdvPinJson(const UEdGraphPin* Pin)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		if (!Pin) return O;
		O->SetStringField(TEXT("name"), Pin->PinName.ToString());
		O->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("input") : TEXT("output"));
		O->SetStringField(TEXT("type"), UEdGraphSchema_Niagara::PinToTypeDefinition(Pin).GetName());
		O->SetStringField(TEXT("defaultValue"), Pin->DefaultValue);
		O->SetBoolField(TEXT("linked"), Pin->LinkedTo.Num() > 0);
		return O;
	}

	/** What is wired into one override pin, and what is wired into that in turn.
	 *  This is the tree no property read can produce: a dynamic input is a
	 *  function-call node hanging off a pin, and its own inputs are more of the
	 *  same one level down. */
	TSharedPtr<FJsonObject> NiaAdvDescribeOverride(UNiagaraGraph* Graph, UEdGraphPin* OverridePin, int32 Depth);

	TSharedPtr<FJsonObject> NiaAdvDescribeDynamicNode(UNiagaraGraph* Graph, UEdGraphNode* Node, int32 Depth)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("nodeClass"), Node->GetClass()->GetName());
		O->SetStringField(TEXT("nodeGuid"), Node->NodeGuid.ToString());

		if (UNiagaraNodeCustomHlsl* Custom = Cast<UNiagaraNodeCustomHlsl>(Node))
		{
			O->SetStringField(TEXT("kind"), TEXT("customHlsl"));
			const FString Body = NiaAdvReadHlsl(Custom);
			O->SetStringField(TEXT("hlsl"), Body);
			O->SetNumberField(TEXT("hlslLength"), Body.Len());
			return O;
		}
		if (UNiagaraNodeFunctionCall* FC = Cast<UNiagaraNodeFunctionCall>(Node))
		{
			O->SetStringField(TEXT("kind"), TEXT("dynamicInput"));
			O->SetStringField(TEXT("functionName"), FC->GetFunctionName());
			O->SetStringField(TEXT("scriptPath"), FC->FunctionScript ? FC->FunctionScript->GetPathName() : FString());
			O->SetBoolField(TEXT("enabled"), Node->GetDesiredEnabledState() != ENodeEnabledState::Disabled);

			if (Depth < 8)
			{
				TArray<UEdGraphPin*> Nested;
				NiaAdvCollectOverridePins(Graph, *FC, Nested);
				TArray<TSharedPtr<FJsonValue>> NestedArr;
				for (UEdGraphPin* Pin : Nested)
				{
					NestedArr.Add(MakeShared<FJsonValueObject>(NiaAdvDescribeOverride(Graph, Pin, Depth + 1)));
				}
				if (NestedArr.Num() > 0) O->SetArrayField(TEXT("nestedOverrides"), NestedArr);
			}
			else
			{
				O->SetBoolField(TEXT("truncatedAtDepthLimit"), true);
			}
			return O;
		}
		O->SetStringField(TEXT("kind"), TEXT("linkedNode"));
		return O;
	}

	TSharedPtr<FJsonObject> NiaAdvDescribeOverride(UNiagaraGraph* Graph, UEdGraphPin* OverridePin, int32 Depth)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		if (!OverridePin) return O;
		O->SetStringField(TEXT("overridePin"), OverridePin->PinName.ToString());
		const FNiagaraParameterHandle Handle(OverridePin->PinName);
		O->SetStringField(TEXT("inputName"), Handle.GetName().ToString());
		O->SetStringField(TEXT("type"), UEdGraphSchema_Niagara::PinToTypeDefinition(OverridePin).GetName());

		if (OverridePin->LinkedTo.Num() == 0)
		{
			O->SetStringField(TEXT("kind"), TEXT("localValue"));
			O->SetStringField(TEXT("value"), OverridePin->DefaultValue);
			return O;
		}
		UEdGraphPin* Source = OverridePin->LinkedTo[0];
		UEdGraphNode* Node = Source ? Source->GetOwningNode() : nullptr;
		if (!Node)
		{
			O->SetStringField(TEXT("kind"), TEXT("danglingLink"));
			return O;
		}
		TSharedPtr<FJsonObject> Wired = NiaAdvDescribeDynamicNode(Graph, Node, Depth);
		FString Kind;
		Wired->TryGetStringField(TEXT("kind"), Kind);
		O->SetStringField(TEXT("kind"), Kind);
		O->SetObjectField(TEXT("wiredTo"), Wired);
		return O;
	}

	/** Compile the system and write it back. Every mutating handler ends here so
	 *  the asset a follow-up read opens is the asset that was written. */
	void NiaAdvFinalize(UNiagaraSystem* System, UNiagaraEmitter* Emitter, UNiagaraGraph* Graph)
	{
		if (Graph) Graph->NotifyGraphChanged();
		if (Emitter) Emitter->PostEditChange();
		if (System)
		{
			System->PostEditChange();
			System->RequestCompile(false);
			UEditorAssetLibrary::SaveLoadedAsset(System);
		}
	}

	/** The addressing every action in this file shares, echoed back so a caller
	 *  can build the inverse call from the response alone. */
	void NiaAdvEchoAddress(TSharedPtr<FJsonObject> Obj, const FString& SystemPath,
		const FString& EmitterName, int32 EmitterIndex, UNiagaraEmitter* Emitter)
	{
		Obj->SetStringField(TEXT("systemPath"), SystemPath);
		Obj->SetStringField(TEXT("emitterName"), EmitterName.IsEmpty() && Emitter ? Emitter->GetName() : EmitterName);
		Obj->SetNumberField(TEXT("emitterIndex"), EmitterIndex);
		if (Emitter) Obj->SetStringField(TEXT("emitterObjectPath"), Emitter->GetPathName());
	}

	TSharedPtr<FJsonObject> NiaAdvAddressPayload(const FString& SystemPath, const FString& EmitterName, int32 EmitterIndex)
	{
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("systemPath"), SystemPath);
		if (!EmitterName.IsEmpty()) P->SetStringField(TEXT("emitterName"), EmitterName);
		P->SetNumberField(TEXT("emitterIndex"), EmitterIndex);
		return P;
	}

	/** Create the output node and its parameter-map input node for a script
	 *  usage that has no chain yet. ResetGraphForOutput is the engine's version
	 *  of this and is not exported from NiagaraEditor, so the two nodes are
	 *  built directly: an output node stamped with the usage and a fresh usage
	 *  id, an input node carrying the parameter map, and one link between them.
	 *  Until this exists, the struct fields on a simulation stage or event
	 *  handler point at a script that can never compile. */
	UNiagaraNodeOutput* NiaAdvCreateOutputChain(UNiagaraGraph& Graph, ENiagaraScriptUsage Usage, const FGuid& UsageId)
	{
		Graph.Modify();

		UNiagaraNodeOutput* OutputNode = nullptr;
		{
			FGraphNodeCreator<UNiagaraNodeOutput> Creator(Graph);
			OutputNode = Creator.CreateNode();
			OutputNode->SetUsage(Usage);
			OutputNode->SetUsageId(UsageId);
			OutputNode->Outputs.Reset();
			OutputNode->Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetParameterMapDef(), TEXT("OutParamMap")));
			Creator.Finalize();
		}

		UNiagaraNodeInput* InputNode = nullptr;
		{
			FGraphNodeCreator<UNiagaraNodeInput> Creator(Graph);
			InputNode = Creator.CreateNode();
			InputNode->Input = FNiagaraVariable(FNiagaraTypeDefinition::GetParameterMapDef(), TEXT("InputMap"));
			InputNode->Usage = ENiagaraInputNodeUsage::Parameter;
			Creator.Finalize();
		}

		UEdGraphPin* From = InputNode ? InputNode->GetOutputPin(0) : nullptr;
		UEdGraphPin* To = OutputNode ? OutputNode->GetInputPin(0) : nullptr;
		if (From && To) From->MakeLinkTo(To);
		Graph.NotifyGraphChanged();
		return OutputNode;
	}

	UNiagaraNodeOutput* NiaAdvFindOutputNode(UNiagaraGraph* Graph, ENiagaraScriptUsage Usage, const FGuid& UsageId)
	{
		if (!Graph) return nullptr;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			UNiagaraNodeOutput* Out = Cast<UNiagaraNodeOutput>(Node);
			if (Out && Out->GetUsage() == Usage && Out->GetUsageId() == UsageId) return Out;
		}
		return nullptr;
	}

	/** Delete an output node and everything that feeds only it. */
	int32 NiaAdvDeleteOutputChain(UNiagaraGraph* Graph, UNiagaraNodeOutput* OutputNode)
	{
		if (!Graph || !OutputNode) return 0;
		TSet<UEdGraphNode*> Doomed;
		NiaAdvCollectExclusiveUpstream(OutputNode, Doomed);
		Graph->Modify();
		int32 Removed = 0;
		for (UEdGraphNode* Node : Doomed)
		{
			if (Node) { Graph->RemoveNode(Node); ++Removed; }
		}
		Graph->RemoveNode(OutputNode);
		++Removed;
		Graph->NotifyGraphChanged();
		return Removed;
	}

	/** Build a script that a simulation stage or event handler can point at:
	 *  the UNiagaraScript object, its usage and fresh usage id, the emitter's
	 *  shared graph as its source, and the output node that makes the usage id
	 *  mean something. */
	UNiagaraScript* NiaAdvCreateStageScript(
		UNiagaraEmitter* Emitter, FVersionedNiagaraEmitterData* Data,
		ENiagaraScriptUsage Usage, const TCHAR* NameHint, FGuid& OutUsageId, FString& OutError)
	{
		UNiagaraGraph* Graph = NiaAdvEmitterGraph(Data);
		if (!Graph)
		{
			OutError = TEXT("Emitter has no editor graph source (GraphSource is null or not a UNiagaraScriptSource). The emitter asset cannot be authored through the bridge.");
			return nullptr;
		}
		const FName ScriptName = MakeUniqueObjectName(Emitter, UNiagaraScript::StaticClass(), FName(NameHint));
		UNiagaraScript* Script = NewObject<UNiagaraScript>(Emitter, ScriptName, RF_Transactional);
		if (!Script) { OutError = TEXT("Failed to construct the backing UNiagaraScript"); return nullptr; }

		OutUsageId = FGuid::NewGuid();
		Script->SetUsage(Usage);
		Script->SetUsageId(OutUsageId);
		Script->SetLatestSource(Data->GraphSource);

		if (!NiaAdvCreateOutputChain(*Graph, Usage, OutUsageId))
		{
			OutError = TEXT("Failed to create the output node for the new script usage");
			return nullptr;
		}
		return Script;
	}
}

// ===========================================================================
// Dynamic inputs
// ===========================================================================

TSharedPtr<FJsonValue> FNiagaraHandlers::ListDynamicInputs(const TSharedPtr<FJsonObject>& Params)
{
	FString SystemPath;
	if (auto Err = RequireString(Params, TEXT("systemPath"), SystemPath)) return Err;
	const FString EmitterName = OptionalString(Params, TEXT("emitterName"), TEXT(""));
	const int32 EmitterIndex = OptionalInt(Params, TEXT("emitterIndex"), 0);
	const FString StackContext = OptionalString(Params, TEXT("stackContext"), TEXT("all"));
	const FString ModuleFilter = OptionalString(Params, TEXT("moduleName"), TEXT(""));

	UNiagaraSystem* System = Cast<UNiagaraSystem>(UEditorAssetLibrary::LoadAsset(SystemPath));
	if (!System) return MCPError(FString::Printf(TEXT("Niagara system not found: '%s'. Pass a system asset path; niagara(list_niagara_systems) reports every one in the project."), *SystemPath));

	UNiagaraEmitter* Emitter = nullptr;
	FGuid Version;
	FString ResolveError;
	FVersionedNiagaraEmitterData* Data = NiaAdvResolveEmitter(System, EmitterName, EmitterIndex, Emitter, Version, ResolveError);
	if (!Data) return MCPError(ResolveError);

	TArray<FNiaAdvStackSlot> Slots;
	NiaAdvAllContexts(Data, StackContext, Slots);
	if (Slots.Num() == 0)
	{
		return MCPError(FString::Printf(
			TEXT("Unknown stackContext '%s'. Valid values: %s, or 'all'."), *StackContext, NiaAdvValidContexts));
	}

	TArray<TSharedPtr<FJsonValue>> ModuleRows;
	TArray<FString> SeenModules;
	int32 TotalDynamic = 0;

	for (const FNiaAdvStackSlot& Slot : Slots)
	{
		UNiagaraGraph* Graph = NiaAdvGraphOfScript(Slot.Script);
		if (!Graph) continue;
		UNiagaraNodeOutput* OutputNode = NiaAdvFindOutputNode(Graph, Slot.Usage, FGuid());
		TArray<UNiagaraNodeFunctionCall*> Modules;
		NiaAdvOrderedModules(OutputNode, Modules);

		for (int32 Index = 0; Index < Modules.Num(); ++Index)
		{
			UNiagaraNodeFunctionCall* FC = Modules[Index];
			SeenModules.AddUnique(FC->GetFunctionName());
			if (!ModuleFilter.IsEmpty() && !FC->GetFunctionName().Equals(ModuleFilter, ESearchCase::IgnoreCase)) continue;

			TArray<UEdGraphPin*> Overrides;
			NiaAdvCollectOverridePins(Graph, *FC, Overrides);

			TArray<TSharedPtr<FJsonValue>> OverrideRows;
			for (UEdGraphPin* Pin : Overrides)
			{
				TSharedPtr<FJsonObject> Row = NiaAdvDescribeOverride(Graph, Pin, 0);
				FString Kind;
				Row->TryGetStringField(TEXT("kind"), Kind);
				if (Kind == TEXT("dynamicInput") || Kind == TEXT("customHlsl")) ++TotalDynamic;
				OverrideRows.Add(MakeShared<FJsonValueObject>(Row));
			}

			TSharedPtr<FJsonObject> ModObj = MakeShared<FJsonObject>();
			ModObj->SetStringField(TEXT("stackContext"), Slot.Context);
			ModObj->SetNumberField(TEXT("stackIndex"), Index);
			ModObj->SetStringField(TEXT("moduleName"), FC->GetFunctionName());
			ModObj->SetStringField(TEXT("moduleScript"), FC->FunctionScript ? FC->FunctionScript->GetPathName() : FString());
			ModObj->SetBoolField(TEXT("enabled"), FC->GetDesiredEnabledState() != ENodeEnabledState::Disabled);
			ModObj->SetArrayField(TEXT("overrides"), OverrideRows);
			ModObj->SetNumberField(TEXT("overrideCount"), OverrideRows.Num());
			ModuleRows.Add(MakeShared<FJsonValueObject>(ModObj));
		}
	}

	if (!ModuleFilter.IsEmpty() && ModuleRows.Num() == 0)
	{
		return MCPError(FString::Printf(
			TEXT("No module named '%s' in stackContext '%s' of emitter '%s'. Modules present: [%s]. Add one with niagara(add_niagara_module)."),
			*ModuleFilter, *StackContext, Emitter ? *Emitter->GetName() : TEXT("?"),
			SeenModules.Num() ? *FString::Join(SeenModules, TEXT(", ")) : TEXT("none")));
	}

	TSharedPtr<FJsonObject> Res = MCPSuccess();
	NiaAdvEchoAddress(Res, SystemPath, EmitterName, EmitterIndex, Emitter);
	Res->SetStringField(TEXT("stackContext"), StackContext);
	Res->SetArrayField(TEXT("modules"), ModuleRows);
	Res->SetNumberField(TEXT("moduleCount"), ModuleRows.Num());
	Res->SetNumberField(TEXT("dynamicInputCount"), TotalDynamic);
	Res->SetArrayField(TEXT("modulesSeen"), MCPStringListToJson(SeenModules));
	Res->SetStringField(TEXT("note"),
		TEXT("'overrides' is the authored override map, not the module's full input list. kind=localValue is a plain value, kind=dynamicInput is a wired script (its own inputs are under nestedOverrides), kind=customHlsl is an inline expression. Use niagara(list_niagara_module_inputs) for the inputs that have no override at all."));
	return MCPResult(Res);
}

TSharedPtr<FJsonValue> FNiagaraHandlers::SetDynamicInput(const TSharedPtr<FJsonObject>& Params)
{
	FString SystemPath, StackContext, ModuleName, InputName, DynamicInputScript;
	if (auto Err = RequireString(Params, TEXT("systemPath"), SystemPath)) return Err;
	if (auto Err = RequireString(Params, TEXT("stackContext"), StackContext)) return Err;
	if (auto Err = RequireString(Params, TEXT("moduleName"), ModuleName)) return Err;
	if (auto Err = RequireString(Params, TEXT("inputName"), InputName)) return Err;
	if (auto Err = RequireString(Params, TEXT("dynamicInputScript"), DynamicInputScript)) return Err;
	const FString EmitterName = OptionalString(Params, TEXT("emitterName"), TEXT(""));
	const int32 EmitterIndex = OptionalInt(Params, TEXT("emitterIndex"), 0);

	if (MCPIsProtectedAssetPath(SystemPath)) return MCPProtectedPathError(SystemPath);

	UNiagaraSystem* System = Cast<UNiagaraSystem>(UEditorAssetLibrary::LoadAsset(SystemPath));
	if (!System) return MCPError(FString::Printf(TEXT("Niagara system not found: '%s'."), *SystemPath));

	UNiagaraEmitter* Emitter = nullptr;
	FGuid Version;
	FString ResolveError;
	FVersionedNiagaraEmitterData* Data = NiaAdvResolveEmitter(System, EmitterName, EmitterIndex, Emitter, Version, ResolveError);
	if (!Data) return MCPError(ResolveError);

	FNiaAdvStackSlot Slot;
	if (!NiaAdvResolveContext(Data, StackContext, Slot) || !Slot.Script)
	{
		return MCPError(FString::Printf(TEXT("Unknown stackContext '%s'. Valid values: %s."), *StackContext, NiaAdvValidContexts));
	}
	UNiagaraGraph* Graph = NiaAdvGraphOfScript(Slot.Script);
	if (!Graph) return MCPError(FString::Printf(TEXT("The %s script on emitter '%s' has no editor graph."), *Slot.Context, Emitter ? *Emitter->GetName() : TEXT("?")));

	TArray<FString> SeenModules;
	UNiagaraNodeFunctionCall* FC = NiaAdvFindModule(Graph, ModuleName, SeenModules);
	if (!FC)
	{
		return MCPError(FString::Printf(
			TEXT("No module named '%s' in the %s stack. Modules present: [%s]. Add one with niagara(add_niagara_module)."),
			*ModuleName, *Slot.Context, SeenModules.Num() ? *FString::Join(SeenModules, TEXT(", ")) : TEXT("none")));
	}

	// Resolve the input against the module script's real inputs, not the node's
	// pins: the settable values live in the override map, and a name that only
	// matches a pin would write somewhere the stack editor never reads.
	FCompileConstantResolver Resolver(FVersionedNiagaraEmitter(Emitter, Version), Slot.Usage);
	TArray<FNiagaraVariable> InputVars;
	FNiagaraStackGraphUtilities::GetStackFunctionInputs(
		*FC, InputVars, Resolver,
		FNiagaraStackGraphUtilities::ENiagaraGetStackFunctionInputPinsOptions::ModuleInputsOnly, false);

	auto LeafOf = [](const FString& N)
	{
		FString Head, Leaf;
		return N.Split(TEXT("."), &Head, &Leaf, ESearchCase::IgnoreCase, ESearchDir::FromEnd) ? Leaf : N;
	};
	const FNiagaraVariable* Found = InputVars.FindByPredicate([&](const FNiagaraVariable& V)
	{
		const FString N = V.GetName().ToString();
		return N.Equals(InputName, ESearchCase::IgnoreCase) || LeafOf(N).Equals(InputName, ESearchCase::IgnoreCase);
	});
	if (!Found)
	{
		TArray<FString> Names;
		for (const FNiagaraVariable& V : InputVars) Names.Add(LeafOf(V.GetName().ToString()));
		return MCPError(FString::Printf(
			TEXT("Module '%s' has no input named '%s'. Its inputs are: [%s]. niagara(list_niagara_module_inputs) reports them with types."),
			*ModuleName, *InputName, Names.Num() ? *FString::Join(Names, TEXT(", ")) : TEXT("none")));
	}

	UNiagaraScript* DynScript = LoadObject<UNiagaraScript>(nullptr, *DynamicInputScript);
	if (!DynScript && !DynamicInputScript.Contains(TEXT(".")))
	{
		FString Left, Leaf;
		if (DynamicInputScript.Split(TEXT("/"), &Left, &Leaf, ESearchCase::IgnoreCase, ESearchDir::FromEnd))
		{
			DynScript = LoadObject<UNiagaraScript>(nullptr, *(DynamicInputScript + TEXT(".") + Leaf));
		}
	}
	if (!DynScript)
	{
		return MCPError(FString::Printf(
			TEXT("Dynamic input script not found: '%s'. Stock dynamic inputs live under /Niagara/DynamicInputs/...; niagara(list_niagara_modules) reports the scripts the project can see."),
			*DynamicInputScript));
	}
	if (DynScript->GetUsage() != ENiagaraScriptUsage::DynamicInput)
	{
		return MCPError(FString::Printf(
			TEXT("'%s' is a Niagara script but its usage is %d, not DynamicInput (%d). Only a dynamic-input script can be wired into a module input; a module script goes through niagara(add_niagara_module) instead."),
			*DynScript->GetPathName(), (int32)DynScript->GetUsage(), (int32)ENiagaraScriptUsage::DynamicInput));
	}

	const FString FullInputName = Found->GetName().ToString();
	const FName Aliased = NiaAdvAliasedHandle(FullInputName, *FC);

	// Idempotency and rollback capture, both before anything is mutated.
	FString PreviousKind = TEXT("none");
	FString PreviousScriptPath;
	FString PreviousLocalValue;
	if (UEdGraphPin* Existing = NiaAdvFindOverridePin(Graph, Aliased))
	{
		PreviousLocalValue = Existing->DefaultValue;
		if (Existing->LinkedTo.Num() > 0)
		{
			UEdGraphNode* Node = Existing->LinkedTo[0] ? Existing->LinkedTo[0]->GetOwningNode() : nullptr;
			if (UNiagaraNodeFunctionCall* PrevFC = Cast<UNiagaraNodeFunctionCall>(Node))
			{
				PreviousKind = TEXT("dynamicInput");
				PreviousScriptPath = PrevFC->FunctionScript ? PrevFC->FunctionScript->GetPathName() : FString();
				if (PreviousScriptPath == DynScript->GetPathName())
				{
					TSharedPtr<FJsonObject> Same = MCPSuccess();
					MCPSetExisted(Same);
					NiaAdvEchoAddress(Same, SystemPath, EmitterName, EmitterIndex, Emitter);
					Same->SetStringField(TEXT("stackContext"), Slot.Context);
					Same->SetStringField(TEXT("moduleName"), FC->GetFunctionName());
					Same->SetStringField(TEXT("inputName"), FullInputName);
					Same->SetStringField(TEXT("dynamicInputScript"), PreviousScriptPath);
					Same->SetStringField(TEXT("dynamicInputName"), PrevFC->GetFunctionName());
					Same->SetStringField(TEXT("note"),
						TEXT("That dynamic input is already wired into this input; nothing was changed. Set its own inputs with niagara(set_niagara_module_input) using moduleName = dynamicInputName."));
					return MCPResult(Same);
				}
			}
			else if (Cast<UNiagaraNodeCustomHlsl>(Node))
			{
				PreviousKind = TEXT("customHlsl");
			}
			else
			{
				PreviousKind = TEXT("linkedParameter");
			}
		}
		else
		{
			PreviousKind = TEXT("localValue");
		}
	}

	Graph->Modify();
	FC->Modify();

	UEdGraphPin& OverridePin = FNiagaraStackGraphUtilities::GetOrCreateStackFunctionInputOverridePin(
		*FC, FNiagaraParameterHandle(Aliased), Found->GetType(), FGuid(), FGuid());

	// Replacing an existing wiring means removing what was there; leaving it
	// linked would make SetDynamicInputForFunctionInput fight an old node and
	// leave an orphan behind in the graph.
	int32 ReplacedNodes = 0;
	if (OverridePin.LinkedTo.Num() > 0)
	{
		UEdGraphNode* Old = OverridePin.LinkedTo[0] ? OverridePin.LinkedTo[0]->GetOwningNode() : nullptr;
		OverridePin.BreakAllPinLinks(true);
		if (Old)
		{
			TSet<UEdGraphNode*> Doomed;
			NiaAdvCollectExclusiveUpstream(Old, Doomed);
			for (UEdGraphNode* Node : Doomed) { Graph->RemoveNode(Node); ++ReplacedNodes; }
			Graph->RemoveNode(Old);
			++ReplacedNodes;
		}
	}

	UNiagaraNodeFunctionCall* NewDynamic = nullptr;
	FNiagaraStackGraphUtilities::SetDynamicInputForFunctionInput(OverridePin, DynScript, NewDynamic, FGuid(), FString(), FGuid());
	if (!NewDynamic)
	{
		return MCPError(FString::Printf(
			TEXT("Niagara refused to wire '%s' into '%s.%s'. The dynamic input's output type must match the input type (%s); check the script's output pin."),
			*DynScript->GetPathName(), *ModuleName, *InputName, *Found->GetType().GetName()));
	}

	FC->MarkNodeRequiresSynchronization(TEXT("MCP_SetDynamicInput"), true);
	NiaAdvFinalize(System, Emitter, Graph);

	TSharedPtr<FJsonObject> Res = MCPSuccess();
	if (PreviousKind == TEXT("dynamicInput") || PreviousKind == TEXT("customHlsl")) MCPSetUpdated(Res);
	else MCPSetCreated(Res);
	NiaAdvEchoAddress(Res, SystemPath, EmitterName, EmitterIndex, Emitter);
	Res->SetStringField(TEXT("stackContext"), Slot.Context);
	Res->SetStringField(TEXT("moduleName"), FC->GetFunctionName());
	Res->SetStringField(TEXT("inputName"), FullInputName);
	Res->SetStringField(TEXT("overridePin"), Aliased.ToString());
	Res->SetStringField(TEXT("inputType"), Found->GetType().GetName());
	Res->SetStringField(TEXT("dynamicInputScript"), DynScript->GetPathName());
	Res->SetStringField(TEXT("dynamicInputName"), NewDynamic->GetFunctionName());
	Res->SetStringField(TEXT("previousKind"), PreviousKind);
	if (!PreviousScriptPath.IsEmpty()) Res->SetStringField(TEXT("previousDynamicInputScript"), PreviousScriptPath);
	if (ReplacedNodes > 0) Res->SetNumberField(TEXT("replacedNodes"), ReplacedNodes);
	Res->SetStringField(TEXT("note"), FString::Printf(
		TEXT("The dynamic input's own inputs are addressed as a module: niagara(set_niagara_module_input) with moduleName='%s'. niagara(list_niagara_dynamic_inputs) shows the resulting tree."),
		*NewDynamic->GetFunctionName()));

	// Rollback. A previous dynamic input can be restored exactly. A previous
	// local value cannot: removing the wiring drops the override pin and the
	// module default comes back, so say which value is lost rather than
	// presenting a lossy inverse as a complete one.
	TSharedPtr<FJsonObject> RbPayload = NiaAdvAddressPayload(SystemPath, EmitterName, EmitterIndex);
	RbPayload->SetStringField(TEXT("stackContext"), Slot.Context);
	RbPayload->SetStringField(TEXT("moduleName"), FC->GetFunctionName());
	RbPayload->SetStringField(TEXT("inputName"), FullInputName);
	if (PreviousKind == TEXT("dynamicInput") && !PreviousScriptPath.IsEmpty())
	{
		RbPayload->SetStringField(TEXT("dynamicInputScript"), PreviousScriptPath);
		MCPSetRollback(Res, TEXT("set_niagara_dynamic_input"), RbPayload);
		Res->SetBoolField(TEXT("rollbackRestoresScriptOnly"), true);
		Res->SetStringField(TEXT("rollbackNote"), FString::Printf(
			TEXT("Rollback rewires '%s' but does not restore the inputs that were set ON that dynamic input; re-set them with niagara(set_niagara_module_input)."),
			*PreviousScriptPath));
	}
	else
	{
		MCPSetRollback(Res, TEXT("remove_niagara_dynamic_input"), RbPayload);
		if (PreviousKind == TEXT("localValue") && !PreviousLocalValue.IsEmpty())
		{
			Res->SetBoolField(TEXT("rollbackRestoresModuleDefaultOnly"), true);
			Res->SetStringField(TEXT("previousLocalValue"), PreviousLocalValue);
			Res->SetStringField(TEXT("rollbackNote"), FString::Printf(
				TEXT("Rollback removes the wiring and restores the module's own default, NOT the local value '%s' that was overridden here. Re-apply it with niagara(set_niagara_module_input)."),
				*PreviousLocalValue));
		}
		else if (PreviousKind == TEXT("customHlsl") || PreviousKind == TEXT("linkedParameter"))
		{
			Res->SetBoolField(TEXT("rollbackRestoresModuleDefaultOnly"), true);
			Res->SetStringField(TEXT("rollbackNote"), FString::Printf(
				TEXT("This input previously carried a %s override, which rollback cannot rebuild; it restores the module default instead."),
				*PreviousKind));
		}
	}
	return MCPResult(Res);
}

TSharedPtr<FJsonValue> FNiagaraHandlers::RemoveDynamicInput(const TSharedPtr<FJsonObject>& Params)
{
	FString SystemPath, StackContext, ModuleName, InputName;
	if (auto Err = RequireString(Params, TEXT("systemPath"), SystemPath)) return Err;
	if (auto Err = RequireString(Params, TEXT("stackContext"), StackContext)) return Err;
	if (auto Err = RequireString(Params, TEXT("moduleName"), ModuleName)) return Err;
	if (auto Err = RequireString(Params, TEXT("inputName"), InputName)) return Err;
	const FString EmitterName = OptionalString(Params, TEXT("emitterName"), TEXT(""));
	const int32 EmitterIndex = OptionalInt(Params, TEXT("emitterIndex"), 0);

	if (MCPIsProtectedAssetPath(SystemPath)) return MCPProtectedPathError(SystemPath);

	UNiagaraSystem* System = Cast<UNiagaraSystem>(UEditorAssetLibrary::LoadAsset(SystemPath));
	if (!System) return MCPError(FString::Printf(TEXT("Niagara system not found: '%s'."), *SystemPath));

	UNiagaraEmitter* Emitter = nullptr;
	FGuid Version;
	FString ResolveError;
	FVersionedNiagaraEmitterData* Data = NiaAdvResolveEmitter(System, EmitterName, EmitterIndex, Emitter, Version, ResolveError);
	if (!Data) return MCPError(ResolveError);

	FNiaAdvStackSlot Slot;
	if (!NiaAdvResolveContext(Data, StackContext, Slot) || !Slot.Script)
	{
		return MCPError(FString::Printf(TEXT("Unknown stackContext '%s'. Valid values: %s."), *StackContext, NiaAdvValidContexts));
	}
	UNiagaraGraph* Graph = NiaAdvGraphOfScript(Slot.Script);
	if (!Graph) return MCPError(TEXT("The addressed script has no editor graph."));

	TArray<FString> SeenModules;
	UNiagaraNodeFunctionCall* FC = NiaAdvFindModule(Graph, ModuleName, SeenModules);
	if (!FC)
	{
		// The module is gone, so its dynamic input is gone with it. That is the
		// state the caller asked for, and reporting it as a failure would break
		// a replayed teardown.
		TSharedPtr<FJsonObject> Gone = MCPSuccess();
		Gone->SetBoolField(TEXT("alreadyRemoved"), true);
		NiaAdvEchoAddress(Gone, SystemPath, EmitterName, EmitterIndex, Emitter);
		Gone->SetStringField(TEXT("stackContext"), Slot.Context);
		Gone->SetStringField(TEXT("moduleName"), ModuleName);
		Gone->SetStringField(TEXT("inputName"), InputName);
		Gone->SetArrayField(TEXT("modulesSeen"), MCPStringListToJson(SeenModules));
		Gone->SetStringField(TEXT("note"), FString::Printf(
			TEXT("No module named '%s' in the %s stack, so no dynamic input on it could exist. Modules present: [%s]."),
			*ModuleName, *Slot.Context, SeenModules.Num() ? *FString::Join(SeenModules, TEXT(", ")) : TEXT("none")));
		return MCPResult(Gone);
	}

	// Match the override pin by its aliased name. The caller may pass either the
	// leaf ("SpawnRate") or the qualified name ("Module.SpawnRate").
	TArray<UEdGraphPin*> Overrides;
	NiaAdvCollectOverridePins(Graph, *FC, Overrides);
	UEdGraphPin* Target = nullptr;
	TArray<FString> OverrideNames;
	for (UEdGraphPin* Pin : Overrides)
	{
		const FNiagaraParameterHandle Handle(Pin->PinName);
		const FString Leaf = Handle.GetName().ToString();
		OverrideNames.Add(Leaf);
		if (!Target && (Leaf.Equals(InputName, ESearchCase::IgnoreCase)
			|| Pin->PinName.ToString().Equals(InputName, ESearchCase::IgnoreCase)
			|| InputName.EndsWith(TEXT(".") + Leaf, ESearchCase::IgnoreCase)))
		{
			Target = Pin;
		}
	}

	if (!Target || Target->LinkedTo.Num() == 0)
	{
		TSharedPtr<FJsonObject> Gone = MCPSuccess();
		Gone->SetBoolField(TEXT("alreadyRemoved"), true);
		NiaAdvEchoAddress(Gone, SystemPath, EmitterName, EmitterIndex, Emitter);
		Gone->SetStringField(TEXT("stackContext"), Slot.Context);
		Gone->SetStringField(TEXT("moduleName"), FC->GetFunctionName());
		Gone->SetStringField(TEXT("inputName"), InputName);
		Gone->SetArrayField(TEXT("overridesPresent"), MCPStringListToJson(OverrideNames));
		Gone->SetStringField(TEXT("note"), Target
			? TEXT("That input has an override but nothing is wired into it, so it already carries a plain value. Nothing was changed.")
			: TEXT("That input has no override at all, so it already uses the module's own default. Nothing was changed."));
		return MCPResult(Gone);
	}

	UEdGraphNode* Wired = Target->LinkedTo[0] ? Target->LinkedTo[0]->GetOwningNode() : nullptr;
	FString RemovedKind = TEXT("linkedParameter");
	FString RemovedScriptPath;
	FString RemovedHlsl;
	bool bHadNestedOverrides = false;
	if (UNiagaraNodeFunctionCall* WiredFC = Cast<UNiagaraNodeFunctionCall>(Wired))
	{
		RemovedKind = Cast<UNiagaraNodeCustomHlsl>(Wired) ? TEXT("customHlsl") : TEXT("dynamicInput");
		RemovedScriptPath = WiredFC->FunctionScript ? WiredFC->FunctionScript->GetPathName() : FString();
		TArray<UEdGraphPin*> Nested;
		NiaAdvCollectOverridePins(Graph, *WiredFC, Nested);
		bHadNestedOverrides = Nested.Num() > 0;
	}
	if (UNiagaraNodeCustomHlsl* WiredHlsl = Cast<UNiagaraNodeCustomHlsl>(Wired))
	{
		RemovedKind = TEXT("customHlsl");
		RemovedHlsl = NiaAdvReadHlsl(WiredHlsl);
	}
	const FString OverridePinName = Target->PinName.ToString();
	UEdGraphNode* OverrideNode = Target->GetOwningNode();

	Graph->Modify();
	FC->Modify();
	if (OverrideNode) OverrideNode->Modify();

	Target->BreakAllPinLinks(true);
	int32 RemovedNodes = 0;
	if (Wired)
	{
		TSet<UEdGraphNode*> Doomed;
		NiaAdvCollectExclusiveUpstream(Wired, Doomed);
		for (UEdGraphNode* Node : Doomed) { Graph->RemoveNode(Node); ++RemovedNodes; }
		Graph->RemoveNode(Wired);
		++RemovedNodes;
	}
	// Dropping the override pin itself is what restores the module's own
	// default. RemoveNodesForStackFunctionInputOverridePin is the engine's
	// version of this and is not exported from NiagaraEditor.
	if (OverrideNode) OverrideNode->RemovePin(Target);

	FC->MarkNodeRequiresSynchronization(TEXT("MCP_RemoveDynamicInput"), true);
	NiaAdvFinalize(System, Emitter, Graph);

	TSharedPtr<FJsonObject> Res = MCPSuccess();
	MCPSetUpdated(Res);
	Res->SetBoolField(TEXT("alreadyRemoved"), false);
	NiaAdvEchoAddress(Res, SystemPath, EmitterName, EmitterIndex, Emitter);
	Res->SetStringField(TEXT("stackContext"), Slot.Context);
	Res->SetStringField(TEXT("moduleName"), FC->GetFunctionName());
	Res->SetStringField(TEXT("inputName"), InputName);
	Res->SetStringField(TEXT("overridePin"), OverridePinName);
	Res->SetStringField(TEXT("removedKind"), RemovedKind);
	if (!RemovedScriptPath.IsEmpty()) Res->SetStringField(TEXT("removedScript"), RemovedScriptPath);
	if (!RemovedHlsl.IsEmpty()) Res->SetStringField(TEXT("removedHlsl"), RemovedHlsl);
	Res->SetNumberField(TEXT("removedNodes"), RemovedNodes);
	Res->SetStringField(TEXT("note"),
		TEXT("The override pin was dropped, so the input is back on the module script's own default. Give it a plain value with niagara(set_niagara_module_input)."));

	if (RemovedKind == TEXT("dynamicInput") && !RemovedScriptPath.IsEmpty())
	{
		TSharedPtr<FJsonObject> RbPayload = NiaAdvAddressPayload(SystemPath, EmitterName, EmitterIndex);
		RbPayload->SetStringField(TEXT("stackContext"), Slot.Context);
		RbPayload->SetStringField(TEXT("moduleName"), FC->GetFunctionName());
		RbPayload->SetStringField(TEXT("inputName"), InputName);
		RbPayload->SetStringField(TEXT("dynamicInputScript"), RemovedScriptPath);
		MCPSetRollback(Res, TEXT("set_niagara_dynamic_input"), RbPayload);
		if (bHadNestedOverrides)
		{
			Res->SetBoolField(TEXT("rollbackRestoresScriptOnly"), true);
			Res->SetStringField(TEXT("rollbackNote"),
				TEXT("Rollback rewires the same dynamic-input script but not the inputs that were set on it, which this removal destroyed. Re-set them with niagara(set_niagara_module_input)."));
		}
	}
	else
	{
		// A custom expression or a linked parameter has no create action that
		// takes it back, so there is no inverse to advertise.
		Res->SetBoolField(TEXT("rollbackUnavailable"), true);
		Res->SetStringField(TEXT("rollbackNote"), FString::Printf(
			TEXT("A %s override has no inverse action, so nothing can replay it. The removed content is echoed above so it can be re-authored."),
			*RemovedKind));
	}
	return MCPResult(Res);
}

// ===========================================================================
// Simulation stages
// ===========================================================================

TSharedPtr<FJsonValue> FNiagaraHandlers::AddSimulationStage(const TSharedPtr<FJsonObject>& Params)
{
	FString SystemPath, StageName;
	if (auto Err = RequireString(Params, TEXT("systemPath"), SystemPath)) return Err;
	if (auto Err = RequireString(Params, TEXT("stageName"), StageName)) return Err;
	const FString EmitterName = OptionalString(Params, TEXT("emitterName"), TEXT(""));
	const int32 EmitterIndex = OptionalInt(Params, TEXT("emitterIndex"), 0);

	if (MCPIsProtectedAssetPath(SystemPath)) return MCPProtectedPathError(SystemPath);

	UNiagaraSystem* System = Cast<UNiagaraSystem>(UEditorAssetLibrary::LoadAsset(SystemPath));
	if (!System) return MCPError(FString::Printf(TEXT("Niagara system not found: '%s'."), *SystemPath));

	UNiagaraEmitter* Emitter = nullptr;
	FGuid Version;
	FString ResolveError;
	FVersionedNiagaraEmitterData* Data = NiaAdvResolveEmitter(System, EmitterName, EmitterIndex, Emitter, Version, ResolveError);
	if (!Data) return MCPError(ResolveError);

	const FName WantedName(*StageName);
	const TArray<UNiagaraSimulationStageBase*>& Existing = Data->GetSimulationStages();
	TArray<FString> ExistingNames;
	for (UNiagaraSimulationStageBase* Stage : Existing)
	{
		if (!Stage) continue;
		ExistingNames.Add(Stage->SimulationStageName.ToString());
		if (Stage->SimulationStageName == WantedName)
		{
			TSharedPtr<FJsonObject> Same = MCPSuccess();
			MCPSetExisted(Same);
			NiaAdvEchoAddress(Same, SystemPath, EmitterName, EmitterIndex, Emitter);
			Same->SetStringField(TEXT("stageName"), StageName);
			Same->SetStringField(TEXT("simulationStageObjectPath"), Stage->GetPathName());
			Same->SetStringField(TEXT("simulationStageClass"), Stage->GetClass()->GetName());
			Same->SetStringField(TEXT("scriptObjectPath"), Stage->Script ? Stage->Script->GetPathName() : FString());
			Same->SetStringField(TEXT("usageId"), Stage->Script ? Stage->Script->GetUsageId().ToString() : FString());
			Same->SetStringField(TEXT("note"),
				TEXT("A simulation stage with that name already exists; nothing was created. Configure it with asset(set_property) on simulationStageObjectPath (IterationSource, NumIterations, ExecuteBehavior, ...)."));
			return MCPResult(Same);
		}
	}

	FGuid UsageId;
	FString CreateError;
	UNiagaraScript* Script = NiaAdvCreateStageScript(
		Emitter, Data, ENiagaraScriptUsage::ParticleSimulationStageScript, TEXT("SimulationStage"), UsageId, CreateError);
	if (!Script) return MCPError(CreateError);

	Emitter->Modify();
	UNiagaraSimulationStageGeneric* Stage = NewObject<UNiagaraSimulationStageGeneric>(
		Emitter, MakeUniqueObjectName(Emitter, UNiagaraSimulationStageGeneric::StaticClass(), FName(*StageName)), RF_Transactional);
	if (!Stage) return MCPError(TEXT("Failed to construct UNiagaraSimulationStageGeneric"));

	Stage->Script = Script;
	Stage->SimulationStageName = WantedName;
	Stage->bEnabled = OptionalBool(Params, TEXT("enabled"), true) ? 1 : 0;
	Stage->OuterEmitterVersion = Version;
	Emitter->AddSimulationStage(Stage, Version);

	NiaAdvFinalize(System, Emitter, NiaAdvEmitterGraph(Data));

	TSharedPtr<FJsonObject> Res = MCPSuccess();
	MCPSetCreated(Res);
	NiaAdvEchoAddress(Res, SystemPath, EmitterName, EmitterIndex, Emitter);
	Res->SetStringField(TEXT("stageName"), StageName);
	Res->SetStringField(TEXT("simulationStageObjectPath"), Stage->GetPathName());
	Res->SetStringField(TEXT("simulationStageClass"), Stage->GetClass()->GetName());
	Res->SetStringField(TEXT("scriptObjectPath"), Script->GetPathName());
	Res->SetStringField(TEXT("usageId"), UsageId.ToString());
	Res->SetStringField(TEXT("scriptUsage"), TEXT("ParticleSimulationStageScript"));
	Res->SetNumberField(TEXT("simulationStageCount"), Data->GetSimulationStages().Num());
	Res->SetStringField(TEXT("note"),
		TEXT("Stage created with its backing script, an output node stamped with a fresh usage id, and a parameter-map input node, so it compiles. Set IterationSource, NumIterations, ExecuteBehavior, DirectDispatchType and the ElementCount bindings with asset(set_property) on simulationStageObjectPath; add modules to it with niagara(add_niagara_module)."));

	TSharedPtr<FJsonObject> RbPayload = NiaAdvAddressPayload(SystemPath, EmitterName, EmitterIndex);
	RbPayload->SetStringField(TEXT("stageName"), StageName);
	MCPSetRollback(Res, TEXT("remove_niagara_simulation_stage"), RbPayload);
	return MCPResult(Res);
}

TSharedPtr<FJsonValue> FNiagaraHandlers::RemoveSimulationStage(const TSharedPtr<FJsonObject>& Params)
{
	FString SystemPath, StageName;
	if (auto Err = RequireString(Params, TEXT("systemPath"), SystemPath)) return Err;
	if (auto Err = RequireString(Params, TEXT("stageName"), StageName)) return Err;
	const FString EmitterName = OptionalString(Params, TEXT("emitterName"), TEXT(""));
	const int32 EmitterIndex = OptionalInt(Params, TEXT("emitterIndex"), 0);

	if (MCPIsProtectedAssetPath(SystemPath)) return MCPProtectedPathError(SystemPath);

	UNiagaraSystem* System = Cast<UNiagaraSystem>(UEditorAssetLibrary::LoadAsset(SystemPath));
	if (!System) return MCPError(FString::Printf(TEXT("Niagara system not found: '%s'."), *SystemPath));

	UNiagaraEmitter* Emitter = nullptr;
	FGuid Version;
	FString ResolveError;
	FVersionedNiagaraEmitterData* Data = NiaAdvResolveEmitter(System, EmitterName, EmitterIndex, Emitter, Version, ResolveError);
	if (!Data) return MCPError(ResolveError);

	const FName WantedName(*StageName);
	UNiagaraSimulationStageBase* Target = nullptr;
	TArray<FString> ExistingNames;
	for (UNiagaraSimulationStageBase* Stage : Data->GetSimulationStages())
	{
		if (!Stage) continue;
		ExistingNames.Add(Stage->SimulationStageName.ToString());
		if (!Target && Stage->SimulationStageName == WantedName) Target = Stage;
	}

	if (!Target)
	{
		TSharedPtr<FJsonObject> Gone = MCPSuccess();
		Gone->SetBoolField(TEXT("alreadyRemoved"), true);
		NiaAdvEchoAddress(Gone, SystemPath, EmitterName, EmitterIndex, Emitter);
		Gone->SetStringField(TEXT("stageName"), StageName);
		Gone->SetArrayField(TEXT("stagesPresent"), MCPStringListToJson(ExistingNames));
		Gone->SetStringField(TEXT("note"), FString::Printf(
			TEXT("No simulation stage named '%s' on this emitter; nothing was removed. Stages present: [%s]."),
			*StageName, ExistingNames.Num() ? *FString::Join(ExistingNames, TEXT(", ")) : TEXT("none")));
		return MCPResult(Gone);
	}

	// Capture before mutating: the rollback can rebuild the stage but not its
	// modules, so the response has to name what is going away.
	UNiagaraGraph* Graph = NiaAdvEmitterGraph(Data);
	UNiagaraScript* Script = Target->Script;
	const FGuid UsageId = Script ? Script->GetUsageId() : FGuid();
	const FString StagePath = Target->GetPathName();
	const bool bWasEnabled = Target->bEnabled != 0;

	TArray<FString> LostModules;
	UNiagaraNodeOutput* OutputNode = NiaAdvFindOutputNode(Graph, ENiagaraScriptUsage::ParticleSimulationStageScript, UsageId);
	if (OutputNode)
	{
		TArray<UNiagaraNodeFunctionCall*> Modules;
		NiaAdvOrderedModules(OutputNode, Modules);
		for (UNiagaraNodeFunctionCall* FC : Modules) LostModules.Add(FC->GetFunctionName());
	}

	Emitter->Modify();
	const int32 RemovedNodes = NiaAdvDeleteOutputChain(Graph, OutputNode);
	Emitter->RemoveSimulationStage(Target, Version);

	NiaAdvFinalize(System, Emitter, Graph);

	TSharedPtr<FJsonObject> Res = MCPSuccess();
	MCPSetUpdated(Res);
	Res->SetBoolField(TEXT("alreadyRemoved"), false);
	NiaAdvEchoAddress(Res, SystemPath, EmitterName, EmitterIndex, Emitter);
	Res->SetStringField(TEXT("stageName"), StageName);
	Res->SetStringField(TEXT("removedSimulationStageObjectPath"), StagePath);
	Res->SetStringField(TEXT("removedUsageId"), UsageId.ToString());
	Res->SetNumberField(TEXT("removedGraphNodes"), RemovedNodes);
	Res->SetArrayField(TEXT("removedModules"), MCPStringListToJson(LostModules));
	Res->SetNumberField(TEXT("simulationStageCount"), Data->GetSimulationStages().Num());
	Res->SetStringField(TEXT("note"),
		TEXT("The stage, its backing script and the graph nodes that fed only its output node were removed. Nodes shared with another stack were left in place."));

	TSharedPtr<FJsonObject> RbPayload = NiaAdvAddressPayload(SystemPath, EmitterName, EmitterIndex);
	RbPayload->SetStringField(TEXT("stageName"), StageName);
	RbPayload->SetBoolField(TEXT("enabled"), bWasEnabled);
	MCPSetRollback(Res, TEXT("add_niagara_simulation_stage"), RbPayload);
	Res->SetBoolField(TEXT("rollbackRestoresEmptyStageOnly"), true);
	Res->SetStringField(TEXT("rollbackNote"), FString::Printf(
		TEXT("Rollback recreates an EMPTY stage with a NEW usage id. The %d module(s) [%s] and every property set on the stage are lost; re-add them with niagara(add_niagara_module) and asset(set_property)."),
		LostModules.Num(), LostModules.Num() ? *FString::Join(LostModules, TEXT(", ")) : TEXT("none")));
	return MCPResult(Res);
}

// ===========================================================================
// Event handlers
// ===========================================================================

TSharedPtr<FJsonValue> FNiagaraHandlers::AddEventHandler(const TSharedPtr<FJsonObject>& Params)
{
	FString SystemPath, EventName;
	if (auto Err = RequireString(Params, TEXT("systemPath"), SystemPath)) return Err;
	if (auto Err = RequireString(Params, TEXT("eventName"), EventName)) return Err;
	const FString EmitterName = OptionalString(Params, TEXT("emitterName"), TEXT(""));
	const int32 EmitterIndex = OptionalInt(Params, TEXT("emitterIndex"), 0);
	const FString SourceEmitterId = OptionalString(Params, TEXT("sourceEmitterId"), TEXT(""));

	if (MCPIsProtectedAssetPath(SystemPath)) return MCPProtectedPathError(SystemPath);

	UNiagaraSystem* System = Cast<UNiagaraSystem>(UEditorAssetLibrary::LoadAsset(SystemPath));
	if (!System) return MCPError(FString::Printf(TEXT("Niagara system not found: '%s'."), *SystemPath));

	UNiagaraEmitter* Emitter = nullptr;
	FGuid Version;
	FString ResolveError;
	FVersionedNiagaraEmitterData* Data = NiaAdvResolveEmitter(System, EmitterName, EmitterIndex, Emitter, Version, ResolveError);
	if (!Data) return MCPError(ResolveError);

	FGuid ParsedSourceId;
	if (!SourceEmitterId.IsEmpty() && !FGuid::Parse(SourceEmitterId, ParsedSourceId))
	{
		return MCPError(FString::Printf(
			TEXT("sourceEmitterId '%s' is not a GUID. Leave it out to consume events generated by this emitter, or pass the id niagara(get_niagara_info) reports for the generating emitter handle."),
			*SourceEmitterId));
	}

	const FName WantedEvent(*EventName);
	const int32 VersionIdx = NiaAdvVersionDataIndex(Emitter, Version);
	const TArray<FNiagaraEventScriptProperties>& Handlers = Data->GetEventHandlers();
	TArray<FString> ExistingEvents;
	for (int32 i = 0; i < Handlers.Num(); ++i)
	{
		ExistingEvents.Add(Handlers[i].SourceEventName.ToString());
		if (Handlers[i].SourceEventName == WantedEvent)
		{
			TSharedPtr<FJsonObject> Same = MCPSuccess();
			MCPSetExisted(Same);
			NiaAdvEchoAddress(Same, SystemPath, EmitterName, EmitterIndex, Emitter);
			Same->SetStringField(TEXT("eventName"), EventName);
			Same->SetNumberField(TEXT("eventHandlerIndex"), i);
			Same->SetStringField(TEXT("scriptObjectPath"), Handlers[i].Script ? Handlers[i].Script->GetPathName() : FString());
			Same->SetStringField(TEXT("usageId"), Handlers[i].Script ? Handlers[i].Script->GetUsageId().ToString() : FString());
			Same->SetStringField(TEXT("eventHandlerPropertyPath"),
				FString::Printf(TEXT("VersionData[%d].EventHandlerScriptProps[%d]"), VersionIdx, i));
			Same->SetStringField(TEXT("note"),
				TEXT("An event handler for that event already exists; nothing was created. Configure it with asset(set_property) on emitterObjectPath using eventHandlerPropertyPath + '.ExecutionMode' / '.SpawnNumber' / '.MaxEventsPerFrame'."));
			return MCPResult(Same);
		}
	}

	FGuid UsageId;
	FString CreateError;
	UNiagaraScript* Script = NiaAdvCreateStageScript(
		Emitter, Data, ENiagaraScriptUsage::ParticleEventScript, TEXT("EventHandler"), UsageId, CreateError);
	if (!Script) return MCPError(CreateError);

	Emitter->Modify();
	FNiagaraEventScriptProperties Props;
	Props.Script = Script;
	Props.SourceEventName = WantedEvent;
	if (ParsedSourceId.IsValid()) Props.SourceEmitterID = ParsedSourceId;
	Emitter->AddEventHandler(Props, Version);

	NiaAdvFinalize(System, Emitter, NiaAdvEmitterGraph(Data));

	const int32 NewIndex = Data->GetEventHandlers().Num() - 1;

	TSharedPtr<FJsonObject> Res = MCPSuccess();
	MCPSetCreated(Res);
	NiaAdvEchoAddress(Res, SystemPath, EmitterName, EmitterIndex, Emitter);
	Res->SetStringField(TEXT("eventName"), EventName);
	Res->SetNumberField(TEXT("eventHandlerIndex"), NewIndex);
	Res->SetNumberField(TEXT("eventHandlerCount"), Data->GetEventHandlers().Num());
	Res->SetStringField(TEXT("scriptObjectPath"), Script->GetPathName());
	Res->SetStringField(TEXT("usageId"), UsageId.ToString());
	Res->SetStringField(TEXT("scriptUsage"), TEXT("ParticleEventScript"));
	Res->SetStringField(TEXT("eventHandlerPropertyPath"),
		FString::Printf(TEXT("VersionData[%d].EventHandlerScriptProps[%d]"), VersionIdx, NewIndex));
	if (ParsedSourceId.IsValid()) Res->SetStringField(TEXT("sourceEmitterId"), ParsedSourceId.ToString());
	Res->SetStringField(TEXT("note"), FString::Printf(
		TEXT("Handler created with its ParticleEventScript, an output node stamped with a fresh usage id, and a parameter-map input node, so it compiles. ExecutionMode, SpawnNumber, MaxEventsPerFrame, bRandomSpawnNumber and SourceEmitterID are plain properties: asset(set_property) with assetPath='%s' and propertyName='VersionData[%d].EventHandlerScriptProps[%d].SpawnNumber'. Add modules to the handler stack with niagara(add_niagara_module)."),
		*Emitter->GetPathName(), VersionIdx, NewIndex));

	TSharedPtr<FJsonObject> RbPayload = NiaAdvAddressPayload(SystemPath, EmitterName, EmitterIndex);
	RbPayload->SetStringField(TEXT("eventName"), EventName);
	MCPSetRollback(Res, TEXT("remove_niagara_event_handler"), RbPayload);
	return MCPResult(Res);
}

TSharedPtr<FJsonValue> FNiagaraHandlers::RemoveEventHandler(const TSharedPtr<FJsonObject>& Params)
{
	FString SystemPath, EventName;
	if (auto Err = RequireString(Params, TEXT("systemPath"), SystemPath)) return Err;
	if (auto Err = RequireString(Params, TEXT("eventName"), EventName)) return Err;
	const FString EmitterName = OptionalString(Params, TEXT("emitterName"), TEXT(""));
	const int32 EmitterIndex = OptionalInt(Params, TEXT("emitterIndex"), 0);

	if (MCPIsProtectedAssetPath(SystemPath)) return MCPProtectedPathError(SystemPath);

	UNiagaraSystem* System = Cast<UNiagaraSystem>(UEditorAssetLibrary::LoadAsset(SystemPath));
	if (!System) return MCPError(FString::Printf(TEXT("Niagara system not found: '%s'."), *SystemPath));

	UNiagaraEmitter* Emitter = nullptr;
	FGuid Version;
	FString ResolveError;
	FVersionedNiagaraEmitterData* Data = NiaAdvResolveEmitter(System, EmitterName, EmitterIndex, Emitter, Version, ResolveError);
	if (!Data) return MCPError(ResolveError);

	const FName WantedEvent(*EventName);
	const TArray<FNiagaraEventScriptProperties>& Handlers = Data->GetEventHandlers();
	int32 TargetIdx = INDEX_NONE;
	TArray<FString> ExistingEvents;
	for (int32 i = 0; i < Handlers.Num(); ++i)
	{
		ExistingEvents.Add(Handlers[i].SourceEventName.ToString());
		if (TargetIdx == INDEX_NONE && Handlers[i].SourceEventName == WantedEvent) TargetIdx = i;
	}

	if (TargetIdx == INDEX_NONE)
	{
		TSharedPtr<FJsonObject> Gone = MCPSuccess();
		Gone->SetBoolField(TEXT("alreadyRemoved"), true);
		NiaAdvEchoAddress(Gone, SystemPath, EmitterName, EmitterIndex, Emitter);
		Gone->SetStringField(TEXT("eventName"), EventName);
		Gone->SetArrayField(TEXT("eventHandlersPresent"), MCPStringListToJson(ExistingEvents));
		Gone->SetStringField(TEXT("note"), FString::Printf(
			TEXT("No event handler for '%s' on this emitter; nothing was removed. Handlers present: [%s]."),
			*EventName, ExistingEvents.Num() ? *FString::Join(ExistingEvents, TEXT(", ")) : TEXT("none")));
		return MCPResult(Gone);
	}

	UNiagaraGraph* Graph = NiaAdvEmitterGraph(Data);
	UNiagaraScript* Script = Handlers[TargetIdx].Script;
	const FGuid UsageId = Script ? Script->GetUsageId() : FGuid();
	const FString SourceEmitterId = Handlers[TargetIdx].SourceEmitterID.IsValid()
		? Handlers[TargetIdx].SourceEmitterID.ToString() : FString();
	const int32 SpawnNumber = (int32)Handlers[TargetIdx].SpawnNumber;
	const int32 ExecutionMode = (int32)Handlers[TargetIdx].ExecutionMode;

	TArray<FString> LostModules;
	UNiagaraNodeOutput* OutputNode = NiaAdvFindOutputNode(Graph, ENiagaraScriptUsage::ParticleEventScript, UsageId);
	if (OutputNode)
	{
		TArray<UNiagaraNodeFunctionCall*> Modules;
		NiaAdvOrderedModules(OutputNode, Modules);
		for (UNiagaraNodeFunctionCall* FC : Modules) LostModules.Add(FC->GetFunctionName());
	}

	if (!UsageId.IsValid())
	{
		return MCPError(FString::Printf(
			TEXT("Event handler '%s' has no backing script, so it carries no usage id and RemoveEventHandlerByUsageId cannot address it. The emitter asset is malformed; remove the handler in the Niagara editor."),
			*EventName));
	}

	Emitter->Modify();
	const int32 RemovedNodes = NiaAdvDeleteOutputChain(Graph, OutputNode);
	Emitter->RemoveEventHandlerByUsageId(UsageId, Version);

	NiaAdvFinalize(System, Emitter, Graph);

	TSharedPtr<FJsonObject> Res = MCPSuccess();
	MCPSetUpdated(Res);
	Res->SetBoolField(TEXT("alreadyRemoved"), false);
	NiaAdvEchoAddress(Res, SystemPath, EmitterName, EmitterIndex, Emitter);
	Res->SetStringField(TEXT("eventName"), EventName);
	Res->SetStringField(TEXT("removedUsageId"), UsageId.ToString());
	Res->SetNumberField(TEXT("removedGraphNodes"), RemovedNodes);
	Res->SetArrayField(TEXT("removedModules"), MCPStringListToJson(LostModules));
	Res->SetNumberField(TEXT("eventHandlerCount"), Data->GetEventHandlers().Num());
	Res->SetStringField(TEXT("note"),
		TEXT("The handler, its ParticleEventScript and the graph nodes that fed only its output node were removed. Nodes shared with another stack were left in place."));

	TSharedPtr<FJsonObject> RbPayload = NiaAdvAddressPayload(SystemPath, EmitterName, EmitterIndex);
	RbPayload->SetStringField(TEXT("eventName"), EventName);
	if (!SourceEmitterId.IsEmpty()) RbPayload->SetStringField(TEXT("sourceEmitterId"), SourceEmitterId);
	MCPSetRollback(Res, TEXT("add_niagara_event_handler"), RbPayload);
	Res->SetBoolField(TEXT("rollbackRestoresEmptyHandlerOnly"), true);
	Res->SetStringField(TEXT("rollbackNote"), FString::Printf(
		TEXT("Rollback recreates an EMPTY handler with a NEW usage id. The %d module(s) [%s] are lost, and so are ExecutionMode (%d) and SpawnNumber (%d); re-apply those with asset(set_property) on the emitter."),
		LostModules.Num(), LostModules.Num() ? *FString::Join(LostModules, TEXT(", ")) : TEXT("none"),
		ExecutionMode, SpawnNumber));
	return MCPResult(Res);
}

// ===========================================================================
// Custom HLSL nodes
// ===========================================================================

namespace
{
	/** Resolve the graph a custom-HLSL action addresses. Either a standalone
	 *  script asset (a module or dynamic input authored through
	 *  create_niagara_module_from_hlsl) or one emitter stack context. */
	UNiagaraGraph* NiaAdvResolveHlslGraph(
		const TSharedPtr<FJsonObject>& Params,
		UNiagaraSystem*& OutSystem, UNiagaraEmitter*& OutEmitter, FGuid& OutVersion,
		FString& OutContext, TSharedPtr<FJsonValue>& OutError)
	{
		OutSystem = nullptr;
		OutEmitter = nullptr;

		const FString ScriptPath = OptionalString(Params, TEXT("scriptPath"), TEXT(""));
		const FString SystemPath = OptionalString(Params, TEXT("systemPath"), TEXT(""));

		if (!ScriptPath.IsEmpty())
		{
			UObject* Asset = MCPLoadAssetObject(ScriptPath);
			if (!Asset) { OutError = MCPAssetNotFoundError(ScriptPath, TEXT("Niagara script")); return nullptr; }
			UNiagaraScript* Script = Cast<UNiagaraScript>(Asset);
			if (!Script) { OutError = MCPAssetWrongTypeError(ScriptPath, Asset, TEXT("NiagaraScript")); return nullptr; }
			UNiagaraGraph* Graph = NiaAdvGraphOfScript(Script);
			if (!Graph)
			{
				OutError = MCPError(FString::Printf(TEXT("Niagara script '%s' has no editor graph."), *Script->GetPathName()));
				return nullptr;
			}
			OutContext = TEXT("script");
			return Graph;
		}

		if (SystemPath.IsEmpty())
		{
			OutError = MCPError(TEXT("Pass either 'scriptPath' (a NiagaraScript asset) or 'systemPath' plus 'stackContext' to say which graph holds the CustomHLSL node."));
			return nullptr;
		}

		OutSystem = Cast<UNiagaraSystem>(UEditorAssetLibrary::LoadAsset(SystemPath));
		if (!OutSystem)
		{
			OutError = MCPError(FString::Printf(TEXT("Niagara system not found: '%s'."), *SystemPath));
			return nullptr;
		}
		FString ResolveError;
		FVersionedNiagaraEmitterData* Data = NiaAdvResolveEmitter(
			OutSystem, OptionalString(Params, TEXT("emitterName"), TEXT("")),
			OptionalInt(Params, TEXT("emitterIndex"), 0), OutEmitter, OutVersion, ResolveError);
		if (!Data) { OutError = MCPError(ResolveError); return nullptr; }

		const FString StackContext = OptionalString(Params, TEXT("stackContext"), TEXT("ParticleUpdate"));
		FNiaAdvStackSlot Slot;
		if (!NiaAdvResolveContext(Data, StackContext, Slot) || !Slot.Script)
		{
			OutError = MCPError(FString::Printf(TEXT("Unknown stackContext '%s'. Valid values: %s."), *StackContext, NiaAdvValidContexts));
			return nullptr;
		}
		OutContext = Slot.Context;
		UNiagaraGraph* Graph = NiaAdvGraphOfScript(Slot.Script);
		if (!Graph) OutError = MCPError(TEXT("The addressed script has no editor graph."));
		return Graph;
	}
}

TSharedPtr<FJsonValue> FNiagaraHandlers::GetCustomHlsl(const TSharedPtr<FJsonObject>& Params)
{
	UNiagaraSystem* System = nullptr;
	UNiagaraEmitter* Emitter = nullptr;
	FGuid Version;
	FString Context;
	TSharedPtr<FJsonValue> Error;
	UNiagaraGraph* Graph = NiaAdvResolveHlslGraph(Params, System, Emitter, Version, Context, Error);
	if (!Graph) return Error.IsValid() ? Error : MCPError(TEXT("Could not resolve a Niagara graph from the parameters given."));

	TArray<UNiagaraNodeCustomHlsl*> Nodes;
	NiaAdvCollectHlslNodes(Graph, Nodes);

	const int32 WantedIndex = OptionalInt(Params, TEXT("nodeIndex"), -1);
	if (WantedIndex >= 0 && WantedIndex >= Nodes.Num())
	{
		return MCPError(FString::Printf(
			TEXT("nodeIndex %d is out of range: this graph holds %d CustomHLSL node(s). Call without 'nodeIndex' to list them all."),
			WantedIndex, Nodes.Num()));
	}

	TArray<TSharedPtr<FJsonValue>> Rows;
	for (int32 i = 0; i < Nodes.Num(); ++i)
	{
		if (WantedIndex >= 0 && i != WantedIndex) continue;
		UNiagaraNodeCustomHlsl* Node = Nodes[i];
		const FString Body = NiaAdvReadHlsl(Node);

		TArray<TSharedPtr<FJsonValue>> Pins;
		for (UEdGraphPin* Pin : Node->Pins) Pins.Add(MakeShared<FJsonValueObject>(NiaAdvPinJson(Pin)));

		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetNumberField(TEXT("nodeIndex"), i);
		Row->SetStringField(TEXT("nodeGuid"), Node->NodeGuid.ToString());
		Row->SetStringField(TEXT("functionName"), Node->GetFunctionName());
		Row->SetStringField(TEXT("hlsl"), Body);
		Row->SetNumberField(TEXT("hlslLength"), Body.Len());
		Row->SetBoolField(TEXT("enabled"), Node->GetDesiredEnabledState() != ENodeEnabledState::Disabled);
		Row->SetArrayField(TEXT("pins"), Pins);
		Row->SetNumberField(TEXT("pinCount"), Pins.Num());
		Rows.Add(MakeShared<FJsonValueObject>(Row));
	}

	TSharedPtr<FJsonObject> Res = MCPSuccess();
	Res->SetStringField(TEXT("graphContext"), Context);
	Res->SetStringField(TEXT("graphOwnerPath"), Graph->GetPathName());
	if (System) Res->SetStringField(TEXT("systemPath"), System->GetPathName());
	if (Emitter) Res->SetStringField(TEXT("emitterObjectPath"), Emitter->GetPathName());
	Res->SetArrayField(TEXT("customHlslNodes"), Rows);
	Res->SetNumberField(TEXT("nodeCount"), Nodes.Num());
	Res->SetStringField(TEXT("note"), Nodes.Num() == 0
		? TEXT("This graph holds no CustomHLSL node. niagara(create_niagara_module_from_hlsl) creates a module script with one.")
		: TEXT("Pins are derived from the HLSL body by Niagara's parser; niagara(set_niagara_custom_hlsl) rewrites the body and regenerates them."));
	return MCPResult(Res);
}

TSharedPtr<FJsonValue> FNiagaraHandlers::SetCustomHlsl(const TSharedPtr<FJsonObject>& Params)
{
	FString Hlsl;
	if (auto Err = RequireString(Params, TEXT("hlsl"), Hlsl)) return Err;

	UNiagaraSystem* System = nullptr;
	UNiagaraEmitter* Emitter = nullptr;
	FGuid Version;
	FString Context;
	TSharedPtr<FJsonValue> Error;
	UNiagaraGraph* Graph = NiaAdvResolveHlslGraph(Params, System, Emitter, Version, Context, Error);
	if (!Graph) return Error.IsValid() ? Error : MCPError(TEXT("Could not resolve a Niagara graph from the parameters given."));

	const FString OwnerPath = System ? System->GetPathName() : Graph->GetPathName();
	if (MCPIsProtectedAssetPath(OwnerPath)) return MCPProtectedPathError(OwnerPath);

	TArray<UNiagaraNodeCustomHlsl*> Nodes;
	NiaAdvCollectHlslNodes(Graph, Nodes);
	if (Nodes.Num() == 0)
	{
		return MCPError(
			TEXT("This graph holds no CustomHLSL node to write to. Create a module script that carries one with niagara(create_niagara_module_from_hlsl), or wire a custom expression into a module input first."));
	}

	const int32 NodeIndex = OptionalInt(Params, TEXT("nodeIndex"), 0);
	if (NodeIndex < 0 || NodeIndex >= Nodes.Num())
	{
		return MCPError(FString::Printf(
			TEXT("nodeIndex %d is out of range: this graph holds %d CustomHLSL node(s), so valid values are 0-%d. niagara(get_niagara_custom_hlsl) lists them."),
			NodeIndex, Nodes.Num(), Nodes.Num() - 1));
	}

	UNiagaraNodeCustomHlsl* Node = Nodes[NodeIndex];
	FStrProperty* Prop = NiaAdvHlslProperty(Node);
	if (!Prop)
	{
		return MCPError(
			TEXT("UNiagaraNodeCustomHlsl has no reflected 'CustomHlsl' string property in this engine build. SetCustomHlsl is not exported from NiagaraEditor, so reflection is the only write route and this engine has moved the field."));
	}

	const FString Previous = Prop->GetPropertyValue(Prop->ContainerPtrToValuePtr<void>(Node));
	if (Previous.Equals(Hlsl, ESearchCase::CaseSensitive))
	{
		TSharedPtr<FJsonObject> Same = MCPSuccess();
		Same->SetBoolField(TEXT("updated"), false);
		Same->SetBoolField(TEXT("alreadySet"), true);
		Same->SetStringField(TEXT("graphContext"), Context);
		Same->SetNumberField(TEXT("nodeIndex"), NodeIndex);
		Same->SetStringField(TEXT("nodeGuid"), Node->NodeGuid.ToString());
		Same->SetNumberField(TEXT("hlslLength"), Hlsl.Len());
		Same->SetStringField(TEXT("note"), TEXT("The node already carries exactly this HLSL body; nothing was written and no recompile was requested."));
		return MCPResult(Same);
	}

	Node->Modify();
	Graph->Modify();
	Prop->SetPropertyValue(Prop->ContainerPtrToValuePtr<void>(Node), Hlsl);
	// A bare property write leaves the node's pins describing the OLD body and
	// the script uncompiled. ReconstructNode re-parses the HLSL and rebuilds the
	// pins; PostEditChange is what marks the script desynchronised.
	Node->ReconstructNode();
	Node->PostEditChange();
	Node->MarkNodeRequiresSynchronization(TEXT("MCP_SetCustomHlsl"), true);
	Graph->NotifyGraphChanged();

	if (System)
	{
		NiaAdvFinalize(System, Emitter, Graph);
	}
	else
	{
		UObject* Owner = Graph->GetOutermostObject();
		if (Owner) UEditorAssetLibrary::SaveLoadedAsset(Owner);
	}

	TArray<TSharedPtr<FJsonValue>> Pins;
	for (UEdGraphPin* Pin : Node->Pins) Pins.Add(MakeShared<FJsonValueObject>(NiaAdvPinJson(Pin)));

	TSharedPtr<FJsonObject> Res = MCPSuccess();
	MCPSetUpdated(Res);
	Res->SetBoolField(TEXT("alreadySet"), false);
	Res->SetStringField(TEXT("graphContext"), Context);
	Res->SetStringField(TEXT("graphOwnerPath"), Graph->GetPathName());
	if (System) Res->SetStringField(TEXT("systemPath"), System->GetPathName());
	Res->SetNumberField(TEXT("nodeIndex"), NodeIndex);
	Res->SetStringField(TEXT("nodeGuid"), Node->NodeGuid.ToString());
	Res->SetNumberField(TEXT("hlslLength"), Hlsl.Len());
	Res->SetNumberField(TEXT("previousHlslLength"), Previous.Len());
	Res->SetArrayField(TEXT("pins"), Pins);
	Res->SetNumberField(TEXT("pinCount"), Pins.Num());
	Res->SetStringField(TEXT("note"),
		TEXT("Body rewritten and the node reconstructed, so the pins above are the ones Niagara parsed out of the new HLSL. A pin count that did not change the way you expected means the parser did not see the declaration you added."));

	TSharedPtr<FJsonObject> RbPayload = MakeShared<FJsonObject>();
	const FString ScriptPath = OptionalString(Params, TEXT("scriptPath"), TEXT(""));
	if (!ScriptPath.IsEmpty()) RbPayload->SetStringField(TEXT("scriptPath"), ScriptPath);
	if (System)
	{
		RbPayload->SetStringField(TEXT("systemPath"), System->GetPathName());
		RbPayload->SetStringField(TEXT("emitterName"), OptionalString(Params, TEXT("emitterName"), TEXT("")));
		RbPayload->SetNumberField(TEXT("emitterIndex"), OptionalInt(Params, TEXT("emitterIndex"), 0));
		RbPayload->SetStringField(TEXT("stackContext"), Context);
	}
	RbPayload->SetNumberField(TEXT("nodeIndex"), NodeIndex);
	RbPayload->SetStringField(TEXT("hlsl"), Previous);
	MCPSetRollback(Res, TEXT("set_niagara_custom_hlsl"), RbPayload);
	return MCPResult(Res);
}

// ===========================================================================
// Module stack CRUD: remove and enable
// ===========================================================================

TSharedPtr<FJsonValue> FNiagaraHandlers::RemoveModule(const TSharedPtr<FJsonObject>& Params)
{
	FString SystemPath, StackContext, ModuleName;
	if (auto Err = RequireString(Params, TEXT("systemPath"), SystemPath)) return Err;
	if (auto Err = RequireString(Params, TEXT("stackContext"), StackContext)) return Err;
	if (auto Err = RequireString(Params, TEXT("moduleName"), ModuleName)) return Err;
	const FString EmitterName = OptionalString(Params, TEXT("emitterName"), TEXT(""));
	const int32 EmitterIndex = OptionalInt(Params, TEXT("emitterIndex"), 0);

	if (MCPIsProtectedAssetPath(SystemPath)) return MCPProtectedPathError(SystemPath);

	UNiagaraSystem* System = Cast<UNiagaraSystem>(UEditorAssetLibrary::LoadAsset(SystemPath));
	if (!System) return MCPError(FString::Printf(TEXT("Niagara system not found: '%s'."), *SystemPath));

	UNiagaraEmitter* Emitter = nullptr;
	FGuid Version;
	FString ResolveError;
	FVersionedNiagaraEmitterData* Data = NiaAdvResolveEmitter(System, EmitterName, EmitterIndex, Emitter, Version, ResolveError);
	if (!Data) return MCPError(ResolveError);

	FNiaAdvStackSlot Slot;
	if (!NiaAdvResolveContext(Data, StackContext, Slot) || !Slot.Script)
	{
		return MCPError(FString::Printf(TEXT("Unknown stackContext '%s'. Valid values: %s."), *StackContext, NiaAdvValidContexts));
	}
	UNiagaraGraph* Graph = NiaAdvGraphOfScript(Slot.Script);
	if (!Graph) return MCPError(TEXT("The addressed script has no editor graph."));

	UNiagaraNodeOutput* OutputNode = NiaAdvFindOutputNode(Graph, Slot.Usage, FGuid());
	if (!OutputNode)
	{
		return MCPError(FString::Printf(
			TEXT("The %s stack has no output node, so it holds no module chain to remove from."), *Slot.Context));
	}

	TArray<UNiagaraNodeFunctionCall*> Modules;
	NiaAdvOrderedModules(OutputNode, Modules);
	TArray<FString> ModuleNames;
	int32 TargetIdx = INDEX_NONE;
	for (int32 i = 0; i < Modules.Num(); ++i)
	{
		ModuleNames.Add(Modules[i]->GetFunctionName());
		if (TargetIdx == INDEX_NONE && Modules[i]->GetFunctionName().Equals(ModuleName, ESearchCase::IgnoreCase)) TargetIdx = i;
	}

	if (TargetIdx == INDEX_NONE)
	{
		TSharedPtr<FJsonObject> Gone = MCPSuccess();
		Gone->SetBoolField(TEXT("alreadyRemoved"), true);
		NiaAdvEchoAddress(Gone, SystemPath, EmitterName, EmitterIndex, Emitter);
		Gone->SetStringField(TEXT("stackContext"), Slot.Context);
		Gone->SetStringField(TEXT("moduleName"), ModuleName);
		Gone->SetArrayField(TEXT("modulesPresent"), MCPStringListToJson(ModuleNames));
		Gone->SetStringField(TEXT("note"), FString::Printf(
			TEXT("No module named '%s' in the %s stack; nothing was removed. Modules present, in stack order: [%s]."),
			*ModuleName, *Slot.Context, ModuleNames.Num() ? *FString::Join(ModuleNames, TEXT(", ")) : TEXT("none")));
		return MCPResult(Gone);
	}

	UNiagaraNodeFunctionCall* FC = Modules[TargetIdx];
	const FString ModuleScriptPath = FC->FunctionScript ? FC->FunctionScript->GetPathName() : FString();

	// Capture the overrides before the nodes go, so the response can say what
	// the rollback will not bring back.
	TArray<UEdGraphPin*> Overrides;
	NiaAdvCollectOverridePins(Graph, *FC, Overrides);
	TArray<TSharedPtr<FJsonValue>> LostOverrides;
	for (UEdGraphPin* Pin : Overrides)
	{
		LostOverrides.Add(MakeShared<FJsonValueObject>(NiaAdvDescribeOverride(Graph, Pin, 0)));
	}

	UEdGraphPin* MapIn = NiaAdvMapPin(FC, EGPD_Input);
	UEdGraphPin* MapOut = NiaAdvMapPin(FC, EGPD_Output);
	if (!MapIn || !MapOut)
	{
		return MCPError(FString::Printf(
			TEXT("Module '%s' has no parameter-map in/out pin pair, so it is not wired into the stack chain and cannot be unwired safely. Remove it in the Niagara editor."),
			*ModuleName));
	}

	// The chain enters the module through whatever feeds the FIRST node of its
	// group. That is the override map-set node when one exists, and the function
	// call itself otherwise. RemoveModuleFromStack, GetStackNodeGroups and
	// DisconnectStackNodeGroup all do this in the engine and none of them is
	// exported from NiagaraEditor.
	UEdGraphNode* GroupHead = FC;
	UEdGraphPin* GroupInPin = MapIn;
	if (MapIn->LinkedTo.Num() > 0)
	{
		UEdGraphNode* Upstream = MapIn->LinkedTo[0] ? MapIn->LinkedTo[0]->GetOwningNode() : nullptr;
		if (NiaAdvIsMapSetNode(Upstream))
		{
			const FString Prefix = FC->GetFunctionName() + TEXT(".");
			bool bOwnsIt = false;
			for (UEdGraphPin* Pin : Upstream->Pins)
			{
				if (Pin && Pin->Direction == EGPD_Input && Pin->PinName.ToString().StartsWith(Prefix, ESearchCase::IgnoreCase))
				{
					bOwnsIt = true;
					break;
				}
			}
			if (bOwnsIt)
			{
				GroupHead = Upstream;
				GroupInPin = NiaAdvMapPin(Upstream, EGPD_Input);
			}
		}
	}

	UEdGraphPin* UpstreamSource = (GroupInPin && GroupInPin->LinkedTo.Num() > 0) ? GroupInPin->LinkedTo[0] : nullptr;
	TArray<UEdGraphPin*> Downstream = MapOut->LinkedTo;

	Graph->Modify();
	FC->Modify();
	if (GroupHead != FC) GroupHead->Modify();

	if (GroupInPin) GroupInPin->BreakAllPinLinks(true);
	MapOut->BreakAllPinLinks(true);
	if (UpstreamSource)
	{
		for (UEdGraphPin* Sink : Downstream)
		{
			if (Sink) UpstreamSource->MakeLinkTo(Sink);
		}
	}

	TSet<UEdGraphNode*> Doomed;
	NiaAdvCollectExclusiveUpstream(GroupHead, Doomed);
	Doomed.Add(GroupHead);
	Doomed.Add(FC);
	int32 RemovedNodes = 0;
	for (UEdGraphNode* Node : Doomed)
	{
		if (Node) { Graph->RemoveNode(Node); ++RemovedNodes; }
	}
	Graph->NotifyGraphChanged();

	NiaAdvFinalize(System, Emitter, Graph);

	TSharedPtr<FJsonObject> Res = MCPSuccess();
	MCPSetUpdated(Res);
	Res->SetBoolField(TEXT("alreadyRemoved"), false);
	NiaAdvEchoAddress(Res, SystemPath, EmitterName, EmitterIndex, Emitter);
	Res->SetStringField(TEXT("stackContext"), Slot.Context);
	Res->SetStringField(TEXT("moduleName"), ModuleName);
	Res->SetStringField(TEXT("moduleScript"), ModuleScriptPath);
	Res->SetNumberField(TEXT("removedAtIndex"), TargetIdx);
	Res->SetNumberField(TEXT("removedGraphNodes"), RemovedNodes);
	Res->SetArrayField(TEXT("removedOverrides"), LostOverrides);

	TArray<UNiagaraNodeFunctionCall*> Remaining;
	NiaAdvOrderedModules(NiaAdvFindOutputNode(Graph, Slot.Usage, FGuid()), Remaining);
	TArray<FString> RemainingNames;
	for (UNiagaraNodeFunctionCall* Node : Remaining) RemainingNames.Add(Node->GetFunctionName());
	Res->SetArrayField(TEXT("remainingModules"), MCPStringListToJson(RemainingNames));
	Res->SetStringField(TEXT("note"),
		TEXT("The module's node group was unwired and the parameter-map chain closed over the gap, so the modules on either side still see one map. remainingModules is the stack order after the removal."));

	if (!ModuleScriptPath.IsEmpty())
	{
		TSharedPtr<FJsonObject> RbPayload = NiaAdvAddressPayload(SystemPath, EmitterName, EmitterIndex);
		RbPayload->SetStringField(TEXT("stackContext"), Slot.Context);
		RbPayload->SetStringField(TEXT("moduleScript"), ModuleScriptPath);
		RbPayload->SetNumberField(TEXT("targetIndex"), TargetIdx);
		MCPSetRollback(Res, TEXT("add_niagara_module"), RbPayload);
		if (LostOverrides.Num() > 0)
		{
			Res->SetBoolField(TEXT("rollbackRestoresModuleOnly"), true);
			Res->SetStringField(TEXT("rollbackNote"), FString::Printf(
				TEXT("Rollback re-adds the module script at index %d but NOT the %d override(s) removed with it, which are echoed under removedOverrides. Re-apply them with niagara(set_niagara_module_input) and niagara(set_niagara_dynamic_input)."),
				TargetIdx, LostOverrides.Num()));
		}
	}
	else
	{
		Res->SetBoolField(TEXT("rollbackUnavailable"), true);
		Res->SetStringField(TEXT("rollbackNote"),
			TEXT("The removed node referenced no module script asset (a scratch or in-place node), so add_niagara_module has nothing to replay it from."));
	}
	return MCPResult(Res);
}

TSharedPtr<FJsonValue> FNiagaraHandlers::SetModuleEnabled(const TSharedPtr<FJsonObject>& Params)
{
	FString SystemPath, StackContext, ModuleName;
	if (auto Err = RequireString(Params, TEXT("systemPath"), SystemPath)) return Err;
	if (auto Err = RequireString(Params, TEXT("stackContext"), StackContext)) return Err;
	if (auto Err = RequireString(Params, TEXT("moduleName"), ModuleName)) return Err;
	if (!Params->HasField(TEXT("enabled")))
	{
		return MCPError(TEXT("Missing required parameter 'enabled' (boolean). Pass true to enable the module or false to disable it; niagara(list_niagara_dynamic_inputs) reports the current state per module."));
	}
	const bool bEnabled = OptionalBool(Params, TEXT("enabled"), true);
	const FString EmitterName = OptionalString(Params, TEXT("emitterName"), TEXT(""));
	const int32 EmitterIndex = OptionalInt(Params, TEXT("emitterIndex"), 0);

	if (MCPIsProtectedAssetPath(SystemPath)) return MCPProtectedPathError(SystemPath);

	UNiagaraSystem* System = Cast<UNiagaraSystem>(UEditorAssetLibrary::LoadAsset(SystemPath));
	if (!System) return MCPError(FString::Printf(TEXT("Niagara system not found: '%s'."), *SystemPath));

	UNiagaraEmitter* Emitter = nullptr;
	FGuid Version;
	FString ResolveError;
	FVersionedNiagaraEmitterData* Data = NiaAdvResolveEmitter(System, EmitterName, EmitterIndex, Emitter, Version, ResolveError);
	if (!Data) return MCPError(ResolveError);

	FNiaAdvStackSlot Slot;
	if (!NiaAdvResolveContext(Data, StackContext, Slot) || !Slot.Script)
	{
		return MCPError(FString::Printf(TEXT("Unknown stackContext '%s'. Valid values: %s."), *StackContext, NiaAdvValidContexts));
	}
	UNiagaraGraph* Graph = NiaAdvGraphOfScript(Slot.Script);
	if (!Graph) return MCPError(TEXT("The addressed script has no editor graph."));

	TArray<FString> SeenModules;
	UNiagaraNodeFunctionCall* FC = NiaAdvFindModule(Graph, ModuleName, SeenModules);
	if (!FC)
	{
		return MCPError(FString::Printf(
			TEXT("No module named '%s' in the %s stack. Modules present: [%s]. Add one with niagara(add_niagara_module)."),
			*ModuleName, *Slot.Context, SeenModules.Num() ? *FString::Join(SeenModules, TEXT(", ")) : TEXT("none")));
	}

	const bool bWasEnabled = FC->GetDesiredEnabledState() != ENodeEnabledState::Disabled;
	if (bWasEnabled == bEnabled)
	{
		TSharedPtr<FJsonObject> Same = MCPSuccess();
		Same->SetBoolField(TEXT("updated"), false);
		Same->SetBoolField(TEXT("alreadySet"), true);
		NiaAdvEchoAddress(Same, SystemPath, EmitterName, EmitterIndex, Emitter);
		Same->SetStringField(TEXT("stackContext"), Slot.Context);
		Same->SetStringField(TEXT("moduleName"), FC->GetFunctionName());
		Same->SetBoolField(TEXT("enabled"), bEnabled);
		Same->SetStringField(TEXT("note"), FString::Printf(
			TEXT("Module '%s' is already %s; nothing was written and no recompile was requested."),
			*FC->GetFunctionName(), bEnabled ? TEXT("enabled") : TEXT("disabled")));
		return MCPResult(Same);
	}

	Graph->Modify();
	FC->Modify();
	FNiagaraStackGraphUtilities::SetModuleIsEnabled(*FC, bEnabled);
	NiaAdvFinalize(System, Emitter, Graph);

	TSharedPtr<FJsonObject> Res = MCPSuccess();
	MCPSetUpdated(Res);
	Res->SetBoolField(TEXT("alreadySet"), false);
	NiaAdvEchoAddress(Res, SystemPath, EmitterName, EmitterIndex, Emitter);
	Res->SetStringField(TEXT("stackContext"), Slot.Context);
	Res->SetStringField(TEXT("moduleName"), FC->GetFunctionName());
	Res->SetStringField(TEXT("moduleScript"), FC->FunctionScript ? FC->FunctionScript->GetPathName() : FString());
	Res->SetBoolField(TEXT("enabled"), bEnabled);
	Res->SetBoolField(TEXT("previousEnabled"), bWasEnabled);
	Res->SetStringField(TEXT("note"),
		TEXT("A disabled module keeps its node, its inputs and its position in the stack; it is skipped at compile time. This is the reversible way to test whether a module is responsible for a behaviour, as opposed to niagara(remove_niagara_module)."));

	TSharedPtr<FJsonObject> RbPayload = NiaAdvAddressPayload(SystemPath, EmitterName, EmitterIndex);
	RbPayload->SetStringField(TEXT("stackContext"), Slot.Context);
	RbPayload->SetStringField(TEXT("moduleName"), FC->GetFunctionName());
	RbPayload->SetBoolField(TEXT("enabled"), bWasEnabled);
	MCPSetRollback(Res, TEXT("set_niagara_module_enabled"), RbPayload);
	return MCPResult(Res);
}
