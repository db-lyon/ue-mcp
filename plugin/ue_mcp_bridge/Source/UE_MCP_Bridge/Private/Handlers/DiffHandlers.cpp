#include "Handlers/DiffHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"

// ── Structural extraction helpers ────────────────────────────────────────────
namespace
{
	/** Compact, stable string for a pin type, e.g. "int", "object(Actor)",
	 *  "struct(Vector)[]". Used to detect variable type changes. */
	FString PinTypeToShortString(const FEdGraphPinType& T)
	{
		FString S = T.PinCategory.ToString();
		if (const UObject* Sub = T.PinSubCategoryObject.Get())
		{
			S += TEXT("(") + Sub->GetName() + TEXT(")");
		}
		else if (!T.PinSubCategory.IsNone())
		{
			S += TEXT("(") + T.PinSubCategory.ToString() + TEXT(")");
		}
		switch (T.ContainerType)
		{
			case EPinContainerType::Array: S += TEXT("[]"); break;
			case EPinContainerType::Set:   S += TEXT("{}"); break;
			case EPinContainerType::Map:   S += TEXT("{:}"); break;
			default: break;
		}
		return S;
	}

	/** All named graphs of a Blueprint keyed by graph name (event graphs +
	 *  function graphs + macro graphs). */
	TMap<FString, UEdGraph*> CollectGraphs(UBlueprint* BP)
	{
		TMap<FString, UEdGraph*> Out;
		auto Add = [&Out](const TArray<UEdGraph*>& Graphs)
		{
			for (UEdGraph* G : Graphs)
			{
				if (G) Out.Add(G->GetName(), G);
			}
		};
		Add(BP->UbergraphPages);
		Add(BP->FunctionGraphs);
		Add(BP->MacroGraphs);
		return Out;
	}

	TSharedPtr<FJsonObject> NodeSummary(UEdGraphNode* Node)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("id"), Node->NodeGuid.ToString());
		Obj->SetStringField(TEXT("class"), Node->GetClass()->GetName());
		Obj->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
		return Obj;
	}

	/** Canonical strings for every connection in a graph, emitted once per link
	 *  (from the output side). Format: "SrcGuid.Pin -> DstGuid.Pin". */
	TSet<FString> CollectConnections(UEdGraph* Graph)
	{
		TSet<FString> Out;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node) continue;
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (!Pin || Pin->Direction != EGPD_Output) continue;
				for (UEdGraphPin* Linked : Pin->LinkedTo)
				{
					if (!Linked || !Linked->GetOwningNodeUnchecked()) continue;
					Out.Add(FString::Printf(TEXT("%s.%s -> %s.%s"),
						*Node->NodeGuid.ToString(), *Pin->PinName.ToString(),
						*Linked->GetOwningNode()->NodeGuid.ToString(), *Linked->PinName.ToString()));
				}
			}
		}
		return Out;
	}

	TArray<TSharedPtr<FJsonValue>> StringsToJson(const TArray<FString>& In)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		Out.Reserve(In.Num());
		for (const FString& S : In) Out.Add(MakeShared<FJsonValueString>(S));
		return Out;
	}
}

void FDiffHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	Registry.RegisterHandler(TEXT("diff_blueprint"), &FDiffHandlers::DiffBlueprint);
	Registry.RegisterHandler(TEXT("diff_asset"), &FDiffHandlers::DiffAsset);
}

