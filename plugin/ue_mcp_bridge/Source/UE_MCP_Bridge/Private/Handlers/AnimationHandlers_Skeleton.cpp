// Skeleton editing: the real bones, not just the virtual ones.
//
// Before this file the bridge could list a skeleton's bones and add or remove
// VIRTUAL bones, and that was the whole of it. A real bone could not be added,
// removed, renamed, reparented or moved; a bone's translation retargeting mode
// could not be set; a blend profile could not be created or filled in; curve
// metadata could not be authored; and asset(diff) could compute exactly the
// hierarchy delta that answers "could these two skeletons be registered as
// compatible" and then offer no way to act on the answer.
//
// Six actions close that. Five of them write through engine APIs this module
// already links (USkeleton, UBlendProfile, the curve metadata accessors). The
// sixth, the bone hierarchy itself, goes through USkeletonModifier.
//
// ── Why USkeletonModifier is reached by reflection ──────────────────────────
//
// USkeletonModifier lives in the SkeletalMeshModifiers module of the
// MeshModelingToolset plugin. That plugin is NOT enabled by default, so a hard
// Build.cs dependency would make this module fail to load in any project that
// has it switched off, taking the whole bridge with it. Its editing surface is
// entirely UFUNCTIONs, so a reflected call reaches every one of them without a
// link-time dependency, and a project without the plugin gets a typed
// "skeleton_modifier_unavailable" answer instead of an editor that will not
// start. This is the same trade AssetHandlers_MeshBoolean.cpp makes for
// Geometry Script, and FSkeletonModifierCall below is that file's
// FMeshBooleanCall applied to an instance rather than a CDO.
//
// One consequence is worth naming: USkeletonModifier::SetReadOnly and
// SetReferenceSkeleton are plain C++ methods with no UFUNCTION, so they are
// unreachable this way. Nothing here needs them.
//
// ── Why a begin / edit / commit lifecycle ───────────────────────────────────
//
// USkeletonModifier batches. Every AddBone/RemoveBone/RenameBone/ParentBone/
// SetBoneTransform mutates a working FReferenceSkeleton held by the modifier,
// and nothing reaches the asset until CommitSkeletonToSkeletalMesh runs. A
// per-call commit would re-derive the reference skeleton once per bone, rebuild
// the mesh description each time, and leave a half-edited hierarchy behind if
// call three of five failed. So the session is explicit, exactly the way
// animation(begin_control_rig_edit) / apply_control_rig_edits /
// bake_control_rig_edit is: a caller-supplied sessionTag names the open edit,
// edit_skeleton_bones applies to it, and commit_skeleton_edit is the only thing
// that writes.

#include "AnimationHandlers.h"
#include "HandlerFunctionCall.h"
#include "HandlerUtils.h"

#include "Animation/AnimCurveMetadata.h"
#include "Animation/BlendProfile.h"
#include "Animation/BoneReference.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "Modules/ModuleManager.h"
#include "ReferenceSkeleton.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshModel.h"
#include "ScopedTransaction.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

// A named namespace, not an anonymous one. The module is a unity build and the
// animation handlers share a blob, so an anonymous helper here would merge with
// the anonymous namespaces in AnimationHandlers.cpp and its siblings and the
// second definition of any shared name would be a redefinition (C2084).
// Everything below is also spelled with a Skeleton* prefix so nothing collides
// even if a future grouping change puts two of these namespaces in view.
namespace UE_MCP_SkeletonEdit
{

// ── Typed errors ─────────────────────────────────────────────────────────────

/** A refusal with a machine-readable code, the shape configure_ik_rig uses. */
static TSharedPtr<FJsonValue> SkeletonError(
	const FString& Code,
	const FString& Message,
	TSharedPtr<FJsonObject> Detail = nullptr)
{
	TSharedPtr<FJsonObject> Result = Detail.IsValid() ? Detail : MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), false);
	Result->SetStringField(TEXT("errorCode"), Code);
	Result->SetStringField(TEXT("error"), Message);
	return MCPResult(Result);
}

/** The answer when the engine predates the API this file drives. Kept as a
 *  named branch even though 5.4 is the supported floor, so the version the
 *  bone-editing surface requires is stated rather than implied. Deliberately
 *  NOT static: on a 5.4+ build every call site is preprocessed out, and an
 *  unreferenced internal-linkage function is a warning some configurations
 *  promote to an error. */
TSharedPtr<FJsonValue> SkeletonUnsupportedEngine()
{
	return SkeletonError(
		TEXT("unsupported_engine_version"),
		TEXT("Skeleton bone editing requires Unreal Engine 5.4 or newer: USkeletonModifier, the only "
			 "supported way to add, remove, rename or reparent a real bone, does not exist before it."));
}

// ── Name suggestion ──────────────────────────────────────────────────────────

/** Edit distance, capped. Small names, small cost. */
static int32 SkeletonEditDistance(const FString& A, const FString& B)
{
	const int32 LenA = A.Len();
	const int32 LenB = B.Len();
	if (LenA == 0) return LenB;
	if (LenB == 0) return LenA;

	TArray<int32> Prev;
	TArray<int32> Curr;
	Prev.SetNumUninitialized(LenB + 1);
	Curr.SetNumUninitialized(LenB + 1);
	for (int32 J = 0; J <= LenB; ++J) Prev[J] = J;

	for (int32 I = 1; I <= LenA; ++I)
	{
		Curr[0] = I;
		for (int32 J = 1; J <= LenB; ++J)
		{
			const bool bSame = FChar::ToLower(A[I - 1]) == FChar::ToLower(B[J - 1]);
			Curr[J] = FMath::Min3(
				Prev[J] + 1,
				Curr[J - 1] + 1,
				Prev[J - 1] + (bSame ? 0 : 1));
		}
		Prev = Curr;
	}
	return Prev[LenB];
}

/** The names in Candidates a caller most plausibly meant by Wanted. Substring
 *  hits first, then close spellings. A miss that lists nothing is a dead end;
 *  a miss that names three real bones is a fix. */
static TArray<FString> SkeletonNearMisses(const FString& Wanted, const TArray<FString>& Candidates, int32 Max = 6)
{
	struct FScored { FString Name; int32 Score = 0; };
	TArray<FScored> Scored;
	for (const FString& Candidate : Candidates)
	{
		int32 Score = MAX_int32;
		if (Candidate.Equals(Wanted, ESearchCase::IgnoreCase))
		{
			Score = 0;
		}
		else if (Candidate.Contains(Wanted, ESearchCase::IgnoreCase)
			|| Wanted.Contains(Candidate, ESearchCase::IgnoreCase))
		{
			Score = 1;
		}
		else
		{
			const int32 Distance = SkeletonEditDistance(Wanted, Candidate);
			// Only offer a spelling that is genuinely close; a third of the name
			// wrong is a different bone, not a typo.
			if (Distance <= FMath::Max(2, Wanted.Len() / 3)) Score = 2 + Distance;
		}
		if (Score != MAX_int32) Scored.Add({ Candidate, Score });
	}
	Scored.Sort([](const FScored& A, const FScored& B)
	{
		return A.Score != B.Score ? A.Score < B.Score : A.Name.Compare(B.Name) < 0;
	});
	TArray<FString> Out;
	for (const FScored& Entry : Scored)
	{
		if (Out.Num() >= Max) break;
		Out.Add(Entry.Name);
	}
	return Out;
}

/** The standard "no such bone" refusal, with the near misses attached. */
// Takes the context by FString rather than TCHAR*, because most call sites
// build it with FString::Printf and a literal is convertible either way.
static TSharedPtr<FJsonValue> SkeletonBoneNotFound(
	const FString& BoneName,
	const TArray<FString>& Known,
	const FString& Where)
{
	const TArray<FString> Near = SkeletonNearMisses(BoneName, Known);
	TSharedPtr<FJsonObject> Detail = MakeShared<FJsonObject>();
	Detail->SetStringField(TEXT("bone"), BoneName);
	Detail->SetNumberField(TEXT("boneCount"), Known.Num());
	Detail->SetArrayField(TEXT("nearMisses"), MCPStringListToJson(Near));
	FString Message = FString::Printf(TEXT("Bone '%s' does not exist in %s (%d bones)."),
		*BoneName, *Where, Known.Num());
	if (Near.Num() > 0)
	{
		Message += FString::Printf(TEXT(" Did you mean: %s?"), *FString::Join(Near, TEXT(", ")));
	}
	else
	{
		Message += TEXT(" animation(get_skeleton_info) lists every bone name.");
	}
	return SkeletonError(TEXT("bone_not_found"), Message, Detail);
}

// ── Small JSON helpers ───────────────────────────────────────────────────────

static TSharedPtr<FJsonObject> SkeletonTransformJson(const FTransform& Transform)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetObjectField(TEXT("location"), MCPVec3ToJsonObject(Transform.GetLocation()));
	Obj->SetObjectField(TEXT("rotation"), MCPRotatorToJsonObject(Transform.Rotator()));
	Obj->SetObjectField(TEXT("scale"), MCPVec3ToJsonObject(Transform.GetScale3D()));
	return Obj;
}

static TArray<FString> SkeletonNamesToStrings(const TArray<FName>& Names)
{
	TArray<FString> Out;
	Out.Reserve(Names.Num());
	for (const FName& Name : Names) Out.Add(Name.ToString());
	return Out;
}

/** Read a string field off a JSON object, trimmed. */
static FString SkeletonReadString(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key)
{
	FString Value;
	if (Obj.IsValid()) Obj->TryGetStringField(Key, Value);
	Value.TrimStartAndEndInline();
	return Value;
}

/** Every string in Params[Key], accepting both a bare string and an array. */
static TArray<FString> SkeletonReadStringList(const TSharedPtr<FJsonObject>& Params, const TCHAR* Key)
{
	TArray<FString> Out;
	if (!Params.IsValid()) return Out;
	const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
	if (Params->TryGetArrayField(Key, Array) && Array)
	{
		for (const TSharedPtr<FJsonValue>& Entry : *Array)
		{
			FString Value;
			if (Entry.IsValid() && Entry->TryGetString(Value))
			{
				Value.TrimStartAndEndInline();
				if (!Value.IsEmpty()) Out.Add(Value);
			}
		}
		return Out;
	}
	const FString Single = SkeletonReadString(Params, Key);
	if (!Single.IsEmpty()) Out.Add(Single);
	return Out;
}

// ── The reflected USkeletonModifier surface ──────────────────────────────────

static const TCHAR* const SkeletonModifierClassPath = TEXT("/Script/SkeletalMeshModifiers.SkeletonModifier");
static const TCHAR* const SkeletonModifierModule = TEXT("SkeletalMeshModifiers");
static const TCHAR* const SkeletonModifierPlugin = TEXT("MeshModelingToolset");

/** Load the module if the project has it, then answer whether the class is
 *  there. LoadModule, not LoadModuleChecked: a project without the plugin must
 *  get an error response, not a fatal. */
static UClass* SkeletonModifierClass()
{
	const FName ModuleName(SkeletonModifierModule);
	if (!FModuleManager::Get().IsModuleLoaded(ModuleName))
	{
		FModuleManager::Get().LoadModule(ModuleName);
	}
	return FindObject<UClass>(nullptr, SkeletonModifierClassPath);
}

static TSharedPtr<FJsonValue> SkeletonModifierUnavailable(const FString& Detail)
{
	TSharedPtr<FJsonObject> Extra = MakeShared<FJsonObject>();
	Extra->SetStringField(TEXT("requiredPlugin"), SkeletonModifierPlugin);
	Extra->SetStringField(TEXT("requiredModule"), SkeletonModifierModule);
	Extra->SetStringField(TEXT("requiredClass"), SkeletonModifierClassPath);
	return SkeletonError(
		TEXT("skeleton_modifier_unavailable"),
		FString::Printf(
			TEXT("Bone editing needs USkeletonModifier, which ships in the '%s' plugin: %s ")
			TEXT("Enable Edit > Plugins > Mesh Modeling Toolset, restart the editor, and retry. ")
			TEXT("Everything else in the skeleton surface (retargeting modes, blend profiles, ")
			TEXT("curve metadata, compatible skeletons) works without it."),
			SkeletonModifierPlugin, *Detail),
		Extra);
}

/**
 * One reflected call into USkeletonModifier.
 *
 * The frame is heap allocated and every parameter is initialised through its
 * own FProperty, so a parameter list carrying an FTransform or a TArray<FName>
 * is constructed and destroyed correctly without this file knowing the layout
 * UHT generated. Parameters are addressed BY NAME, so a signature that gains a
 * defaulted argument in a later engine does not silently shift a value into the
 * wrong slot: it fails to find the name and says which one.
 */
struct FSkeletonModifierCall
{
	UObject* Instance = nullptr;
	UFunction* Function = nullptr;
	TArray<uint8> Frame;

	FSkeletonModifierCall() = default;
	FSkeletonModifierCall(const FSkeletonModifierCall&) = delete;
	FSkeletonModifierCall& operator=(const FSkeletonModifierCall&) = delete;
	~FSkeletonModifierCall() { Release(); }

	bool Bind(UObject* InInstance, const TCHAR* FunctionName, FString& OutError)
	{
		Release();
		if (!InInstance)
		{
			OutError = TEXT("the skeleton modifier instance is gone.");
			return false;
		}
		Function = InInstance->GetClass()->FindFunctionByName(FName(FunctionName));
		if (!Function)
		{
			OutError = FString::Printf(
				TEXT("USkeletonModifier has no reflected function named '%s' in this engine build."),
				FunctionName);
			return false;
		}
		Instance = InInstance;
		Frame.SetNumZeroed(FMath::Max<int32>(Function->ParmsSize, 1));
		for (TFieldIterator<FProperty> It(Function); It && (It->PropertyFlags & CPF_Parm); ++It)
		{
			It->InitializeValue_InContainer(Frame.GetData());
		}
		return true;
	}

	void Release()
	{
		if (Function && Frame.Num() > 0)
		{
			MCPFunctionCall::DestroyFrame(Function, Frame.GetData());
		}
		Frame.Reset();
		Function = nullptr;
		Instance = nullptr;
	}

	FProperty* Param(const TCHAR* Name) const
	{
		return Function ? Function->FindPropertyByName(FName(Name)) : nullptr;
	}

	FProperty* ReturnParam() const
	{
		if (!Function) return nullptr;
		for (TFieldIterator<FProperty> It(Function); It && (It->PropertyFlags & CPF_Parm); ++It)
		{
			if (It->PropertyFlags & CPF_ReturnParm) return *It;
		}
		return nullptr;
	}

	bool SetName(const TCHAR* Key, const FName& Value)
	{
		FNameProperty* Prop = CastField<FNameProperty>(Param(Key));
		if (!Prop) return false;
		Prop->SetPropertyValue_InContainer(Frame.GetData(), Value);
		return true;
	}

	bool SetBool(const TCHAR* Key, bool Value)
	{
		FBoolProperty* Prop = CastField<FBoolProperty>(Param(Key));
		if (!Prop) return false;
		Prop->SetPropertyValue_InContainer(Frame.GetData(), Value);
		return true;
	}

	bool SetObject(const TCHAR* Key, UObject* Value)
	{
		FObjectPropertyBase* Prop = CastField<FObjectPropertyBase>(Param(Key));
		if (!Prop) return false;
		Prop->SetObjectPropertyValue_InContainer(Frame.GetData(), Value);
		return true;
	}

	bool SetTransform(const TCHAR* Key, const FTransform& Value)
	{
		FStructProperty* Prop = CastField<FStructProperty>(Param(Key));
		if (!Prop || Prop->Struct != TBaseStructure<FTransform>::Get()) return false;
		*Prop->ContainerPtrToValuePtr<FTransform>(Frame.GetData()) = Value;
		return true;
	}

	void Invoke()
	{
		if (Instance && Function) Instance->ProcessEvent(Function, Frame.GetData());
	}

