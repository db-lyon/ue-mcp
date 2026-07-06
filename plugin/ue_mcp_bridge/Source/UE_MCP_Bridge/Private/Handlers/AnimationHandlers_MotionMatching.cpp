// Motion Matching content-pipeline authoring. Translation-unit partition of
// FAnimationHandlers (registration stays in AnimationHandlers.cpp).
//
// Closes the remaining gaps for building a Motion Matching data pipeline entirely
// through the bridge: PoseSearchSchema (with feature channels), MirrorDataTable,
// PoseSearchNormalizationSet, and database tuning. Paired with the existing
// PoseSearchDatabase clip authoring (#684) and Chooser selection layer (#685),
// this makes a locomotion database buildable end to end - no pre-made schema or
// hand-authored mirror table required.

#include "AnimationHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerAssetCreate.h"
#include "PoseSearch/PoseSearchSchema.h"
#include "PoseSearch/PoseSearchFeatureChannel.h"
#include "PoseSearch/PoseSearchFeatureChannel_Pose.h"
#include "PoseSearch/PoseSearchFeatureChannel_Trajectory.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchNormalizationSet.h"
#include "Animation/MirrorDataTable.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimBlueprint.h"
#include "BoneContainer.h"
#include "EditorAssetLibrary.h"
#include "AnimGraphNode_MotionMatching.h"
#include "AnimGraphNode_PoseSearchHistoryCollector.h"
#include "AnimGraphNode_Root.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Self.h"
#include "ChooserFunctionLibrary.h"
#include "Chooser.h"
#include "UObject/UnrealType.h"
#include "Runtime/Launch/Resources/Version.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

// Turn a JSON array of flag names into a bitmask using a name->bit table.
static int32 ParseFlagArray(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Field, const TMap<FString, int32>& Table, int32 Default)
{
	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (!Obj->TryGetArrayField(Field, Arr) || !Arr) return Default;
	int32 Flags = 0;
	for (const TSharedPtr<FJsonValue>& V : *Arr)
	{
		FString Name;
		if (V->TryGetString(Name))
		{
			if (const int32* Bit = Table.Find(Name.ToLower())) Flags |= *Bit;
		}
	}
	return Flags == 0 ? Default : Flags;
}

static const TMap<FString, int32>& BoneFlagTable()
{
	static const TMap<FString, int32> Table = {
		{ TEXT("velocity"), int32(EPoseSearchBoneFlags::Velocity) },
		{ TEXT("position"), int32(EPoseSearchBoneFlags::Position) },
		{ TEXT("rotation"), int32(EPoseSearchBoneFlags::Rotation) },
		{ TEXT("phase"),    int32(EPoseSearchBoneFlags::Phase) },
	};
	return Table;
}

static const TMap<FString, int32>& TrajectoryFlagTable()
{
	static const TMap<FString, int32> Table = {
		{ TEXT("velocity"),            int32(EPoseSearchTrajectoryFlags::Velocity) },
		{ TEXT("position"),            int32(EPoseSearchTrajectoryFlags::Position) },
		{ TEXT("velocitydirection"),   int32(EPoseSearchTrajectoryFlags::VelocityDirection) },
		{ TEXT("facingdirection"),     int32(EPoseSearchTrajectoryFlags::FacingDirection) },
		{ TEXT("velocityxy"),          int32(EPoseSearchTrajectoryFlags::VelocityXY) },
		{ TEXT("positionxy"),          int32(EPoseSearchTrajectoryFlags::PositionXY) },
		{ TEXT("velocitydirectionxy"), int32(EPoseSearchTrajectoryFlags::VelocityDirectionXY) },
		{ TEXT("facingdirectionxy"),   int32(EPoseSearchTrajectoryFlags::FacingDirectionXY) },
	};
	return Table;
}

// Finalize a schema after channel edits (recomputes cardinality + finalized
// channels). Finalize() is private; PostEditChangeProperty triggers it publicly.
static void FinalizeSchema(UPoseSearchSchema* Schema)
{
	FPropertyChangedEvent EmptyEvent(nullptr);
	Schema->PostEditChangeProperty(EmptyEvent);
}

// ─── AnimGraph node authoring helpers ─────────────────────────────────────

// Anim graph nodes keep their settings in a private `Node` USTRUCT member. Reach
// it by reflection so we can set fields without the (private) C++ member access.
static void* GetAnimNodeMemory(UObject* GraphNode, UScriptStruct*& OutStruct)
{
	OutStruct = nullptr;
	if (!GraphNode) return nullptr;
	FStructProperty* NodeProp = CastField<FStructProperty>(GraphNode->GetClass()->FindPropertyByName(TEXT("Node")));
	if (!NodeProp) return nullptr;
	OutStruct = NodeProp->Struct;
	return NodeProp->ContainerPtrToValuePtr<void>(GraphNode);
}