TSharedPtr<FJsonValue> FDiffHandlers::DiffBlueprint(const TSharedPtr<FJsonObject>& Params)
{
	FString PathA;
	if (TSharedPtr<FJsonValue> Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), PathA)) return Err;
	FString PathB;
	if (TSharedPtr<FJsonValue> Err = RequireString(Params, TEXT("otherPath"), PathB)) return Err;

	// Revision-based diffing (loading a depot revision into a transient package)
	// is a documented follow-up; for now both sides are loadable asset paths.
	if (Params->HasField(TEXT("fromRevision")) || Params->HasField(TEXT("toRevision")))
	{
		return MCPError(TEXT("Revision-based diffing (fromRevision/toRevision via source control) is not wired yet - pass 'otherPath' to diff two loaded Blueprint assets. Revision loading is a staged follow-up."));
	}

	UBlueprint* A = LoadAssetByPath<UBlueprint>(PathA);
	if (!A) return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *PathA));
	UBlueprint* B = LoadAssetByPath<UBlueprint>(PathB);
	if (!B) return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *PathB));

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetStringField(TEXT("assetType"), TEXT("Blueprint"));
	Result->SetStringField(TEXT("from"), PathA);
	Result->SetStringField(TEXT("to"), PathB);

	int32 ChangeCount = 0;

	// ── Parent class ────────────────────────────────────────────────────────
	const FString ParentA = A->ParentClass ? A->ParentClass->GetPathName() : TEXT("");
	const FString ParentB = B->ParentClass ? B->ParentClass->GetPathName() : TEXT("");
	if (ParentA != ParentB)
	{
		TSharedPtr<FJsonObject> PC = MakeShared<FJsonObject>();
		PC->SetStringField(TEXT("from"), ParentA);
		PC->SetStringField(TEXT("to"), ParentB);
		Result->SetObjectField(TEXT("parentClassChanged"), PC);
		++ChangeCount;
	}

	// ── Variables ───────────────────────────────────────────────────────────
	auto VarMap = [](UBlueprint* BP)
	{
		TMap<FString, TPair<FString, FString>> M; // name -> (type, default)
		for (const FBPVariableDescription& V : BP->NewVariables)
		{
			M.Add(V.VarName.ToString(), TPair<FString, FString>(PinTypeToShortString(V.VarType), V.DefaultValue));
		}
		return M;
	};
	const TMap<FString, TPair<FString, FString>> VA = VarMap(A);
	const TMap<FString, TPair<FString, FString>> VB = VarMap(B);
	{
		TArray<FString> Added, Removed;
		TArray<TSharedPtr<FJsonValue>> Changed;
		for (const auto& Pair : VB) if (!VA.Contains(Pair.Key)) Added.Add(Pair.Key);
		for (const auto& Pair : VA)
		{
			if (const TPair<FString, FString>* Bv = VB.Find(Pair.Key))
			{
				if (Bv->Key != Pair.Value.Key || Bv->Value != Pair.Value.Value)
				{
					TSharedPtr<FJsonObject> C = MakeShared<FJsonObject>();
					C->SetStringField(TEXT("name"), Pair.Key);
					C->SetStringField(TEXT("fromType"), Pair.Value.Key);
					C->SetStringField(TEXT("toType"), Bv->Key);
					C->SetStringField(TEXT("fromDefault"), Pair.Value.Value);
					C->SetStringField(TEXT("toDefault"), Bv->Value);
					Changed.Add(MakeShared<FJsonValueObject>(C));
				}
			}
			else
			{
				Removed.Add(Pair.Key);
			}
		}
		if (Added.Num() || Removed.Num() || Changed.Num())
		{
			TSharedPtr<FJsonObject> V = MakeShared<FJsonObject>();
			V->SetArrayField(TEXT("added"), StringsToJson(Added));
			V->SetArrayField(TEXT("removed"), StringsToJson(Removed));
			V->SetArrayField(TEXT("changed"), Changed);
			Result->SetObjectField(TEXT("variables"), V);
			ChangeCount += Added.Num() + Removed.Num() + Changed.Num();
		}
	}

	// ── Components (SimpleConstructionScript) ───────────────────────────────
	auto CompMap = [](UBlueprint* BP)
	{
		TMap<FString, FString> M; // name -> class
		if (BP->SimpleConstructionScript)
		{
			for (USCS_Node* Node : BP->SimpleConstructionScript->GetAllNodes())
			{
				if (!Node) continue;
				const FString Name = Node->GetVariableName().ToString();
				const FString Cls = Node->ComponentClass ? Node->ComponentClass->GetName() : TEXT("");
				M.Add(Name, Cls);
			}
		}
		return M;
	};
	const TMap<FString, FString> CA = CompMap(A);
	const TMap<FString, FString> CB = CompMap(B);
	{
		TArray<TSharedPtr<FJsonValue>> Added, Removed, Changed;
		for (const auto& Pair : CB) if (!CA.Contains(Pair.Key))
		{
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("name"), Pair.Key);
			O->SetStringField(TEXT("class"), Pair.Value);
			Added.Add(MakeShared<FJsonValueObject>(O));
		}
		for (const auto& Pair : CA)
		{
			if (const FString* Bc = CB.Find(Pair.Key))
			{
				if (*Bc != Pair.Value)
				{
					TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
					O->SetStringField(TEXT("name"), Pair.Key);
					O->SetStringField(TEXT("fromClass"), Pair.Value);
					O->SetStringField(TEXT("toClass"), *Bc);
					Changed.Add(MakeShared<FJsonValueObject>(O));
				}
			}
			else
			{
				TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
				O->SetStringField(TEXT("name"), Pair.Key);
				O->SetStringField(TEXT("class"), Pair.Value);
				Removed.Add(MakeShared<FJsonValueObject>(O));
			}
		}
		if (Added.Num() || Removed.Num() || Changed.Num())
		{
			TSharedPtr<FJsonObject> C = MakeShared<FJsonObject>();
			C->SetArrayField(TEXT("added"), Added);
			C->SetArrayField(TEXT("removed"), Removed);
			C->SetArrayField(TEXT("changed"), Changed);
			Result->SetObjectField(TEXT("components"), C);
			ChangeCount += Added.Num() + Removed.Num() + Changed.Num();
		}
	}

	// ── Graphs: node + connection deltas keyed on stable NodeGuid ───────────
	const TMap<FString, UEdGraph*> GA = CollectGraphs(A);
	const TMap<FString, UEdGraph*> GB = CollectGraphs(B);
	{
		// Functions/macros/event-graphs present on only one side.
		TArray<FString> GraphsAdded, GraphsRemoved;
		for (const auto& Pair : GB) if (!GA.Contains(Pair.Key)) GraphsAdded.Add(Pair.Key);
		for (const auto& Pair : GA) if (!GB.Contains(Pair.Key)) GraphsRemoved.Add(Pair.Key);
		if (GraphsAdded.Num() || GraphsRemoved.Num())
		{
			TSharedPtr<FJsonObject> G = MakeShared<FJsonObject>();
			G->SetArrayField(TEXT("added"), StringsToJson(GraphsAdded));
			G->SetArrayField(TEXT("removed"), StringsToJson(GraphsRemoved));
			Result->SetObjectField(TEXT("graphsAddedRemoved"), G);
			ChangeCount += GraphsAdded.Num() + GraphsRemoved.Num();
		}

		TArray<TSharedPtr<FJsonValue>> GraphDeltas;
		for (const auto& Pair : GA)
		{
			UEdGraph* GraphA = Pair.Value;
			UEdGraph* const* GraphBPtr = GB.Find(Pair.Key);
			if (!GraphBPtr) continue; // whole-graph add/remove handled above
			UEdGraph* GraphB = *GraphBPtr;

			TMap<FString, UEdGraphNode*> NodesA, NodesB;
			for (UEdGraphNode* N : GraphA->Nodes) if (N) NodesA.Add(N->NodeGuid.ToString(), N);
			for (UEdGraphNode* N : GraphB->Nodes) if (N) NodesB.Add(N->NodeGuid.ToString(), N);

			TArray<TSharedPtr<FJsonValue>> NodesAdded, NodesRemoved;
			for (const auto& NB : NodesB) if (!NodesA.Contains(NB.Key)) NodesAdded.Add(MakeShared<FJsonValueObject>(NodeSummary(NB.Value)));
			for (const auto& NA : NodesA) if (!NodesB.Contains(NA.Key)) NodesRemoved.Add(MakeShared<FJsonValueObject>(NodeSummary(NA.Value)));

			const TSet<FString> ConnA = CollectConnections(GraphA);
			const TSet<FString> ConnB = CollectConnections(GraphB);
			TArray<FString> ConnAdded, ConnRemoved;
			for (const FString& C : ConnB) if (!ConnA.Contains(C)) ConnAdded.Add(C);
			for (const FString& C : ConnA) if (!ConnB.Contains(C)) ConnRemoved.Add(C);

			if (NodesAdded.Num() || NodesRemoved.Num() || ConnAdded.Num() || ConnRemoved.Num())
			{
				TSharedPtr<FJsonObject> GD = MakeShared<FJsonObject>();
				GD->SetStringField(TEXT("graph"), Pair.Key);
				GD->SetArrayField(TEXT("nodesAdded"), NodesAdded);
				GD->SetArrayField(TEXT("nodesRemoved"), NodesRemoved);
				GD->SetArrayField(TEXT("connectionsAdded"), StringsToJson(ConnAdded));
				GD->SetArrayField(TEXT("connectionsRemoved"), StringsToJson(ConnRemoved));
				GraphDeltas.Add(MakeShared<FJsonValueObject>(GD));
				ChangeCount += NodesAdded.Num() + NodesRemoved.Num() + ConnAdded.Num() + ConnRemoved.Num();
			}
		}
		if (GraphDeltas.Num())
		{
			Result->SetArrayField(TEXT("graphs"), GraphDeltas);
		}
	}

	Result->SetNumberField(TEXT("changeCount"), ChangeCount);
	Result->SetBoolField(TEXT("identical"), ChangeCount == 0);
	Result->SetStringField(TEXT("summary"), ChangeCount == 0
		? FString::Printf(TEXT("%s and %s are structurally identical"), *PathA, *PathB)
		: FString::Printf(TEXT("%d structural change(s) from %s to %s"), ChangeCount, *PathA, *PathB));

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FDiffHandlers::DiffAsset(const TSharedPtr<FJsonObject>& Params)
{
	FString PathA;
	if (TSharedPtr<FJsonValue> Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), PathA)) return Err;

	if (UObject* Asset = LoadAssetByPath<UObject>(PathA))
	{
		if (Asset->IsA<UBlueprint>())
		{
			return DiffBlueprint(Params);
		}
		return MCPError(FString::Printf(
			TEXT("Diffing '%s' assets is not supported yet. Blueprint diffing is available now; StateTree and other graph assets are staged follow-ups."),
			*Asset->GetClass()->GetName()));
	}
	return MCPError(FString::Printf(TEXT("Asset not found: %s"), *PathA));
}