	bool BoolReturn() const
	{
		FBoolProperty* Prop = CastField<FBoolProperty>(ReturnParam());
		return Prop && Prop->GetPropertyValue_InContainer(Frame.GetData());
	}

	FName NameReturn() const
	{
		FNameProperty* Prop = CastField<FNameProperty>(ReturnParam());
		return Prop ? Prop->GetPropertyValue_InContainer(Frame.GetData()) : NAME_None;
	}

	FTransform TransformReturn() const
	{
		FStructProperty* Prop = CastField<FStructProperty>(ReturnParam());
		if (!Prop || Prop->Struct != TBaseStructure<FTransform>::Get()) return FTransform::Identity;
		return *Prop->ContainerPtrToValuePtr<FTransform>(Frame.GetData());
	}

	TArray<FName> NameArrayReturn() const
	{
		TArray<FName> Out;
		FArrayProperty* Prop = CastField<FArrayProperty>(ReturnParam());
		if (!Prop || !CastField<FNameProperty>(Prop->Inner)) return Out;
		FScriptArrayHelper Helper(Prop, Prop->ContainerPtrToValuePtr<void>(Frame.GetData()));
		for (int32 Index = 0; Index < Helper.Num(); ++Index)
		{
			Out.Add(*reinterpret_cast<FName*>(Helper.GetRawPtr(Index)));
		}
		return Out;
	}
};

// Thin typed wrappers, so the handler body reads as calls rather than as frames.

static TArray<FName> SkeletonModifierAllBones(UObject* Modifier)
{
	FSkeletonModifierCall Call;
	FString Error;
	if (!Call.Bind(Modifier, TEXT("GetAllBoneNames"), Error)) return TArray<FName>();
	Call.Invoke();
	return Call.NameArrayReturn();
}

static FName SkeletonModifierParentOf(UObject* Modifier, const FName& Bone)
{
	FSkeletonModifierCall Call;
	FString Error;
	if (!Call.Bind(Modifier, TEXT("GetParentName"), Error)) return NAME_None;
	if (!Call.SetName(TEXT("InBoneName"), Bone)) return NAME_None;
	Call.Invoke();
	return Call.NameReturn();
}

static FTransform SkeletonModifierTransformOf(UObject* Modifier, const FName& Bone, bool bGlobal)
{
	FSkeletonModifierCall Call;
	FString Error;
	if (!Call.Bind(Modifier, TEXT("GetBoneTransform"), Error)) return FTransform::Identity;
	Call.SetName(TEXT("InBoneName"), Bone);
	Call.SetBool(TEXT("bGlobal"), bGlobal);
	Call.Invoke();
	return Call.TransformReturn();
}

// ── The open-edit session store ──────────────────────────────────────────────

/** One open skeleton edit. Holds the modifier alive, the baseline it started
 *  from (which is what any inverse has to be expressed against), and the edits
 *  applied so far so a read can report them without a second call. */
struct FSkeletonEditSession
{
	FString Tag;
	FString SkeletalMeshPath;
	FString SkeletonPath;
	TStrongObjectPtr<UObject> Modifier;
	TWeakObjectPtr<USkeletalMesh> Mesh;

	/** Bone name -> parent name, as the session opened. */
	TMap<FName, FName> BaselineParent;
	/** Bone name -> local transform, as the session opened. */
	TMap<FName, FTransform> BaselineTransform;
	/** Declared order at open time, so an inverse re-adds parents before children. */
	TArray<FName> BaselineOrder;

	/** Every edit applied since the session opened, in order, as echoed back. */
	TArray<TSharedPtr<FJsonValue>> AppliedEdits;
	bool bAnyLossyEdit = false;
};

/** Session table. Keyed by the caller's sessionTag, exactly the way the Control
 *  Rig edit surface keys on bindingTag: the caller names its own session and
 *  every later call addresses it by that name. */
static TMap<FString, TSharedPtr<FSkeletonEditSession>>& SkeletonSessions()
{
	static TMap<FString, TSharedPtr<FSkeletonEditSession>> Sessions;
	return Sessions;
}

/** Drop sessions whose skeletal mesh was garbage collected or deleted out from
 *  under us, so a stale tag reports "no open session" rather than crashing. */
static void SkeletonPruneDeadSessions()
{
	TArray<FString> Dead;
	for (const TPair<FString, TSharedPtr<FSkeletonEditSession>>& Pair : SkeletonSessions())
	{
		if (!Pair.Value.IsValid() || !Pair.Value->Mesh.IsValid() || !Pair.Value->Modifier.IsValid())
		{
			Dead.Add(Pair.Key);
		}
	}
	for (const FString& Key : Dead) SkeletonSessions().Remove(Key);
}

static FString SkeletonDefaultSessionTag(const USkeletalMesh* Mesh)
{
	return Mesh ? FString::Printf(TEXT("Skel_%s"), *Mesh->GetName()) : FString(TEXT("Skel_Session"));
}

/** Resolve which open session a call means. sessionTag wins; skeletalMeshPath
 *  is accepted as a second spelling and refuses rather than guesses when it
 *  names more than one open session. */
static TSharedPtr<FSkeletonEditSession> SkeletonResolveSession(
	const TSharedPtr<FJsonObject>& Params,
	TSharedPtr<FJsonValue>& OutError)
{
	OutError.Reset();
	SkeletonPruneDeadSessions();

	const FString Tag = SkeletonReadString(Params, TEXT("sessionTag"));
	const FString MeshPath = SkeletonReadString(Params, TEXT("skeletalMeshPath"));

	TArray<FString> OpenTags;
	for (const TPair<FString, TSharedPtr<FSkeletonEditSession>>& Pair : SkeletonSessions())
	{
		OpenTags.Add(Pair.Key);
	}
	OpenTags.Sort();

	if (!Tag.IsEmpty())
	{
		if (TSharedPtr<FSkeletonEditSession>* Found = SkeletonSessions().Find(Tag))
		{
			return *Found;
		}
		TSharedPtr<FJsonObject> Detail = MakeShared<FJsonObject>();
		Detail->SetStringField(TEXT("sessionTag"), Tag);
		Detail->SetArrayField(TEXT("openSessions"), MCPStringListToJson(OpenTags));
		OutError = SkeletonError(
			TEXT("no_open_session"),
			FString::Printf(
				TEXT("No open skeleton edit named '%s'. Call animation(begin_skeleton_edit) with ")
				TEXT("skeletalMeshPath and this sessionTag first; bone edits are batched into a session ")
				TEXT("and only commit_skeleton_edit writes them. Open sessions right now: [%s]."),
				*Tag, OpenTags.Num() ? *FString::Join(OpenTags, TEXT(", ")) : TEXT("none")),
			Detail);
		return nullptr;
	}

	if (!MeshPath.IsEmpty())
	{
		TArray<TSharedPtr<FSkeletonEditSession>> Matches;
		TArray<FString> MatchTags;
		for (const TPair<FString, TSharedPtr<FSkeletonEditSession>>& Pair : SkeletonSessions())
		{
			if (Pair.Value->SkeletalMeshPath.Equals(MeshPath, ESearchCase::IgnoreCase)
				|| (Pair.Value->Mesh.IsValid() && Pair.Value->Mesh->GetPathName().Equals(MeshPath, ESearchCase::IgnoreCase)))
			{
				Matches.Add(Pair.Value);
				MatchTags.Add(Pair.Key);
			}
		}
		if (Matches.Num() == 1) return Matches[0];
		if (Matches.Num() > 1)
		{
			TSharedPtr<FJsonObject> Detail = MakeShared<FJsonObject>();
			Detail->SetArrayField(TEXT("openSessions"), MCPStringListToJson(MatchTags));
			OutError = SkeletonError(
				TEXT("ambiguous_session"),
				FString::Printf(
					TEXT("'%s' has %d open skeleton edits: [%s]. Pass 'sessionTag' to say which one."),
					*MeshPath, Matches.Num(), *FString::Join(MatchTags, TEXT(", "))),
				Detail);
			return nullptr;
		}
	}

	TSharedPtr<FJsonObject> Detail = MakeShared<FJsonObject>();
	Detail->SetArrayField(TEXT("openSessions"), MCPStringListToJson(OpenTags));
	OutError = SkeletonError(
		TEXT("no_open_session"),
		FString::Printf(
			TEXT("No open skeleton edit for this call. Call animation(begin_skeleton_edit) with ")
			TEXT("skeletalMeshPath first, then pass its sessionTag here. Open sessions right now: [%s]."),
			OpenTags.Num() ? *FString::Join(OpenTags, TEXT(", ")) : TEXT("none")),
		Detail);
	return nullptr;
}

/** The state a session is in, in enough detail to verify without another call. */
static TSharedPtr<FJsonObject> SkeletonSessionJson(const TSharedPtr<FSkeletonEditSession>& Session)
{
	TSharedPtr<FJsonObject> Result = MCPSuccess();
	if (!Session.IsValid()) return Result;
	Result->SetStringField(TEXT("sessionTag"), Session->Tag);
	Result->SetStringField(TEXT("skeletalMeshPath"), Session->SkeletalMeshPath);
	Result->SetStringField(TEXT("skeletonPath"), Session->SkeletonPath);
	Result->SetBoolField(TEXT("open"), true);

	const TArray<FName> Bones = SkeletonModifierAllBones(Session->Modifier.Get());
	Result->SetNumberField(TEXT("boneCount"), Bones.Num());
	Result->SetNumberField(TEXT("baselineBoneCount"), Session->BaselineOrder.Num());
	Result->SetArrayField(TEXT("bones"), MCPStringListToJson(SkeletonNamesToStrings(Bones)));
	Result->SetNumberField(TEXT("pendingEditCount"), Session->AppliedEdits.Num());
	Result->SetArrayField(TEXT("pendingEdits"), Session->AppliedEdits);
	Result->SetBoolField(TEXT("hasLossyEdit"), Session->bAnyLossyEdit);
	Result->SetStringField(TEXT("note"),
		TEXT("Nothing is written to the asset until animation(commit_skeleton_edit). "
			 "animation(cancel_skeleton_edit) discards every pending edit."));
	return Result;
}

// ── Skin dependency ──────────────────────────────────────────────────────────

/** Every bone name that some LOD section's vertices actually skin to.
 *
 *  Removing one of these silently reweights or breaks the mesh, which is why
 *  edit_skeleton_bones refuses without 'force'. BoneMap is the per-section list
 *  of skeleton bone indices the section's vertex influences index into, so it
 *  is the honest answer to "do any vertices reference this bone", where
 *  RequiredBones would also include parents nothing is skinned to. */
static void SkeletonCollectSkinnedBones(
	const USkeletalMesh* Mesh,
	TMap<FString, int32>& OutBoneToSectionCount)
{
	OutBoneToSectionCount.Reset();
#if WITH_EDITORONLY_DATA
	if (!Mesh) return;
	const FSkeletalMeshModel* Model = Mesh->GetImportedModel();
	if (!Model) return;
	const FReferenceSkeleton& RefSkeleton = Mesh->GetRefSkeleton();
	for (const FSkeletalMeshLODModel& LOD : Model->LODModels)
	{
		for (const FSkelMeshSection& Section : LOD.Sections)
		{
			for (const FBoneIndexType BoneIndex : Section.BoneMap)
			{
				if (!RefSkeleton.IsValidIndex(static_cast<int32>(BoneIndex))) continue;
				const FString Name = RefSkeleton.GetBoneName(static_cast<int32>(BoneIndex)).ToString();
				OutBoneToSectionCount.FindOrAdd(Name)++;
			}
		}
	}
#endif
}

// ── Retargeting modes ────────────────────────────────────────────────────────

struct FSkeletonRetargetModeEntry
{
	const TCHAR* Name;
	EBoneTranslationRetargetingMode::Type Value;
};

static const FSkeletonRetargetModeEntry SkeletonRetargetModes[] =
{
	{ TEXT("Animation"),         EBoneTranslationRetargetingMode::Animation },
	{ TEXT("Skeleton"),          EBoneTranslationRetargetingMode::Skeleton },
	{ TEXT("AnimationScaled"),   EBoneTranslationRetargetingMode::AnimationScaled },
	{ TEXT("AnimationRelative"), EBoneTranslationRetargetingMode::AnimationRelative },
	{ TEXT("OrientAndScale"),    EBoneTranslationRetargetingMode::OrientAndScale },
};

static bool SkeletonParseRetargetMode(const FString& Text, EBoneTranslationRetargetingMode::Type& OutMode)
{
	for (const FSkeletonRetargetModeEntry& Entry : SkeletonRetargetModes)
	{
		if (Text.Equals(Entry.Name, ESearchCase::IgnoreCase))
		{
			OutMode = Entry.Value;
			return true;
		}
	}
	return false;
}

static FString SkeletonRetargetModeName(EBoneTranslationRetargetingMode::Type Mode)
{
	for (const FSkeletonRetargetModeEntry& Entry : SkeletonRetargetModes)
	{
		if (Entry.Value == Mode) return Entry.Name;
	}
	return TEXT("Animation");
}

static TArray<FString> SkeletonRetargetModeNames()
{
	TArray<FString> Out;
	for (const FSkeletonRetargetModeEntry& Entry : SkeletonRetargetModes) Out.Add(Entry.Name);
	return Out;
}

// ── Blend profile modes ──────────────────────────────────────────────────────

static bool SkeletonParseBlendProfileMode(const FString& Text, EBlendProfileMode& OutMode)
{
	if (Text.Equals(TEXT("TimeFactor"), ESearchCase::IgnoreCase))   { OutMode = EBlendProfileMode::TimeFactor;   return true; }
	if (Text.Equals(TEXT("WeightFactor"), ESearchCase::IgnoreCase)) { OutMode = EBlendProfileMode::WeightFactor; return true; }
	if (Text.Equals(TEXT("BlendMask"), ESearchCase::IgnoreCase))    { OutMode = EBlendProfileMode::BlendMask;    return true; }
	return false;
}

static FString SkeletonBlendProfileModeName(EBlendProfileMode Mode)
{
	switch (Mode)
	{
	case EBlendProfileMode::WeightFactor: return TEXT("WeightFactor");
	case EBlendProfileMode::BlendMask:    return TEXT("BlendMask");
	default:                              return TEXT("TimeFactor");
	}
}

/** Every entry in a blend profile, as JSON. A read that reports the whole
 *  profile is what lets a caller verify a set without a second action. */
static TArray<TSharedPtr<FJsonValue>> SkeletonBlendProfileEntriesJson(const UBlendProfile* Profile)
{
	TArray<TSharedPtr<FJsonValue>> Out;
	if (!Profile) return Out;
	for (int32 Index = 0; Index < Profile->GetNumBlendEntries(); ++Index)
	{
		const FBlendProfileBoneEntry& Entry = Profile->GetEntry(Index);
		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("bone"), Entry.BoneReference.BoneName.ToString());
		Row->SetNumberField(TEXT("scale"), Entry.BlendScale);
		Out.Add(MakeShared<FJsonValueObject>(Row));
	}
	return Out;
}

/** The skeleton's blend profile names, for a miss message and for verification. */
static TArray<FString> SkeletonBlendProfileNames(const USkeleton* Skeleton)
{
	TArray<FString> Out;
	if (!Skeleton) return Out;
	for (const TObjectPtr<UBlendProfile>& Profile : Skeleton->BlendProfiles)
	{
		if (Profile) Out.Add(Profile->GetName());
	}
	return Out;
}

// ── Curve metadata ───────────────────────────────────────────────────────────

/** Every curve metadata entry with its flags, which is exactly what
 *  compare_curves_to_morph_targets reads and could never write. */