static void SetNodeObject(UScriptStruct* S, void* Data, const TCHAR* Field, UObject* Value)
{
	if (FObjectPropertyBase* P = CastField<FObjectPropertyBase>(S->FindPropertyByName(Field)))
		P->SetObjectPropertyValue(P->ContainerPtrToValuePtr<void>(Data), Value);
}
static void SetNodeInt(UScriptStruct* S, void* Data, const TCHAR* Field, int32 Value)
{
	if (FIntProperty* P = CastField<FIntProperty>(S->FindPropertyByName(Field)))
		P->SetPropertyValue(P->ContainerPtrToValuePtr<void>(Data), Value);
}
static void SetNodeFloat(UScriptStruct* S, void* Data, const TCHAR* Field, float Value)
{
	if (FFloatProperty* P = CastField<FFloatProperty>(S->FindPropertyByName(Field)))
		P->SetPropertyValue(P->ContainerPtrToValuePtr<void>(Data), Value);
}
static void SetNodeBool(UScriptStruct* S, void* Data, const TCHAR* Field, bool Value)
{
	if (FBoolProperty* P = CastField<FBoolProperty>(S->FindPropertyByName(Field)))
		P->SetPropertyValue(P->ContainerPtrToValuePtr<void>(Data), Value);
}

// First pose pin in the given direction (anim graph nodes expose exactly one).
static UEdGraphPin* GetPosePin(UEdGraphNode* Node, EEdGraphPinDirection Dir)
{
	if (!Node) return nullptr;
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin && Pin->Direction == Dir) return Pin;
	}
	return nullptr;
}

static UAnimGraphNode_Root* FindOutputPoseNode(UEdGraph* Graph)
{
	for (UEdGraphNode* N : Graph->Nodes)
	{
		if (UAnimGraphNode_Root* Root = Cast<UAnimGraphNode_Root>(N)) return Root;
	}
	return nullptr;
}

static UAnimGraphNode_MotionMatching* FindMotionMatchingNode(UEdGraph* Graph)
{
	for (UEdGraphNode* N : Graph->Nodes)
	{
		if (UAnimGraphNode_MotionMatching* MM = Cast<UAnimGraphNode_MotionMatching>(N)) return MM;
	}
	return nullptr;
}

// Place a freshly-constructed anim graph node into the graph.
static void PlaceAnimNode(UEdGraph* Graph, UEdGraphNode* Node, int32 X, int32 Y)
{
	Graph->AddNode(Node, false, false);
	Node->CreateNewGuid();
	Node->PostPlacedNewNode();
	Node->AllocateDefaultPins();
	Node->NodePosX = X;
	Node->NodePosY = Y;
}

// ─── PoseSearchSchema ─────────────────────────────────────────────────────

TSharedPtr<FJsonValue> FAnimationHandlers::CreatePoseSearchSchema(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;
	FString SkeletonPath;
	if (auto Err = RequireString(Params, TEXT("skeletonPath"), SkeletonPath)) return Err;
	const FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/MotionMatching"));

	USkeleton* Skeleton = LoadAssetByPath<USkeleton>(SkeletonPath);
	if (!Skeleton) return MCPError(FString::Printf(TEXT("Skeleton not found: %s"), *SkeletonPath));

	UMirrorDataTable* MirrorTable = nullptr;
	const FString MirrorPath = OptionalString(Params, TEXT("mirrorDataTablePath"));
	if (!MirrorPath.IsEmpty())
	{
		MirrorTable = LoadAssetByPath<UMirrorDataTable>(MirrorPath);
		if (!MirrorTable) return MCPError(FString::Printf(TEXT("MirrorDataTable not found: %s"), *MirrorPath));
	}

	auto Created = MCPCreateAssetIdempotentNewObject<UPoseSearchSchema>(Name, PackagePath, OptionalString(Params, TEXT("onConflict"), TEXT("skip")), TEXT("PoseSearchSchema"));
	if (Created.EarlyReturn) return Created.EarlyReturn;
	UPoseSearchSchema* Schema = Created.Asset;

	Schema->AddSkeleton(Skeleton, MirrorTable);
	int32 SampleRate = 0;
	if (Params->TryGetNumberField(TEXT("sampleRate"), SampleRate) && SampleRate > 0)
	{
		Schema->SampleRate = SampleRate;
	}
	// Default channels (Trajectory + Pose on the root bone) give a schema you can
	// immediately build an index against; add_pose_search_schema_*_channel refines it.
	if (OptionalBool(Params, TEXT("addDefaultChannels"), true))
	{
		Schema->AddDefaultChannels();
	}
	FinalizeSchema(Schema);
	UEditorAssetLibrary::SaveLoadedAsset(Schema);

	TSharedPtr<FJsonObject> Res = MCPSuccess();
	MCPSetCreated(Res);
	Res->SetStringField(TEXT("path"), Schema->GetPathName());
	Res->SetStringField(TEXT("skeletonPath"), SkeletonPath);
	Res->SetStringField(TEXT("mirrorDataTablePath"), MirrorPath);
	Res->SetNumberField(TEXT("sampleRate"), Schema->SampleRate);
	Res->SetNumberField(TEXT("channelCount"), Schema->GetChannels().Num());
	MCPSetDeleteAssetRollback(Res, Schema->GetPathName());
	return MCPResult(Res);
}

