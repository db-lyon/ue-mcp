#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"
#include "UObject/UObjectIterator.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "Engine/World.h"
#include "Engine/Blueprint.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"

// ── Quick result builders ────────────────────────────────────────────────────

/** Return an error response: { success: false, error: "..." } */
inline TSharedPtr<FJsonValue> MCPError(const FString& Message)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), false);
	Obj->SetStringField(TEXT("error"), Message);
	return MakeShared<FJsonValueObject>(Obj);
}

/** Return a formatted error. Usage: MCPError(FString::Printf(TEXT("Not found: %s"), *Path)) */
// NOTE: Do not use a variadic template wrapper — UE 5.7's consteval format
// string validation requires TEXT() literals passed directly to FString::Printf.

/** Wrap a populated FJsonObject as a FJsonValue (the common return). */
inline TSharedPtr<FJsonValue> MCPResult(TSharedPtr<FJsonObject> Obj)
{
	return MakeShared<FJsonValueObject>(Obj);
}

/** Create a fresh result object with success=true pre-set. */
inline TSharedPtr<FJsonObject> MCPSuccess()
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), true);
	return Obj;
}

/** Attach a rollback record to a result. The TS bridge lifts this onto
 *  TaskResult.rollback so FlowRunner can invoke it on failure. */
inline void MCPSetRollback(
	TSharedPtr<FJsonObject> Result,
	const FString& InverseMethod,
	TSharedPtr<FJsonObject> Payload)
{
	TSharedPtr<FJsonObject> Rollback = MakeShared<FJsonObject>();
	Rollback->SetStringField(TEXT("method"), InverseMethod);
	Rollback->SetObjectField(TEXT("payload"), Payload);
	Result->SetObjectField(TEXT("rollback"), Rollback);
}

/** Mark a result as "already existed, nothing created" — idempotent replay. */
inline void MCPSetExisted(TSharedPtr<FJsonObject> Result)
{
	Result->SetBoolField(TEXT("existed"), true);
	Result->SetBoolField(TEXT("created"), false);
}

/** Mark a result as "created this time". */
inline void MCPSetCreated(TSharedPtr<FJsonObject> Result)
{
	Result->SetBoolField(TEXT("existed"), false);
	Result->SetBoolField(TEXT("created"), true);
}

/** Mark a result as "updated the existing entity". */
inline void MCPSetUpdated(TSharedPtr<FJsonObject> Result)
{
	Result->SetBoolField(TEXT("updated"), true);
}

/** Check for an existing asset at `PackagePath/Name`. Returns a fully-formed
 *  "already existed" result on hit (caller can return it directly), or an
 *  unset pointer on miss so the caller proceeds to create. Also honors an
 *  optional `onConflict: "error"` to return an MCPError instead.
 *  On miss, returns a null shared pointer (check with `.IsValid()`). */
inline TSharedPtr<FJsonValue> MCPCheckAssetExists(
	const FString& PackagePath,
	const FString& Name,
	const FString& OnConflict,
	const FString& FriendlyType = TEXT("Asset"))
{
	const FString ProbePath = PackagePath + TEXT("/") + Name + TEXT(".") + Name;
	if (UObject* Existing = LoadObject<UObject>(nullptr, *ProbePath))
	{
		if (OnConflict == TEXT("error"))
		{
			return MCPError(FString::Printf(TEXT("%s '%s' already exists"), *FriendlyType, *ProbePath));
		}
		auto Res = MCPSuccess();
		MCPSetExisted(Res);
		Res->SetStringField(TEXT("path"), Existing->GetPathName());
		Res->SetStringField(TEXT("name"), Name);
		Res->SetStringField(TEXT("packagePath"), PackagePath);
		return MCPResult(Res);
	}
	return TSharedPtr<FJsonValue>();
}

/** Emit the standard delete_asset rollback record on a create result. */
inline void MCPSetDeleteAssetRollback(TSharedPtr<FJsonObject> Result, const FString& AssetPath)
{
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	MCPSetRollback(Result, TEXT("delete_asset"), Payload);
}

/** Find an actor by GetActorLabel(). Returns nullptr on miss. Centralises
 *  the iterator-based lookup that previously lived as a private static in
 *  several handler translation units. */