static TArray<TSharedPtr<FJsonValue>> SkeletonCurveMetadataJson(const USkeleton* Skeleton)
{
	TArray<TSharedPtr<FJsonValue>> Out;
	if (!Skeleton) return Out;
	TArray<FName> Names;
	Skeleton->GetCurveMetaDataNames(Names);
	Names.Sort(FNameLexicalLess());
	for (const FName& Name : Names)
	{
		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("curve"), Name.ToString());
		if (const FCurveMetaData* Meta = Skeleton->GetCurveMetaData(Name))
		{
			Row->SetBoolField(TEXT("material"), Meta->Type.bMaterial);
			Row->SetBoolField(TEXT("morphTarget"), Meta->Type.bMorphtarget);
			Row->SetNumberField(TEXT("maxLOD"), Meta->MaxLOD);
			TArray<FString> Linked;
			for (const FBoneReference& Bone : Meta->LinkedBones) Linked.Add(Bone.BoneName.ToString());
			Row->SetArrayField(TEXT("linkedBones"), MCPStringListToJson(Linked));
		}
		Out.Add(MakeShared<FJsonValueObject>(Row));
	}
	return Out;
}

/** Load a USkeleton for a mutating action: resolves the path, rejects a
 *  protected mount, and refuses up front when the package cannot be written. */
static USkeleton* SkeletonRequireWritableSkeleton(
	const TSharedPtr<FJsonObject>& Params,
	const TCHAR* Operation,
	FString& OutPath,
	TSharedPtr<FJsonValue>& OutError)
{
	OutError.Reset();
	if (auto Err = RequireString(Params, TEXT("skeletonPath"), OutPath))
	{
		OutError = Err;
		return nullptr;
	}
	if (MCPIsProtectedAssetPath(OutPath))
	{
		OutError = MCPProtectedPathError(OutPath);
		return nullptr;
	}
	USkeleton* Skeleton = LoadAssetByPath<USkeleton>(OutPath);
	if (!Skeleton)
	{
		OutError = MCPAssetLoadError(OutPath, TEXT("Skeleton"));
		return nullptr;
	}
	if (auto Blocked = MCPAssetWriteBlockedError(Skeleton, OutPath, Operation))
	{
		OutError = Blocked;
		return nullptr;
	}
	return Skeleton;
}

/** Every bone name in a skeleton, for near-miss suggestions. */
static TArray<FString> SkeletonBoneNames(const USkeleton* Skeleton)
{
	TArray<FString> Out;
	if (!Skeleton) return Out;
	const FReferenceSkeleton& Ref = Skeleton->GetReferenceSkeleton();
	for (int32 Index = 0; Index < Ref.GetNum(); ++Index)
	{
		Out.Add(Ref.GetBoneName(Index).ToString());
	}
	return Out;
}

} // namespace UE_MCP_SkeletonEdit

// ═════════════════════════════════════════════════════════════════════════════
// begin_skeleton_edit
// ═════════════════════════════════════════════════════════════════════════════

TSharedPtr<FJsonValue> FAnimationHandlers::BeginSkeletonEdit(const TSharedPtr<FJsonObject>& Params)
{
	using namespace UE_MCP_SkeletonEdit;

#if !UE_MCP_HAS_5_4_API
	return SkeletonUnsupportedEngine();
#else
	if (!Params.IsValid()) return SkeletonError(TEXT("invalid_params"), TEXT("Parameters are required"));

	FString MeshPath;
	if (auto Err = RequireString(Params, TEXT("skeletalMeshPath"), MeshPath)) return Err;
	if (MCPIsProtectedAssetPath(MeshPath)) return MCPProtectedPathError(MeshPath);

	USkeletalMesh* Mesh = LoadAssetByPath<USkeletalMesh>(MeshPath);
	if (!Mesh) return MCPAssetLoadError(MeshPath, TEXT("SkeletalMesh"));
	if (auto Blocked = MCPAssetWriteBlockedError(Mesh, MeshPath, TEXT("edit this skeleton")))
	{
		return Blocked;
	}

	const FString Tag = OptionalString(Params, TEXT("sessionTag"), SkeletonDefaultSessionTag(Mesh));
	if (Tag.IsEmpty()) return SkeletonError(TEXT("invalid_params"), TEXT("'sessionTag' must not be empty"));

	SkeletonPruneDeadSessions();

	// Idempotent replay: the tag already names an open edit on this same mesh,
	// so hand back the session rather than opening a second modifier over the
	// same asset and letting two working copies race to commit.
	if (TSharedPtr<FSkeletonEditSession>* Existing = SkeletonSessions().Find(Tag))
	{
		if ((*Existing)->Mesh.Get() == Mesh)
		{
			TSharedPtr<FJsonObject> Result = SkeletonSessionJson(*Existing);
			MCPSetExisted(Result);
			TSharedPtr<FJsonObject> Rollback = MakeShared<FJsonObject>();
			Rollback->SetStringField(TEXT("sessionTag"), Tag);
			MCPSetRollback(Result, TEXT("cancel_skeleton_edit"), Rollback);
			return MCPResult(Result);
		}
		TSharedPtr<FJsonObject> Detail = MakeShared<FJsonObject>();
		Detail->SetStringField(TEXT("sessionTag"), Tag);
		Detail->SetStringField(TEXT("openOnSkeletalMeshPath"), (*Existing)->SkeletalMeshPath);
		return SkeletonError(
			TEXT("session_tag_in_use"),
			FString::Printf(
				TEXT("sessionTag '%s' is already open on a different skeletal mesh ('%s'). ")
				TEXT("Commit or cancel that edit, or pass a different sessionTag."),
				*Tag, *(*Existing)->SkeletalMeshPath),
			Detail);
	}

	UClass* ModifierClass = SkeletonModifierClass();
	if (!ModifierClass)
	{
		return SkeletonModifierUnavailable(TEXT("its class is not registered in this editor."));
	}

	UObject* Modifier = NewObject<UObject>(GetTransientPackage(), ModifierClass);
	if (!Modifier)
	{
		return SkeletonModifierUnavailable(TEXT("the modifier object could not be constructed."));
	}
	// Rooted for the life of the session via TStrongObjectPtr below; until then
	// nothing references it, so keep the construction and the store adjacent.
	TStrongObjectPtr<UObject> Held(Modifier);

	{
		FSkeletonModifierCall Call;
		FString BindError;
		if (!Call.Bind(Modifier, TEXT("SetSkeletalMesh"), BindError))
		{
			return SkeletonModifierUnavailable(BindError);
		}
		if (!Call.SetObject(TEXT("InSkeletalMesh"), Mesh))
		{
			return SkeletonModifierUnavailable(
				TEXT("SetSkeletalMesh has no 'InSkeletalMesh' parameter in this engine build."));
		}
		Call.Invoke();
		if (!Call.BoolReturn())
		{
			return SkeletonError(
				TEXT("modifier_rejected_mesh"),
				FString::Printf(
					TEXT("USkeletonModifier refused '%s'. The mesh has no editable reference skeleton, ")
					TEXT("which usually means it has no imported model (a cooked or nanite-only asset)."),
					*MeshPath));
		}
	}

	TSharedPtr<FSkeletonEditSession> Session = MakeShared<FSkeletonEditSession>();
	Session->Tag = Tag;
	Session->SkeletalMeshPath = Mesh->GetPathName();
	Session->Modifier = Held;
	Session->Mesh = Mesh;
	if (const USkeleton* Skeleton = Mesh->GetSkeleton())
	{
		Session->SkeletonPath = Skeleton->GetPathName();
	}

	// The baseline is captured now, before anything can move, because every
	// inverse this surface emits is expressed against it.
	for (const FName& Bone : SkeletonModifierAllBones(Modifier))
	{
		Session->BaselineOrder.Add(Bone);
		Session->BaselineParent.Add(Bone, SkeletonModifierParentOf(Modifier, Bone));
		Session->BaselineTransform.Add(Bone, SkeletonModifierTransformOf(Modifier, Bone, false));
	}

	SkeletonSessions().Add(Tag, Session);

	TSharedPtr<FJsonObject> Result = SkeletonSessionJson(Session);
	MCPSetCreated(Result);

	TMap<FString, int32> SkinnedBones;
	SkeletonCollectSkinnedBones(Mesh, SkinnedBones);
	Result->SetNumberField(TEXT("skinnedBoneCount"), SkinnedBones.Num());

	TSharedPtr<FJsonObject> Rollback = MakeShared<FJsonObject>();
	Rollback->SetStringField(TEXT("sessionTag"), Tag);
	MCPSetRollback(Result, TEXT("cancel_skeleton_edit"), Rollback);
	return MCPResult(Result);
#endif
}

// ═════════════════════════════════════════════════════════════════════════════
// edit_skeleton_bones
// ═════════════════════════════════════════════════════════════════════════════

