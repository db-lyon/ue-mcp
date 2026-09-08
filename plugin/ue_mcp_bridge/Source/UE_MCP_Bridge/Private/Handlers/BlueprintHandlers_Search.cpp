// Split from BlueprintHandlers.cpp to keep that file under 3k lines. All
// functions below are still members of FBlueprintHandlers - this file is a
// translation-unit partition, not a new class. Handler registration stays in
// BlueprintHandlers.cpp::RegisterHandlers.
//
// #945: project-wide search for authored function CALL SITES.
//
// A Blueprint audit ("who still calls this deprecated function", "which tasks
// call FinishExecute") previously meant enumerating every Blueprint, listing
// every graph, and reading or T3D-exporting each one. On a real project that is
// thousands of bridge calls, megabytes of payload, and enough traffic to
// overwhelm the bridge. This action does the whole sweep in one call.
//
// Being fast is the reason it exists, so the work is shed in this order:
//
//   1. The Asset Registry narrows to Blueprint assets under one directory,
//      which costs no package loads at all.
//   2. When the declaring package of every requested function can be resolved,
//      the registry's dependency graph rules out every candidate that does not
//      reference that package. A Blueprint that never references /Script/AIModule
//      cannot contain a call to a function declared there, so it is never
//      loaded. This is where the bulk of a large project disappears.
//   3. Only what survives is loaded and walked.
//
// Step 2 is reported (narrowedByRegistry, blueprintsSkippedByRegistry) and can
// be switched off with narrowByRegistry=false, because a filter that silently
// drops a hit is worse than a slow search. It is also skipped automatically
// when any requested name has no resolvable declaring class, since the filter
// would then have nothing to match on for that name.

#include "BlueprintHandlers.h"
#include "BlueprintHandlers_Internal.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerPagination.h"
#include "Engine/Blueprint.h"
#include "Engine/World.h"
#include "Engine/LevelScriptBlueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphNode.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CallParentFunction.h"
#include "K2Node_Variable.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "EdGraphSchema_K2.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/AssetData.h"
#include "UObject/UObjectIterator.h"
#include "UObject/TopLevelAssetPath.h"
#include "UObject/UnrealType.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
	// Bounds. Every one of these exists because this action runs against an
	// unknown-sized project and a caller cannot know in advance how big the
	// answer is. Each is reported back when it bites.
	constexpr int32 MaxRequestedFunctionNames = 50;
	constexpr int32 DefaultCallSiteLimit = 200;
	constexpr int32 MaxCallSiteLimit = 1000;
	constexpr int32 DefaultMaxBlueprints = 2000;
	constexpr int32 MaxMaxBlueprints = 20000;
	constexpr int32 MaxCollectedHits = 5000;

	// A pin default is only meaningful when nothing is wired into the pin: a
	// literal is ignored the moment the pin is linked. Reporting linked pins as
	// defaults would put a stale value in an audit.
	bool IsReportablePinDefault(const UEdGraphPin* Pin)
	{
		if (!Pin || Pin->Direction != EGPD_Input) return false;
		if (Pin->bHidden || Pin->bOrphanedPin) return false;
		if (Pin->LinkedTo.Num() > 0) return false;
		if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec) return false;
		return !Pin->DefaultValue.IsEmpty()
			|| !Pin->DefaultTextValue.IsEmpty()
			|| Pin->DefaultObject != nullptr;
	}

	TSharedPtr<FJsonObject> DescribePinDefault(const UEdGraphPin* Pin)
	{
		TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
		PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
		PinObj->SetStringField(TEXT("type"), Pin->PinType.PinCategory.ToString());
		if (!Pin->DefaultValue.IsEmpty())
		{
			PinObj->SetStringField(TEXT("defaultValue"), Pin->DefaultValue);
		}
		if (!Pin->DefaultTextValue.IsEmpty())
		{
			PinObj->SetStringField(TEXT("defaultTextValue"), Pin->DefaultTextValue.ToString());
		}
		if (Pin->DefaultObject)
		{
			PinObj->SetStringField(TEXT("defaultObject"), Pin->DefaultObject->GetPathName());
		}
		return PinObj;
	}

	TSharedPtr<FJsonObject> DescribeNeighbourNode(const UEdGraphNode* Node)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("nodeId"), Node->NodeGuid.ToString());
		Obj->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
		Obj->SetStringField(TEXT("class"), Node->GetClass()->GetName());
		return Obj;
	}

	// The nodes immediately upstream and downstream, split by whether the link
	// is execution or data. Enough to see the shape a call sits in without
	// pulling the whole graph across.
	void AppendNeighbours(const TSharedPtr<FJsonObject>& HitObj, const UEdGraphNode* Node)
	{
		TArray<TSharedPtr<FJsonValue>> ExecIn, ExecOut, DataIn, DataOut;

		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin) continue;
			const bool bExec = Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec;
			for (const UEdGraphPin* Linked : Pin->LinkedTo)
			{
				if (!Linked || !Linked->GetOwningNodeUnchecked()) continue;
				const UEdGraphNode* Other = Linked->GetOwningNodeUnchecked();
				if (Other == Node) continue;
				// Deduplicate per direction+kind bucket rather than globally:
				// the same node can legitimately be both a data source and an
				// exec successor, and both facts are worth reporting.
				const bool bIncoming = Pin->Direction == EGPD_Input;
				TArray<TSharedPtr<FJsonValue>>& Bucket =
					bExec ? (bIncoming ? ExecIn : ExecOut) : (bIncoming ? DataIn : DataOut);
				bool bAlready = false;
				for (const TSharedPtr<FJsonValue>& Existing : Bucket)
				{
					const TSharedPtr<FJsonObject> ExistingObj = Existing->AsObject();
					if (ExistingObj.IsValid() &&
						ExistingObj->GetStringField(TEXT("nodeId")) == Other->NodeGuid.ToString())
					{
						bAlready = true;
						break;
					}
				}
				if (bAlready) continue;
				Bucket.Add(MakeShared<FJsonValueObject>(DescribeNeighbourNode(Other)));
			}
		}

		TSharedPtr<FJsonObject> Neighbours = MakeShared<FJsonObject>();
		Neighbours->SetArrayField(TEXT("execIn"), ExecIn);
		Neighbours->SetArrayField(TEXT("execOut"), ExecOut);
		Neighbours->SetArrayField(TEXT("dataIn"), DataIn);
		Neighbours->SetArrayField(TEXT("dataOut"), DataOut);
		HitObj->SetObjectField(TEXT("neighbours"), Neighbours);
	}

	// Which packages declare the requested functions. This is the input to the
	// registry narrowing: a Blueprint that does not depend on any of them
	// cannot call any of them. bOutAllResolved is false as soon as one
	// requested name has no declaring class anywhere in the loaded type system,
	// which disables the narrowing rather than letting it drop that name's hits.
	void CollectDeclaringPackages(
		const TArray<FString>& FunctionNames,
		UClass* FilterClass,
		TSet<FName>& OutPackages,
		bool& bOutAllResolved)
	{
		bOutAllResolved = true;

		if (FilterClass)
		{
			// A class filter is the strongest narrowing there is: every hit has
			// to be declared on it or a subclass, and a subclass lives in the
			// same package or in one that already depends on it.
			if (UPackage* Package = FilterClass->GetOutermost())
			{
				OutPackages.Add(Package->GetFName());
			}
			return;
		}

		TArray<FName> WantedNames;
		WantedNames.Reserve(FunctionNames.Num());
		for (const FString& Name : FunctionNames)
		{
			WantedNames.Add(FName(*Name));
		}

		TArray<bool> Found;
		Found.Init(false, WantedNames.Num());

		for (TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt)
		{
			UClass* Class = *ClassIt;
			if (!Class) continue;
			for (int32 Index = 0; Index < WantedNames.Num(); ++Index)
			{
				// ExcludeSuper: we want the class that DECLARES the function,
				// because that is the package a caller records a dependency on.
				if (!Class->FindFunctionByName(WantedNames[Index], EIncludeSuperFlag::ExcludeSuper)) continue;
				Found[Index] = true;
				if (UPackage* Package = Class->GetOutermost())
				{
					OutPackages.Add(Package->GetFName());
				}
			}
		}

		for (bool bWasFound : Found)
		{
			if (!bWasFound)
			{
				bOutAllResolved = false;
				break;
			}
		}
	}
}

