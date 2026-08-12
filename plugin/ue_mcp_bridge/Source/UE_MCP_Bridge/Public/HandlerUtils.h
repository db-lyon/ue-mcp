#pragma once

#include "CoreMinimal.h"
#include "Runtime/Launch/Resources/Version.h"
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

// Engine API tiers. One macro per supported minor version, so a gate reads the
// same everywhere and nobody writes a second scheme. The supported range is
// UE 5.4 through 5.8; 5.4 is the floor, which is why UE_MCP_HAS_5_4_API is
// true for every engine the plugin builds against and exists only so a gate
// can name the floor explicitly instead of leaving it implied.
#define UE_MCP_HAS_5_4_API ((ENGINE_MAJOR_VERSION > 5) || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 4))

// True on UE 5.5+ (and any future 6.x). Used to gate APIs introduced in 5.5
// that don't exist in 5.4: StateTreeEditingSubsystem, FExpressionInputIterator,
// AActor::Get/SetNetUpdateFrequency, UWidgetBlueprint::WidgetVariableNameToGuidMap,
// UPCGEditorGraphNodeBase, UIKRetargeterController::AssignIKRigToAllOps, etc.
#define UE_MCP_HAS_5_5_API ((ENGINE_MAJOR_VERSION > 5) || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5))

// True on UE 5.6+ (and any future 6.x). The tier between 5.5 and 5.7, kept so
// an API that arrived in 5.6 is gated by name rather than by an open-coded
// ENGINE_MINOR_VERSION test.
#define UE_MCP_HAS_5_6_API ((ENGINE_MAJOR_VERSION > 5) || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6))

// True on UE 5.7+. Gates EFindObjectFlags (the bool bExactClass overloads are
// deprecated there) and UPoseSearchDatabase's non-templated
// GetDatabaseAnimationAsset.
#define UE_MCP_HAS_5_7_API ((ENGINE_MAJOR_VERSION > 5) || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7))

// True on UE 5.8+. Gates EGetObjectsFlags and
// FStringTable::ImportStringsFromCSVFile; the bool / ImportStrings forms they
// replace are deprecated in 5.8 and warn on every user build, but do not exist
// before it. Also gates the one-argument UMaterial::SetMaterialUsage (5.7 has
// only the bNeedsRecompile form) and FCoreDelegates::ApplicationHeartbeat
// (added in 5.8; the status module carries its own copy of this macro because
// it must not depend on this one).
#define UE_MCP_HAS_5_8_API ((ENGINE_MAJOR_VERSION > 5) || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8))

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
// NOTE: Do not use a variadic template wrapper - UE 5.7's consteval format
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

/** Mark a result as "already existed, nothing created" - idempotent replay. */
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

/** Protected mount guardrail. Engine-shipped content (/Engine/, /Script/,
 *  /Memory/, /Temp/) and Verse runtime classes must never be mutated through
 *  the bridge: UEditorAssetLibrary::DeleteAsset will happily destroy files
 *  under <engineRoot>/Engine/Content/ if not stopped. Every handler that
 *  deletes, moves, renames or writes an asset calls this. Plugin content roots
 *  (mounted under /<PluginName>/) are NOT protected; per-project plugin content
 *  is expected to be writable.
 *
 *  This lives here rather than as a file-local copy per translation unit
 *  because the asset handlers are split across several files that share one
 *  unity blob: duplicate definitions collide at compile time, and independent
 *  copies drift, which is how a write path ends up enforcing a weaker rule
 *  than its neighbours. */