TSharedPtr<FJsonValue> FAnimationHandlers::EditSkeletonBones(const TSharedPtr<FJsonObject>& Params)
{
	using namespace UE_MCP_SkeletonEdit;

#if !UE_MCP_HAS_5_4_API
	return SkeletonUnsupportedEngine();
#else
	if (!Params.IsValid()) return SkeletonError(TEXT("invalid_params"), TEXT("Parameters are required"));

	TSharedPtr<FJsonValue> SessionError;
	TSharedPtr<FSkeletonEditSession> Session = SkeletonResolveSession(Params, SessionError);
	if (!Session.IsValid()) return SessionError;

	UObject* Modifier = Session->Modifier.Get();
	USkeletalMesh* Mesh = Session->Mesh.Get();
	if (!Modifier || !Mesh)
	{
		SkeletonSessions().Remove(Session->Tag);
		return SkeletonError(
			TEXT("session_expired"),
			FString::Printf(
				TEXT("The skeleton edit '%s' is no longer valid: its skeletal mesh or modifier was ")
				TEXT("collected. Call animation(begin_skeleton_edit) again."),
				*Session->Tag));
	}

	const TArray<TSharedPtr<FJsonValue>>* EditsArray = nullptr;
	if (!Params->TryGetArrayField(TEXT("edits"), EditsArray) || !EditsArray || EditsArray->Num() == 0)
	{
		return SkeletonError(
			TEXT("invalid_params"),
			TEXT("'edits' must be a non-empty array. Each entry is "
				 "{ op: \"add\"|\"remove\"|\"rename\"|\"reparent\"|\"set_transform\", bone: \"...\", ... }."));
	}

	const bool bForce = OptionalBool(Params, TEXT("force"), false);

	// ── Working model of the hierarchy, so the WHOLE batch is validated before
	//    a single bone moves. Nothing below touches the modifier until every
	//    entry has been checked against the state the previous entries leave.
	TMap<FName, FName> ParentOf;
	TArray<FName> Order;
	for (const FName& Bone : SkeletonModifierAllBones(Modifier))
	{
		Order.Add(Bone);
		ParentOf.Add(Bone, SkeletonModifierParentOf(Modifier, Bone));
	}
	TArray<FString> KnownNames = SkeletonNamesToStrings(Order);

	TMap<FString, int32> SkinnedBones;
	SkeletonCollectSkinnedBones(Mesh, SkinnedBones);

	auto ChildrenOfWorking = [&ParentOf](const FName& Bone)
	{
		TArray<FName> Children;
		for (const TPair<FName, FName>& Pair : ParentOf)
		{
			if (Pair.Value == Bone) Children.Add(Pair.Key);
		}
		Children.Sort(FNameLexicalLess());
		return Children;
	};

	auto DescendantsOfWorking = [&ChildrenOfWorking](const FName& Bone)
	{
		TArray<FName> Out;
		TArray<FName> Stack = ChildrenOfWorking(Bone);
		while (Stack.Num() > 0)
		{
			const FName Current = Stack.Pop();
			Out.Add(Current);
			Stack.Append(ChildrenOfWorking(Current));
		}
		return Out;
	};

	// One validated, normalised edit.
	struct FPlannedEdit
	{
		FString Op;
		FName Bone;
		FName Parent;
		FName NewName;
		FTransform Transform = FTransform::Identity;
		bool bRemoveChildren = false;
		bool bMoveChildren = false;
		bool bNoOp = false;             // already in the requested state
		FString NoOpReason;
		// Captured for the inverse, BEFORE anything mutates.
		FName PriorParent;
		FTransform PriorTransform = FTransform::Identity;
		bool bInverseLossy = false;
		FString InverseNote;
	};

	TArray<FPlannedEdit> Plan;
	Plan.Reserve(EditsArray->Num());

	int32 EditIndex = -1;
	for (const TSharedPtr<FJsonValue>& Entry : *EditsArray)
	{
		++EditIndex;
		const TSharedPtr<FJsonObject> Edit = Entry.IsValid() ? Entry->AsObject() : nullptr;
		if (!Edit.IsValid())
		{
			return SkeletonError(
				TEXT("invalid_params"),
				FString::Printf(TEXT("edits[%d] is not an object."), EditIndex));
		}

		FPlannedEdit Planned;
		Planned.Op = SkeletonReadString(Edit, TEXT("op")).ToLower();
		const FString BoneText = SkeletonReadString(Edit, TEXT("bone"));
		Planned.Bone = FName(*BoneText);

		if (BoneText.IsEmpty())
		{
			return SkeletonError(
				TEXT("invalid_params"),
				FString::Printf(TEXT("edits[%d] has no 'bone'."), EditIndex));
		}

		if (Planned.Op == TEXT("add"))
		{
			const FString ParentText = SkeletonReadString(Edit, TEXT("parent"));
			Planned.Parent = FName(*ParentText);
			Planned.Transform = OptionalTransform(Edit, TEXT("transform"));

			if (ParentOf.Contains(Planned.Bone))
			{
				// Idempotent: adding a bone that is already there is reported,
				// not refused. A rerun of the same batch must converge.
				Planned.bNoOp = true;
				Planned.NoOpReason = TEXT("bone already exists");
				Planned.PriorParent = ParentOf[Planned.Bone];
			}
			else
			{
				if (ParentText.IsEmpty())
				{
					return SkeletonError(
						TEXT("invalid_params"),
						FString::Printf(
							TEXT("edits[%d] adds '%s' with no 'parent'. Every added bone needs one; "
								 "pass the existing root's name to attach at the top."),
							EditIndex, *BoneText));
				}
				if (!ParentOf.Contains(Planned.Parent))
				{
					return SkeletonBoneNotFound(ParentText, KnownNames,
						TEXT("this skeleton edit (as the parent of an added bone)"));
				}
				ParentOf.Add(Planned.Bone, Planned.Parent);
				KnownNames.Add(BoneText);
			}
		}
		else if (Planned.Op == TEXT("remove"))
		{
			Planned.bRemoveChildren = OptionalBool(Edit, TEXT("removeChildren"), false);
			if (!ParentOf.Contains(Planned.Bone))
			{
				Planned.bNoOp = true;
				Planned.NoOpReason = TEXT("bone does not exist");
			}
			else
			{
				Planned.PriorParent = ParentOf[Planned.Bone];
				Planned.PriorTransform = SkeletonModifierTransformOf(Modifier, Planned.Bone, false);

				const TArray<FName> Children = ChildrenOfWorking(Planned.Bone);
				const int32* SkinnedSections = SkinnedBones.Find(BoneText);

				if (Children.Num() > 0 && !Planned.bRemoveChildren && !bForce)
				{
					TSharedPtr<FJsonObject> Detail = MakeShared<FJsonObject>();
					Detail->SetStringField(TEXT("bone"), BoneText);
					Detail->SetArrayField(TEXT("children"),
						MCPStringListToJson(SkeletonNamesToStrings(Children)));
					Detail->SetNumberField(TEXT("childCount"), Children.Num());
					return SkeletonError(
						TEXT("bone_has_children"),
						FString::Printf(
							TEXT("Refusing to remove '%s': %d bones are parented to it [%s]. "
								 "Pass removeChildren:true on this edit to delete the whole subtree, "
								 "or force:true on the call to delete just this bone and reparent its "
								 "children onto its parent. Nothing was changed."),
							*BoneText, Children.Num(),
							*FString::Join(SkeletonNamesToStrings(Children), TEXT(", "))),
						Detail);
				}

				if (SkinnedSections && !bForce)
				{
					TSharedPtr<FJsonObject> Detail = MakeShared<FJsonObject>();
					Detail->SetStringField(TEXT("bone"), BoneText);
					Detail->SetNumberField(TEXT("skinnedSectionCount"), *SkinnedSections);
					return SkeletonError(
						TEXT("bone_is_skinned"),
						FString::Printf(
							TEXT("Refusing to remove '%s': %d mesh section(s) on '%s' skin vertices to it, "
								 "so removing it silently reweights or breaks the mesh. Pass force:true "
								 "to remove it anyway. Nothing was changed."),
							*BoneText, *SkinnedSections, *Session->SkeletalMeshPath),
						Detail);
				}

				if (SkinnedSections)
				{
					Planned.bInverseLossy = true;
					Planned.InverseNote = FString::Printf(
						TEXT("Re-adding '%s' restores the bone, its parent and its transform, but NOT the "
							 "vertex weights the %d skinned section(s) had on it."),
						*BoneText, *SkinnedSections);
				}

				// Update the working model: children either go with the bone or
				// climb to its parent, matching what the modifier will do.
				TArray<FName> Doomed;
				Doomed.Add(Planned.Bone);
				if (Planned.bRemoveChildren) Doomed.Append(DescendantsOfWorking(Planned.Bone));
				if (!Planned.bRemoveChildren)
				{
					for (const FName& Child : Children) ParentOf[Child] = Planned.PriorParent;
					if (Children.Num() > 0)
					{
						Planned.bInverseLossy = true;
						Planned.InverseNote += FString::Printf(
							TEXT(" Its %d child bone(s) were reparented onto '%s' and the inverse does not "
								 "move them back."),
							Children.Num(), *Planned.PriorParent.ToString());
					}
				}
				for (const FName& Gone : Doomed)
				{
					ParentOf.Remove(Gone);
					KnownNames.Remove(Gone.ToString());
				}
				if (Planned.bRemoveChildren && Doomed.Num() > 1)
				{
					Planned.bInverseLossy = true;
					Planned.InverseNote += FString::Printf(
						TEXT(" %d descendant bone(s) went with it and the inverse re-adds only '%s'."),
						Doomed.Num() - 1, *BoneText);
				}
			}
		}
		else if (Planned.Op == TEXT("rename"))
		{
			const FString NewText = SkeletonReadString(Edit, TEXT("newName"));
			Planned.NewName = FName(*NewText);
			if (NewText.IsEmpty())
			{
				return SkeletonError(
					TEXT("invalid_params"),
					FString::Printf(TEXT("edits[%d] renames '%s' but has no 'newName'."), EditIndex, *BoneText));
			}
			if (!ParentOf.Contains(Planned.Bone))
			{
				// Already renamed by an earlier run is the common case, and it
				// is a converged state rather than an error.
				if (ParentOf.Contains(Planned.NewName))
				{
					Planned.bNoOp = true;
					Planned.NoOpReason = TEXT("already renamed");
				}
				else
				{
					return SkeletonBoneNotFound(BoneText, KnownNames, TEXT("this skeleton edit"));
				}
			}
			else if (ParentOf.Contains(Planned.NewName))
			{
				return SkeletonError(
					TEXT("bone_name_taken"),
					FString::Printf(
						TEXT("Cannot rename '%s' to '%s': a bone by that name already exists. "
							 "Nothing was changed."),
						*BoneText, *NewText));
			}
			else
			{
				Planned.PriorParent = ParentOf[Planned.Bone];
				// Rebuild the working model under the new name.
				ParentOf.Remove(Planned.Bone);
				ParentOf.Add(Planned.NewName, Planned.PriorParent);
				for (TPair<FName, FName>& Pair : ParentOf)
				{
					if (Pair.Value == Planned.Bone) Pair.Value = Planned.NewName;
				}
				KnownNames.Remove(BoneText);
				KnownNames.Add(NewText);
			}
		}
		else if (Planned.Op == TEXT("reparent"))
		{
			const FString ParentText = SkeletonReadString(Edit, TEXT("parent"));
			Planned.Parent = FName(*ParentText);
			if (!ParentOf.Contains(Planned.Bone))
			{
				return SkeletonBoneNotFound(BoneText, KnownNames, TEXT("this skeleton edit"));
			}
			if (ParentText.IsEmpty())
			{
				return SkeletonError(
					TEXT("invalid_params"),
					FString::Printf(
						TEXT("edits[%d] reparents '%s' but has no 'parent'."), EditIndex, *BoneText));
			}
			if (!ParentOf.Contains(Planned.Parent))
			{
				return SkeletonBoneNotFound(ParentText, KnownNames,
					TEXT("this skeleton edit (as the new parent)"));
			}
			if (Planned.Parent == Planned.Bone)
			{
				return SkeletonError(
					TEXT("reparent_cycle"),
					FString::Printf(
						TEXT("Cannot parent '%s' to itself. Nothing was changed."), *BoneText));
			}
			const TArray<FName> Descendants = DescendantsOfWorking(Planned.Bone);
			if (Descendants.Contains(Planned.Parent))
			{
				TSharedPtr<FJsonObject> Detail = MakeShared<FJsonObject>();
				Detail->SetStringField(TEXT("bone"), BoneText);
				Detail->SetStringField(TEXT("parent"), ParentText);
				Detail->SetStringField(TEXT("cycleBone"), ParentText);
				Detail->SetArrayField(TEXT("descendants"),
					MCPStringListToJson(SkeletonNamesToStrings(Descendants)));
				return SkeletonError(
					TEXT("reparent_cycle"),
					FString::Printf(
						TEXT("Cannot parent '%s' to '%s': '%s' is already a descendant of '%s', so the "
							 "hierarchy would contain a cycle. Nothing was changed."),
						*BoneText, *ParentText, *ParentText, *BoneText),
					Detail);
			}
			Planned.PriorParent = ParentOf[Planned.Bone];
			if (Planned.PriorParent == Planned.Parent)
			{
				Planned.bNoOp = true;
				Planned.NoOpReason = TEXT("already parented to that bone");
			}
			else
			{
				ParentOf[Planned.Bone] = Planned.Parent;
			}
		}
		else if (Planned.Op == TEXT("set_transform"))
		{
			Planned.bMoveChildren = OptionalBool(Edit, TEXT("moveChildren"), true);
			if (!ParentOf.Contains(Planned.Bone))
			{
				return SkeletonBoneNotFound(BoneText, KnownNames, TEXT("this skeleton edit"));
			}
			if (!Edit->HasField(TEXT("transform")))
			{
				return SkeletonError(
					TEXT("invalid_params"),
					FString::Printf(
						TEXT("edits[%d] sets the transform of '%s' but has no 'transform' "
							 "{ location, rotation, scale }."),
						EditIndex, *BoneText));
			}
			Planned.Transform = OptionalTransform(Edit, TEXT("transform"));
			Planned.PriorTransform = SkeletonModifierTransformOf(Modifier, Planned.Bone, false);
			Planned.PriorParent = ParentOf[Planned.Bone];
		}
		else
		{
			return SkeletonError(
				TEXT("invalid_params"),
				FString::Printf(
					TEXT("edits[%d] has op '%s'. Valid ops: add, remove, rename, reparent, set_transform."),
					EditIndex, *Planned.Op));
		}

		Plan.Add(MoveTemp(Planned));
	}

	// ── Apply. Every entry above validated against the state its predecessors
	//    leave, so the only failures left are the modifier refusing a call.
	TArray<TSharedPtr<FJsonValue>> Applied;
	TArray<TSharedPtr<FJsonValue>> InverseEdits;
	int32 ChangedCount = 0;
	int32 NoOpCount = 0;
	bool bAnyLossy = false;
	FString LossyNote;

	for (int32 Index = 0; Index < Plan.Num(); ++Index)
	{
		const FPlannedEdit& Planned = Plan[Index];

		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("op"), Planned.Op);
		Row->SetStringField(TEXT("bone"), Planned.Bone.ToString());

		if (Planned.bNoOp)
		{
			++NoOpCount;
			Row->SetBoolField(TEXT("changed"), false);
			Row->SetBoolField(TEXT("alreadyApplied"), true);
			Row->SetStringField(TEXT("reason"), Planned.NoOpReason);
			Applied.Add(MakeShared<FJsonValueObject>(Row));
			continue;
		}

		FSkeletonModifierCall Call;
		FString BindError;
		bool bOk = false;

		if (Planned.Op == TEXT("add"))
		{
			if (!Call.Bind(Modifier, TEXT("AddBone"), BindError)) return SkeletonModifierUnavailable(BindError);
			Call.SetName(TEXT("InBoneName"), Planned.Bone);
			Call.SetName(TEXT("InParentName"), Planned.Parent);
			Call.SetTransform(TEXT("InTransform"), Planned.Transform);
			Call.Invoke();
			bOk = Call.BoolReturn();
			Row->SetStringField(TEXT("parent"), Planned.Parent.ToString());
			Row->SetObjectField(TEXT("transform"), SkeletonTransformJson(Planned.Transform));

			TSharedPtr<FJsonObject> Inverse = MakeShared<FJsonObject>();
			Inverse->SetStringField(TEXT("op"), TEXT("remove"));
			Inverse->SetStringField(TEXT("bone"), Planned.Bone.ToString());
			Inverse->SetBoolField(TEXT("removeChildren"), true);
			InverseEdits.Insert(MakeShared<FJsonValueObject>(Inverse), 0);
		}
		else if (Planned.Op == TEXT("remove"))
		{
			if (!Call.Bind(Modifier, TEXT("RemoveBone"), BindError)) return SkeletonModifierUnavailable(BindError);
			Call.SetName(TEXT("InBoneName"), Planned.Bone);
			Call.SetBool(TEXT("bRemoveChildren"), Planned.bRemoveChildren);
			Call.Invoke();
			bOk = Call.BoolReturn();
			Row->SetBoolField(TEXT("removeChildren"), Planned.bRemoveChildren);
			Row->SetStringField(TEXT("priorParent"), Planned.PriorParent.ToString());
			Row->SetObjectField(TEXT("priorTransform"), SkeletonTransformJson(Planned.PriorTransform));

			// The inverse needs the parent and the transform, which is why both
			// were captured before the call rather than read back after it.
			TSharedPtr<FJsonObject> Inverse = MakeShared<FJsonObject>();
			Inverse->SetStringField(TEXT("op"), TEXT("add"));
			Inverse->SetStringField(TEXT("bone"), Planned.Bone.ToString());
			Inverse->SetStringField(TEXT("parent"), Planned.PriorParent.ToString());
			Inverse->SetObjectField(TEXT("transform"), SkeletonTransformJson(Planned.PriorTransform));
			InverseEdits.Insert(MakeShared<FJsonValueObject>(Inverse), 0);
		}
		else if (Planned.Op == TEXT("rename"))
		{
			if (!Call.Bind(Modifier, TEXT("RenameBone"), BindError)) return SkeletonModifierUnavailable(BindError);
			Call.SetName(TEXT("InOldBoneName"), Planned.Bone);
			Call.SetName(TEXT("InNewBoneName"), Planned.NewName);
			Call.Invoke();
			bOk = Call.BoolReturn();
			Row->SetStringField(TEXT("newName"), Planned.NewName.ToString());

			TSharedPtr<FJsonObject> Inverse = MakeShared<FJsonObject>();
			Inverse->SetStringField(TEXT("op"), TEXT("rename"));
			Inverse->SetStringField(TEXT("bone"), Planned.NewName.ToString());
			Inverse->SetStringField(TEXT("newName"), Planned.Bone.ToString());
			InverseEdits.Insert(MakeShared<FJsonValueObject>(Inverse), 0);
		}
		else if (Planned.Op == TEXT("reparent"))
		{
			if (!Call.Bind(Modifier, TEXT("ParentBone"), BindError)) return SkeletonModifierUnavailable(BindError);
			Call.SetName(TEXT("InBoneName"), Planned.Bone);
			Call.SetName(TEXT("InParentName"), Planned.Parent);
			Call.Invoke();
			bOk = Call.BoolReturn();
			Row->SetStringField(TEXT("parent"), Planned.Parent.ToString());
			Row->SetStringField(TEXT("priorParent"), Planned.PriorParent.ToString());

			TSharedPtr<FJsonObject> Inverse = MakeShared<FJsonObject>();
			Inverse->SetStringField(TEXT("op"), TEXT("reparent"));
			Inverse->SetStringField(TEXT("bone"), Planned.Bone.ToString());
			Inverse->SetStringField(TEXT("parent"), Planned.PriorParent.ToString());
			InverseEdits.Insert(MakeShared<FJsonValueObject>(Inverse), 0);
		}
		else // set_transform
		{
			if (!Call.Bind(Modifier, TEXT("SetBoneTransform"), BindError)) return SkeletonModifierUnavailable(BindError);
			Call.SetName(TEXT("InBoneName"), Planned.Bone);
			Call.SetTransform(TEXT("InNewTransform"), Planned.Transform);
			Call.SetBool(TEXT("bMoveChildren"), Planned.bMoveChildren);
			Call.Invoke();
			bOk = Call.BoolReturn();
			Row->SetObjectField(TEXT("transform"), SkeletonTransformJson(Planned.Transform));
			Row->SetObjectField(TEXT("priorTransform"), SkeletonTransformJson(Planned.PriorTransform));
			Row->SetBoolField(TEXT("moveChildren"), Planned.bMoveChildren);

			TSharedPtr<FJsonObject> Inverse = MakeShared<FJsonObject>();
			Inverse->SetStringField(TEXT("op"), TEXT("set_transform"));
			Inverse->SetStringField(TEXT("bone"), Planned.Bone.ToString());
			Inverse->SetObjectField(TEXT("transform"), SkeletonTransformJson(Planned.PriorTransform));
			Inverse->SetBoolField(TEXT("moveChildren"), Planned.bMoveChildren);
			InverseEdits.Insert(MakeShared<FJsonValueObject>(Inverse), 0);
		}

		Row->SetBoolField(TEXT("changed"), bOk);
		if (Planned.bInverseLossy)
		{
			bAnyLossy = true;
			Row->SetBoolField(TEXT("rollbackLossy"), true);
			Row->SetStringField(TEXT("rollbackNote"), Planned.InverseNote);
			LossyNote += Planned.InverseNote + TEXT(" ");
		}
		Applied.Add(MakeShared<FJsonValueObject>(Row));

		if (!bOk)
		{
			// The batch validated, so a refusal here is the modifier disagreeing
			// with the plan. Say which entry, and say plainly that earlier
			// entries in this batch are still pending in the session.
			TSharedPtr<FJsonObject> Detail = MakeShared<FJsonObject>();
			Detail->SetStringField(TEXT("sessionTag"), Session->Tag);
			Detail->SetNumberField(TEXT("failedEditIndex"), Index);
			Detail->SetArrayField(TEXT("applied"), Applied);
			Detail->SetArrayField(TEXT("inverseEdits"), InverseEdits);
			Detail->SetBoolField(TEXT("committed"), false);
			return SkeletonError(
				TEXT("modifier_rejected_edit"),
				FString::Printf(
					TEXT("USkeletonModifier refused edits[%d] (%s '%s'). The %d edit(s) before it are "
						 "still pending in session '%s' and nothing has been written to the asset. "
						 "Call animation(cancel_skeleton_edit) to discard them, or replay "
						 "'inverseEdits' to undo them."),
					Index, *Planned.Op, *Planned.Bone.ToString(), Index, *Session->Tag),
				Detail);
		}
		++ChangedCount;
	}

	Session->AppliedEdits.Append(Applied);
	Session->bAnyLossyEdit = Session->bAnyLossyEdit || bAnyLossy;

	TSharedPtr<FJsonObject> Result = SkeletonSessionJson(Session);
	if (ChangedCount > 0) MCPSetUpdated(Result);
	Result->SetBoolField(TEXT("updated"), ChangedCount > 0);
	Result->SetNumberField(TEXT("editsApplied"), ChangedCount);
	Result->SetNumberField(TEXT("editsAlreadyApplied"), NoOpCount);
	Result->SetArrayField(TEXT("edits"), Applied);
	Result->SetBoolField(TEXT("committed"), false);
	Result->SetBoolField(TEXT("forceUsed"), bForce);

	if (bAnyLossy)
	{
		Result->SetBoolField(TEXT("rollbackLossy"), true);
		Result->SetStringField(TEXT("rollbackNote"), LossyNote.TrimStartAndEnd());
	}

	TSharedPtr<FJsonObject> Rollback = MakeShared<FJsonObject>();
	Rollback->SetStringField(TEXT("sessionTag"), Session->Tag);
	Rollback->SetArrayField(TEXT("edits"), InverseEdits);
	Rollback->SetBoolField(TEXT("force"), true);
	MCPSetRollback(Result, TEXT("edit_skeleton_bones"), Rollback);
	return MCPResult(Result);