inline AActor* FindActorByLabel(UWorld* World, const FString& Label)
{
	if (!World) return nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->GetActorLabel() == Label) return *It;
	}
	return nullptr;
}

/** Find an actor by either editor label or internal UObject name. Used by
 *  PIE / runtime handlers where callers may pass either form. */
inline AActor* FindActorByLabelOrName(UWorld* World, const FString& LabelOrName)
{
	if (!World) return nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->GetActorLabel() == LabelOrName || It->GetName() == LabelOrName) return *It;
	}
	return nullptr;
}

/** Find an actor by either editor label or full object path. Used by
 *  get_actor_details / get_component_tree which accept either form. */
inline AActor* FindActorByLabelOrPath(UWorld* World, const FString& Label, const FString& Path)
{
	if (!World) return nullptr;
	const bool bHasLabel = !Label.IsEmpty();
	const bool bHasPath = !Path.IsEmpty();
	if (!bHasLabel && !bHasPath) return nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (bHasPath && It->GetPathName() == Path) return *It;
		if (bHasLabel && It->GetActorLabel() == Label) return *It;
	}
	return nullptr;
}

/** Three-way actor lookup: label, internal name, or full path. Used by
 *  EditorHandlers_PIE invoke_function which accepts any of the three. */
inline AActor* FindActorByLabelNameOrPath(UWorld* World, const FString& Token)
{
	if (!World || Token.IsEmpty()) return nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* A = *It;
		if (A->GetName() == Token || A->GetActorLabel() == Token || A->GetPathName() == Token) return A;
	}
	return nullptr;
}

/** Spawn-by-label idempotency check. If World already has an actor with the
 *  given Label, returns a fully-formed "already existed" result the caller
 *  can return directly (or an MCPError when OnConflict == "error"). When
 *  Label is empty or no match exists, returns an unset shared pointer so the
 *  caller proceeds to spawn. Mirrors MCPCheckAssetExists's contract for
 *  in-world actors. */
inline TSharedPtr<FJsonValue> MCPCheckActorLabelExists(
	UWorld* World,
	const FString& Label,
	const FString& OnConflict,
	const FString& FriendlyType = TEXT("Actor"))
{
	if (!World || Label.IsEmpty()) return TSharedPtr<FJsonValue>();
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->GetActorLabel() == Label)
		{
			if (OnConflict == TEXT("error"))
			{
				return MCPError(FString::Printf(TEXT("%s '%s' already exists"), *FriendlyType, *Label));
			}
			auto Existing = MCPSuccess();
			MCPSetExisted(Existing);
			Existing->SetStringField(TEXT("actorLabel"), Label);
			Existing->SetStringField(TEXT("actorPath"), It->GetPathName());
			return MCPResult(Existing);
		}
	}
	return TSharedPtr<FJsonValue>();
}

/** Load a Blueprint by path and return its CDO cast to T. Returns nullptr
 *  on miss; writes a structured error to OutError. Centralises the
 *  pattern that previously lived in NetworkingHandlers::LoadBlueprintCDO,
 *  GasHandlers, and GameplayHandlers. */
template <typename T = AActor>
inline T* LoadBlueprintCDO(const FString& BlueprintPath, TSharedPtr<FJsonValue>& OutError)
{
	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath);
	if (!Blueprint && !BlueprintPath.Contains(TEXT(".")))
	{
		// Retry in ObjectPath form ("/Game/Foo/Bar" → "/Game/Foo/Bar.Bar").
		FString AssetName;
		BlueprintPath.Split(TEXT("/"), nullptr, &AssetName, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		Blueprint = LoadObject<UBlueprint>(nullptr, *(BlueprintPath + TEXT(".") + AssetName));
	}
	if (!Blueprint || !Blueprint->GeneratedClass)
	{
		OutError = MCPError(FString::Printf(TEXT("Blueprint not found or has no generated class: %s"), *BlueprintPath));
		return nullptr;
	}
	T* CDO = Cast<T>(Blueprint->GeneratedClass->GetDefaultObject());
	if (!CDO)
	{
		OutError = MCPError(FString::Printf(
			TEXT("Blueprint CDO at '%s' is not a %s"),
			*BlueprintPath,
			*T::StaticClass()->GetName()));
		return nullptr;
	}
	return CDO;
}