inline bool MCPIsProtectedAssetPath(const FString& Path)
{
	FString Normalized = Path;
	Normalized.TrimStartAndEndInline();
	if (Normalized.IsEmpty()) return false;
	Normalized = FPackageName::ExportTextPathToObjectPath(Normalized);
	Normalized.TrimStartAndEndInline();
	// Tolerate the surface form, which may arrive without a leading slash.
	if (!Normalized.StartsWith(TEXT("/"))) Normalized = TEXT("/") + Normalized;
	const FString Lower = Normalized.ToLower();
	if (Lower == TEXT("/engine") || Lower.StartsWith(TEXT("/engine/"))) return true;
	if (Lower == TEXT("/memory") || Lower.StartsWith(TEXT("/memory/"))) return true;
	if (Lower == TEXT("/temp") || Lower.StartsWith(TEXT("/temp/"))) return true;
	// Verse runtime objects surface as /Script/CoreUObject.* etc, so /Script/
	// is rejected wherever it appears, not just as a prefix.
	if (Lower == TEXT("/script") || Lower.Contains(TEXT("/script/"))) return true;
	return false;
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

/** Three-way actor lookup against the placed instances in a world: label
 *  first, then internal object name, then full path. Used by
 *  EditorHandlers_PIE invoke_function which accepts any of the three.
 *
 *  #806: the priority is fixed rather than "first actor that matches any of
 *  the three", because a token can be one actor's label and another actor's
 *  internal name at the same time, and which one won then depended on level
 *  iteration order. The label is what the outliner shows and what callers
 *  pass, so it decides outright; name and path only resolve the misses. */
inline AActor* FindActorByLabelNameOrPath(UWorld* World, const FString& Token)
{
	if (!World || Token.IsEmpty()) return nullptr;
	AActor* NameMatch = nullptr;
	AActor* PathMatch = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* A = *It;
		if (!IsValid(A)) continue;
		if (A->GetActorLabel() == Token) return A;
		if (!NameMatch && A->GetName() == Token) NameMatch = A;
		if (!PathMatch && A->GetPathName() == Token) PathMatch = A;
	}
	return NameMatch ? NameMatch : PathMatch;
}

/** Build the "no such actor" message for a failed label/name/path lookup.
 *  Names what was searched and offers the labels that contain the token, so a
 *  caller that guessed a label sees the real one instead of a bare miss. */
inline FString MCPDescribeActorLookupMiss(UWorld* World, const FString& Token, const FString& WorldLabel)
{
	int32 ActorCount = 0;
	TArray<FString> Near;
	if (World)
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* A = *It;
			if (!IsValid(A)) continue;
			++ActorCount;
			if (Near.Num() < 8 && A->GetActorLabel().Contains(Token))
			{
				Near.Add(A->GetActorLabel());
			}
		}
	}
	FString Msg = FString::Printf(
		TEXT("Actor '%s' not found in the %s world. Searched every placed actor by editor label, then by internal object name, then by full object path (%d actors)."),
		*Token, *WorldLabel, ActorCount);
	if (Near.Num() > 0)
	{
		Msg += FString::Printf(TEXT(" Labels containing that text: [%s]."), *FString::Join(Near, TEXT(", ")));
	}
	Msg += TEXT(" List the real labels with level(get_outliner).");
	return Msg;
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