#endif
}

// ═════════════════════════════════════════════════════════════════════════════
// commit_skeleton_edit
// ═════════════════════════════════════════════════════════════════════════════

TSharedPtr<FJsonValue> FAnimationHandlers::CommitSkeletonEdit(const TSharedPtr<FJsonObject>& Params)
{
	using namespace UE_MCP_SkeletonEdit;

#if !UE_MCP_HAS_5_4_API
	return SkeletonUnsupportedEngine();
#else
	if (!Params.IsValid()) return SkeletonError(TEXT("invalid_params"), TEXT("Parameters are required"));

	TSharedPtr<FJsonValue> SessionError;
	TSharedPtr<FSkeletonEditSession> Session = SkeletonResolveSession(Params, SessionError);
	if (!Session.IsValid()) return SessionError;

	UObject* Modifier = Session->Modifier.Get();
	USkeletalMesh* Mesh = Session->Mesh.Get();
	if (!Modifier || !Mesh)
	{
		SkeletonSessions().Remove(Session->Tag);
		return SkeletonError(
			TEXT("session_expired"),
			FString::Printf(
				TEXT("The skeleton edit '%s' is no longer valid: its skeletal mesh or modifier was "
					 "collected. Nothing was written."),
				*Session->Tag));
	}

	if (auto Blocked = MCPAssetWriteBlockedError(Mesh, Session->SkeletalMeshPath, TEXT("commit this skeleton edit")))
	{
		return Blocked;
	}

	// The inverse has to be built from the baseline BEFORE the commit, because
	// after it the modifier's working skeleton is the new one and the old parent
	// and transform of every bone are gone.
	TArray<TSharedPtr<FJsonValue>> InverseEdits;
	{
		const TArray<FName> Current = SkeletonModifierAllBones(Modifier);
		TSet<FName> CurrentSet(Current);
		// Re-add what was removed, parents first (baseline order is hierarchical).
		for (const FName& Bone : Session->BaselineOrder)
		{
			if (CurrentSet.Contains(Bone)) continue;
			TSharedPtr<FJsonObject> Inverse = MakeShared<FJsonObject>();
			Inverse->SetStringField(TEXT("op"), TEXT("add"));
			Inverse->SetStringField(TEXT("bone"), Bone.ToString());
			Inverse->SetStringField(TEXT("parent"), Session->BaselineParent.FindRef(Bone).ToString());
			Inverse->SetObjectField(TEXT("transform"),
				SkeletonTransformJson(Session->BaselineTransform.FindRef(Bone)));
			InverseEdits.Add(MakeShared<FJsonValueObject>(Inverse));
		}
		// Remove what was added.
		for (const FName& Bone : Current)
		{
			if (Session->BaselineParent.Contains(Bone)) continue;
			TSharedPtr<FJsonObject> Inverse = MakeShared<FJsonObject>();
			Inverse->SetStringField(TEXT("op"), TEXT("remove"));
			Inverse->SetStringField(TEXT("bone"), Bone.ToString());
			Inverse->SetBoolField(TEXT("removeChildren"), true);
			InverseEdits.Add(MakeShared<FJsonValueObject>(Inverse));
		}
		// Restore parent and transform for what survived but moved.
		for (const FName& Bone : Current)
		{
			const FName* BaselineParent = Session->BaselineParent.Find(Bone);
			if (!BaselineParent) continue;
			if (SkeletonModifierParentOf(Modifier, Bone) != *BaselineParent)
			{
				TSharedPtr<FJsonObject> Inverse = MakeShared<FJsonObject>();
				Inverse->SetStringField(TEXT("op"), TEXT("reparent"));
				Inverse->SetStringField(TEXT("bone"), Bone.ToString());
				Inverse->SetStringField(TEXT("parent"), BaselineParent->ToString());
				InverseEdits.Add(MakeShared<FJsonValueObject>(Inverse));
			}
			const FTransform* BaselineTransform = Session->BaselineTransform.Find(Bone);
			if (BaselineTransform
				&& !SkeletonModifierTransformOf(Modifier, Bone, false).Equals(*BaselineTransform, 0.0001f))
			{
				TSharedPtr<FJsonObject> Inverse = MakeShared<FJsonObject>();
				Inverse->SetStringField(TEXT("op"), TEXT("set_transform"));
				Inverse->SetStringField(TEXT("bone"), Bone.ToString());
				Inverse->SetObjectField(TEXT("transform"), SkeletonTransformJson(*BaselineTransform));
				Inverse->SetBoolField(TEXT("moveChildren"), false);
				InverseEdits.Add(MakeShared<FJsonValueObject>(Inverse));
			}
		}
	}

	const int32 PendingEdits = Session->AppliedEdits.Num();
	if (PendingEdits == 0)
	{
		// Nothing to write. Close the session and say so rather than rebuilding
		// the reference skeleton and dirtying two packages for no reason.
		TSharedPtr<FJsonObject> Result = SkeletonSessionJson(Session);
		Result->SetBoolField(TEXT("open"), false);
		Result->SetBoolField(TEXT("committed"), false);
		Result->SetBoolField(TEXT("alreadyUpToDate"), true);
		Result->SetBoolField(TEXT("updated"), false);
		Result->SetStringField(TEXT("note"),
			TEXT("The session had no pending edits, so nothing was written. The session is now closed."));
		SkeletonSessions().Remove(Session->Tag);
		return MCPResult(Result);
	}

	USkeleton* Skeleton = Mesh->GetSkeleton();

	bool bCommitted = false;
	{
		const FScopedTransaction Transaction(
			NSLOCTEXT("UE_MCP", "CommitSkeletonEdit", "Commit Skeleton Edit"));
		Mesh->Modify();
		if (Skeleton) Skeleton->Modify();

		FSkeletonModifierCall Call;
		FString BindError;
		if (!Call.Bind(Modifier, TEXT("CommitSkeletonToSkeletalMesh"), BindError))
		{
			return SkeletonModifierUnavailable(BindError);
		}
		Call.Invoke();
		bCommitted = Call.BoolReturn();
	}

	if (!bCommitted)
	{
		TSharedPtr<FJsonObject> Detail = MakeShared<FJsonObject>();
		Detail->SetStringField(TEXT("sessionTag"), Session->Tag);
		Detail->SetNumberField(TEXT("pendingEditCount"), PendingEdits);
		Detail->SetBoolField(TEXT("committed"), false);
		return SkeletonError(
			TEXT("commit_failed"),
			FString::Printf(
				TEXT("USkeletonModifier refused to commit session '%s' to '%s'. The %d pending edit(s) "
					 "are still held in the session; the output log carries the engine's reason. "
					 "Call animation(cancel_skeleton_edit) to discard them."),
				*Session->Tag, *Session->SkeletalMeshPath, PendingEdits),
			Detail);
	}

	Mesh->PostEditChange();
	if (Skeleton) Skeleton->PostEditChange();

	FString MeshSaveReason;
	const bool bMeshSaved = SaveAssetPackageChecked(Mesh, MeshSaveReason);
	FString SkeletonSaveReason;
	const bool bSkeletonSaved = Skeleton ? SaveAssetPackageChecked(Skeleton, SkeletonSaveReason) : true;

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetStringField(TEXT("sessionTag"), Session->Tag);
	Result->SetStringField(TEXT("skeletalMeshPath"), Session->SkeletalMeshPath);
	Result->SetStringField(TEXT("skeletonPath"), Session->SkeletonPath);
	Result->SetBoolField(TEXT("open"), false);
	Result->SetBoolField(TEXT("committed"), true);
	MCPSetUpdated(Result);
	Result->SetNumberField(TEXT("editsCommitted"), PendingEdits);
	Result->SetArrayField(TEXT("edits"), Session->AppliedEdits);

	const FReferenceSkeleton& Committed = Mesh->GetRefSkeleton();
	TArray<FString> CommittedBones;
	for (int32 Index = 0; Index < Committed.GetNum(); ++Index)
	{
		CommittedBones.Add(Committed.GetBoneName(Index).ToString());
	}
	Result->SetNumberField(TEXT("boneCount"), CommittedBones.Num());
	Result->SetArrayField(TEXT("bones"), MCPStringListToJson(CommittedBones));
	Result->SetBoolField(TEXT("skeletonSaved"), bSkeletonSaved);
	if (!bSkeletonSaved && !SkeletonSaveReason.IsEmpty())
	{
		Result->SetStringField(TEXT("skeletonSaveError"), SkeletonSaveReason);
	}

	// A commit is not reversible in full: the reference skeleton is rebuilt, the
	// mesh description is re-derived, and dependent AnimSequences, physics
	// bodies and IK assets were fixed up against the new hierarchy. The inverse
	// restores the HIERARCHY and nothing else, so it says so out loud.
	Result->SetBoolField(TEXT("rollbackLossy"), true);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("The inverse replays this edit backwards into a fresh session and restores the bone "
			 "hierarchy, parents and reference-pose transforms. It does NOT restore skin weights "
			 "dropped with a removed bone, nor the fix-ups the commit applied to dependent "
			 "AnimSequences, physics assets, IK rigs or blend profiles. Treat it as a repair, "
			 "not an undo."));
	TSharedPtr<FJsonObject> Rollback = MakeShared<FJsonObject>();
	Rollback->SetStringField(TEXT("skeletalMeshPath"), Session->SkeletalMeshPath);
	Rollback->SetStringField(TEXT("sessionTag"), Session->Tag + TEXT("_Undo"));
	Rollback->SetArrayField(TEXT("edits"), InverseEdits);
	Rollback->SetBoolField(TEXT("force"), true);
	Rollback->SetBoolField(TEXT("autoCommit"), true);
	MCPSetRollback(Result, TEXT("edit_skeleton_bones"), Rollback);

	MCPNoteSaveOutcome(Result, Session->SkeletalMeshPath, bMeshSaved, MeshSaveReason);
	SkeletonSessions().Remove(Session->Tag);
	return MCPResult(Result);
#endif
}

// ═════════════════════════════════════════════════════════════════════════════
// cancel_skeleton_edit
// ═════════════════════════════════════════════════════════════════════════════

TSharedPtr<FJsonValue> FAnimationHandlers::CancelSkeletonEdit(const TSharedPtr<FJsonObject>& Params)
{
	using namespace UE_MCP_SkeletonEdit;

#if !UE_MCP_HAS_5_4_API
	return SkeletonUnsupportedEngine();
#else
	if (!Params.IsValid()) return SkeletonError(TEXT("invalid_params"), TEXT("Parameters are required"));

	SkeletonPruneDeadSessions();
	const FString Tag = SkeletonReadString(Params, TEXT("sessionTag"));
	const FString MeshPath = SkeletonReadString(Params, TEXT("skeletalMeshPath"));

	TSharedPtr<FJsonValue> SessionError;
	TSharedPtr<FSkeletonEditSession> Session = SkeletonResolveSession(Params, SessionError);

	if (!Session.IsValid())
	{
		// Cancelling a session that is not open is the converged state, not an
		// error: a rollback replay must not fail because the thing it was
		// undoing was already discarded.
		TSharedPtr<FJsonObject> Result = MCPSuccess();
		Result->SetStringField(TEXT("sessionTag"), Tag);
		if (!MeshPath.IsEmpty()) Result->SetStringField(TEXT("skeletalMeshPath"), MeshPath);
		Result->SetBoolField(TEXT("open"), false);
		Result->SetBoolField(TEXT("alreadyClosed"), true);
		Result->SetBoolField(TEXT("cancelled"), false);
		Result->SetStringField(TEXT("note"),
			TEXT("No skeleton edit was open under that name, so there was nothing to discard. "
				 "Nothing was written to any asset."));
		return MCPResult(Result);
	}

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetStringField(TEXT("sessionTag"), Session->Tag);
	Result->SetStringField(TEXT("skeletalMeshPath"), Session->SkeletalMeshPath);
	Result->SetStringField(TEXT("skeletonPath"), Session->SkeletonPath);
	Result->SetBoolField(TEXT("open"), false);
	Result->SetBoolField(TEXT("cancelled"), true);
	Result->SetBoolField(TEXT("committed"), false);
	Result->SetNumberField(TEXT("discardedEditCount"), Session->AppliedEdits.Num());
	Result->SetArrayField(TEXT("discardedEdits"), Session->AppliedEdits);
	Result->SetStringField(TEXT("note"),
		TEXT("The working copy was thrown away. Nothing was ever written to the skeletal mesh or "
			 "skeleton, so the assets on disk are exactly as they were before begin_skeleton_edit."));

	// Reopening restores the session but NOT the edits that were discarded,
	// which is the whole point of a cancel. Say so rather than implying an undo.
	Result->SetBoolField(TEXT("rollbackLossy"), Session->AppliedEdits.Num() > 0);
	if (Session->AppliedEdits.Num() > 0)
	{
		Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
			TEXT("The inverse reopens an edit session on '%s' but does NOT replay the %d discarded "
				 "edit(s). Re-send them to animation(edit_skeleton_bones) if they are still wanted; "
				 "they are listed under 'discardedEdits'."),
			*Session->SkeletalMeshPath, Session->AppliedEdits.Num()));
	}
	TSharedPtr<FJsonObject> Rollback = MakeShared<FJsonObject>();
	Rollback->SetStringField(TEXT("skeletalMeshPath"), Session->SkeletalMeshPath);
	Rollback->SetStringField(TEXT("sessionTag"), Session->Tag);
	MCPSetRollback(Result, TEXT("begin_skeleton_edit"), Rollback);

	SkeletonSessions().Remove(Session->Tag);
	return MCPResult(Result);
#endif
}

// ═════════════════════════════════════════════════════════════════════════════
// set_bone_retargeting
// ═════════════════════════════════════════════════════════════════════════════