TSharedPtr<FJsonValue> FAnimationHandlers::AddPoseSearchSchemaPoseChannel(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("schemaPath"), AssetPath)) return Err;
	UPoseSearchSchema* Schema = LoadAssetByPath<UPoseSearchSchema>(AssetPath);
	if (!Schema) return MCPError(FString::Printf(TEXT("PoseSearchSchema not found: %s"), *AssetPath));

	const TArray<TSharedPtr<FJsonValue>>* Bones = nullptr;
	if (!Params->TryGetArrayField(TEXT("bones"), Bones) || !Bones || Bones->Num() == 0)
	{
		return MCPError(TEXT("Missing 'bones' (array of {bone, flags?:[velocity,position,rotation,phase], weight?})"));
	}

	Schema->Modify();
	UPoseSearchFeatureChannel_Pose* Channel = NewObject<UPoseSearchFeatureChannel_Pose>(Schema, NAME_None, RF_Transactional);
	double ChannelWeight = 0.0;
	if (Params->TryGetNumberField(TEXT("weight"), ChannelWeight)) Channel->Weight = (float)ChannelWeight;

	TArray<TSharedPtr<FJsonValue>> Added;
	for (const TSharedPtr<FJsonValue>& V : *Bones)
	{
		const TSharedPtr<FJsonObject>* BoneObj = nullptr;
		FString BoneName;
		int32 Flags = int32(EPoseSearchBoneFlags::Position);
		float Weight = 1.f;
		if (V->TryGetObject(BoneObj) && BoneObj && (*BoneObj).IsValid())
		{
			if (!(*BoneObj)->TryGetStringField(TEXT("bone"), BoneName))
				return MCPError(TEXT("Each bone entry needs a 'bone' name"));
			Flags = ParseFlagArray(*BoneObj, TEXT("flags"), BoneFlagTable(), int32(EPoseSearchBoneFlags::Position));
			double W = 0.0;
			if ((*BoneObj)->TryGetNumberField(TEXT("weight"), W)) Weight = (float)W;
		}
		else if (!V->TryGetString(BoneName))
		{
			return MCPError(TEXT("Each bone entry must be an object or a bone-name string"));
		}

		FPoseSearchBone Bone;
		Bone.Reference.BoneName = FName(*BoneName);
		Bone.Flags = Flags;
		Bone.Weight = Weight;
		Channel->SampledBones.Add(Bone);
		Added.Add(MakeShared<FJsonValueString>(BoneName));
	}

	Schema->AddChannel(Channel);
	FinalizeSchema(Schema);
	UEditorAssetLibrary::SaveLoadedAsset(Schema);

	TSharedPtr<FJsonObject> Res = MCPSuccess();
	MCPSetUpdated(Res);
	Res->SetStringField(TEXT("path"), AssetPath);
	Res->SetStringField(TEXT("channelType"), TEXT("Pose"));
	Res->SetNumberField(TEXT("boneCount"), Added.Num());
	Res->SetArrayField(TEXT("bones"), Added);
	Res->SetNumberField(TEXT("channelCount"), Schema->GetChannels().Num());
	return MCPResult(Res);
}

TSharedPtr<FJsonValue> FAnimationHandlers::AddPoseSearchSchemaTrajectoryChannel(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("schemaPath"), AssetPath)) return Err;
	UPoseSearchSchema* Schema = LoadAssetByPath<UPoseSearchSchema>(AssetPath);
	if (!Schema) return MCPError(FString::Printf(TEXT("PoseSearchSchema not found: %s"), *AssetPath));

	const TArray<TSharedPtr<FJsonValue>>* Samples = nullptr;
	if (!Params->TryGetArrayField(TEXT("samples"), Samples) || !Samples || Samples->Num() == 0)
	{
		return MCPError(TEXT("Missing 'samples' (array of {offset, flags?:[position,velocity,facingDirection,...], weight?}). Negative offsets are history, positive are prediction."));
	}

	Schema->Modify();
	UPoseSearchFeatureChannel_Trajectory* Channel = NewObject<UPoseSearchFeatureChannel_Trajectory>(Schema, NAME_None, RF_Transactional);
	double ChannelWeight = 0.0;
	if (Params->TryGetNumberField(TEXT("weight"), ChannelWeight)) Channel->Weight = (float)ChannelWeight;

	int32 Count = 0;
	for (const TSharedPtr<FJsonValue>& V : *Samples)
	{
		const TSharedPtr<FJsonObject>* SampleObj = nullptr;
		if (!V->TryGetObject(SampleObj) || !SampleObj || !(*SampleObj).IsValid())
			return MCPError(TEXT("Each sample must be an object {offset, flags?, weight?}"));

		FPoseSearchTrajectorySample Sample;
		double Offset = 0.0;
		(*SampleObj)->TryGetNumberField(TEXT("offset"), Offset);
		Sample.Offset = (float)Offset;
		Sample.Flags = ParseFlagArray(*SampleObj, TEXT("flags"), TrajectoryFlagTable(), int32(EPoseSearchTrajectoryFlags::Position));
		double W = 0.0;
		if ((*SampleObj)->TryGetNumberField(TEXT("weight"), W)) Sample.Weight = (float)W;
		Channel->Samples.Add(Sample);
		++Count;
	}

	Schema->AddChannel(Channel);
	FinalizeSchema(Schema);
	UEditorAssetLibrary::SaveLoadedAsset(Schema);

	TSharedPtr<FJsonObject> Res = MCPSuccess();
	MCPSetUpdated(Res);
	Res->SetStringField(TEXT("path"), AssetPath);
	Res->SetStringField(TEXT("channelType"), TEXT("Trajectory"));
	Res->SetNumberField(TEXT("sampleCount"), Count);
	Res->SetNumberField(TEXT("channelCount"), Schema->GetChannels().Num());
	return MCPResult(Res);
}