// ── Parameter extraction ─────────────────────────────────────────────────────

/** Extract a required string parameter.  Returns error JSON on failure, nullptr on success. */
inline TSharedPtr<FJsonValue> RequireString(
	const TSharedPtr<FJsonObject>& Params,
	const TCHAR* Key,
	FString& OutValue)
{
	if (Params->TryGetStringField(Key, OutValue) && !OutValue.IsEmpty())
		return nullptr;
	return MCPError(FString::Printf(TEXT("Missing required parameter '%s'"), Key));
}

/** Extract a required string from either of two keys (e.g. "path" or "assetPath"). */
inline TSharedPtr<FJsonValue> RequireStringAlt(
	const TSharedPtr<FJsonObject>& Params,
	const TCHAR* Key1,
	const TCHAR* Key2,
	FString& OutValue)
{
	if (Params->TryGetStringField(Key1, OutValue) && !OutValue.IsEmpty())
		return nullptr;
	if (Params->TryGetStringField(Key2, OutValue) && !OutValue.IsEmpty())
		return nullptr;
	return MCPError(FString::Printf(TEXT("Missing required parameter '%s' (or '%s')"), Key1, Key2));
}

/** Extract an optional string, returning DefaultValue if absent. */
inline FString OptionalString(
	const TSharedPtr<FJsonObject>& Params,
	const TCHAR* Key,
	const FString& DefaultValue = TEXT(""))
{
	FString Value;
	return Params->TryGetStringField(Key, Value) ? Value : DefaultValue;
}

/** Extract an optional int32, returning DefaultValue if absent. */
inline int32 OptionalInt(
	const TSharedPtr<FJsonObject>& Params,
	const TCHAR* Key,
	int32 DefaultValue = 0)
{
	int32 Value;
	return Params->TryGetNumberField(Key, Value) ? Value : DefaultValue;
}

/** Extract an optional double, returning DefaultValue if absent. */
inline double OptionalNumber(
	const TSharedPtr<FJsonObject>& Params,
	const TCHAR* Key,
	double DefaultValue = 0.0)
{
	double Value;
	return Params->TryGetNumberField(Key, Value) ? Value : DefaultValue;
}

/** Extract an optional bool, returning DefaultValue if absent. */
inline bool OptionalBool(
	const TSharedPtr<FJsonObject>& Params,
	const TCHAR* Key,
	bool DefaultValue = false)
{
	bool Value;
	return Params->TryGetBoolField(Key, Value) ? Value : DefaultValue;
}

// ── Vector/Rotator/Color/Transform extraction ────────────────────────────────
//
// Wire shape contract (matches src/schemas.ts):
//   Vec3:    { x: number, y: number, z: number }
//   Rotator: { pitch: number, yaw: number, roll: number }
//   Color:   { r, g, b, a? }                          (a defaults to 1)
//   Transform: { location: Vec3, rotation: Rotator, scale: Vec3 }
//
// Per-axis numeric fields are individually optional. Missing axes inherit
// from the default value passed in. Use the *Strict variants when every
// axis must be present.

/** Read x/y/z fields out of a JSON object into Out. Returns true if any field
 *  was present. */
inline bool ReadVec3Fields(const TSharedPtr<FJsonObject>& Obj, FVector& Out)
{
	if (!Obj.IsValid()) return false;
	double Tmp;
	bool Any = false;
	if (Obj->TryGetNumberField(TEXT("x"), Tmp)) { Out.X = Tmp; Any = true; }
	if (Obj->TryGetNumberField(TEXT("y"), Tmp)) { Out.Y = Tmp; Any = true; }
	if (Obj->TryGetNumberField(TEXT("z"), Tmp)) { Out.Z = Tmp; Any = true; }
	return Any;
}

inline bool ReadRotatorFields(const TSharedPtr<FJsonObject>& Obj, FRotator& Out)
{
	if (!Obj.IsValid()) return false;
	double Tmp;
	bool Any = false;
	if (Obj->TryGetNumberField(TEXT("pitch"), Tmp)) { Out.Pitch = Tmp; Any = true; }
	if (Obj->TryGetNumberField(TEXT("yaw"),   Tmp)) { Out.Yaw   = Tmp; Any = true; }
	if (Obj->TryGetNumberField(TEXT("roll"),  Tmp)) { Out.Roll  = Tmp; Any = true; }
	return Any;
}