TSharedPtr<FJsonValue> FAnimationHandlers::SetBoneRetargeting(const TSharedPtr<FJsonObject>& Params)
{
	using namespace UE_MCP_SkeletonEdit;

	if (!Params.IsValid()) return SkeletonError(TEXT("invalid_params"), TEXT("Parameters are required"));

	FString SkeletonPath;
	TSharedPtr<FJsonValue> LoadError;
	USkeleton* Skeleton = SkeletonRequireWritableSkeleton(
		Params, TEXT("set bone retargeting on this skeleton"), SkeletonPath, LoadError);
	if (!Skeleton) return LoadError;

	const FReferenceSkeleton& RefSkeleton = Skeleton->GetReferenceSkeleton();
	const TArray<FString> KnownBones = SkeletonBoneNames(Skeleton);

	// ── The inverse form, handled first ──────────────────────────────────────
	// The rollback this action emits is a per-bone list of prior modes, which
	// has no single 'mode' to require. Reading it here is what makes the
	// rollback replay through this same action rather than needing a second one.
	const TArray<TSharedPtr<FJsonValue>>* RestoreArray = nullptr;
	if (Params->TryGetArrayField(TEXT("restore"), RestoreArray) && RestoreArray)
	{
		TArray<TSharedPtr<FJsonValue>> RestoreRows;
		TArray<TSharedPtr<FJsonValue>> PriorForInverse;
		int32 RestoreChanged = 0;
		{
			const FScopedTransaction Transaction(
				NSLOCTEXT("UE_MCP", "RestoreBoneRetargeting", "Restore Bone Translation Retargeting"));
			Skeleton->Modify();
			for (const TSharedPtr<FJsonValue>& RestoreEntry : *RestoreArray)
			{
				const TSharedPtr<FJsonObject> Obj = RestoreEntry.IsValid() ? RestoreEntry->AsObject() : nullptr;
				if (!Obj.IsValid()) continue;
				const FString BoneText = SkeletonReadString(Obj, TEXT("bone"));
				const FString RestoreModeText = SkeletonReadString(Obj, TEXT("mode"));
				EBoneTranslationRetargetingMode::Type RestoreMode = EBoneTranslationRetargetingMode::Animation;
				if (!SkeletonParseRetargetMode(RestoreModeText, RestoreMode))
				{
					return SkeletonError(
						TEXT("invalid_mode"),
						FString::Printf(
							TEXT("restore entry for '%s' has mode '%s'. Valid modes: %s."),
							*BoneText, *RestoreModeText,
							*FString::Join(SkeletonRetargetModeNames(), TEXT(", "))));
				}
				const int32 Index = RefSkeleton.FindBoneIndex(FName(*BoneText));
				if (Index == INDEX_NONE)
				{
					return SkeletonBoneNotFound(BoneText, KnownBones,
						FString::Printf(TEXT("skeleton '%s'"), *SkeletonPath));
				}
				const EBoneTranslationRetargetingMode::Type Prior =
					Skeleton->GetBoneTranslationRetargetingMode(Index);

				TSharedPtr<FJsonObject> PriorRow = MakeShared<FJsonObject>();
				PriorRow->SetStringField(TEXT("bone"), BoneText);
				PriorRow->SetStringField(TEXT("mode"), SkeletonRetargetModeName(Prior));
				PriorForInverse.Add(MakeShared<FJsonValueObject>(PriorRow));

				TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
				Row->SetStringField(TEXT("bone"), BoneText);
				Row->SetStringField(TEXT("priorMode"), SkeletonRetargetModeName(Prior));
				Row->SetStringField(TEXT("mode"), SkeletonRetargetModeName(RestoreMode));
				Row->SetBoolField(TEXT("changed"), Prior != RestoreMode);
				RestoreRows.Add(MakeShared<FJsonValueObject>(Row));

				if (Prior != RestoreMode)
				{
					Skeleton->SetBoneTranslationRetargetingMode(Index, RestoreMode, false);
					++RestoreChanged;
				}
			}
			Skeleton->PostEditChange();
		}

		TSharedPtr<FJsonObject> RestoreResult = MCPSuccess();
		if (RestoreChanged > 0) MCPSetUpdated(RestoreResult);
		else RestoreResult->SetBoolField(TEXT("updated"), false);
		RestoreResult->SetBoolField(TEXT("alreadyInMode"), RestoreChanged == 0);
		RestoreResult->SetStringField(TEXT("skeletonPath"), SkeletonPath);
		RestoreResult->SetBoolField(TEXT("restored"), true);
		RestoreResult->SetNumberField(TEXT("bonesChanged"), RestoreChanged);
		RestoreResult->SetNumberField(TEXT("bonesInspected"), RestoreRows.Num());
		RestoreResult->SetArrayField(TEXT("bones"), RestoreRows);

		TSharedPtr<FJsonObject> RestoreRollback = MakeShared<FJsonObject>();
		RestoreRollback->SetStringField(TEXT("skeletonPath"), SkeletonPath);
		RestoreRollback->SetArrayField(TEXT("restore"), PriorForInverse);
		MCPSetRollback(RestoreResult, TEXT("set_bone_retargeting"), RestoreRollback);

		FString RestoreSaveReason;
		const bool bRestoreSaved = SaveAssetPackageChecked(Skeleton, RestoreSaveReason);
		MCPNoteSaveOutcome(RestoreResult, SkeletonPath, bRestoreSaved, RestoreSaveReason);
		return MCPResult(RestoreResult);
	}

	FString ModeText;
	if (auto Err = RequireString(Params, TEXT("mode"), ModeText)) return Err;

	EBoneTranslationRetargetingMode::Type Mode = EBoneTranslationRetargetingMode::Animation;
	if (!SkeletonParseRetargetMode(ModeText, Mode))
	{
		TSharedPtr<FJsonObject> Detail = MakeShared<FJsonObject>();
		Detail->SetArrayField(TEXT("validModes"), MCPStringListToJson(SkeletonRetargetModeNames()));
		return SkeletonError(
			TEXT("invalid_mode"),
			FString::Printf(
				TEXT("'%s' is not a bone translation retargeting mode. Valid modes: %s."),
				*ModeText, *FString::Join(SkeletonRetargetModeNames(), TEXT(", "))),
			Detail);
	}

	TArray<FString> RequestedBones = SkeletonReadStringList(Params, TEXT("bones"));
	if (RequestedBones.Num() == 0) RequestedBones = SkeletonReadStringList(Params, TEXT("bone"));

	const bool bIncludeChildren = OptionalBool(Params, TEXT("includeChildren"), false);

	const bool bAllBones = RequestedBones.Num() == 0;
	if (bAllBones)
	{
		// No selector means every bone, which is what "set this skeleton's
		// retargeting" usually means for a root-motion or IK pass.
		RequestedBones = KnownBones;
	}

	// Validate the whole selection before writing any of it.
	TArray<int32> Indices;
	for (const FString& BoneText : RequestedBones)
	{
		const int32 Index = RefSkeleton.FindBoneIndex(FName(*BoneText));
		if (Index == INDEX_NONE)
		{
			return SkeletonBoneNotFound(BoneText, KnownBones,
				FString::Printf(TEXT("skeleton '%s'"), *SkeletonPath));
		}
		Indices.AddUnique(Index);
	}

	// Capture the prior mode of every bone this call can touch, which is what
	// the inverse needs and what makes the change idempotent.
	TSet<int32> Touched;
	for (const int32 Index : Indices)
	{
		Touched.Add(Index);
		if (!bIncludeChildren) continue;
		TArray<int32> Stack;
		Stack.Add(Index);
		while (Stack.Num() > 0)
		{
			const int32 Current = Stack.Pop();
			TArray<int32> Children;
			RefSkeleton.GetDirectChildBones(Current, Children);
			for (const int32 Child : Children)
			{
				if (Touched.Contains(Child)) continue;
				Touched.Add(Child);
				Stack.Add(Child);
			}
		}
	}

	TArray<TSharedPtr<FJsonValue>> PriorEntries;
	TArray<TSharedPtr<FJsonValue>> Rows;
	int32 ChangedCount = 0;
	for (const int32 Index : Touched)
	{
		const EBoneTranslationRetargetingMode::Type Prior = Skeleton->GetBoneTranslationRetargetingMode(Index);
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("bone"), RefSkeleton.GetBoneName(Index).ToString());
		Entry->SetStringField(TEXT("mode"), SkeletonRetargetModeName(Prior));
		PriorEntries.Add(MakeShared<FJsonValueObject>(Entry));

		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("bone"), RefSkeleton.GetBoneName(Index).ToString());
		Row->SetStringField(TEXT("priorMode"), SkeletonRetargetModeName(Prior));
		Row->SetStringField(TEXT("mode"), SkeletonRetargetModeName(Mode));
		Row->SetBoolField(TEXT("changed"), Prior != Mode);
		if (Prior != Mode) ++ChangedCount;
		Rows.Add(MakeShared<FJsonValueObject>(Row));
	}

	TSharedPtr<FJsonObject> Result = MCPSuccess();

	if (ChangedCount > 0)
	{
		const FScopedTransaction Transaction(
			NSLOCTEXT("UE_MCP", "SetBoneRetargeting", "Set Bone Translation Retargeting"));
		Skeleton->Modify();
		// SetBoneTranslationRetargetingMode owns the recursion, so a
		// bIncludeChildren call names the roots and lets the engine walk. The
		// Touched set above exists to capture the prior modes, not to drive
		// the write.
		for (const int32 Index : Indices)
		{
			Skeleton->SetBoneTranslationRetargetingMode(Index, Mode, bIncludeChildren);
		}
		Skeleton->PostEditChange();
		MCPSetUpdated(Result);
	}
	else
	{
		Result->SetBoolField(TEXT("updated"), false);
	}
	Result->SetBoolField(TEXT("alreadyInMode"), ChangedCount == 0);

	Result->SetStringField(TEXT("skeletonPath"), SkeletonPath);
	Result->SetStringField(TEXT("mode"), SkeletonRetargetModeName(Mode));
	Result->SetBoolField(TEXT("includeChildren"), bIncludeChildren);
	Result->SetBoolField(TEXT("appliedToEveryBone"), bAllBones);
	Result->SetNumberField(TEXT("bonesChanged"), ChangedCount);
	Result->SetNumberField(TEXT("bonesInspected"), Rows.Num());
	Result->SetArrayField(TEXT("bones"), Rows);

	TSharedPtr<FJsonObject> Rollback = MakeShared<FJsonObject>();
	Rollback->SetStringField(TEXT("skeletonPath"), SkeletonPath);
	Rollback->SetArrayField(TEXT("restore"), PriorEntries);
	MCPSetRollback(Result, TEXT("set_bone_retargeting"), Rollback);

	FString SaveReason;
	const bool bSaved = SaveAssetPackageChecked(Skeleton, SaveReason);
	MCPNoteSaveOutcome(Result, SkeletonPath, bSaved, SaveReason);
	return MCPResult(Result);
}

// ═════════════════════════════════════════════════════════════════════════════
// author_blend_profile
// ═════════════════════════════════════════════════════════════════════════════