TSharedPtr<FJsonValue> FAnimationHandlers::ReadPoseSearchSchema(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("schemaPath"), AssetPath)) return Err;
	UPoseSearchSchema* Schema = LoadAssetByPath<UPoseSearchSchema>(AssetPath);
	if (!Schema) return MCPError(FString::Printf(TEXT("PoseSearchSchema not found: %s"), *AssetPath));

	TSharedPtr<FJsonObject> Res = MCPSuccess();
	Res->SetStringField(TEXT("path"), AssetPath);
	Res->SetNumberField(TEXT("sampleRate"), Schema->SampleRate);

	TArray<TSharedPtr<FJsonValue>> Skeletons;
	for (const FPoseSearchRoledSkeleton& Roled : Schema->GetRoledSkeletons())
	{
		TSharedPtr<FJsonObject> S = MakeShared<FJsonObject>();
		S->SetStringField(TEXT("role"), Roled.Role.ToString());
		S->SetStringField(TEXT("skeleton"), Roled.Skeleton ? Roled.Skeleton->GetPathName() : FString());
		S->SetStringField(TEXT("mirrorDataTable"), Roled.MirrorDataTable ? Roled.MirrorDataTable->GetPathName() : FString());
		Skeletons.Add(MakeShared<FJsonValueObject>(S));
	}
	Res->SetArrayField(TEXT("skeletons"), Skeletons);

	TArray<TSharedPtr<FJsonValue>> Channels;
	for (const TObjectPtr<UPoseSearchFeatureChannel>& Ch : Schema->GetChannels())
	{
		if (Ch) Channels.Add(MakeShared<FJsonValueString>(Ch->GetClass()->GetName()));
	}
	Res->SetArrayField(TEXT("channels"), Channels);
	Res->SetNumberField(TEXT("channelCount"), Channels.Num());
	return MCPResult(Res);
}

// ─── MirrorDataTable ──────────────────────────────────────────────────────

TSharedPtr<FJsonValue> FAnimationHandlers::CreateMirrorDataTable(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;
	FString SkeletonPath;
	if (auto Err = RequireString(Params, TEXT("skeletonPath"), SkeletonPath)) return Err;
	const FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/MotionMatching"));

	USkeleton* Skeleton = LoadAssetByPath<USkeleton>(SkeletonPath);
	if (!Skeleton) return MCPError(FString::Printf(TEXT("Skeleton not found: %s"), *SkeletonPath));

	auto Created = MCPCreateAssetIdempotentNewObject<UMirrorDataTable>(Name, PackagePath, OptionalString(Params, TEXT("onConflict"), TEXT("skip")), TEXT("MirrorDataTable"));
	if (Created.EarlyReturn) return Created.EarlyReturn;
	UMirrorDataTable* Table = Created.Asset;

	Table->RowStruct = FMirrorTableRow::StaticStruct();
	Table->Skeleton = Skeleton;
	Table->MirrorAxis = EAxis::X;
	Table->bMirrorRootMotion = true;

	// Build find/replace expressions. Default matches the UE mannequin (_l <-> _r suffix).
	auto MethodFromString = [](const FString& M) -> EMirrorFindReplaceMethod::Type
	{
		if (M.Equals(TEXT("prefix"), ESearchCase::IgnoreCase)) return EMirrorFindReplaceMethod::Prefix;
		if (M.Equals(TEXT("regex"), ESearchCase::IgnoreCase) || M.Equals(TEXT("regularexpression"), ESearchCase::IgnoreCase)) return EMirrorFindReplaceMethod::RegularExpression;
		return EMirrorFindReplaceMethod::Suffix;
	};

	Table->MirrorFindReplaceExpressions.Empty();
	const TArray<TSharedPtr<FJsonValue>>* Exprs = nullptr;
	if (Params->TryGetArrayField(TEXT("expressions"), Exprs) && Exprs && Exprs->Num() > 0)
	{
		for (const TSharedPtr<FJsonValue>& V : *Exprs)
		{
			const TSharedPtr<FJsonObject>* E = nullptr;
			if (!V->TryGetObject(E) || !E || !(*E).IsValid()) continue;
			const FString Find = OptionalString(*E, TEXT("find"));
			const FString Replace = OptionalString(*E, TEXT("replace"));
			if (Find.IsEmpty() || Replace.IsEmpty()) continue;
			Table->MirrorFindReplaceExpressions.Add(FMirrorFindReplaceExpression(FName(*Find), FName(*Replace), MethodFromString(OptionalString(*E, TEXT("method"), TEXT("suffix")))));
		}
	}
	if (Table->MirrorFindReplaceExpressions.Num() == 0)
	{
		Table->MirrorFindReplaceExpressions.Add(FMirrorFindReplaceExpression(TEXT("_l"), TEXT("_r"), EMirrorFindReplaceMethod::Suffix));
		Table->MirrorFindReplaceExpressions.Add(FMirrorFindReplaceExpression(TEXT("_r"), TEXT("_l"), EMirrorFindReplaceMethod::Suffix));
	}

	Table->UpdateFromFindReplaceExpressions(UMirrorDataTable::FFindReplaceOptions::Sync());
	UEditorAssetLibrary::SaveLoadedAsset(Table);

	TSharedPtr<FJsonObject> Res = MCPSuccess();
	MCPSetCreated(Res);
	Res->SetStringField(TEXT("path"), Table->GetPathName());
	Res->SetStringField(TEXT("skeletonPath"), SkeletonPath);
	Res->SetNumberField(TEXT("expressionCount"), Table->MirrorFindReplaceExpressions.Num());
	Res->SetNumberField(TEXT("rowCount"), Table->GetRowMap().Num());
	MCPSetDeleteAssetRollback(Res, Table->GetPathName());
	return MCPResult(Res);
}

