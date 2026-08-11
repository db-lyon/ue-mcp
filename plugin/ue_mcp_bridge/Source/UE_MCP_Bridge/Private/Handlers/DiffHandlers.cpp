#include "Handlers/DiffHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "ReferenceSkeleton.h"

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

	struct FVirtualBoneDiffRecord
	{
		FName Name;
		FName Source;
		FName Target;

		bool operator==(const FVirtualBoneDiffRecord& Other) const
		{
			return Name == Other.Name && Source == Other.Source && Target == Other.Target;
		}
	};

	bool VirtualBoneRecordLess(const FVirtualBoneDiffRecord& A, const FVirtualBoneDiffRecord& B)
	{
		if (A.Name != B.Name) return A.Name.ToString() < B.Name.ToString();
		if (A.Source != B.Source) return A.Source.ToString() < B.Source.ToString();
		return A.Target.ToString() < B.Target.ToString();
	}

	TArray<FVirtualBoneDiffRecord> CollectVirtualBones(const USkeleton* Skeleton)
	{
		TArray<FVirtualBoneDiffRecord> Out;
		if (!Skeleton)
		{
			return Out;
		}
		Out.Reserve(Skeleton->GetVirtualBones().Num());
		for (const FVirtualBone& Bone : Skeleton->GetVirtualBones())
		{
			Out.Add({
				Bone.VirtualBoneName,
				Bone.SourceBoneName,
				Bone.TargetBoneName
			});
		}
		Out.Sort(VirtualBoneRecordLess);
		return Out;
	}

	TArray<TSharedPtr<FJsonValue>> VirtualBonesToJson(const TArray<FVirtualBoneDiffRecord>& In)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		Out.Reserve(In.Num());
		for (const FVirtualBoneDiffRecord& Bone : In)
		{
			TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetStringField(TEXT("name"), Bone.Name.ToString());
			Obj->SetStringField(TEXT("sourceBone"), Bone.Source.ToString());
			Obj->SetStringField(TEXT("targetBone"), Bone.Target.ToString());
			Out.Add(MakeShared<FJsonValueObject>(Obj));
		}
		return Out;
	}

	FName RawParentName(const TArray<FMeshBoneInfo>& Bones, int32 BoneIndex)
	{
		const int32 ParentIndex = Bones[BoneIndex].ParentIndex;
		if (ParentIndex == INDEX_NONE) return NAME_None;
		if (Bones.IsValidIndex(ParentIndex)) return Bones[ParentIndex].Name;
		return FName(*FString::Printf(TEXT("<invalid:%d>"), ParentIndex));
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

TSharedPtr<FJsonValue> FDiffHandlers::DiffSkeleton(const TSharedPtr<FJsonObject>& Params)
{
	FString PathA;
	if (TSharedPtr<FJsonValue> Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), PathA)) return Err;
	FString PathB;
	if (TSharedPtr<FJsonValue> Err = RequireString(Params, TEXT("otherPath"), PathB)) return Err;

	UObject* AssetA = LoadAssetByPath<UObject>(PathA);
	if (!AssetA) return MCPError(FString::Printf(TEXT("Asset not found: %s"), *PathA));
	const bool bSkeletonAssets = AssetA->GetClass() == USkeleton::StaticClass();
	const bool bSkeletalMeshAssets = AssetA->GetClass() == USkeletalMesh::StaticClass();
	if (!bSkeletonAssets && !bSkeletalMeshAssets)
	{
		return MCPError(FString::Printf(
			TEXT("Asset at '%s' must be exactly a Skeleton or SkeletalMesh; found '%s'"),
			*PathA, *AssetA->GetClass()->GetName()));
	}

	UObject* AssetB = LoadAssetByPath<UObject>(PathB);
	if (!AssetB) return MCPError(FString::Printf(TEXT("Asset not found: %s"), *PathB));
	if (AssetB->GetClass() != AssetA->GetClass())
	{
		return MCPError(FString::Printf(
			TEXT("Asset at '%s' must be exactly a %s to match the first asset; found '%s'"),
			*PathB, *AssetA->GetClass()->GetName(), *AssetB->GetClass()->GetName()));
	}

	const USkeleton* SkeletonAssetA = bSkeletonAssets ? CastChecked<USkeleton>(AssetA) : nullptr;
	const USkeleton* SkeletonAssetB = bSkeletonAssets ? CastChecked<USkeleton>(AssetB) : nullptr;
	const USkeletalMesh* SkeletalMeshA = bSkeletalMeshAssets ? CastChecked<USkeletalMesh>(AssetA) : nullptr;
	const USkeletalMesh* SkeletalMeshB = bSkeletalMeshAssets ? CastChecked<USkeletalMesh>(AssetB) : nullptr;
	const FReferenceSkeleton& ReferenceA = bSkeletonAssets
		? SkeletonAssetA->GetReferenceSkeleton()
		: SkeletalMeshA->GetRefSkeleton();
	const FReferenceSkeleton& ReferenceB = bSkeletonAssets
		? SkeletonAssetB->GetReferenceSkeleton()
		: SkeletalMeshB->GetRefSkeleton();
	const USkeleton* PolicySkeletonA = bSkeletonAssets ? SkeletonAssetA : SkeletalMeshA->GetSkeleton();
	const USkeleton* PolicySkeletonB = bSkeletonAssets ? SkeletonAssetB : SkeletalMeshB->GetSkeleton();
	const TArray<FMeshBoneInfo>& RawA = ReferenceA.GetRawRefBoneInfo();
	const TArray<FMeshBoneInfo>& RawB = ReferenceB.GetRawRefBoneInfo();

	TMap<FName, FName> ParentByBoneA;
	TMap<FName, FName> ParentByBoneB;
	TMap<FName, int32> IndexByBoneA;
	TMap<FName, int32> IndexByBoneB;
	for (int32 Index = 0; Index < RawA.Num(); ++Index)
	{
		ParentByBoneA.Add(RawA[Index].Name, RawParentName(RawA, Index));
		IndexByBoneA.Add(RawA[Index].Name, Index);
	}
	for (int32 Index = 0; Index < RawB.Num(); ++Index)
	{
		ParentByBoneB.Add(RawB[Index].Name, RawParentName(RawB, Index));
		IndexByBoneB.Add(RawB[Index].Name, Index);
	}

	TArray<FString> RawBonesAdded;
	TArray<FString> RawBonesRemoved;
	TArray<FName> SharedRawBones;
	for (const TPair<FName, FName>& Bone : ParentByBoneB)
	{
		if (!ParentByBoneA.Contains(Bone.Key)) RawBonesAdded.Add(Bone.Key.ToString());
	}
	for (const TPair<FName, FName>& Bone : ParentByBoneA)
	{
		if (ParentByBoneB.Contains(Bone.Key)) SharedRawBones.Add(Bone.Key);
		else RawBonesRemoved.Add(Bone.Key.ToString());
	}
	RawBonesAdded.Sort();
	RawBonesRemoved.Sort();
	SharedRawBones.Sort([](const FName& Left, const FName& Right)
	{
		return Left.ToString() < Right.ToString();
	});

	TArray<TSharedPtr<FJsonValue>> Reparented;
	TArray<TSharedPtr<FJsonValue>> RawBoneIndexChanges;
	bool bSharedParentsMatch = true;
	for (const FName& BoneName : SharedRawBones)
	{
		const FName ParentA = ParentByBoneA.FindChecked(BoneName);
		const FName ParentB = ParentByBoneB.FindChecked(BoneName);
		if (ParentA != ParentB)
		{
			bSharedParentsMatch = false;
			TSharedPtr<FJsonObject> Delta = MakeShared<FJsonObject>();
			Delta->SetStringField(TEXT("bone"), BoneName.ToString());
			Delta->SetStringField(TEXT("fromParent"), ParentA.ToString());
			Delta->SetStringField(TEXT("toParent"), ParentB.ToString());
			Reparented.Add(MakeShared<FJsonValueObject>(Delta));
		}

		const int32 IndexA = IndexByBoneA.FindChecked(BoneName);
		const int32 IndexB = IndexByBoneB.FindChecked(BoneName);
		if (IndexA != IndexB)
		{
			TSharedPtr<FJsonObject> Delta = MakeShared<FJsonObject>();
			Delta->SetStringField(TEXT("bone"), BoneName.ToString());
			Delta->SetNumberField(TEXT("fromIndex"), IndexA);
			Delta->SetNumberField(TEXT("toIndex"), IndexB);
			RawBoneIndexChanges.Add(MakeShared<FJsonValueObject>(Delta));
		}
	}

	const TArray<FVirtualBoneDiffRecord> VirtualA = CollectVirtualBones(PolicySkeletonA);
	const TArray<FVirtualBoneDiffRecord> VirtualB = CollectVirtualBones(PolicySkeletonB);
	TArray<FVirtualBoneDiffRecord> VirtualBonesAdded;
	TArray<FVirtualBoneDiffRecord> VirtualBonesRemoved;
	for (const FVirtualBoneDiffRecord& Bone : VirtualB)
	{
		if (!VirtualA.Contains(Bone)) VirtualBonesAdded.Add(Bone);
	}
	for (const FVirtualBoneDiffRecord& Bone : VirtualA)
	{
		if (!VirtualB.Contains(Bone)) VirtualBonesRemoved.Add(Bone);
	}
	VirtualBonesAdded.Sort(VirtualBoneRecordLess);
	VirtualBonesRemoved.Sort(VirtualBoneRecordLess);

	const bool bSameRawRoot = RawA.Num() > 0
		&& RawB.Num() > 0
		&& RawA[0].ParentIndex == INDEX_NONE
		&& RawB[0].ParentIndex == INDEX_NONE
		&& RawA[0].Name == RawB[0].Name;
	const bool bHierarchyCompatible = bSameRawRoot && bSharedParentsMatch;
	const bool bEditorCompatible = PolicySkeletonA
		&& PolicySkeletonB
		&& (PolicySkeletonA == PolicySkeletonB
			|| PolicySkeletonA->IsCompatibleForEditor(PolicySkeletonB)
			|| PolicySkeletonB->IsCompatibleForEditor(PolicySkeletonA));
	const int32 ChangeCount = RawBonesAdded.Num()
		+ RawBonesRemoved.Num()
		+ Reparented.Num()
		+ RawBoneIndexChanges.Num()
		+ VirtualBonesAdded.Num()
		+ VirtualBonesRemoved.Num();

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	const FString AssetType = bSkeletonAssets ? TEXT("Skeleton") : TEXT("SkeletalMesh");
	Result->SetStringField(TEXT("assetType"), AssetType);
	Result->SetStringField(TEXT("from"), PathA);
	Result->SetStringField(TEXT("to"), PathB);
	Result->SetNumberField(TEXT("boneCountFrom"), ReferenceA.GetNum());
	Result->SetNumberField(TEXT("boneCountTo"), ReferenceB.GetNum());
	Result->SetNumberField(TEXT("rawBoneCountFrom"), RawA.Num());
	Result->SetNumberField(TEXT("rawBoneCountTo"), RawB.Num());
	Result->SetNumberField(TEXT("virtualBoneCountFrom"), VirtualA.Num());
	Result->SetNumberField(TEXT("virtualBoneCountTo"), VirtualB.Num());
	Result->SetStringField(TEXT("virtualBoneSourceFrom"), GetPathNameSafe(PolicySkeletonA));
	Result->SetStringField(TEXT("virtualBoneSourceTo"), GetPathNameSafe(PolicySkeletonB));
	Result->SetNumberField(TEXT("sharedRawBoneCount"), SharedRawBones.Num());
	Result->SetArrayField(TEXT("rawBonesAdded"), StringsToJson(RawBonesAdded));
	Result->SetArrayField(TEXT("rawBonesRemoved"), StringsToJson(RawBonesRemoved));
	Result->SetArrayField(TEXT("reparented"), Reparented);
	Result->SetArrayField(TEXT("rawBoneIndexChanges"), RawBoneIndexChanges);
	Result->SetArrayField(TEXT("virtualBonesAdded"), VirtualBonesToJson(VirtualBonesAdded));
	Result->SetArrayField(TEXT("virtualBonesRemoved"), VirtualBonesToJson(VirtualBonesRemoved));
	Result->SetBoolField(TEXT("editorCompatible"), bEditorCompatible);
	Result->SetBoolField(TEXT("hierarchyCompatible"), bHierarchyCompatible);
	Result->SetBoolField(TEXT("referencePoseCompared"), false);
	Result->SetBoolField(TEXT("exportNamesCompared"), false);
	Result->SetStringField(
		TEXT("structureScope"),
		TEXT("raw bone names, parent names, raw indices, and declared virtual bones; excludes reference-pose transforms and export names"));
	Result->SetNumberField(TEXT("changeCount"), ChangeCount);
	Result->SetBoolField(TEXT("structurallyIdentical"), ChangeCount == 0);
	Result->SetBoolField(TEXT("identical"), ChangeCount == 0);
	Result->SetStringField(TEXT("summary"), ChangeCount == 0
		? FString::Printf(
			TEXT("%s structures are identical: %d final, %d raw, %d declared virtual"),
			*AssetType, ReferenceA.GetNum(), RawA.Num(), VirtualA.Num())
		: FString::Printf(
			TEXT("%s delta: %d change(s), final %d -> %d, raw %d -> %d, declared virtual %d -> %d"),
			*AssetType, ChangeCount,
			ReferenceA.GetNum(), ReferenceB.GetNum(),
			RawA.Num(), RawB.Num(), VirtualA.Num(), VirtualB.Num()));

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
		if (Asset->GetClass() == USkeleton::StaticClass()
			|| Asset->GetClass() == USkeletalMesh::StaticClass())
		{
			return DiffSkeleton(Params);
		}
		return MCPError(FString::Printf(
			TEXT("Diffing '%s' assets is not supported yet. Blueprint, Skeleton, and SkeletalMesh diffing are available now; StateTree and other graph assets are staged follow-ups."),
			*Asset->GetClass()->GetName()));
	}
	return MCPError(FString::Printf(TEXT("Asset not found: %s"), *PathA));
}