TSharedPtr<FJsonValue> FAnimationHandlers::AuthorBlendProfile(const TSharedPtr<FJsonObject>& Params)
{
	using namespace UE_MCP_SkeletonEdit;

	if (!Params.IsValid()) return SkeletonError(TEXT("invalid_params"), TEXT("Parameters are required"));

	FString SkeletonPath;
	TSharedPtr<FJsonValue> LoadError;
	USkeleton* Skeleton = SkeletonRequireWritableSkeleton(
		Params, TEXT("author a blend profile on this skeleton"), SkeletonPath, LoadError);
	if (!Skeleton) return LoadError;

	FString ProfileName;
	if (auto Err = RequireString(Params, TEXT("profileName"), ProfileName)) return Err;

	const FString Operation = OptionalString(Params, TEXT("operation"), TEXT("upsert")).ToLower();
	const TArray<FString> ValidOperations = { TEXT("upsert"), TEXT("remove"), TEXT("rename") };
	if (!ValidOperations.Contains(Operation))
	{
		return SkeletonError(
			TEXT("invalid_params"),
			FString::Printf(
				TEXT("operation '%s' is not one of: upsert, remove, rename."), *Operation));
	}

	const TArray<FString> ExistingProfiles = SkeletonBlendProfileNames(Skeleton);
	UBlendProfile* Profile = Skeleton->GetBlendProfile(FName(*ProfileName));

	// ── remove ────────────────────────────────────────────────────────────────
	if (Operation == TEXT("remove"))
	{
		TSharedPtr<FJsonObject> Result = MCPSuccess();
		Result->SetStringField(TEXT("skeletonPath"), SkeletonPath);
		Result->SetStringField(TEXT("profileName"), ProfileName);

		if (!Profile)
		{
			Result->SetBoolField(TEXT("alreadyDeleted"), true);
			Result->SetBoolField(TEXT("deleted"), false);
			Result->SetArrayField(TEXT("blendProfiles"), MCPStringListToJson(ExistingProfiles));
			Result->SetStringField(TEXT("note"),
				TEXT("No blend profile by that name existed, so nothing was removed."));
			return MCPResult(Result);
		}

		// Capture the whole profile before it goes, so the inverse can rebuild
		// it entry for entry rather than leaving an empty shell behind.
		TArray<TSharedPtr<FJsonValue>> PriorEntries = SkeletonBlendProfileEntriesJson(Profile);
		const FString PriorMode = SkeletonBlendProfileModeName(Profile->GetMode());

		{
			const FScopedTransaction Transaction(
				NSLOCTEXT("UE_MCP", "RemoveBlendProfile", "Remove Blend Profile"));
			Skeleton->Modify();
			Skeleton->BlendProfiles.Remove(Profile);
			Skeleton->PostEditChange();
		}

		Result->SetBoolField(TEXT("deleted"), true);
		MCPSetUpdated(Result);
		Result->SetNumberField(TEXT("removedEntryCount"), PriorEntries.Num());
		Result->SetArrayField(TEXT("removedEntries"), PriorEntries);
		Result->SetArrayField(TEXT("blendProfiles"),
			MCPStringListToJson(SkeletonBlendProfileNames(Skeleton)));

		TSharedPtr<FJsonObject> Rollback = MakeShared<FJsonObject>();
		Rollback->SetStringField(TEXT("skeletonPath"), SkeletonPath);
		Rollback->SetStringField(TEXT("profileName"), ProfileName);
		Rollback->SetStringField(TEXT("operation"), TEXT("upsert"));
		Rollback->SetStringField(TEXT("mode"), PriorMode);
		Rollback->SetArrayField(TEXT("entries"), PriorEntries);
		MCPSetRollback(Result, TEXT("author_blend_profile"), Rollback);

		FString SaveReason;
		const bool bSaved = SaveAssetPackageChecked(Skeleton, SaveReason);
		MCPNoteSaveOutcome(Result, SkeletonPath, bSaved, SaveReason);
		return MCPResult(Result);
	}

	// ── rename ────────────────────────────────────────────────────────────────
	if (Operation == TEXT("rename"))
	{
		FString NewName;
		if (auto Err = RequireString(Params, TEXT("newProfileName"), NewName)) return Err;

		if (!Profile)
		{
			if (Skeleton->GetBlendProfile(FName(*NewName)))
			{
				TSharedPtr<FJsonObject> Result = MCPSuccess();
				Result->SetStringField(TEXT("skeletonPath"), SkeletonPath);
				Result->SetStringField(TEXT("profileName"), NewName);
				Result->SetBoolField(TEXT("alreadyRenamed"), true);
				Result->SetBoolField(TEXT("updated"), false);
				Result->SetArrayField(TEXT("blendProfiles"), MCPStringListToJson(ExistingProfiles));
				return MCPResult(Result);
			}
			TSharedPtr<FJsonObject> Detail = MakeShared<FJsonObject>();
			Detail->SetArrayField(TEXT("blendProfiles"), MCPStringListToJson(ExistingProfiles));
			Detail->SetArrayField(TEXT("nearMisses"),
				MCPStringListToJson(SkeletonNearMisses(ProfileName, ExistingProfiles)));
			return SkeletonError(
				TEXT("blend_profile_not_found"),
				FString::Printf(
					TEXT("Skeleton '%s' has no blend profile named '%s'. It has: [%s]."),
					*SkeletonPath, *ProfileName,
					ExistingProfiles.Num() ? *FString::Join(ExistingProfiles, TEXT(", ")) : TEXT("none")),
				Detail);
		}
		if (Skeleton->GetBlendProfile(FName(*NewName)))
		{
			return SkeletonError(
				TEXT("blend_profile_name_taken"),
				FString::Printf(
					TEXT("Cannot rename '%s' to '%s': a blend profile by that name already exists. "
						 "Nothing was changed."),
					*ProfileName, *NewName));
		}

		UBlendProfile* Renamed = nullptr;
		{
			const FScopedTransaction Transaction(
				NSLOCTEXT("UE_MCP", "RenameBlendProfile", "Rename Blend Profile"));
			Skeleton->Modify();
			Renamed = Skeleton->RenameBlendProfile(FName(*ProfileName), FName(*NewName));
			Skeleton->PostEditChange();
		}
		if (!Renamed)
		{
			return SkeletonError(
				TEXT("rename_failed"),
				FString::Printf(
					TEXT("The engine refused to rename blend profile '%s' to '%s'. Nothing was changed."),
					*ProfileName, *NewName));
		}

		TSharedPtr<FJsonObject> Result = MCPSuccess();
		MCPSetUpdated(Result);
		Result->SetStringField(TEXT("skeletonPath"), SkeletonPath);
		Result->SetStringField(TEXT("profileName"), NewName);
		Result->SetStringField(TEXT("priorProfileName"), ProfileName);
		Result->SetStringField(TEXT("mode"), SkeletonBlendProfileModeName(Renamed->GetMode()));
		Result->SetArrayField(TEXT("entries"), SkeletonBlendProfileEntriesJson(Renamed));
		Result->SetArrayField(TEXT("blendProfiles"),
			MCPStringListToJson(SkeletonBlendProfileNames(Skeleton)));

		TSharedPtr<FJsonObject> Rollback = MakeShared<FJsonObject>();
		Rollback->SetStringField(TEXT("skeletonPath"), SkeletonPath);
		Rollback->SetStringField(TEXT("operation"), TEXT("rename"));
		Rollback->SetStringField(TEXT("profileName"), NewName);
		Rollback->SetStringField(TEXT("newProfileName"), ProfileName);
		MCPSetRollback(Result, TEXT("author_blend_profile"), Rollback);

		FString SaveReason;
		const bool bSaved = SaveAssetPackageChecked(Skeleton, SaveReason);
		MCPNoteSaveOutcome(Result, SkeletonPath, bSaved, SaveReason);
		return MCPResult(Result);
	}

	// ── upsert: create the profile if absent, then set and remove entries ─────
	const TArray<FString> KnownBones = SkeletonBoneNames(Skeleton);
	const FReferenceSkeleton& RefSkeleton = Skeleton->GetReferenceSkeleton();

	struct FEntryRequest
	{
		FName Bone;
		float Scale = 1.0f;
		bool bRecursive = false;
	};
	TArray<FEntryRequest> EntryRequests;

	const TArray<TSharedPtr<FJsonValue>>* EntriesArray = nullptr;
	if (Params->TryGetArrayField(TEXT("entries"), EntriesArray) && EntriesArray)
	{
		int32 EntryIndex = -1;
		for (const TSharedPtr<FJsonValue>& Value : *EntriesArray)
		{
			++EntryIndex;
			const TSharedPtr<FJsonObject> Obj = Value.IsValid() ? Value->AsObject() : nullptr;
			if (!Obj.IsValid())
			{
				return SkeletonError(
					TEXT("invalid_params"),
					FString::Printf(TEXT("entries[%d] is not an object { bone, scale, recursive? }."), EntryIndex));
			}
			const FString BoneText = SkeletonReadString(Obj, TEXT("bone"));
			if (BoneText.IsEmpty())
			{
				return SkeletonError(
					TEXT("invalid_params"),
					FString::Printf(TEXT("entries[%d] has no 'bone'."), EntryIndex));
			}
			if (RefSkeleton.FindBoneIndex(FName(*BoneText)) == INDEX_NONE)
			{
				return SkeletonBoneNotFound(BoneText, KnownBones,
					FString::Printf(TEXT("skeleton '%s'"), *SkeletonPath));
			}
			if (!Obj->HasField(TEXT("scale")))
			{
				return SkeletonError(
					TEXT("invalid_params"),
					FString::Printf(
						TEXT("entries[%d] ('%s') has no 'scale'. A blend profile entry is a bone plus "
							 "its per-bone factor."),
						EntryIndex, *BoneText));
			}
			FEntryRequest Request;
			Request.Bone = FName(*BoneText);
			Request.Scale = static_cast<float>(OptionalNumber(Obj, TEXT("scale"), 1.0));
			Request.bRecursive = OptionalBool(Obj, TEXT("recursive"), false);
			EntryRequests.Add(Request);
		}
	}

	TArray<FString> RemoveBones = SkeletonReadStringList(Params, TEXT("removeEntries"));
	for (const FString& BoneText : RemoveBones)
	{
		if (RefSkeleton.FindBoneIndex(FName(*BoneText)) == INDEX_NONE)
		{
			return SkeletonBoneNotFound(BoneText, KnownBones,
				FString::Printf(TEXT("skeleton '%s'"), *SkeletonPath));
		}
	}

	const bool bCreating = Profile == nullptr;

	EBlendProfileMode Mode = EBlendProfileMode::TimeFactor;
	bool bModeRequested = false;
	if (Params->HasField(TEXT("mode")))
	{
		const FString ModeText = SkeletonReadString(Params, TEXT("mode"));
		if (!SkeletonParseBlendProfileMode(ModeText, Mode))
		{
			return SkeletonError(
				TEXT("invalid_mode"),
				FString::Printf(
					TEXT("'%s' is not a blend profile mode. Valid modes: TimeFactor, WeightFactor, BlendMask."),
					*ModeText));
		}
		bModeRequested = true;
	}

	// Prior state, captured before anything is written, for the inverse.
	TArray<TSharedPtr<FJsonValue>> PriorEntries = SkeletonBlendProfileEntriesJson(Profile);
	const FString PriorMode = Profile
		? SkeletonBlendProfileModeName(Profile->GetMode())
		: SkeletonBlendProfileModeName(Mode);

	int32 ChangedEntries = 0;
	int32 RemovedEntries = 0;
	{
		const FScopedTransaction Transaction(
			NSLOCTEXT("UE_MCP", "AuthorBlendProfile", "Author Blend Profile"));
		Skeleton->Modify();

		if (!Profile)
		{
			Profile = Skeleton->CreateNewBlendProfile(FName(*ProfileName));
			if (!Profile)
			{
				return SkeletonError(
					TEXT("create_failed"),
					FString::Printf(
						TEXT("The engine refused to create blend profile '%s' on '%s'."),
						*ProfileName, *SkeletonPath));
			}
		}
		Profile->Modify();
		if (bModeRequested && Profile->Mode != Mode)
		{
			Profile->Mode = Mode;
			++ChangedEntries;
		}

		for (const FEntryRequest& Request : EntryRequests)
		{
			const float Prior = Profile->GetBoneBlendScale(Request.Bone);
			// bCreate:true is what makes this an upsert rather than a no-op on a
			// bone that has no entry yet.
			Profile->SetBoneBlendScale(Request.Bone, Request.Scale, Request.bRecursive, true);
			if (!FMath::IsNearlyEqual(Prior, Request.Scale)) ++ChangedEntries;
		}

		for (const FString& BoneText : RemoveBones)
		{
			const int32 BoneIndex = RefSkeleton.FindBoneIndex(FName(*BoneText));
			if (Profile->GetEntryIndex(FName(*BoneText)) == INDEX_NONE) continue;
			Profile->RemoveEntry(BoneIndex);
			++RemovedEntries;
		}

		Skeleton->PostEditChange();
	}

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	if (bCreating) MCPSetCreated(Result); else MCPSetExisted(Result);
	if (ChangedEntries > 0 || RemovedEntries > 0) MCPSetUpdated(Result);
	Result->SetBoolField(TEXT("alreadyUpToDate"), !bCreating && ChangedEntries == 0 && RemovedEntries == 0);
	Result->SetStringField(TEXT("skeletonPath"), SkeletonPath);
	Result->SetStringField(TEXT("profileName"), ProfileName);
	Result->SetStringField(TEXT("mode"), SkeletonBlendProfileModeName(Profile->GetMode()));
	Result->SetNumberField(TEXT("entriesChanged"), ChangedEntries);
	Result->SetNumberField(TEXT("entriesRemoved"), RemovedEntries);
	Result->SetNumberField(TEXT("entryCount"), Profile->GetNumBlendEntries());
	Result->SetArrayField(TEXT("entries"), SkeletonBlendProfileEntriesJson(Profile));
	Result->SetArrayField(TEXT("blendProfiles"), MCPStringListToJson(SkeletonBlendProfileNames(Skeleton)));

	TSharedPtr<FJsonObject> Rollback = MakeShared<FJsonObject>();
	Rollback->SetStringField(TEXT("skeletonPath"), SkeletonPath);
	Rollback->SetStringField(TEXT("profileName"), ProfileName);
	if (bCreating)
	{
		// It did not exist before, so the clean inverse is to delete it.
		Rollback->SetStringField(TEXT("operation"), TEXT("remove"));
	}
	else
	{
		Rollback->SetStringField(TEXT("operation"), TEXT("upsert"));
		Rollback->SetStringField(TEXT("mode"), PriorMode);
		Rollback->SetArrayField(TEXT("entries"), PriorEntries);
		// Entries this call created did not exist before and restoring the old
		// values would leave them behind at their new scale, so name them.
		TArray<FString> AddedBones;
		for (const FEntryRequest& Request : EntryRequests)
		{
			bool bWasThere = false;
			for (const TSharedPtr<FJsonValue>& PriorEntry : PriorEntries)
			{
				const TSharedPtr<FJsonObject> Obj = PriorEntry.IsValid() ? PriorEntry->AsObject() : nullptr;
				if (Obj.IsValid() && SkeletonReadString(Obj, TEXT("bone")) == Request.Bone.ToString())
				{
					bWasThere = true;
					break;
				}
			}
			if (!bWasThere) AddedBones.Add(Request.Bone.ToString());
		}
		Rollback->SetArrayField(TEXT("removeEntries"), MCPStringListToJson(AddedBones));
		if (RemovedEntries > 0 && EntryRequests.Num() > 0)
		{
			Result->SetBoolField(TEXT("rollbackLossy"), false);
		}
	}
	MCPSetRollback(Result, TEXT("author_blend_profile"), Rollback);

	// A recursive set touches bones the request did not name, and the inverse
	// restores only the entries that existed. Say so when it applies.
	bool bAnyRecursive = false;
	for (const FEntryRequest& Request : EntryRequests) bAnyRecursive = bAnyRecursive || Request.bRecursive;
	if (bAnyRecursive && !bCreating)
	{
		Result->SetBoolField(TEXT("rollbackLossy"), true);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("A recursive entry wrote a scale onto every descendant bone. The inverse restores the "
				 "entries that existed before this call and deletes the ones it named, but a descendant "
				 "that had no entry and was given one by the recursion is deleted only if it is listed "
				 "in 'removeEntries'. Re-read 'entries' after a rollback to confirm."));
	}

	FString SaveReason;
	const bool bSaved = SaveAssetPackageChecked(Skeleton, SaveReason);
	MCPNoteSaveOutcome(Result, SkeletonPath, bSaved, SaveReason);
	return MCPResult(Result);
}

// ═════════════════════════════════════════════════════════════════════════════
// edit_curve_metadata
// ═════════════════════════════════════════════════════════════════════════════