TSharedPtr<FJsonValue> FAnimationHandlers::ReadMirrorDataTable(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;
	UMirrorDataTable* Table = LoadAssetByPath<UMirrorDataTable>(AssetPath);
	if (!Table) return MCPError(FString::Printf(TEXT("MirrorDataTable not found: %s"), *AssetPath));

	TSharedPtr<FJsonObject> Res = MCPSuccess();
	Res->SetStringField(TEXT("path"), AssetPath);
	Res->SetStringField(TEXT("skeleton"), Table->Skeleton ? Table->Skeleton->GetPathName() : FString());

	TArray<TSharedPtr<FJsonValue>> Rows;
	for (const auto& Pair : Table->GetRowMap())
	{
		const FMirrorTableRow* Row = reinterpret_cast<const FMirrorTableRow*>(Pair.Value);
		if (!Row) continue;
		TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
		R->SetStringField(TEXT("name"), Row->Name.ToString());
		R->SetStringField(TEXT("mirroredName"), Row->MirroredName.ToString());
		Rows.Add(MakeShared<FJsonValueObject>(R));
	}
	Res->SetArrayField(TEXT("rows"), Rows);
	Res->SetNumberField(TEXT("rowCount"), Rows.Num());
	return MCPResult(Res);
}

// ─── NormalizationSet ─────────────────────────────────────────────────────

TSharedPtr<FJsonValue> FAnimationHandlers::CreatePoseSearchNormalizationSet(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;
	const FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/MotionMatching"));

	auto Created = MCPCreateAssetIdempotentNewObject<UPoseSearchNormalizationSet>(Name, PackagePath, OptionalString(Params, TEXT("onConflict"), TEXT("skip")), TEXT("PoseSearchNormalizationSet"));
	if (Created.EarlyReturn) return Created.EarlyReturn;
	UPoseSearchNormalizationSet* NormSet = Created.Asset;

	TArray<TSharedPtr<FJsonValue>> AddedDatabases;
	const TArray<TSharedPtr<FJsonValue>>* Databases = nullptr;
	if (Params->TryGetArrayField(TEXT("databases"), Databases) && Databases)
	{
		for (const TSharedPtr<FJsonValue>& V : *Databases)
		{
			FString DbPath;
			if (!V->TryGetString(DbPath)) continue;
			UPoseSearchDatabase* Db = LoadAssetByPath<UPoseSearchDatabase>(DbPath);
			if (!Db) return MCPError(FString::Printf(TEXT("PoseSearchDatabase not found: %s"), *DbPath));
			NormSet->Databases.Add(Db);
			AddedDatabases.Add(MakeShared<FJsonValueString>(Db->GetPathName()));
		}
	}

	UEditorAssetLibrary::SaveLoadedAsset(NormSet);

	TSharedPtr<FJsonObject> Res = MCPSuccess();
	MCPSetCreated(Res);
	Res->SetStringField(TEXT("path"), NormSet->GetPathName());
	Res->SetNumberField(TEXT("databaseCount"), AddedDatabases.Num());
	Res->SetArrayField(TEXT("databases"), AddedDatabases);
	MCPSetDeleteAssetRollback(Res, NormSet->GetPathName());
	return MCPResult(Res);
}

// ─── Database tuning ──────────────────────────────────────────────────────