inline bool ReadLinearColorFields(const TSharedPtr<FJsonObject>& Obj, FLinearColor& Out)
{
	if (!Obj.IsValid()) return false;
	double Tmp;
	bool Any = false;
	if (Obj->TryGetNumberField(TEXT("r"), Tmp)) { Out.R = Tmp; Any = true; }
	if (Obj->TryGetNumberField(TEXT("g"), Tmp)) { Out.G = Tmp; Any = true; }
	if (Obj->TryGetNumberField(TEXT("b"), Tmp)) { Out.B = Tmp; Any = true; }
	if (Obj->TryGetNumberField(TEXT("a"), Tmp)) { Out.A = Tmp; Any = true; }
	return Any;
}

/** Extract an optional FVector from Params[Key]. Missing or non-object: returns DefaultValue.
 *  Individual missing axes inherit from DefaultValue. */
inline FVector OptionalVec3(
	const TSharedPtr<FJsonObject>& Params,
	const TCHAR* Key,
	const FVector& DefaultValue = FVector::ZeroVector)
{
	const TSharedPtr<FJsonObject>* Obj = nullptr;
	if (!Params->TryGetObjectField(Key, Obj) || !Obj || !(*Obj).IsValid()) return DefaultValue;
	FVector Out = DefaultValue;
	ReadVec3Fields(*Obj, Out);
	return Out;
}

/** Extract a required FVector. Returns error JSON on miss/malformed, nullptr on success. */
inline TSharedPtr<FJsonValue> RequireVec3(
	const TSharedPtr<FJsonObject>& Params,
	const TCHAR* Key,
	FVector& Out)
{
	const TSharedPtr<FJsonObject>* Obj = nullptr;
	if (!Params->TryGetObjectField(Key, Obj) || !Obj || !(*Obj).IsValid())
		return MCPError(FString::Printf(TEXT("Missing required vector parameter '%s' ({x,y,z})"), Key));
	Out = FVector::ZeroVector;
	if (!ReadVec3Fields(*Obj, Out))
		return MCPError(FString::Printf(TEXT("Vector '%s' has no x/y/z fields"), Key));
	return nullptr;
}

inline FRotator OptionalRotator(
	const TSharedPtr<FJsonObject>& Params,
	const TCHAR* Key,
	const FRotator& DefaultValue = FRotator::ZeroRotator)
{
	const TSharedPtr<FJsonObject>* Obj = nullptr;
	if (!Params->TryGetObjectField(Key, Obj) || !Obj || !(*Obj).IsValid()) return DefaultValue;
	FRotator Out = DefaultValue;
	ReadRotatorFields(*Obj, Out);
	return Out;
}

inline TSharedPtr<FJsonValue> RequireRotator(
	const TSharedPtr<FJsonObject>& Params,
	const TCHAR* Key,
	FRotator& Out)
{
	const TSharedPtr<FJsonObject>* Obj = nullptr;
	if (!Params->TryGetObjectField(Key, Obj) || !Obj || !(*Obj).IsValid())
		return MCPError(FString::Printf(TEXT("Missing required rotator parameter '%s' ({pitch,yaw,roll})"), Key));
	Out = FRotator::ZeroRotator;
	if (!ReadRotatorFields(*Obj, Out))
		return MCPError(FString::Printf(TEXT("Rotator '%s' has no pitch/yaw/roll fields"), Key));
	return nullptr;
}

inline FLinearColor OptionalLinearColor(
	const TSharedPtr<FJsonObject>& Params,
	const TCHAR* Key,
	const FLinearColor& DefaultValue = FLinearColor::White)
{
	const TSharedPtr<FJsonObject>* Obj = nullptr;
	if (!Params->TryGetObjectField(Key, Obj) || !Obj || !(*Obj).IsValid()) return DefaultValue;
	FLinearColor Out = DefaultValue;
	ReadLinearColorFields(*Obj, Out);
	return Out;
}