TSharedPtr<FJsonValue> FAnimationHandlers::EditCurveMetadata(const TSharedPtr<FJsonObject>& Params)
{
	using namespace UE_MCP_SkeletonEdit;

#if !WITH_EDITOR
	return SkeletonError(
		TEXT("editor_only"),
		TEXT("Curve metadata authoring uses editor-only USkeleton accessors and is unavailable in "
			 "this build."));
#else
	if (!Params.IsValid()) return SkeletonError(TEXT("invalid_params"), TEXT("Parameters are required"));

	FString SkeletonPath;
	TSharedPtr<FJsonValue> LoadError;
	USkeleton* Skeleton = SkeletonRequireWritableSkeleton(
		Params, TEXT("edit curve metadata on this skeleton"), SkeletonPath, LoadError);
	if (!Skeleton) return LoadError;

	const TArray<FString> AddCurves = SkeletonReadStringList(Params, TEXT("add"));
	const TArray<FString> RemoveCurves = SkeletonReadStringList(Params, TEXT("remove"));

	// rename: [{ from, to }]
	struct FRenameRequest { FString From; FString To; };
	TArray<FRenameRequest> Renames;
	const TArray<TSharedPtr<FJsonValue>>* RenameArray = nullptr;
	if (Params->TryGetArrayField(TEXT("rename"), RenameArray) && RenameArray)
	{
		int32 Index = -1;
		for (const TSharedPtr<FJsonValue>& Value : *RenameArray)
		{
			++Index;
			const TSharedPtr<FJsonObject> Obj = Value.IsValid() ? Value->AsObject() : nullptr;
			const FString From = SkeletonReadString(Obj, TEXT("from"));
			const FString To = SkeletonReadString(Obj, TEXT("to"));
			if (From.IsEmpty() || To.IsEmpty())
			{
				return SkeletonError(
					TEXT("invalid_params"),
					FString::Printf(TEXT("rename[%d] needs both 'from' and 'to'."), Index));
			}
			Renames.Add({ From, To });
		}
	}

	// flags: [{ curve, material?, morphTarget? }]
	struct FFlagRequest
	{
		FString Curve;
		TOptional<bool> bMaterial;
		TOptional<bool> bMorphTarget;
	};
	TArray<FFlagRequest> Flags;
	const TArray<TSharedPtr<FJsonValue>>* FlagArray = nullptr;
	if (Params->TryGetArrayField(TEXT("flags"), FlagArray) && FlagArray)
	{
		int32 Index = -1;
		for (const TSharedPtr<FJsonValue>& Value : *FlagArray)
		{
			++Index;
			const TSharedPtr<FJsonObject> Obj = Value.IsValid() ? Value->AsObject() : nullptr;
			const FString Curve = SkeletonReadString(Obj, TEXT("curve"));
			if (Curve.IsEmpty())
			{
				return SkeletonError(
					TEXT("invalid_params"),
					FString::Printf(TEXT("flags[%d] has no 'curve'."), Index));
			}
			FFlagRequest Request;
			Request.Curve = Curve;
			bool Bool = false;
			if (Obj->TryGetBoolField(TEXT("material"), Bool)) Request.bMaterial = Bool;
			if (Obj->TryGetBoolField(TEXT("morphTarget"), Bool)) Request.bMorphTarget = Bool;
			if (!Request.bMaterial.IsSet() && !Request.bMorphTarget.IsSet())
			{
				return SkeletonError(
					TEXT("invalid_params"),
					FString::Printf(
						TEXT("flags[%d] ('%s') sets neither 'material' nor 'morphTarget'."), Index, *Curve));
			}
			Flags.Add(Request);
		}
	}

	if (AddCurves.Num() == 0 && RemoveCurves.Num() == 0 && Renames.Num() == 0 && Flags.Num() == 0)
	{
		return SkeletonError(
			TEXT("invalid_params"),
			TEXT("Nothing to do. Pass at least one of 'add' (curve names), 'remove' (curve names), "
				 "'rename' ([{from,to}]) or 'flags' ([{curve, material?, morphTarget?}])."));
	}

	// ── Validate the whole batch against the current metadata before writing.
	TArray<FName> ExistingNames;
	Skeleton->GetCurveMetaDataNames(ExistingNames);
	TSet<FString> Working;
	TArray<FString> KnownCurves;
	for (const FName& Name : ExistingNames)
	{
		Working.Add(Name.ToString());
		KnownCurves.Add(Name.ToString());
	}

	auto CurveNotFound = [&](const FString& Curve, const TCHAR* What) -> TSharedPtr<FJsonValue>
	{
		const TArray<FString> Near = SkeletonNearMisses(Curve, KnownCurves);
		TSharedPtr<FJsonObject> Detail = MakeShared<FJsonObject>();
		Detail->SetStringField(TEXT("curve"), Curve);
		Detail->SetArrayField(TEXT("nearMisses"), MCPStringListToJson(Near));
		Detail->SetNumberField(TEXT("curveCount"), KnownCurves.Num());
		FString Message = FString::Printf(
			TEXT("Skeleton '%s' has no curve metadata entry named '%s' (%s, %d entries)."),
			*SkeletonPath, *Curve, What, KnownCurves.Num());
		if (Near.Num() > 0)
		{
			Message += FString::Printf(TEXT(" Did you mean: %s?"), *FString::Join(Near, TEXT(", ")));
		}
		else
		{
			Message += TEXT(" Add it first with this action's 'add' list.");
		}
		return SkeletonError(TEXT("curve_not_found"), Message, Detail);
	};

	for (const FRenameRequest& Rename : Renames)
	{
		if (!Working.Contains(Rename.From))
		{
			// Already renamed is a converged state, not a failure.
			if (Working.Contains(Rename.To)) continue;
			return CurveNotFound(Rename.From, TEXT("as a rename source"));
		}
		if (Working.Contains(Rename.To))
		{
			return SkeletonError(
				TEXT("curve_name_taken"),
				FString::Printf(
					TEXT("Cannot rename curve '%s' to '%s': an entry by that name already exists. "
						 "Nothing was changed."),
					*Rename.From, *Rename.To));
		}
		Working.Remove(Rename.From);
		Working.Add(Rename.To);
	}
	for (const FString& Curve : AddCurves) Working.Add(Curve);
	for (const FFlagRequest& Flag : Flags)
	{
		if (!Working.Contains(Flag.Curve))
		{
			return CurveNotFound(Flag.Curve, TEXT("as a flags target"));
		}
	}

	// ── Prior state for the inverse, captured before the first write.
	TArray<TSharedPtr<FJsonValue>> PriorMetadata = SkeletonCurveMetadataJson(Skeleton);

	TArray<TSharedPtr<FJsonValue>> Rows;
	int32 Added = 0, AlreadyPresent = 0, Removed = 0, AlreadyAbsent = 0, Renamed = 0, Flagged = 0;

	{
		const FScopedTransaction Transaction(
			NSLOCTEXT("UE_MCP", "EditCurveMetadata", "Edit Curve Metadata"));
		Skeleton->Modify();

		for (const FRenameRequest& Rename : Renames)
		{
			TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("op"), TEXT("rename"));
			Row->SetStringField(TEXT("from"), Rename.From);
			Row->SetStringField(TEXT("to"), Rename.To);
			const bool bOk = Skeleton->RenameCurveMetaData(FName(*Rename.From), FName(*Rename.To));
			Row->SetBoolField(TEXT("changed"), bOk);
			if (!bOk) Row->SetBoolField(TEXT("alreadyApplied"), true);
			if (bOk) ++Renamed;
			Rows.Add(MakeShared<FJsonValueObject>(Row));
		}

		for (const FString& Curve : AddCurves)
		{
			TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("op"), TEXT("add"));
			Row->SetStringField(TEXT("curve"), Curve);
			// Returns false when an entry already existed, which is the
			// idempotent case rather than a failure.
			const bool bNew = Skeleton->AddCurveMetaData(FName(*Curve), false);
			Row->SetBoolField(TEXT("created"), bNew);
			Row->SetBoolField(TEXT("existed"), !bNew);
			bNew ? ++Added : ++AlreadyPresent;
			Rows.Add(MakeShared<FJsonValueObject>(Row));
		}

		for (const FFlagRequest& Flag : Flags)
		{
			TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("op"), TEXT("flags"));
			Row->SetStringField(TEXT("curve"), Flag.Curve);
			bool bChanged = false;
			if (Flag.bMaterial.IsSet())
			{
				const bool Prior = Skeleton->GetCurveMetaDataMaterial(FName(*Flag.Curve));
				Row->SetBoolField(TEXT("priorMaterial"), Prior);
				Row->SetBoolField(TEXT("material"), Flag.bMaterial.GetValue());
				if (Prior != Flag.bMaterial.GetValue())
				{
					Skeleton->SetCurveMetaDataMaterial(FName(*Flag.Curve), Flag.bMaterial.GetValue());
					bChanged = true;
				}
			}
			if (Flag.bMorphTarget.IsSet())
			{
				const bool Prior = Skeleton->GetCurveMetaDataMorphTarget(FName(*Flag.Curve));
				Row->SetBoolField(TEXT("priorMorphTarget"), Prior);
				Row->SetBoolField(TEXT("morphTarget"), Flag.bMorphTarget.GetValue());
				if (Prior != Flag.bMorphTarget.GetValue())
				{
					Skeleton->SetCurveMetaDataMorphTarget(FName(*Flag.Curve), Flag.bMorphTarget.GetValue());
					bChanged = true;
				}
			}
			Row->SetBoolField(TEXT("changed"), bChanged);
			if (!bChanged) Row->SetBoolField(TEXT("alreadyApplied"), true);
			if (bChanged) ++Flagged;
			Rows.Add(MakeShared<FJsonValueObject>(Row));
		}

		for (const FString& Curve : RemoveCurves)
		{
			TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("op"), TEXT("remove"));
			Row->SetStringField(TEXT("curve"), Curve);
			const bool bOk = Skeleton->RemoveCurveMetaData(FName(*Curve));
			Row->SetBoolField(TEXT("deleted"), bOk);
			if (!bOk) Row->SetBoolField(TEXT("alreadyDeleted"), true);
			bOk ? ++Removed : ++AlreadyAbsent;
			Rows.Add(MakeShared<FJsonValueObject>(Row));
		}

		Skeleton->PostEditChange();
	}

	const int32 ChangedTotal = Added + Removed + Renamed + Flagged;

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	if (ChangedTotal > 0) MCPSetUpdated(Result); else Result->SetBoolField(TEXT("updated"), false);
	Result->SetBoolField(TEXT("alreadyUpToDate"), ChangedTotal == 0);
	Result->SetStringField(TEXT("skeletonPath"), SkeletonPath);
	Result->SetNumberField(TEXT("curvesAdded"), Added);
	Result->SetNumberField(TEXT("curvesAlreadyPresent"), AlreadyPresent);
	Result->SetNumberField(TEXT("curvesRemoved"), Removed);
	Result->SetNumberField(TEXT("curvesAlreadyAbsent"), AlreadyAbsent);
	Result->SetNumberField(TEXT("curvesRenamed"), Renamed);
	Result->SetNumberField(TEXT("flagsChanged"), Flagged);
	Result->SetArrayField(TEXT("operations"), Rows);
	Result->SetNumberField(TEXT("curveCount"), Skeleton->GetNumCurveMetaData());
	Result->SetArrayField(TEXT("curveMetadata"), SkeletonCurveMetadataJson(Skeleton));

	// The inverse: re-add what was removed with its flags, remove what was
	// added, rename back, restore flags.
	TSharedPtr<FJsonObject> Rollback = MakeShared<FJsonObject>();
	Rollback->SetStringField(TEXT("skeletonPath"), SkeletonPath);
	Rollback->SetArrayField(TEXT("add"), MCPStringListToJson(RemoveCurves));
	Rollback->SetArrayField(TEXT("remove"), MCPStringListToJson(AddCurves));
	TArray<TSharedPtr<FJsonValue>> InverseRenames;
	for (int32 Index = Renames.Num() - 1; Index >= 0; --Index)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("from"), Renames[Index].To);
		Obj->SetStringField(TEXT("to"), Renames[Index].From);
		InverseRenames.Add(MakeShared<FJsonValueObject>(Obj));
	}
	Rollback->SetArrayField(TEXT("rename"), InverseRenames);
	TArray<TSharedPtr<FJsonValue>> InverseFlags;
	for (const TSharedPtr<FJsonValue>& PriorRow : PriorMetadata)
	{
		const TSharedPtr<FJsonObject> Obj = PriorRow.IsValid() ? PriorRow->AsObject() : nullptr;
		if (!Obj.IsValid()) continue;
		const FString Curve = SkeletonReadString(Obj, TEXT("curve"));
		bool bTouched = false;
		for (const FFlagRequest& Flag : Flags) bTouched = bTouched || Flag.Curve == Curve;
		for (const FString& Removed2 : RemoveCurves) bTouched = bTouched || Removed2 == Curve;
		if (!bTouched) continue;
		TSharedPtr<FJsonObject> Restore = MakeShared<FJsonObject>();
		Restore->SetStringField(TEXT("curve"), Curve);
		Restore->SetBoolField(TEXT("material"), OptionalBool(Obj, TEXT("material"), false));
		Restore->SetBoolField(TEXT("morphTarget"), OptionalBool(Obj, TEXT("morphTarget"), false));
		InverseFlags.Add(MakeShared<FJsonValueObject>(Restore));
	}
	Rollback->SetArrayField(TEXT("flags"), InverseFlags);
	MCPSetRollback(Result, TEXT("edit_curve_metadata"), Rollback);

	if (Removed > 0)
	{
		// FCurveMetaData also carries MaxLOD and LinkedBones, which this action
		// does not author and therefore cannot restore.
		Result->SetBoolField(TEXT("rollbackLossy"), true);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("Re-adding a removed curve restores its name and its material/morph-target flags. "
				 "It does NOT restore the entry's MaxLOD or its LinkedBones, which this action does "
				 "not author. The removed entries' full prior state is under 'priorCurveMetadata'."));
		Result->SetArrayField(TEXT("priorCurveMetadata"), PriorMetadata);
	}

	FString SaveReason;
	const bool bSaved = SaveAssetPackageChecked(Skeleton, SaveReason);
	MCPNoteSaveOutcome(Result, SkeletonPath, bSaved, SaveReason);
	return MCPResult(Result);
#endif
}

// ═════════════════════════════════════════════════════════════════════════════
// register_compatible_skeleton
// ═════════════════════════════════════════════════════════════════════════════

TSharedPtr<FJsonValue> FAnimationHandlers::RegisterCompatibleSkeleton(const TSharedPtr<FJsonObject>& Params)
{
	using namespace UE_MCP_SkeletonEdit;

	if (!Params.IsValid()) return SkeletonError(TEXT("invalid_params"), TEXT("Parameters are required"));

	FString SkeletonPath;
	TSharedPtr<FJsonValue> LoadError;
	USkeleton* Skeleton = SkeletonRequireWritableSkeleton(
		Params, TEXT("register a compatible skeleton"), SkeletonPath, LoadError);
	if (!Skeleton) return LoadError;

	TArray<FString> SourcePaths = SkeletonReadStringList(Params, TEXT("compatibleSkeletonPaths"));
	if (SourcePaths.Num() == 0) SourcePaths = SkeletonReadStringList(Params, TEXT("compatibleSkeletonPath"));
	if (SourcePaths.Num() == 0)
	{
		return SkeletonError(
			TEXT("invalid_params"),
			TEXT("Pass 'compatibleSkeletonPath' (or 'compatibleSkeletonPaths') naming the skeleton(s) "
				 "whose animations should be usable on this one. asset(diff) between two skeletons "
				 "reports the hierarchy delta that says whether this is safe."));
	}

	const bool bRemove = OptionalBool(Params, TEXT("remove"), false);

	// Resolve every source before mutating anything.
	TArray<USkeleton*> Sources;
	for (const FString& Path : SourcePaths)
	{
		USkeleton* Source = LoadAssetByPath<USkeleton>(Path);
		if (!Source) return MCPAssetLoadError(Path, TEXT("Skeleton"));
		if (Source == Skeleton)
		{
			return SkeletonError(
				TEXT("self_reference"),
				FString::Printf(
					TEXT("'%s' cannot be registered as compatible with itself. Nothing was changed."),
					*Path));
		}
		Sources.Add(Source);
	}

	auto CurrentCompatible = [Skeleton]()
	{
		TArray<FString> Out;
		for (const TSoftObjectPtr<USkeleton>& Entry : Skeleton->GetCompatibleSkeletons())
		{
			Out.Add(Entry.ToString());
		}
		Out.Sort();
		return Out;
	};

	const TArray<FString> Before = CurrentCompatible();

	TArray<TSharedPtr<FJsonValue>> Rows;
	TArray<FString> ActuallyChanged;
	int32 Changed = 0;
	int32 AlreadyInState = 0;

	{
		const FScopedTransaction Transaction(
			NSLOCTEXT("UE_MCP", "RegisterCompatibleSkeleton", "Register Compatible Skeleton"));
		Skeleton->Modify();

		for (int32 Index = 0; Index < Sources.Num(); ++Index)
		{
			USkeleton* Source = Sources[Index];
			const FString SourcePath = Source->GetPathName();

			bool bPresent = false;
			for (const TSoftObjectPtr<USkeleton>& Entry : Skeleton->GetCompatibleSkeletons())
			{
				if (Entry.ToString() == SourcePath || Entry.Get() == Source) { bPresent = true; break; }
			}

			TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("compatibleSkeletonPath"), SourcePath);
			Row->SetStringField(TEXT("requestedPath"), SourcePaths[Index]);
			Row->SetBoolField(TEXT("wasRegistered"), bPresent);

			if (bRemove)
			{
				if (bPresent)
				{
					Skeleton->RemoveCompatibleSkeleton(Source);
					Row->SetBoolField(TEXT("registered"), false);
					Row->SetBoolField(TEXT("changed"), true);
					ActuallyChanged.Add(SourcePath);
					++Changed;
				}
				else
				{
					Row->SetBoolField(TEXT("registered"), false);
					Row->SetBoolField(TEXT("changed"), false);
					Row->SetBoolField(TEXT("alreadyDeleted"), true);
					++AlreadyInState;
				}
			}
			else
			{
				if (!bPresent)
				{
					Skeleton->AddCompatibleSkeleton(Source);
					Row->SetBoolField(TEXT("registered"), true);
					Row->SetBoolField(TEXT("changed"), true);
					ActuallyChanged.Add(SourcePath);
					++Changed;
				}
				else
				{
					Row->SetBoolField(TEXT("registered"), true);
					Row->SetBoolField(TEXT("changed"), false);
					Row->SetBoolField(TEXT("existed"), true);
					++AlreadyInState;
				}
			}

			// The reason a caller asked, reported back: whether the engine's own
			// editor compatibility check agrees, and how the bone counts compare.
#if WITH_EDITORONLY_DATA
			Row->SetBoolField(TEXT("engineReportsCompatible"), Skeleton->IsCompatibleForEditor(Source));
#endif
			Row->SetNumberField(TEXT("boneCount"), Source->GetReferenceSkeleton().GetNum());
			Rows.Add(MakeShared<FJsonValueObject>(Row));
		}

		Skeleton->PostEditChange();
	}

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	if (Changed > 0)
	{
		MCPSetUpdated(Result);
		if (!bRemove) MCPSetCreated(Result);
	}
	else
	{
		Result->SetBoolField(TEXT("updated"), false);
		if (!bRemove) MCPSetExisted(Result);
	}
	Result->SetBoolField(bRemove ? TEXT("alreadyDeleted") : TEXT("alreadyRegistered"),
		Changed == 0 && AlreadyInState > 0);
	Result->SetStringField(TEXT("skeletonPath"), SkeletonPath);
	Result->SetBoolField(TEXT("removed"), bRemove);
	Result->SetNumberField(TEXT("changed"), Changed);
	Result->SetNumberField(TEXT("alreadyInRequestedState"), AlreadyInState);
	Result->SetArrayField(TEXT("results"), Rows);
	Result->SetNumberField(TEXT("boneCount"), Skeleton->GetReferenceSkeleton().GetNum());
	Result->SetArrayField(TEXT("compatibleSkeletonsBefore"), MCPStringListToJson(Before));
	Result->SetArrayField(TEXT("compatibleSkeletons"), MCPStringListToJson(CurrentCompatible()));

	TSharedPtr<FJsonObject> Rollback = MakeShared<FJsonObject>();
	Rollback->SetStringField(TEXT("skeletonPath"), SkeletonPath);
	Rollback->SetArrayField(TEXT("compatibleSkeletonPaths"), MCPStringListToJson(ActuallyChanged));
	Rollback->SetBoolField(TEXT("remove"), !bRemove);
	MCPSetRollback(Result, TEXT("register_compatible_skeleton"), Rollback);

	FString SaveReason;
	const bool bSaved = SaveAssetPackageChecked(Skeleton, SaveReason);
	MCPNoteSaveOutcome(Result, SkeletonPath, bSaved, SaveReason);
	return MCPResult(Result);
}