TSharedPtr<FJsonValue> FAnimationHandlers::SetPoseSearchDatabaseSettings(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;
	UPoseSearchDatabase* Database = LoadAssetByPath<UPoseSearchDatabase>(AssetPath);
	if (!Database) return MCPError(FString::Printf(TEXT("PoseSearchDatabase not found: %s"), *AssetPath));

	Database->Modify();
	double Num = 0.0;
	if (Params->TryGetNumberField(TEXT("continuingPoseCostBias"), Num)) Database->ContinuingPoseCostBias = (float)Num;
	if (Params->TryGetNumberField(TEXT("baseCostBias"), Num)) Database->BaseCostBias = (float)Num;
	if (Params->TryGetNumberField(TEXT("loopingCostBias"), Num)) Database->LoopingCostBias = (float)Num;
	int32 IntVal = 0;
	if (Params->TryGetNumberField(TEXT("kdTreeQueryNumNeighbors"), IntVal)) Database->KDTreeQueryNumNeighbors = IntVal;

	FString Mode;
	if (Params->TryGetStringField(TEXT("poseSearchMode"), Mode))
	{
		if (Mode.Equals(TEXT("bruteforce"), ESearchCase::IgnoreCase)) Database->PoseSearchMode = EPoseSearchMode::BruteForce;
		else if (Mode.Equals(TEXT("pcakdtree"), ESearchCase::IgnoreCase)) Database->PoseSearchMode = EPoseSearchMode::PCAKDTree;
		else if (Mode.Equals(TEXT("vptree"), ESearchCase::IgnoreCase)) Database->PoseSearchMode = EPoseSearchMode::VPTree;
		else if (Mode.Equals(TEXT("eventonly"), ESearchCase::IgnoreCase)) Database->PoseSearchMode = EPoseSearchMode::EventOnly;
	}

#if WITH_EDITORONLY_DATA
	if (Params->TryGetNumberField(TEXT("numberOfPrincipalComponents"), IntVal)) Database->NumberOfPrincipalComponents = IntVal;
	const FString NormSetPath = OptionalString(Params, TEXT("normalizationSetPath"));
	if (!NormSetPath.IsEmpty())
	{
		UPoseSearchNormalizationSet* NormSet = LoadAssetByPath<UPoseSearchNormalizationSet>(NormSetPath);
		if (!NormSet) return MCPError(FString::Printf(TEXT("PoseSearchNormalizationSet not found: %s"), *NormSetPath));
		Database->NormalizationSet = NormSet;
	}
#endif

	Database->PostEditChange();
	UEditorAssetLibrary::SaveLoadedAsset(Database);

	TSharedPtr<FJsonObject> Res = MCPSuccess();
	MCPSetUpdated(Res);
	Res->SetStringField(TEXT("path"), AssetPath);
	Res->SetNumberField(TEXT("continuingPoseCostBias"), Database->ContinuingPoseCostBias);
	Res->SetNumberField(TEXT("baseCostBias"), Database->BaseCostBias);
	Res->SetNumberField(TEXT("loopingCostBias"), Database->LoopingCostBias);
	Res->SetNumberField(TEXT("kdTreeQueryNumNeighbors"), Database->KDTreeQueryNumNeighbors);
	return MCPResult(Res);
}

// ─── AnimGraph runtime nodes ──────────────────────────────────────────────

TSharedPtr<FJsonValue> FAnimationHandlers::AddMotionMatchingNode(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;
	UAnimBlueprint* AnimBP = LoadAssetByPath<UAnimBlueprint>(AssetPath);
	if (!AnimBP) return MCPError(FString::Printf(TEXT("AnimBlueprint not found: %s"), *AssetPath));

	const FString GraphName = OptionalString(Params, TEXT("graphName"), TEXT("AnimGraph"));
	UEdGraph* Graph = nullptr;
	TArray<UEdGraph*> All;
	AnimBP->GetAllGraphs(All);
	for (UEdGraph* G : All) { if (G && G->GetName() == GraphName) { Graph = G; break; } }
	if (!Graph) return MCPError(FString::Printf(TEXT("Graph not found: %s"), *GraphName));

	UPoseSearchDatabase* Database = nullptr;
	const FString DbPath = OptionalString(Params, TEXT("databasePath"));
	if (!DbPath.IsEmpty())
	{
		Database = LoadAssetByPath<UPoseSearchDatabase>(DbPath);
		if (!Database) return MCPError(FString::Printf(TEXT("PoseSearchDatabase not found: %s"), *DbPath));
	}

	UAnimGraphNode_MotionMatching* MMNode = NewObject<UAnimGraphNode_MotionMatching>(Graph);
	PlaceAnimNode(Graph, MMNode, 0, 0);

	UScriptStruct* NodeStruct = nullptr;
	void* NodeData = GetAnimNodeMemory(MMNode, NodeStruct);
	if (NodeStruct && NodeData)
	{
		if (Database) SetNodeObject(NodeStruct, NodeData, TEXT("Database"), Database);
		double BlendTime = 0.0;
		if (Params->TryGetNumberField(TEXT("blendTime"), BlendTime)) SetNodeFloat(NodeStruct, NodeData, TEXT("BlendTime"), (float)BlendTime);
	}

	bool bConnected = false;
	if (OptionalBool(Params, TEXT("connectToOutput"), true))
	{
		if (UAnimGraphNode_Root* Root = FindOutputPoseNode(Graph))
		{
			UEdGraphPin* RootIn = GetPosePin(Root, EGPD_Input);
			UEdGraphPin* NodeOut = GetPosePin(MMNode, EGPD_Output);
			if (RootIn && NodeOut) { RootIn->BreakAllPinLinks(); NodeOut->MakeLinkTo(RootIn); bConnected = true; }
		}
	}

	FKismetEditorUtilities::CompileBlueprint(AnimBP);
	SaveAssetPackage(AnimBP);

	TSharedPtr<FJsonObject> Res = MCPSuccess();
	MCPSetCreated(Res);
	Res->SetStringField(TEXT("assetPath"), AssetPath);
	Res->SetStringField(TEXT("graphName"), GraphName);
	Res->SetStringField(TEXT("nodeGuid"), MMNode->NodeGuid.ToString());
	Res->SetStringField(TEXT("databasePath"), Database ? Database->GetPathName() : FString());
	Res->SetBoolField(TEXT("connectedToOutput"), bConnected);
	return MCPResult(Res);
}