/** Extract a JSON array of strings into a TArray<FString>. */
inline TArray<FString> JsonArrayToStringList(const TArray<TSharedPtr<FJsonValue>>* Arr)
{
	TArray<FString> Out;
	if (!Arr) return Out;
	for (const TSharedPtr<FJsonValue>& V : *Arr)
	{
		FString S;
		if (V.IsValid() && V->TryGetString(S)) Out.Add(S);
	}
	return Out;
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

// ── Class name resolution (#823) ─────────────────────────────────────────────
//
// UE reflection registers a class under its C++ name minus the type prefix:
// AActor is the UClass named "Actor", UMyConfig is "MyConfig", and the path is
// /Script/MyGame.MyConfig with no "U" in it. Callers reading engine headers
// naturally pass the prefixed source name (or the prefixed path), every
// exact-match lookup missed, and the bridge answered "Class not found" for a
// class that had been loaded the whole time. Every string-to-UClass path in the
// plugin goes through MCPResolveClass so that one normalization covers all of
// them instead of each handler growing its own half of the rules.

namespace MCPClassResolve
{
	/** Strip one leading UE type prefix (A/U/F/E/I/S/T) when what follows still
	 *  looks like a class name: "UMyConfig" becomes "MyConfig". Names whose
	 *  second character is not upper case are left alone, so "Actor" and
	 *  "Texture2D" survive untouched. */
	inline FString StripPrefix(const FString& Name)
	{
		if (Name.Len() < 3) return Name;
		const TCHAR First = Name[0];
		const bool bIsPrefix =
			First == TEXT('A') || First == TEXT('U') || First == TEXT('F') ||
			First == TEXT('E') || First == TEXT('I') || First == TEXT('S') ||
			First == TEXT('T');
		if (!bIsPrefix || !FChar::IsUpper(Name[1])) return Name;
		return Name.RightChop(1);
	}

	/** Engine bookkeeping classes that must never win a fuzzy match. */
	inline bool IsTransientClassName(const FString& Name)
	{
		return Name.StartsWith(TEXT("SKEL_")) || Name.StartsWith(TEXT("REINST_")) ||
		       Name.StartsWith(TEXT("TRASHCLASS_")) || Name.StartsWith(TEXT("HOTRELOADED_")) ||
		       Name.StartsWith(TEXT("PLACEHOLDER-"));
	}

	/** Every spelling the resolver will try, in the order it tries them. */
	inline TArray<FString> BuildCandidates(const FString& Spec)
	{
		TArray<FString> Out;
		const FString Trimmed = Spec.TrimStartAndEnd();
		if (Trimmed.IsEmpty()) return Out;

		auto Add = [&Out](const FString& Candidate)
		{
			if (!Candidate.IsEmpty()) Out.AddUnique(Candidate);
		};

		// Object path form: /Script/Module.Class or /Game/Path/Asset[.Asset].
		if (Trimmed.StartsWith(TEXT("/")))
		{
			Add(Trimmed);
			FString PackagePart, ObjectPart;
			if (Trimmed.Split(TEXT("."), &PackagePart, &ObjectPart, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
			{
				const FString StrippedObject = StripPrefix(ObjectPart);
				if (StrippedObject != ObjectPart)
				{
					Add(PackagePart + TEXT(".") + StrippedObject);
				}
				if (!ObjectPart.EndsWith(TEXT("_C")))
				{
					Add(Trimmed + TEXT("_C"));
				}
			}
			else
			{
				// Package-only path: /Game/Cfg/MyAsset resolves via /Game/Cfg/MyAsset.MyAsset.
				FString Leaf = Trimmed;
				int32 SlashIndex = INDEX_NONE;
				if (Trimmed.FindLastChar(TEXT('/'), SlashIndex)) Leaf = Trimmed.RightChop(SlashIndex + 1);
				if (!Leaf.IsEmpty())
				{
					Add(Trimmed + TEXT(".") + Leaf);
					Add(Trimmed + TEXT(".") + Leaf + TEXT("_C"));
				}
			}
			return Out;
		}

		// "Module.Class" shorthand: promote it to the /Script path, both spellings.
		FString ModulePart, NamePart;
		if (Trimmed.Split(TEXT("."), &ModulePart, &NamePart, ESearchCase::CaseSensitive, ESearchDir::FromEnd) &&
		    !ModulePart.IsEmpty() && !NamePart.IsEmpty())
		{
			Add(FString::Printf(TEXT("/Script/%s.%s"), *ModulePart, *NamePart));
			const FString StrippedName = StripPrefix(NamePart);
			if (StrippedName != NamePart)
			{
				Add(FString::Printf(TEXT("/Script/%s.%s"), *ModulePart, *StrippedName));
			}
			return Out;
		}

		// Bare name: literal, then prefix-stripped, then the prefixes agents drop.
		Add(Trimmed);
		const FString Stripped = StripPrefix(Trimmed);
		Add(Stripped);
		Add(TEXT("A") + Trimmed);
		Add(TEXT("U") + Trimmed);
		if (Trimmed.EndsWith(TEXT("_C")))
		{
			const FString WithoutGenerated = Trimmed.LeftChop(2);
			Add(WithoutGenerated);
			Add(StripPrefix(WithoutGenerated));
		}
		else
		{
			// Blueprint generated class for a bare Blueprint name.
			Add(Trimmed + TEXT("_C"));
			if (Stripped != Trimmed) Add(Stripped + TEXT("_C"));
		}
		return Out;
	}

	/** One exact lookup. Paths go through FindObject and (optionally) a quiet
	 *  load; bare names go through FindFirstObject, which is the UE 5.6+
	 *  replacement for the "any package" FindObject pattern. */
	inline UClass* LookupExact(const FString& Candidate, bool bAllowLoad)
	{
		if (Candidate.IsEmpty()) return nullptr;
		if (Candidate.Contains(TEXT("/")))
		{
			if (UClass* Found = FindObject<UClass>(nullptr, *Candidate)) return Found;
			if (!bAllowLoad) return nullptr;
			if (UClass* Loaded = LoadObject<UClass>(nullptr, *Candidate, nullptr, LOAD_NoWarn | LOAD_Quiet))
			{
				return Loaded;
			}
			return LoadClass<UObject>(nullptr, *Candidate, nullptr, LOAD_NoWarn | LOAD_Quiet, nullptr);
		}
		return FindFirstObject<UClass>(*Candidate, EFindFirstObjectOptions::NativeFirst);
	}

	/** Last resort: case-insensitive sweep of loaded classes against the same
	 *  candidate spellings. Native classes win ties so the answer stays stable
	 *  between sessions. */
	inline UClass* ScanCaseInsensitive(const TArray<FString>& Candidates)
	{
		UClass* NativeHit = nullptr;
		UClass* ContentHit = nullptr;
		for (TObjectIterator<UClass> It; It; ++It)
		{
			const FString Name = It->GetName();
			if (IsTransientClassName(Name)) continue;

			bool bMatch = false;
			for (const FString& Candidate : Candidates)
			{
				if (Candidate.Contains(TEXT("/"))) continue;
				if (Name.Equals(Candidate, ESearchCase::IgnoreCase)) { bMatch = true; break; }
			}
			if (!bMatch) continue;

			const UPackage* Package = It->GetOutermost();
			const bool bNative = Package && Package->GetName().StartsWith(TEXT("/Script/"));
			if (bNative) { if (!NativeHit) NativeHit = *It; }
			else if (!ContentHit) { ContentHit = *It; }
		}
		return NativeHit ? NativeHit : ContentHit;
	}

	/** Loaded class names closest to what the caller asked for, for error text. */
	inline TArray<FString> Suggest(const FString& Spec, int32 MaxResults = 5)
	{
		TArray<FString> Result;
		FString Needle = StripPrefix(Spec.TrimStartAndEnd());
		int32 SeparatorIndex = INDEX_NONE;
		if (Needle.FindLastChar(TEXT('.'), SeparatorIndex)) Needle = Needle.RightChop(SeparatorIndex + 1);
		if (Needle.FindLastChar(TEXT('/'), SeparatorIndex)) Needle = Needle.RightChop(SeparatorIndex + 1);
		Needle = StripPrefix(Needle).ToLower();
		if (Needle.Len() < 3) return Result;

		struct FScored { int32 Score; FString Name; };
		TArray<FScored> Scored;
		for (TObjectIterator<UClass> It; It; ++It)
		{
			const FString Name = It->GetName();
			if (IsTransientClassName(Name)) continue;
			const FString Lower = Name.ToLower();
			int32 Score;
			if (Lower == Needle)                 Score = 0;
			else if (Lower.StartsWith(Needle))   Score = 1;
			else if (Lower.EndsWith(Needle))     Score = 2;
			else if (Lower.Contains(Needle))     Score = 3;
			else continue;
			Scored.Add({ Score, Name });
		}
		Scored.Sort([](const FScored& A, const FScored& B)
		{
			if (A.Score != B.Score) return A.Score < B.Score;
			if (A.Name.Len() != B.Name.Len()) return A.Name.Len() < B.Name.Len();
			return A.Name < B.Name;
		});
		for (const FScored& Entry : Scored)
		{
			if (Result.Num() >= MaxResults) break;
			Result.AddUnique(Entry.Name);
		}
		return Result;
	}

	/** Full resolution. OutTried receives the candidate spellings in order. */
	inline UClass* Resolve(const FString& Spec, bool bAllowLoad, TArray<FString>* OutTried)
	{
		const TArray<FString> Candidates = BuildCandidates(Spec);
		if (OutTried) *OutTried = Candidates;
		for (const FString& Candidate : Candidates)
		{
			if (UClass* Found = LookupExact(Candidate, bAllowLoad)) return Found;
		}
		return ScanCaseInsensitive(Candidates);
	}

	/** Resolution constrained to subclasses of Base. Only reached when the
	 *  unconstrained answer is the wrong kind of class: "Timeline" must land on
	 *  the graph node, not on the component that shares the leaf name. */
	inline UClass* ResolveOfType(const FString& Spec, UClass* Base, bool bAllowLoad)
	{
		UClass* Direct = Resolve(Spec, bAllowLoad, nullptr);
		if (Direct && (!Base || Direct->IsChildOf(Base))) return Direct;
		if (!Base) return nullptr;

		const TArray<FString> Candidates = BuildCandidates(Spec);
		UClass* LooseHit = nullptr;
		for (TObjectIterator<UClass> It; It; ++It)
		{
			if (!It->IsChildOf(Base)) continue;
			const FString Name = It->GetName();
			if (IsTransientClassName(Name)) continue;
			for (const FString& Candidate : Candidates)
			{
				if (Candidate.Contains(TEXT("/"))) continue;
				if (Name.Equals(Candidate, ESearchCase::CaseSensitive)) return *It;
				if (!LooseHit && Name.Equals(Candidate, ESearchCase::IgnoreCase)) LooseHit = *It;
			}
		}
		return LooseHit;
	}
}

/** Resolve a class name or path to a UClass, tolerating the C++ type prefix.
 *  Order: the literal spelling, the prefix-stripped spelling, prefixed
 *  spellings, the /Script/Module.Class path form, the Blueprint generated
 *  class, then a case-insensitive sweep. Pass bAllowLoad=false to keep the
 *  lookup non-loading (a hit then means "already in memory"). */
inline UClass* MCPResolveClass(const FString& Spec, bool bAllowLoad = true)
{
	return MCPClassResolve::Resolve(Spec, bAllowLoad, nullptr);
}

/** Same resolution, restricted to subclasses of Base. Use it wherever only one
 *  family of class is meaningful (graph nodes, schemas, factories) so a leaf
 *  name shared with an unrelated class cannot win. */
inline UClass* MCPResolveClassOfType(const FString& Spec, UClass* Base, bool bAllowLoad = true)
{
	return MCPClassResolve::ResolveOfType(Spec, Base, bAllowLoad);
}

/** "Class not found", with the exact spellings tried and the closest loaded
 *  class names, so a caller can correct the argument without guessing. */
inline TSharedPtr<FJsonValue> MCPClassNotFoundError(
	const FString& Spec,
	const FString& ParamName = TEXT("className"))
{
	const TArray<FString> Tried = MCPClassResolve::BuildCandidates(Spec);
	const TArray<FString> Suggestions = MCPClassResolve::Suggest(Spec);

	FString Message = FString::Printf(
		TEXT("Class not found for %s '%s'. Tried: %s. UE reflection stores class names without the C++ type prefix, so UMyConfig is registered as 'MyConfig' and its path is /Script/<Module>.MyConfig."),
		*ParamName, *Spec, *FString::Join(Tried, TEXT(", ")));
	if (Suggestions.Num() > 0)
	{
		Message += FString::Printf(TEXT(" Closest loaded classes: %s."), *FString::Join(Suggestions, TEXT(", ")));
	}
	else
	{
		Message += TEXT(" No loaded class name resembles it: the owning module may not be loaded yet (check reflection(is_module_loaded)).");
	}

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), false);
	Obj->SetStringField(TEXT("error"), Message);
	Obj->SetStringField(TEXT("reason"), TEXT("class_not_found"));
	Obj->SetStringField(TEXT("requested"), Spec);
	TArray<TSharedPtr<FJsonValue>> TriedJson;
	for (const FString& Candidate : Tried) TriedJson.Add(MakeShared<FJsonValueString>(Candidate));
	Obj->SetArrayField(TEXT("tried"), TriedJson);
	TArray<TSharedPtr<FJsonValue>> SuggestJson;
	for (const FString& Name : Suggestions) SuggestJson.Add(MakeShared<FJsonValueString>(Name));
	Obj->SetArrayField(TEXT("suggestions"), SuggestJson);
	return MakeShared<FJsonValueObject>(Obj);
}

/** The name resolved but the class cannot be used here. Reported separately
 *  from "not found" so a caller stops re-spelling a name that was correct. */
inline TSharedPtr<FJsonValue> MCPClassUnusableError(
	const FString& Spec,
	UClass* Resolved,
	const FString& Reason,
	const FString& Detail)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), false);
	Obj->SetStringField(TEXT("error"), FString::Printf(
		TEXT("Class '%s' resolved to %s but cannot be used here: %s"),
		*Spec, Resolved ? *Resolved->GetPathName() : TEXT("<null>"), *Detail));
	Obj->SetStringField(TEXT("reason"), Reason);
	Obj->SetStringField(TEXT("requested"), Spec);
	if (Resolved)
	{
		Obj->SetStringField(TEXT("resolvedClass"), Resolved->GetName());
		Obj->SetStringField(TEXT("resolvedPath"), Resolved->GetPathName());
	}
	return MakeShared<FJsonValueObject>(Obj);
}