// #998 + #1015: find nodes anywhere in one Blueprint.
//
// Two reports asked for the same walk with different predicates. #998 wanted
// every node whose title carries one of a set of terms, across every authored
// graph including collapsed ones. #1015 wanted every get and set of a named
// variable, which is the variable analogue of search_call_sites. Neither was
// reachable: read_graph and epic_find_nodes take one named graph at a time and
// do not descend into a collapsed subgraph, list_graphs names graphs without
// looking inside them, and search_call_sites only matches function calls. So
// both ended up walking ubergraph_pages, function_graphs, macro_graphs and
// delegate_signature_graphs by hand in Python.
//
// One action rather than two, because the difference between them is which
// predicate runs per node, and a caller wanting "every get of bIsAiming, and
// anything titled Sprint" should not have to make two passes over the same
// hundred graphs to get it.
// #996 gap 2: read the edges of a Blueprint graph, addressed by node GUID.
//
// read_graph reports each pin as connected true or false and never says to
// what, and read_graph_summary carries exec edges but no data edges. So there
// was no way to answer "what feeds this pin", which is what verifying or
// re-targeting wiring needs. The workaround was to read the graph through
// Python and rebuild the edge list by hand.
//
// Every edge is reported from its OUTPUT side, once. Walking both sides would
// report each edge twice and leave the caller to dedupe something it cannot
// see the identity of, and an edge has a direction anyway: data flows out of
// a source pin into a target pin, and exec runs from a then to an execute.
//
// Nodes are named by GUID as well as title, because title is exactly what the
// report says is ambiguous: a graph with five "float * float" nodes cannot be
// rewired by title, and a GUID survives a recompile.
TSharedPtr<FJsonValue> FBlueprintHandlers::GetConnections(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	UBlueprint* const Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint) return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));

	// One graph when named, every graph otherwise: an audit of "what is wired
	// to what" across a Blueprint is as reasonable a question as one graph.
	FString Requested = OptionalString(Params, TEXT("graphSelector"), TEXT(""));
	if (Requested.IsEmpty()) Requested = OptionalString(Params, TEXT("graphName"), TEXT(""));

	FString Kind = OptionalString(Params, TEXT("kind"), TEXT("all")).ToLower();
	if (Kind.IsEmpty()) Kind = TEXT("all");
	if (Kind != TEXT("all") && Kind != TEXT("exec") && Kind != TEXT("data"))
	{
		return MCPError(FString::Printf(
			TEXT("'kind' must be exec, data or all, got '%s'"), *Kind));
	}
	const bool bWantExec = Kind != TEXT("data");
	const bool bWantData = Kind != TEXT("exec");

	const bool bIncludeNestedGraphs = OptionalBool(Params, TEXT("includeNestedGraphs"), true);

	MCPPagination::FPageRequest Page;
	if (auto Err = MCPPagination::ReadPageRequest(
			Params,
			FString::Printf(TEXT("get_blueprint_connections|asset=%s|graph=%s|kind=%s|nested=%d"),
				*Blueprint->GetPathName(), *Requested, *Kind, bIncludeNestedGraphs ? 1 : 0),
			DefaultCallSiteLimit, MaxCallSiteLimit, Page))
	{
		return Err;
	}

	TArray<UEdGraph*> AllGraphs;
	Blueprint->GetAllGraphs(AllGraphs);
	TMap<FString, int32> NameCounts;
	CountGraphNames(AllGraphs, NameCounts);
	TMap<FString, int32> SeenCounts;

	TArray<MCPPagination::FPageRow> Edges;
	int32 GraphsScanned = 0;
	int32 NodesScanned = 0;
	int32 NameMatches = 0;
	bool bTruncated = false;

	for (UEdGraph* Graph : AllGraphs)
	{
		if (bTruncated) break;
		if (!Graph) continue;

		const FString GraphName = Graph->GetName();
		const int32 DuplicateIndex = SeenCounts.FindOrAdd(GraphName)++;
		const FString Selector = MakeGraphSelector(GraphName, DuplicateIndex, NameCounts.FindRef(GraphName));
		const bool bNested = Graph->GetOuter() != Blueprint;
		if (bNested && !bIncludeNestedGraphs) continue;
		if (!Requested.IsEmpty())
		{
			const bool bSelectorHit = Selector.Equals(Requested, ESearchCase::IgnoreCase);
			const bool bNameHit = GraphName.Equals(Requested, ESearchCase::IgnoreCase);
			if (!bSelectorHit && !bNameHit) continue;
			if (bNameHit && !bSelectorHit) ++NameMatches;
		}

		++GraphsScanned;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node) continue;
			++NodesScanned;
			for (const UEdGraphPin* Pin : Node->Pins)
			{
				// Output side only, so each edge is reported once and carries the
				// direction it actually has.
				if (!Pin || Pin->Direction != EGPD_Output) continue;
				const bool bExec = Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec;
				if (bExec ? !bWantExec : !bWantData) continue;

				for (const UEdGraphPin* Linked : Pin->LinkedTo)
				{
					if (!Linked) continue;
					const UEdGraphNode* const Target = Linked->GetOwningNodeUnchecked();
					if (!Target) continue;

					TSharedPtr<FJsonObject> Edge = MakeShared<FJsonObject>();
					Edge->SetStringField(TEXT("kind"), bExec ? TEXT("exec") : TEXT("data"));
					Edge->SetStringField(TEXT("graphName"), GraphName);
					Edge->SetStringField(TEXT("graphSelector"), Selector);
					Edge->SetBoolField(TEXT("nestedGraph"), bNested);
					Edge->SetStringField(TEXT("fromNodeId"), Node->NodeGuid.ToString());
					Edge->SetStringField(TEXT("fromNodeTitle"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
					Edge->SetStringField(TEXT("fromNodeClass"), Node->GetClass()->GetName());
					Edge->SetStringField(TEXT("fromPin"), Pin->PinName.ToString());
					Edge->SetStringField(TEXT("toNodeId"), Target->NodeGuid.ToString());
					Edge->SetStringField(TEXT("toNodeTitle"), Target->GetNodeTitle(ENodeTitleType::ListView).ToString());
					Edge->SetStringField(TEXT("toNodeClass"), Target->GetClass()->GetName());
					Edge->SetStringField(TEXT("toPin"), Linked->PinName.ToString());
					if (!bExec)
					{
						// The pin type is what tells a caller whether a re-target is even
						// legal, and it is the thing a title cannot carry.
						Edge->SetStringField(TEXT("pinCategory"), Pin->PinType.PinCategory.ToString());
						if (!Pin->PinType.PinSubCategory.IsNone())
						{
							Edge->SetStringField(TEXT("pinSubCategory"), Pin->PinType.PinSubCategory.ToString());
						}
					}

					// A pin pair inside one graph is the edge's identity, and both
					// halves survive a recompile where a row index does not.
					const FString RowId = FString::Printf(TEXT("%s|%s.%s|%s.%s"),
						*Selector,
						*Node->NodeGuid.ToString(), *Pin->PinName.ToString(),
						*Target->NodeGuid.ToString(), *Linked->PinName.ToString());
					Edges.Add({ RowId, MakeShared<FJsonValueObject>(Edge) });
					if (Edges.Num() >= MaxCollectedHits)
					{
						bTruncated = true;
						break;
					}
				}
				if (bTruncated) break;
			}
			if (bTruncated) break;
		}
	}

	if (!Requested.IsEmpty() && GraphsScanned == 0)
	{
		return MCPError(FString::Printf(
			TEXT("no graph named '%s' in %s; blueprint(list_graphs) reports the names and selectors it has"),
			*Requested, *Blueprint->GetPathName()));
	}
	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), Blueprint->GetPathName());
	if (!Requested.IsEmpty()) Result->SetStringField(TEXT("graph"), Requested);
	Result->SetStringField(TEXT("kind"), Kind);
	Result->SetBoolField(TEXT("includeNestedGraphs"), bIncludeNestedGraphs);
	if (NameMatches > 1)
	{
		// Reading is not destructive, so this reports rather than refuses - but
		// a caller feeding these edges back into a write needs to know the name
		// it gave covered more than one graph.
		Result->SetBoolField(TEXT("ambiguousGraphName"), true);
		Result->SetNumberField(TEXT("graphsMatchingName"), NameMatches);
	}

	TSharedPtr<FJsonObject> Stats = MakeShared<FJsonObject>();
	Stats->SetNumberField(TEXT("graphsInBlueprint"), AllGraphs.Num());
	Stats->SetNumberField(TEXT("graphsScanned"), GraphsScanned);
	Stats->SetNumberField(TEXT("nodesScanned"), NodesScanned);
	Result->SetObjectField(TEXT("stats"), Stats);
	if (bTruncated)
	{
		Result->SetBoolField(TEXT("truncatedAtMaxEdges"), true);
		Result->SetNumberField(TEXT("maxEdges"), MaxCollectedHits);
	}

	MCPPagination::EmitPage(Page, Edges, TEXT("connections"), Result, !bTruncated);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FBlueprintHandlers::SearchNodes(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("blueprintPath"), AssetPath)) return Err;

	// LoadBlueprint resolves a World path to its level script too, which is the
	// same alias read/list_graphs/read_graph already accept.
	UBlueprint* const Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint) return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));

	auto ReadLowerStrings = [&](const TCHAR* Field, TArray<FString>& OutRaw, TSet<FString>& OutLower)
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (!Params->TryGetArrayField(Field, Arr) || !Arr) return;
		for (const TSharedPtr<FJsonValue>& Value : *Arr)
		{
			FString S;
			if (!Value.IsValid() || !Value->TryGetString(S)) continue;
			S.TrimStartAndEndInline();
			if (S.IsEmpty() || OutLower.Contains(S.ToLower())) continue;
			OutRaw.Add(S);
			OutLower.Add(S.ToLower());
		}
	};

	TArray<FString> Titles;      TSet<FString> TitlesLower;
	TArray<FString> NodeClasses; TSet<FString> NodeClassesLower;
	ReadLowerStrings(TEXT("titles"), Titles, TitlesLower);
	ReadLowerStrings(TEXT("nodeClasses"), NodeClasses, NodeClassesLower);

	const FString VariableName = OptionalString(Params, TEXT("variableName"), TEXT(""));
	FString VariableAccess = OptionalString(Params, TEXT("variableAccess"), TEXT("any")).ToLower();
	if (VariableAccess.IsEmpty()) VariableAccess = TEXT("any");
	if (VariableAccess != TEXT("any") && VariableAccess != TEXT("get") && VariableAccess != TEXT("set"))
	{
		return MCPError(FString::Printf(
			TEXT("'variableAccess' must be get, set or any, got '%s'"), *VariableAccess));
	}

	// A filterless call would walk every node in the Blueprint and hand back
	// all of them, which is read_graph with extra steps and a response nobody
	// sized for. Refusing names the three ways to narrow it rather than
	// returning a truncated dump that looks like a search result.
	if (Titles.Num() == 0 && NodeClasses.Num() == 0 && VariableName.IsEmpty())
	{
		return MCPError(TEXT(
			"Name what to look for: 'titles' (substrings matched against the node title), "
			"'nodeClasses' (exact node class names such as K2Node_VariableGet), or "
			"'variableName' (a member the node reads or writes)."));
	}

	const bool bIncludeNestedGraphs = OptionalBool(Params, TEXT("includeNestedGraphs"), true);
	// Transient and generated graphs are compiler artifacts. They are off by
	// default because a caller auditing what a person authored does not want
	// them, and the Python this replaces filtered them out by hand every time.
	const bool bAuthoredOnly = OptionalBool(Params, TEXT("authoredOnly"), true);

	MCPPagination::FPageRequest Page;
	if (auto Err = MCPPagination::ReadPageRequest(
			Params,
			FString::Printf(
				TEXT("search_blueprint_nodes|asset=%s|titles=%s|classes=%s|var=%s|access=%s|nested=%d|authored=%d"),
				*Blueprint->GetPathName(),
				*FString::Join(Titles, TEXT(",")),
				*FString::Join(NodeClasses, TEXT(",")),
				*VariableName,
				*VariableAccess,
				bIncludeNestedGraphs ? 1 : 0,
				bAuthoredOnly ? 1 : 0),
			DefaultCallSiteLimit, MaxCallSiteLimit, Page))
	{
		return Err;
	}

	TArray<UEdGraph*> AllGraphs;
	Blueprint->GetAllGraphs(AllGraphs);
	// Selectors are computed over EVERY graph even when nested graphs are being
	// skipped, so a reported selector always means what list_graphs says it
	// means rather than shifting with this call's filters.
	TMap<FString, int32> NameCounts;
	CountGraphNames(AllGraphs, NameCounts);
	TMap<FString, int32> SeenCounts;

	TArray<MCPPagination::FPageRow> Hits;
	int32 GraphsScanned = 0;
	int32 NodesScanned = 0;
	int32 GraphsSkippedAsNested = 0;
	int32 GraphsSkippedAsGenerated = 0;
	bool bTruncatedAtMaxHits = false;

	for (UEdGraph* Graph : AllGraphs)
	{
		if (bTruncatedAtMaxHits) break;
		if (!Graph) continue;

		const FString GraphName = Graph->GetName();
		const int32 DuplicateIndex = SeenCounts.FindOrAdd(GraphName)++;
		const FString Selector = MakeGraphSelector(GraphName, DuplicateIndex, NameCounts.FindRef(GraphName));

		// A top-level graph is owned by the Blueprint. Collapsed graphs, state
		// machine graphs and transition graphs hang off a node or a parent graph
		// instead, which is what "nested" means here and what #1015 reported as
		// invisible.
		const bool bNested = Graph->GetOuter() != Blueprint;
		if (bNested && !bIncludeNestedGraphs)
		{
			++GraphsSkippedAsNested;
			continue;
		}
		if (bAuthoredOnly && (Graph->HasAnyFlags(RF_Transient) || Graph->GetPackage() == GetTransientPackage()))
		{
			++GraphsSkippedAsGenerated;
			continue;
		}

		++GraphsScanned;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node) continue;
			++NodesScanned;
			if (bAuthoredOnly && Node->HasAnyFlags(RF_Transient)) continue;

			const FString NodeTitle = Node->GetNodeTitle(ENodeTitleType::ListView).ToString();
			const FString NodeClass = Node->GetClass()->GetName();

			// A variable node carries the member it references whether or not the
			// property still resolves, which is the case an audit hunting a renamed
			// variable cares about most.
			const UK2Node_Variable* const VariableNode = Cast<UK2Node_Variable>(Node);
			FString MemberName;
			FString Access;
			if (VariableNode)
			{
				MemberName = VariableNode->VariableReference.GetMemberName().ToString();
				if (Node->IsA<UK2Node_VariableSet>()) Access = TEXT("set");
				else if (Node->IsA<UK2Node_VariableGet>()) Access = TEXT("get");
			}

			// Any named filter matching is a hit. They are alternatives rather than
			// a conjunction: a caller naming both a title and a variable is asking
			// for one pass over the graphs, not for nodes that satisfy both.
			bool bMatched = false;
			FString MatchedOn;

			if (!VariableName.IsEmpty() && !MemberName.IsEmpty()
				&& MemberName.Equals(VariableName, ESearchCase::IgnoreCase)
				&& (VariableAccess == TEXT("any") || VariableAccess == Access))
			{
				bMatched = true;
				MatchedOn = TEXT("variable");
			}
			if (!bMatched && NodeClassesLower.Num() > 0 && NodeClassesLower.Contains(NodeClass.ToLower()))
			{
				bMatched = true;
				MatchedOn = TEXT("nodeClass");
			}
			if (!bMatched && TitlesLower.Num() > 0)
			{
				const FString TitleLower = NodeTitle.ToLower();
				for (const FString& Term : TitlesLower)
				{
					if (TitleLower.Contains(Term))
					{
						bMatched = true;
						MatchedOn = TEXT("title");
						break;
					}
				}
			}
			if (!bMatched) continue;

			TSharedPtr<FJsonObject> HitObj = MakeShared<FJsonObject>();
			HitObj->SetStringField(TEXT("graphName"), GraphName);
			HitObj->SetStringField(TEXT("graphSelector"), Selector);
			HitObj->SetStringField(TEXT("graphObjectPath"), Graph->GetPathName());
			HitObj->SetBoolField(TEXT("nestedGraph"), bNested);
			HitObj->SetStringField(TEXT("nodeId"), Node->NodeGuid.ToString());
			HitObj->SetStringField(TEXT("nodeTitle"), NodeTitle);
			HitObj->SetStringField(TEXT("nodeClass"), NodeClass);
			HitObj->SetStringField(TEXT("matchedOn"), MatchedOn);
			HitObj->SetNumberField(TEXT("posX"), Node->NodePosX);
			HitObj->SetNumberField(TEXT("posY"), Node->NodePosY);
			if (VariableNode)
			{
				HitObj->SetStringField(TEXT("memberName"), MemberName);
				if (!Access.IsEmpty()) HitObj->SetStringField(TEXT("access"), Access);
				if (UClass* const Owner = VariableNode->VariableReference.GetMemberParentClass())
				{
					HitObj->SetStringField(TEXT("memberParentClass"), Owner->GetName());
					HitObj->SetStringField(TEXT("memberParentClassPath"), Owner->GetPathName());
				}
				HitObj->SetBoolField(TEXT("selfContext"), VariableNode->VariableReference.IsSelfContext());
			}

			// Same anchor shape as search_call_sites: the graph selector that
			// separates two graphs of one name, and the node GUID, which survives a
			// recompile where a row index does not.
			const FString RowId = FString::Printf(TEXT("%s|%s|%s"),
				*Blueprint->GetPathName(), *Selector, *Node->NodeGuid.ToString());
			Hits.Add({ RowId, MakeShared<FJsonValueObject>(HitObj) });
			if (Hits.Num() >= MaxCollectedHits)
			{
				bTruncatedAtMaxHits = true;
				break;
			}
		}
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), Blueprint->GetPathName());
	Result->SetBoolField(TEXT("levelScript"), Blueprint->IsA<ULevelScriptBlueprint>());
	TArray<TSharedPtr<FJsonValue>> TitlesJson, ClassesJson;
	for (const FString& S : Titles) TitlesJson.Add(MakeShared<FJsonValueString>(S));
	for (const FString& S : NodeClasses) ClassesJson.Add(MakeShared<FJsonValueString>(S));
	if (TitlesJson.Num() > 0) Result->SetArrayField(TEXT("titles"), TitlesJson);
	if (ClassesJson.Num() > 0) Result->SetArrayField(TEXT("nodeClasses"), ClassesJson);
	if (!VariableName.IsEmpty())
	{
		Result->SetStringField(TEXT("variableName"), VariableName);
		Result->SetStringField(TEXT("variableAccess"), VariableAccess);
	}
	Result->SetBoolField(TEXT("includeNestedGraphs"), bIncludeNestedGraphs);
	Result->SetBoolField(TEXT("authoredOnly"), bAuthoredOnly);

	// What was walked and what was passed over, so an empty result can be told
	// apart from a filter that skipped the graph the node was in.
	TSharedPtr<FJsonObject> Stats = MakeShared<FJsonObject>();
	Stats->SetNumberField(TEXT("graphsInBlueprint"), AllGraphs.Num());
	Stats->SetNumberField(TEXT("graphsScanned"), GraphsScanned);
	Stats->SetNumberField(TEXT("graphsSkippedAsNested"), GraphsSkippedAsNested);
	Stats->SetNumberField(TEXT("graphsSkippedAsGenerated"), GraphsSkippedAsGenerated);
	Stats->SetNumberField(TEXT("nodesScanned"), NodesScanned);
	Result->SetObjectField(TEXT("stats"), Stats);
	if (bTruncatedAtMaxHits)
	{
		Result->SetBoolField(TEXT("truncatedAtMaxHits"), true);
		Result->SetNumberField(TEXT("maxHits"), MaxCollectedHits);
	}

	MCPPagination::EmitPage(Page, Hits, TEXT("nodes"), Result, !bTruncatedAtMaxHits);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FBlueprintHandlers::SearchCallSites(const TSharedPtr<FJsonObject>& Params)
{
	// ── Arguments ───────────────────────────────────────────────────────────
	const TArray<TSharedPtr<FJsonValue>>* NamesArray = nullptr;
	if (!Params->TryGetArrayField(TEXT("functionNames"), NamesArray) || !NamesArray)
	{
		return MCPError(TEXT("Missing 'functionNames' (string array of function names to find call sites for)"));
	}

	TArray<FString> FunctionNames;
	TSet<FString> WantedLower;
	for (const TSharedPtr<FJsonValue>& Value : *NamesArray)
	{
		FString Name;
		if (!Value.IsValid() || !Value->TryGetString(Name)) continue;
		Name.TrimStartAndEndInline();
		if (Name.IsEmpty()) continue;
		if (WantedLower.Contains(Name.ToLower())) continue;
		FunctionNames.Add(Name);
		WantedLower.Add(Name.ToLower());
	}
	if (FunctionNames.Num() == 0)
	{
		return MCPError(TEXT("'functionNames' is empty - name at least one function to find call sites for"));
	}
	if (FunctionNames.Num() > MaxRequestedFunctionNames)
	{
		return MCPError(FString::Printf(
			TEXT("'functionNames' has %d entries, which is over the %d limit. Split the audit into batches."),
			FunctionNames.Num(), MaxRequestedFunctionNames));
	}

	const FString ClassName = OptionalString(Params, TEXT("className"), TEXT(""));
	UClass* FilterClass = nullptr;
	if (!ClassName.IsEmpty())
	{
		FilterClass = MCPResolveClass(ClassName);
		if (!FilterClass)
		{
			// Silently matching nothing would read as "no call sites", which is
			// the wrong answer to a typo in an audit.
			return MCPClassNotFoundError(ClassName);
		}
	}

	FString Directory = OptionalString(Params, TEXT("directory"), TEXT("/Game"));
	Directory.TrimStartAndEndInline();
	if (Directory.IsEmpty()) Directory = TEXT("/Game");
	while (Directory.Len() > 1 && Directory.EndsWith(TEXT("/")))
	{
		Directory.LeftChopInline(1);
	}
	if (!Directory.StartsWith(TEXT("/")))
	{
		return MCPError(FString::Printf(
			TEXT("'directory' must be a mount-rooted content path such as /Game or /Game/AI, got '%s'"), *Directory));
	}

	const bool bIncludeNestedGraphs = OptionalBool(Params, TEXT("includeNestedGraphs"), true);
	// Off by default: it loads map packages, which is far more expensive than
	// loading Blueprints and cannot be narrowed by the registry the same way.
	const bool bIncludeLevelScripts = OptionalBool(Params, TEXT("includeLevelScripts"), false);
	const bool bIncludeNeighbours = OptionalBool(Params, TEXT("includeNeighbours"), false);
	const bool bNarrowByRegistry = OptionalBool(Params, TEXT("narrowByRegistry"), true);
	const bool bDumpToFile = OptionalBool(Params, TEXT("dumpToFile"), false);
	const FString OutputPath = OptionalString(Params, TEXT("outputPath"), TEXT(""));

	const int32 Offset = FMath::Max(0, OptionalInt(Params, TEXT("offset"), 0));
	// T3: paged. `offset` keeps working and still means the same row index; the
	// cursor is the resumable form of it, anchored on the identity of the last
	// row rather than on a count into a result set that a recompile moves.
	MCPPagination::FPageRequest Page;
	if (auto Err = MCPPagination::ReadPageRequest(
			Params,
			FString::Printf(
				TEXT("search_blueprint_call_sites|names=%s|class=%s|dir=%s|nested=%d|levelScripts=%d|neighbours=%d|narrow=%d"),
				*FString::Join(FunctionNames, TEXT(",")),
				FilterClass ? *FilterClass->GetPathName() : TEXT(""),
				*Directory,
				bIncludeNestedGraphs ? 1 : 0,
				bIncludeLevelScripts ? 1 : 0,
				bIncludeNeighbours ? 1 : 0,
				bNarrowByRegistry ? 1 : 0),
			DefaultCallSiteLimit, MaxCallSiteLimit, Page))
	{
		return Err;
	}
	const int32 MaxBlueprints = FMath::Clamp(
		OptionalInt(Params, TEXT("maxBlueprints"), DefaultMaxBlueprints), 1, MaxMaxBlueprints);

	// ── Registry pass: candidates, then narrowing ───────────────────────────
	FAssetRegistryModule& AssetRegistryModule =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& Registry = AssetRegistryModule.Get();

	// An audit run while the registry is still scanning would silently miss
	// every asset it has not reached yet, and report the shortfall as "no call
	// sites". Waiting is the correct trade here: this handler carries its own
	// long timeout precisely because it is allowed to be slow, and a wrong
	// answer to "does anything still call this" is worse than a slow one.
	const bool bWaitedForRegistry = Registry.IsLoadingAssets();
	if (bWaitedForRegistry)
	{
		Registry.WaitForCompletion();
	}

	FARFilter Filter;
	Filter.PackagePaths.Add(FName(*Directory));
	Filter.bRecursivePaths = true;
	Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
	// Anim and widget Blueprints are UBlueprint subclasses holding callable
	// graphs, so the search would miss them without recursion. Level scripts
	// are NOT covered by this filter: they are subobjects of a map package, not
	// assets of their own, so the registry never lists one. includeLevelScripts
	// below sweeps the World assets for them separately (#942).
	Filter.bRecursiveClasses = true;

	TArray<FAssetData> Candidates;
	Registry.GetAssets(Filter, Candidates);
	const int32 BlueprintsInDirectory = Candidates.Num();

	TSet<FName> DeclaringPackages;
	bool bAllNamesResolved = false;
	CollectDeclaringPackages(FunctionNames, FilterClass, DeclaringPackages, bAllNamesResolved);
	if (FilterClass)
	{
		// A resolved class filter always gives a usable package to match on.
		bAllNamesResolved = true;
	}
	const bool bNarrowing = bNarrowByRegistry && bAllNamesResolved && DeclaringPackages.Num() > 0;

	int32 SkippedByRegistry = 0;
	TArray<FAssetData> ToScan;
	ToScan.Reserve(Candidates.Num());
	for (const FAssetData& Candidate : Candidates)
	{
		if (bNarrowing)
		{
			// A Blueprint that declares the function itself records no
			// dependency on its own package, so that case is checked directly.
			bool bKeep = DeclaringPackages.Contains(Candidate.PackageName);
			if (!bKeep)
			{
				TArray<FName> Dependencies;
				Registry.GetDependencies(
					Candidate.PackageName, Dependencies, UE::AssetRegistry::EDependencyCategory::Package);
				for (const FName& Dependency : Dependencies)
				{
					if (DeclaringPackages.Contains(Dependency))
					{
						bKeep = true;
						break;
					}
				}
			}
			if (!bKeep)
			{
				++SkippedByRegistry;
				continue;
			}
		}
		ToScan.Add(Candidate);
	}

	const bool bTruncatedAtMaxBlueprints = ToScan.Num() > MaxBlueprints;
	if (bTruncatedAtMaxBlueprints)
	{
		ToScan.SetNum(MaxBlueprints);
	}

	// ── Load and walk ───────────────────────────────────────────────────────
	TArray<MCPPagination::FPageRow> Hits;
	TArray<FString> FailedToLoad;
	int32 BlueprintsLoaded = 0;
	int32 GraphsScanned = 0;
	int32 NodesScanned = 0;
	bool bTruncatedAtMaxHits = false;

	// One walk, used for Blueprint assets and for the level scripts below, so
	// the two report identical hit records rather than nearly identical ones.
	auto ScanBlueprint = [&](UBlueprint* Blueprint, const FName PackageName)
	{
		// Selectors are always computed over EVERY graph, even when nested
		// graphs are not being scanned, so a reported selector always matches
		// what list_graphs would say about the same Blueprint.
		TArray<UEdGraph*> AllGraphs;
		Blueprint->GetAllGraphs(AllGraphs);
		TMap<FString, int32> NameCounts;
		CountGraphNames(AllGraphs, NameCounts);
		TMap<FString, int32> SeenCounts;

		for (UEdGraph* Graph : AllGraphs)
		{
			if (!Graph) continue;
			const FString GraphName = Graph->GetName();
			const int32 DuplicateIndex = SeenCounts.FindOrAdd(GraphName)++;
			const FString Selector =
				MakeGraphSelector(GraphName, DuplicateIndex, NameCounts.FindRef(GraphName));

			// A top-level graph is owned by the Blueprint. Collapsed graphs,
			// state machine graphs and transition graphs hang off a node or a
			// parent graph instead, which is what "nested" means here.
			const bool bNested = Graph->GetOuter() != Blueprint;
			if (bNested && !bIncludeNestedGraphs) continue;

			++GraphsScanned;
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (!Node) continue;
				++NodesScanned;

				UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node);
				if (!Call) continue;

				// The member name survives even when the target function no
				// longer resolves, which is exactly the case an audit hunting
				// a removed function cares about most.
				const FString MemberName = Call->FunctionReference.GetMemberName().ToString();
				UFunction* Target = Call->GetTargetFunction();
				const FString ResolvedName = Target ? Target->GetName() : FString();

				if (!WantedLower.Contains(MemberName.ToLower()) &&
					(ResolvedName.IsEmpty() || !WantedLower.Contains(ResolvedName.ToLower())))
				{
					continue;
				}

				UClass* DeclaringClass = Target
					? Target->GetOwnerClass()
					: Call->FunctionReference.GetMemberParentClass();
				if (FilterClass && (!DeclaringClass || !DeclaringClass->IsChildOf(FilterClass)))
				{
					continue;
				}

				TSharedPtr<FJsonObject> HitObj = MakeShared<FJsonObject>();
				HitObj->SetStringField(TEXT("assetPath"), Blueprint->GetPathName());
				HitObj->SetStringField(TEXT("packageName"), PackageName.ToString());
				HitObj->SetBoolField(TEXT("levelScript"), Blueprint->IsA<ULevelScriptBlueprint>());
				HitObj->SetStringField(TEXT("graphName"), GraphName);
				HitObj->SetStringField(TEXT("graphSelector"), Selector);
				HitObj->SetStringField(TEXT("graphObjectPath"), Graph->GetPathName());
				HitObj->SetBoolField(TEXT("nestedGraph"), bNested);
				HitObj->SetStringField(TEXT("nodeId"), Node->NodeGuid.ToString());
				HitObj->SetStringField(TEXT("nodeTitle"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
				HitObj->SetStringField(TEXT("nodeClass"), Node->GetClass()->GetName());
				HitObj->SetStringField(TEXT("memberName"), MemberName);
				if (!ResolvedName.IsEmpty())
				{
					HitObj->SetStringField(TEXT("resolvedFunction"), ResolvedName);
				}
				else
				{
					// An unresolved target is a finding in its own right: the
					// node is authored but the function it names is gone.
					HitObj->SetBoolField(TEXT("unresolvedTarget"), true);
				}
				if (DeclaringClass)
				{
					HitObj->SetStringField(TEXT("declaringClass"), DeclaringClass->GetName());
					HitObj->SetStringField(TEXT("declaringClassPath"), DeclaringClass->GetPathName());
				}
				if (Node->IsA<UK2Node_CallParentFunction>())
				{
					HitObj->SetBoolField(TEXT("parentCall"), true);
				}

				TArray<TSharedPtr<FJsonValue>> PinDefaults;
				for (const UEdGraphPin* Pin : Node->Pins)
				{
					if (IsReportablePinDefault(Pin))
					{
						PinDefaults.Add(MakeShared<FJsonValueObject>(DescribePinDefault(Pin)));
					}
				}
				HitObj->SetArrayField(TEXT("pinDefaults"), PinDefaults);

				if (bIncludeNeighbours)
				{
					AppendNeighbours(HitObj, Node);
				}

				// The page anchor names the exact node: the owning asset, the
				// graph selector that disambiguates two graphs of one name, and
				// the node GUID. A node GUID is stable across a recompile,
				// which a row index is not.
				const FString RowId = FString::Printf(TEXT("%s|%s|%s"),
					*Blueprint->GetPathName(), *Selector, *Node->NodeGuid.ToString());
				Hits.Add({ RowId, MakeShared<FJsonValueObject>(HitObj) });
				if (Hits.Num() >= MaxCollectedHits)
				{
					bTruncatedAtMaxHits = true;
					return;
				}
			}
		}
	};

	for (const FAssetData& Candidate : ToScan)
	{
		if (bTruncatedAtMaxHits) break;

		UBlueprint* Blueprint = LoadAssetByPath<UBlueprint>(Candidate.GetObjectPathString());
		if (!Blueprint)
		{
			if (FailedToLoad.Num() < 25)
			{
				FailedToLoad.Add(Candidate.GetObjectPathString());
			}
			continue;
		}
		++BlueprintsLoaded;
		ScanBlueprint(Blueprint, Candidate.PackageName);
	}

	// #942 + #945: level scripts hold authored call nodes like any other graph,
	// but they are subobjects of a map package rather than assets, so the
	// Blueprint filter above cannot see them. Opt in, because reaching one
	// means loading the whole map: a World asset carries every actor in it.
	int32 WorldsInDirectory = 0;
	int32 LevelScriptsScanned = 0;
	if (bIncludeLevelScripts && !bTruncatedAtMaxHits)
	{
		FARFilter WorldFilter;
		WorldFilter.PackagePaths.Add(FName(*Directory));
		WorldFilter.bRecursivePaths = true;
		WorldFilter.ClassPaths.Add(UWorld::StaticClass()->GetClassPathName());

		TArray<FAssetData> Worlds;
		Registry.GetAssets(WorldFilter, Worlds);
		WorldsInDirectory = Worlds.Num();

		for (const FAssetData& World : Worlds)
		{
			if (bTruncatedAtMaxHits) break;
			if (LevelScriptsScanned >= MaxBlueprints) break;

			// LoadBlueprint resolves a World path to its level script, which is
			// the same alias read/list_graphs/read_graph accept (#942).
			UBlueprint* LevelScript = LoadBlueprint(World.GetObjectPathString());
			if (!LevelScript) continue;
			++LevelScriptsScanned;
			ScanBlueprint(LevelScript, World.PackageName);
		}
	}

	// ── Response ────────────────────────────────────────────────────────────
	// Everything about the query and the work done. The rows themselves are
	// added by the caller, because the file dump wants all of them and the
	// response wants one page.
	auto BuildEnvelope = [&]() -> TSharedPtr<FJsonObject>
	{
		TSharedPtr<FJsonObject> Obj = MCPSuccess();
		TArray<TSharedPtr<FJsonValue>> RequestedNames;
		for (const FString& Name : FunctionNames)
		{
			RequestedNames.Add(MakeShared<FJsonValueString>(Name));
		}
		Obj->SetArrayField(TEXT("functionNames"), RequestedNames);
		if (FilterClass)
		{
			Obj->SetStringField(TEXT("className"), FilterClass->GetPathName());
		}
		Obj->SetStringField(TEXT("directory"), Directory);
		Obj->SetBoolField(TEXT("includeNestedGraphs"), bIncludeNestedGraphs);
		Obj->SetBoolField(TEXT("includeLevelScripts"), bIncludeLevelScripts);

		// The work that was done and the work that was avoided, so a caller can
		// tell a genuinely empty result from an over-eager filter.
		TSharedPtr<FJsonObject> Stats = MakeShared<FJsonObject>();
		Stats->SetNumberField(TEXT("blueprintsInDirectory"), BlueprintsInDirectory);
		Stats->SetNumberField(TEXT("blueprintsSkippedByRegistry"), SkippedByRegistry);
		Stats->SetNumberField(TEXT("blueprintsConsidered"), ToScan.Num());
		Stats->SetNumberField(TEXT("blueprintsLoaded"), BlueprintsLoaded);
		Stats->SetNumberField(TEXT("graphsScanned"), GraphsScanned);
		Stats->SetNumberField(TEXT("nodesScanned"), NodesScanned);
		Stats->SetBoolField(TEXT("narrowedByRegistry"), bNarrowing);
		Stats->SetBoolField(TEXT("waitedForAssetRegistryScan"), bWaitedForRegistry);
		Stats->SetBoolField(TEXT("includedLevelScripts"), bIncludeLevelScripts);
		if (bIncludeLevelScripts)
		{
			Stats->SetNumberField(TEXT("worldsInDirectory"), WorldsInDirectory);
			Stats->SetNumberField(TEXT("levelScriptsScanned"), LevelScriptsScanned);
		}
		if (!bNarrowing && bNarrowByRegistry)
		{
			Stats->SetStringField(TEXT("narrowingSkippedReason"),
				DeclaringPackages.Num() == 0
					? TEXT("no declaring class was found for the requested function names, so every Blueprint under the directory was loaded")
					: TEXT("at least one requested function name has no declaring class in the loaded type system, so narrowing would have dropped its call sites"));
		}
		if (FailedToLoad.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> FailedArray;
			for (const FString& Failed : FailedToLoad)
			{
				FailedArray.Add(MakeShared<FJsonValueString>(Failed));
			}
			Stats->SetArrayField(TEXT("failedToLoad"), FailedArray);
		}
		Obj->SetObjectField(TEXT("stats"), Stats);

		if (bTruncatedAtMaxBlueprints)
		{
			Obj->SetBoolField(TEXT("truncatedAtMaxBlueprints"), true);
			Obj->SetNumberField(TEXT("maxBlueprints"), MaxBlueprints);
		}
		if (bTruncatedAtMaxHits)
		{
			Obj->SetBoolField(TEXT("truncatedAtMaxHits"), true);
			Obj->SetNumberField(TEXT("maxHits"), MaxCollectedHits);
		}
		return Obj;
	};

	if (bDumpToFile)
	{
		// Same convention as read_graph: the file holds the whole result set,
		// the response holds where to find it. A dump is never paged.
		const TSharedPtr<FJsonObject> DumpResult = BuildEnvelope();
		TArray<TSharedPtr<FJsonValue>> AllRows;
		AllRows.Reserve(Hits.Num());
		for (const MCPPagination::FPageRow& Row : Hits)
		{
			AllRows.Add(Row.Value);
		}
		DumpResult->SetArrayField(TEXT("callSites"), AllRows);
		DumpResult->SetNumberField(TEXT("returned"), AllRows.Num());
		DumpResult->SetNumberField(TEXT("total"), AllRows.Num());
		FString ResolvedDumpPath;
		FString DumpError;
		if (!WriteJsonObjectToFile(DumpResult, OutputPath, Directory, TEXT("call_sites"), ResolvedDumpPath, DumpError))
		{
			return MCPError(DumpError);
		}

		auto Result = MCPSuccess();
		Result->SetStringField(TEXT("directory"), Directory);
		Result->SetBoolField(TEXT("dumpedToFile"), true);
		Result->SetStringField(TEXT("outputPath"), ResolvedDumpPath);
		Result->SetNumberField(TEXT("total"), Hits.Num());
		Result->SetObjectField(TEXT("stats"), DumpResult->GetObjectField(TEXT("stats")));
		if (bTruncatedAtMaxBlueprints) Result->SetBoolField(TEXT("truncatedAtMaxBlueprints"), true);
		if (bTruncatedAtMaxHits) Result->SetBoolField(TEXT("truncatedAtMaxHits"), true);
		return MCPResult(Result);
	}

	// A caller that passed the older `offset` instead of a cursor gets exactly
	// the page a cursor issued at that offset would have given it: the anchor
	// is read out of this same enumeration, so the boundary is exact and
	// nothing is reported as having changed.
	const int32 LegacyStart = FMath::Min(Offset, Hits.Num());
	if (!Page.bResumed && LegacyStart > 0)
	{
		Page.bResumed = true;
		Page.ResumeOffset = LegacyStart;
		Page.ResumeAnchor = Hits[LegacyStart - 1].Id;
	}

	TSharedPtr<FJsonObject> Result = BuildEnvelope();
	// bRowsAreComplete is false once the walk stopped at MaxCollectedHits: the
	// collection was not fully enumerated, so `total` would be a floor rather
	// than a count and the page says `totalKnown: false` instead.
	MCPPagination::EmitPage(Page, Hits, TEXT("callSites"), Result, !bTruncatedAtMaxHits);

	// The older offset-shaped fields, kept so an existing caller reads the same
	// answer out of the same names.
	double PageOffset = 0.0;
	double PageCount = 0.0;
	Result->TryGetNumberField(TEXT("pageOffset"), PageOffset);
	Result->TryGetNumberField(TEXT("count"), PageCount);
	const int32 SliceEnd = static_cast<int32>(PageOffset) + static_cast<int32>(PageCount);
	Result->SetNumberField(TEXT("returned"), PageCount);
	Result->SetNumberField(TEXT("offset"), PageOffset);
	Result->SetNumberField(TEXT("limit"), Page.Limit);
	Result->SetNumberField(TEXT("nextOffset"), SliceEnd < Hits.Num() ? SliceEnd : -1);
	return MCPResult(Result);
}