TSharedPtr<FJsonValue> FAnimationHandlers::AddPoseHistoryNode(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;
	UAnimBlueprint* AnimBP = LoadAssetByPath<UAnimBlueprint>(AssetPath);
	if (!AnimBP) return MCPError(FString::Printf(TEXT("AnimBlueprint not found: %s"), *AssetPath));

	const FString GraphName = OptionalString(Params, TEXT("graphName"), TEXT("AnimGraph"));
	UEdGraph* Graph = nullptr;
	TArray<UEdGraph*> All;
	AnimBP->GetAllGraphs(All);
	for (UEdGraph* G : All) { if (G && G->GetName() == GraphName) { Graph = G; break; } }
	if (!Graph) return MCPError(FString::Printf(TEXT("Graph not found: %s"), *GraphName));

	UAnimGraphNode_PoseSearchHistoryCollector* HistNode = NewObject<UAnimGraphNode_PoseSearchHistoryCollector>(Graph);
	PlaceAnimNode(Graph, HistNode, -300, 0);

	UScriptStruct* NodeStruct = nullptr;
	void* NodeData = GetAnimNodeMemory(HistNode, NodeStruct);
	if (NodeStruct && NodeData)
	{
		int32 IntVal = 0;
		if (Params->TryGetNumberField(TEXT("poseCount"), IntVal)) SetNodeInt(NodeStruct, NodeData, TEXT("PoseCount"), IntVal);
		double Num = 0.0;
		if (Params->TryGetNumberField(TEXT("samplingInterval"), Num)) SetNodeFloat(NodeStruct, NodeData, TEXT("SamplingInterval"), (float)Num);
		// Default to self-generated trajectory so no external trajectory pin is required.
		SetNodeBool(NodeStruct, NodeData, TEXT("bGenerateTrajectory"), OptionalBool(Params, TEXT("generateTrajectory"), true));
		if (Params->TryGetNumberField(TEXT("trajectoryHistoryCount"), IntVal)) SetNodeInt(NodeStruct, NodeData, TEXT("TrajectoryHistoryCount"), IntVal);
		if (Params->TryGetNumberField(TEXT("trajectoryPredictionCount"), IntVal)) SetNodeInt(NodeStruct, NodeData, TEXT("TrajectoryPredictionCount"), IntVal);
	}

	// Insert into the pose chain feeding the output: whatever currently drives the
	// output pose becomes this node's Source, and this node drives the output.
	bool bInserted = false;
	if (OptionalBool(Params, TEXT("insertBeforeOutput"), true))
	{
		if (UAnimGraphNode_Root* Root = FindOutputPoseNode(Graph))
		{
			UEdGraphPin* RootIn = GetPosePin(Root, EGPD_Input);
			UEdGraphPin* HistIn = GetPosePin(HistNode, EGPD_Input);
			UEdGraphPin* HistOut = GetPosePin(HistNode, EGPD_Output);
			if (RootIn && HistIn && HistOut)
			{
				if (RootIn->LinkedTo.Num() > 0)
				{
					UEdGraphPin* PrevSource = RootIn->LinkedTo[0];
					RootIn->BreakAllPinLinks();
					PrevSource->MakeLinkTo(HistIn);
				}
				HistOut->MakeLinkTo(RootIn);
				bInserted = true;
			}
		}
	}

	FKismetEditorUtilities::CompileBlueprint(AnimBP);
	SaveAssetPackage(AnimBP);

	TSharedPtr<FJsonObject> Res = MCPSuccess();
	MCPSetCreated(Res);
	Res->SetStringField(TEXT("assetPath"), AssetPath);
	Res->SetStringField(TEXT("graphName"), GraphName);
	Res->SetStringField(TEXT("nodeGuid"), HistNode->NodeGuid.ToString());
	Res->SetBoolField(TEXT("insertedBeforeOutput"), bInserted);
	return MCPResult(Res);
}