/** Inline FVector→JSON. Mirrors FMCPJsonSerializer::SerializeVector. Use this
 *  in handlers building result objects so the wire shape stays consistent. */
inline TSharedPtr<FJsonObject> MCPVec3ToJsonObject(const FVector& V)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetNumberField(TEXT("x"), V.X);
	Obj->SetNumberField(TEXT("y"), V.Y);
	Obj->SetNumberField(TEXT("z"), V.Z);
	return Obj;
}

inline TSharedPtr<FJsonObject> MCPRotatorToJsonObject(const FRotator& R)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetNumberField(TEXT("pitch"), R.Pitch);
	Obj->SetNumberField(TEXT("yaw"),   R.Yaw);
	Obj->SetNumberField(TEXT("roll"),  R.Roll);
	return Obj;
}

inline TSharedPtr<FJsonObject> MCPLinearColorToJsonObject(const FLinearColor& C)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetNumberField(TEXT("r"), C.R);
	Obj->SetNumberField(TEXT("g"), C.G);
	Obj->SetNumberField(TEXT("b"), C.B);
	Obj->SetNumberField(TEXT("a"), C.A);
	return Obj;
}

/** Extract an optional FTransform from Params[Key]. Reads location/rotation/scale sub-objects.
 *  Missing entirely or non-object: returns FTransform::Identity. */
inline FTransform OptionalTransform(
	const TSharedPtr<FJsonObject>& Params,
	const TCHAR* Key)
{
	const TSharedPtr<FJsonObject>* Obj = nullptr;
	if (!Params->TryGetObjectField(Key, Obj) || !Obj || !(*Obj).IsValid()) return FTransform::Identity;
	FVector  Loc   = FVector::ZeroVector;
	FRotator Rot   = FRotator::ZeroRotator;
	FVector  Scale = FVector::OneVector;
	const TSharedPtr<FJsonObject>* Sub = nullptr;
	if ((*Obj)->TryGetObjectField(TEXT("location"), Sub) && Sub) ReadVec3Fields(*Sub, Loc);
	if ((*Obj)->TryGetObjectField(TEXT("rotation"), Sub) && Sub) ReadRotatorFields(*Sub, Rot);
	if ((*Obj)->TryGetObjectField(TEXT("scale"),    Sub) && Sub) ReadVec3Fields(*Sub, Scale);
	return FTransform(Rot, Loc, Scale);
}

// ── Common helpers ───────────────────────────────────────────────────────────

/** Find a UClass by short name, handling A/U prefix resolution.
 *  e.g. "StaticMeshActor" finds AStaticMeshActor, "AnimInstance" finds UAnimInstance. */
inline UClass* FindClassByShortName(const FString& ClassName)
{
	UClass* PrefixedMatch = nullptr;
	for (TObjectIterator<UClass> It; It; ++It)
	{
		const FString& Name = It->GetName();
		if (Name == ClassName) return *It;
		if (!PrefixedMatch && (Name == TEXT("U") + ClassName || Name == TEXT("A") + ClassName))
		{
			PrefixedMatch = *It;
		}
	}
	return PrefixedMatch;
}

/** Get the editor world, or nullptr if not available. */
inline UWorld* GetEditorWorld()
{
	if (!GEditor) return nullptr;
	return GEditor->GetEditorWorldContext().World();
}

/** Get the active PIE/Game world if one is running, or nullptr. */
inline UWorld* GetPIEWorld()
{
	if (!GEngine) return nullptr;
	for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
	{
		if (Ctx.WorldType == EWorldType::PIE || Ctx.WorldType == EWorldType::Game)
		{
			if (UWorld* W = Ctx.World()) return W;
		}
	}
	return nullptr;
}

/** Resolve a world scope string ("editor"|"pie"|"game"|"auto") to a UWorld. "auto" prefers PIE if running. */
inline UWorld* ResolveWorldScope(const FString& Scope)
{
	if (Scope.Equals(TEXT("pie"), ESearchCase::IgnoreCase) || Scope.Equals(TEXT("game"), ESearchCase::IgnoreCase))
	{
		return GetPIEWorld();
	}
	if (Scope.Equals(TEXT("auto"), ESearchCase::IgnoreCase))
	{
		if (UWorld* W = GetPIEWorld()) return W;
		return GetEditorWorld();
	}
	return GetEditorWorld();
}

