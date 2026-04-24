#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"
#include "UObject/UObjectIterator.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"

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