// Drive a Motion Matching node's Database from a ChooserTable, so the database is
// selected at runtime by the character's state (the whole point of a pose-search
// chooser). EvaluateChooser is a BlueprintPure + thread-safe library call whose
// return type follows its ObjectClass input (DeterminesOutputType), so it can feed
// the MM node's Database input pin directly - no anim-node-function graph needed.
// The chooser's columns are read from the ContextObject; default is the anim
// instance itself (Self), matching choosers that branch on AnimBP variables.
TSharedPtr<FJsonValue> FAnimationHandlers::SetMotionMatchingChooser(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;
	UAnimBlueprint* AnimBP = LoadAssetByPath<UAnimBlueprint>(AssetPath);
	if (!AnimBP) return MCPError(FString::Printf(TEXT("AnimBlueprint not found: %s"), *AssetPath));

	FString ChooserPath;
	if (auto Err = RequireStringAlt(Params, TEXT("chooserPath"), TEXT("table"), ChooserPath)) return Err;
	UChooserTable* Chooser = LoadAssetByPath<UChooserTable>(ChooserPath);
	if (!Chooser) return MCPError(FString::Printf(TEXT("ChooserTable not found: %s"), *ChooserPath));

	const FString GraphName = OptionalString(Params, TEXT("graphName"), TEXT("AnimGraph"));
	UEdGraph* Graph = nullptr;
	TArray<UEdGraph*> All;
	AnimBP->GetAllGraphs(All);
	for (UEdGraph* G : All) { if (G && G->GetName() == GraphName) { Graph = G; break; } }
	if (!Graph) return MCPError(FString::Printf(TEXT("Graph not found: %s"), *GraphName));

	UAnimGraphNode_MotionMatching* MMNode = FindMotionMatchingNode(Graph);
	if (!MMNode) return MCPError(TEXT("No Motion Matching node in the graph - call add_motion_matching_node first"));
	UEdGraphPin* DatabasePin = MMNode->FindPin(TEXT("Database"), EGPD_Input);
	if (!DatabasePin) return MCPError(TEXT("Motion Matching node has no Database input pin"));

	UFunction* EvalFunc = UChooserFunctionLibrary::StaticClass()->FindFunctionByName(TEXT("EvaluateChooser"));
	if (!EvalFunc) return MCPError(TEXT("UChooserFunctionLibrary::EvaluateChooser not found"));

	Graph->Modify();

	// EvaluateChooser(ContextObject, ChooserTable, ObjectClass) -> UObject (typed to ObjectClass).
	UK2Node_CallFunction* EvalNode = NewObject<UK2Node_CallFunction>(Graph);
	EvalNode->SetFromFunction(EvalFunc);
	Graph->AddNode(EvalNode, false, false);
	EvalNode->CreateNewGuid();
	EvalNode->PostPlacedNewNode();
	EvalNode->AllocateDefaultPins();
	EvalNode->NodePosX = MMNode->NodePosX - 350;
	EvalNode->NodePosY = MMNode->NodePosY + 150;

	// Chooser literal + result type = PoseSearchDatabase (retypes the return pin).
	if (UEdGraphPin* ChooserPin = EvalNode->FindPin(TEXT("ChooserTable"), EGPD_Input)) ChooserPin->DefaultObject = Chooser;
	if (UEdGraphPin* ClassPin = EvalNode->FindPin(TEXT("ObjectClass"), EGPD_Input)) ClassPin->DefaultObject = UPoseSearchDatabase::StaticClass();
	EvalNode->ReconstructNode();

	// Context object: the anim instance (Self) by default.
	UEdGraphPin* ContextPin = EvalNode->FindPin(TEXT("ContextObject"), EGPD_Input);
	bool bContextWired = false;
	if (ContextPin)
	{
		UK2Node_Self* SelfNode = NewObject<UK2Node_Self>(Graph);
		Graph->AddNode(SelfNode, false, false);
		SelfNode->CreateNewGuid();
		SelfNode->PostPlacedNewNode();
		SelfNode->AllocateDefaultPins();
		SelfNode->NodePosX = EvalNode->NodePosX - 200;
		SelfNode->NodePosY = EvalNode->NodePosY;
		for (UEdGraphPin* Pin : SelfNode->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Output) { Pin->MakeLinkTo(ContextPin); bContextWired = true; break; }
		}
	}

	// Wire the evaluated database into the MM node's Database pin.
	UEdGraphPin* ReturnPin = EvalNode->GetReturnValuePin();
	if (!ReturnPin) return MCPError(TEXT("EvaluateChooser node produced no return pin"));
	DatabasePin->BreakAllPinLinks();
	ReturnPin->MakeLinkTo(DatabasePin);

	Graph->NotifyGraphChanged();
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
	FKismetEditorUtilities::CompileBlueprint(AnimBP);
	SaveAssetPackage(AnimBP);

	const bool bConnected = DatabasePin->LinkedTo.Contains(ReturnPin);

	TSharedPtr<FJsonObject> Res = MCPSuccess();
	MCPSetUpdated(Res);
	Res->SetStringField(TEXT("assetPath"), AssetPath);
	Res->SetStringField(TEXT("graphName"), GraphName);
	Res->SetStringField(TEXT("chooserPath"), Chooser->GetPathName());
	Res->SetStringField(TEXT("motionMatchingNodeGuid"), MMNode->NodeGuid.ToString());
	Res->SetBoolField(TEXT("databasePinDriven"), bConnected);
	Res->SetBoolField(TEXT("contextWiredToSelf"), bContextWired);
	return MCPResult(Res);
}