/** Guard a resolved class: concrete (optional) and derived from RequiredBase
 *  (optional). Returns an error value to return directly, or an unset pointer
 *  when the class is fine. */
inline TSharedPtr<FJsonValue> MCPCheckClassUsable(
	const FString& Spec,
	UClass* Resolved,
	UClass* RequiredBase = nullptr,
	bool bRequireConcrete = true)
{
	if (!Resolved) return MCPClassNotFoundError(Spec);
	if (RequiredBase && !Resolved->IsChildOf(RequiredBase))
	{
		return MCPClassUnusableError(Spec, Resolved, TEXT("wrong_base"), FString::Printf(
			TEXT("it does not derive from %s (its parent chain starts at %s)"),
			*RequiredBase->GetName(),
			Resolved->GetSuperClass() ? *Resolved->GetSuperClass()->GetName() : TEXT("none")));
	}
	if (bRequireConcrete && Resolved->HasAnyClassFlags(CLASS_Abstract))
	{
		return MCPClassUnusableError(Spec, Resolved, TEXT("abstract"),
			TEXT("it is abstract, so it cannot be instantiated. Pass a concrete subclass."));
	}
	if (bRequireConcrete && Resolved->HasAnyClassFlags(CLASS_Deprecated))
	{
		return MCPClassUnusableError(Spec, Resolved, TEXT("deprecated"),
			TEXT("it is deprecated and the engine refuses to instantiate it."));
	}
	return TSharedPtr<FJsonValue>();
}