/** Get the editor world or return an error response. */
#define REQUIRE_EDITOR_WORLD(WorldVar) \
	UWorld* WorldVar = GetEditorWorld(); \
	if (!WorldVar) return MCPError(TEXT("Editor world not available"));

/** Load an asset by path with fallback to ObjectPath format.  Returns nullptr if not found. */
template <typename T>
T* LoadAssetByPath(const FString& AssetPath)
{
	T* Asset = LoadObject<T>(nullptr, *AssetPath);
	if (Asset) return Asset;

	// Try ObjectPath format: "/Game/Foo/Bar" → "/Game/Foo/Bar.Bar"
	if (!AssetPath.Contains(TEXT(".")))
	{
		FString AssetName;
		AssetPath.Split(TEXT("/"), nullptr, &AssetName, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		Asset = LoadObject<T>(nullptr, *(AssetPath + TEXT(".") + AssetName));
	}
	return Asset;
}

/** Load an asset or return an error response.  Assigns to OutVar. */
#define REQUIRE_ASSET(Type, OutVar, AssetPath) \
	Type* OutVar = LoadAssetByPath<Type>(AssetPath); \
	if (!OutVar) return MCPError(FString::Printf(TEXT("%s not found: %s"), TEXT(#Type), *AssetPath));

// ── Package save ─────────────────────────────────────────────────────────────

/** Mark the asset's package dirty and save it to disk. Used by every create/
 *  mutate handler that wants changes persisted across editor restarts.
 *  No-op if Asset or its package is null. Returns true on successful save. */
inline bool SaveAssetPackage(UObject* Asset)
{
	if (!Asset) return false;
	UPackage* Package = Asset->GetOutermost();
	if (!Package) return false;
	Package->MarkPackageDirty();
	const FString PackageFileName = FPackageName::LongPackageNameToFilename(
		Package->GetName(), FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Standalone;
	return UPackage::SavePackage(Package, nullptr, *PackageFileName, SaveArgs);
}

// ── GC root RAII ─────────────────────────────────────────────────────────────

/** RAII: root a UObject on construction, unroot on scope exit. Prevents the
 *  AddToRoot/RemoveFromRoot pairs from leaking when an early return (validation
 *  error, import failure) sneaks into the middle of the pair. */
class FGCRootScope
{
public:
	explicit FGCRootScope(UObject* InObject) : Object(InObject)
	{
		if (Object) Object->AddToRoot();
	}
	~FGCRootScope()
	{
		if (Object && Object->IsRooted()) Object->RemoveFromRoot();
	}
	FGCRootScope(const FGCRootScope&) = delete;
	FGCRootScope& operator=(const FGCRootScope&) = delete;
private:
	UObject* Object = nullptr;
};

// ── Reflection helpers ───────────────────────────────────────────────────────

/** Find a property by name and error out cleanly if missing. Returns nullptr
 *  and writes an error JSON to OutError when the property does not exist on
 *  the class, so callers get a typed response instead of a null deref. */
inline FProperty* FindPropertyChecked(
	UClass* Cls,
	const TCHAR* PropertyName,
	TSharedPtr<FJsonValue>& OutError)
{
	if (!Cls)
	{
		OutError = MCPError(FString::Printf(TEXT("FindPropertyChecked('%s'): null class"), PropertyName));
		return nullptr;
	}
	FProperty* Prop = Cls->FindPropertyByName(FName(PropertyName));
	if (!Prop)
	{
		OutError = MCPError(FString::Printf(
			TEXT("Property '%s' not found on class '%s' - engine version drift?"),
			PropertyName, *Cls->GetName()));
	}
	return Prop;
}

// ── Thread context ───────────────────────────────────────────────────────────

/** Defence-in-depth: assert we are on the game thread. UObject API calls from
 *  a non-game thread can corrupt engine state. Handlers are dispatched from
 *  GameThreadExecutor, so this should always hold; when it doesn't, the
 *  assertion surfaces the bug loudly rather than producing a silent race. */
#define MCP_CHECK_GAME_THREAD() \
	checkf(IsInGameThread(), TEXT("MCP handler ran off the game thread - UObject access would be racy"))