// ── Common helpers ───────────────────────────────────────────────────────────

/** Find a UClass by short name, handling UE type prefix resolution in both
 *  directions: "StaticMeshActor" finds AStaticMeshActor and "UMyConfig" finds
 *  the class registered as "MyConfig". Thin wrapper over MCPResolveClass so
 *  every existing caller inherits the full resolution order. */
inline UClass* FindClassByShortName(const FString& ClassName)
{
	return MCPResolveClass(ClassName);
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

/**
 * #778: get a specific PIE world by its instance id. GetPIEWorld() returns the
 * first PIE context it finds, which in a multi-instance session is the server
 * - so every runtime read resolved to the server and there was no way to
 * inspect a client at all. Pass INDEX_NONE for "first available".
 */
inline UWorld* GetPIEWorldByInstance(int32 PIEInstance)
{
	if (!GEngine) return nullptr;
	for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
	{
		if (Ctx.WorldType != EWorldType::PIE && Ctx.WorldType != EWorldType::Game) continue;
		if (PIEInstance != INDEX_NONE && Ctx.PIEInstance != PIEInstance) continue;
		if (UWorld* W = Ctx.World()) return W;
	}
	return nullptr;
}

/** Net role of a PIE world, as a short string for reporting. */
inline FString DescribePIENetMode(UWorld* World)
{
	if (!World) return TEXT("none");
	switch (World->GetNetMode())
	{
		case NM_Standalone:      return TEXT("standalone");
		case NM_DedicatedServer: return TEXT("dedicatedServer");
		case NM_ListenServer:    return TEXT("listenServer");
		case NM_Client:          return TEXT("client");
		default:                 return TEXT("unknown");
	}
}

/** Resolve a world scope string ("editor"|"pie"|"game"|"auto") to a UWorld. "auto" prefers PIE if running. */
inline UWorld* ResolveWorldScope(const FString& Scope, int32 PIEInstance = INDEX_NONE)
{
	if (Scope.Equals(TEXT("pie"), ESearchCase::IgnoreCase) || Scope.Equals(TEXT("game"), ESearchCase::IgnoreCase))
	{
		return GetPIEWorldByInstance(PIEInstance);
	}
	if (Scope.Equals(TEXT("auto"), ESearchCase::IgnoreCase))
	{
		if (UWorld* W = GetPIEWorldByInstance(PIEInstance)) return W;
		return GetEditorWorld();
	}
	return GetEditorWorld();
}

/**
 * Resolve the world a request targets from its own params: `world`
 * (editor|pie|game|auto) plus an optional `pieInstance` selector. Keeping this
 * in one place means adding multi-instance support to an action is a one-line
 * change at the call site rather than a re-implementation.
 */
inline UWorld* ResolveWorldFromParams(const TSharedPtr<FJsonObject>& Params, const TCHAR* DefaultScope = TEXT("editor"))
{
	const FString Scope = OptionalString(Params, TEXT("world"), DefaultScope);
	int32 PIEInstance = INDEX_NONE;
	double Raw = 0.0;
	if (Params.IsValid() && Params->TryGetNumberField(TEXT("pieInstance"), Raw))
	{
		PIEInstance = FMath::RoundToInt(Raw);
	}
	return ResolveWorldScope(Scope, PIEInstance);
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
