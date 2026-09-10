#pragma once

#include "CoreMinimal.h"
#include "Runtime/Launch/Resources/Version.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"
#include "UObject/UObjectIterator.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Engine/World.h"
#include "Engine/Blueprint.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EditorAssetLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/Guid.h"
#include "UObject/UObjectHash.h"
#include "Templates/Casts.h"

#include <type_traits>

// Engine API version gates. One macro per supported minor version, so a gate reads the
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

// True on UE 5.6+ (and any future 6.x). The gate between 5.5 and 5.7, kept so
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

// ── Engine API shims ─────────────────────────────────────────────────────────
//
// Where an engine API was renamed or wrapped between 5.4 and 5.8, the shim goes
// here rather than in a handler, so two handlers cannot end up with two
// spellings of the same rule. Headers that moved live in MCPEngineCompat.h.

/** UStaticMesh Nanite settings. 5.5 added the accessor pair; 5.4 has only the
 *  member, which 5.7 in turn deprecated direct access to. */
inline FMeshNaniteSettings MCPGetNaniteSettings(const UStaticMesh* Mesh)
{
	if (!Mesh) return FMeshNaniteSettings();
#if UE_MCP_HAS_5_5_API
	return Mesh->GetNaniteSettings();
#else
	return Mesh->NaniteSettings;
#endif
}

/** Counterpart to MCPGetNaniteSettings. The caller still owns Modify(). */
inline void MCPSetNaniteSettings(UStaticMesh* Mesh, const FMeshNaniteSettings& InSettings)
{
	if (!Mesh) return;
#if UE_MCP_HAS_5_5_API
	Mesh->SetNaniteSettings(InSettings);
#else
	Mesh->NaniteSettings = InSettings;
#endif
}

/** Per-component Nanite opt-outs. 5.5 wrapped the bitfields in accessors. */
inline bool MCPIsDisallowNanite(const UStaticMeshComponent* Component)
{
	if (!Component) return false;
#if UE_MCP_HAS_5_5_API
	return Component->IsDisallowNanite();
#else
	return Component->bDisallowNanite != 0;
#endif
}

inline bool MCPIsForceDisableNanite(const UStaticMeshComponent* Component)
{
	if (!Component) return false;
#if UE_MCP_HAS_5_5_API
	return Component->IsForceDisableNanite();
#else
	return Component->bForceDisableNanite != 0;
#endif
}

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

/** State that this mutation has no inverse, and say why.
 *
 *  The counterpart to MCPSetRollback, and the difference between a mutation
 *  that FORGOT its inverse and one that DECIDED it has none. Both emit no
 *  rollback record, so from the outside they are indistinguishable unless the
 *  second one says so in the result body.
 *
 *  The reason is a required argument rather than an optional one. A bare
 *  `rollbackPossible: false` tells a caller that recovery is off the table
 *  without telling it why, which is the half of the answer that decides what
 *  the caller does next; and a handler that sets the flag and forgets the note
 *  still reads as considered to anything auditing the pair. Taking both in one
 *  call is what makes them inseparable.
 *
 *  Keep the note concrete: what was changed, and what call would have to exist
 *  for it to be undone. See docs/handler-conventions.md. */
inline void MCPSetNoRollback(TSharedPtr<FJsonObject> Result, const FString& Reason)
{
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"), Reason);
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

/** State that whether this call changed anything CANNOT BE READ, and say why.
 *
 *  The idempotency counterpart to MCPSetNoRollback, and it exists for the same
 *  reason: a handler that cannot answer and says so has finished the job, and
 *  one that says nothing has not, but from the outside the two look identical.
 *
 *  The cases are real and narrow. epic(call_tool) dispatches a wrapped engine
 *  tool whose writes it never sees, and the Fab module publishes no
 *  authentication or library state this build can read back. Both are asking
 *  about somebody else's code, so a fabricated `unchanged: false` would be a
 *  claim rather than a reading, which is worse than an honest absence.
 *
 *  This is NOT the escape hatch for a handler that could measure its effect and
 *  did not. If the state is readable, read it before and after and report what
 *  moved. The reason is a required argument for the same purpose it is on
 *  MCPSetNoRollback: the flag alone tells a caller that replay safety is
 *  unknown without telling it why, and the pair cannot come apart if one call
 *  sets both.
 *
 *  The spelling was already in use in FabHandlers before this helper existed,
 *  which is exactly how the earlier rollbackPossible drift started: an idiom
 *  spread by copy while the convention audit recognised none of it. */
inline void MCPSetIdempotencyUnobservable(TSharedPtr<FJsonObject> Result, const FString& Reason)
{
	Result->SetBoolField(TEXT("idempotencyObservable"), false);
	Result->SetStringField(TEXT("idempotencyNote"), Reason);
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

// -- Asset path forms ---------------------------------------------------------

/** The forms one asset path can take. `/Game/Foo/DT_Thing` names the package
 *  and `/Game/Foo/DT_Thing.DT_Thing` names the asset inside it; both are
 *  legitimate ways to address the same asset, asset(search) reports the first
 *  one, and #957 was a set of actions that accepted only the second. Deriving
 *  both forms once, here, is what lets every action accept either and lets a
 *  miss report which form it actually tried. */
struct FMCPAssetPathForms
{
	/** Exactly what the caller sent, untouched. */
	FString Input;
	/** `/Game/Foo/DT_Thing` */
	FString PackagePath;
	/** `/Game/Foo/DT_Thing.DT_Thing` */
	FString ObjectPath;
	/** `DT_Thing` */
	FString AssetName;
	/** True when the caller already supplied the object name. */
	bool bInputCarriedObjectName = false;
};

/** Derive every path form from whatever the caller supplied. Accepts the
 *  export-text form (`DataTable'/Game/Foo/DT.DT'`), the object path and the
 *  bare package path, and tolerates surrounding whitespace. */
inline FMCPAssetPathForms MCPAssetPathForms(const FString& AssetPath)
{
	FMCPAssetPathForms Forms;
	Forms.Input = AssetPath;

	FString Normalized = FPackageName::ExportTextPathToObjectPath(AssetPath);
	Normalized.TrimStartAndEndInline();
	if (Normalized.IsEmpty()) return Forms;

	Forms.PackagePath = FPackageName::ObjectPathToPackageName(Normalized);
	// ObjectPathToPackageName is the identity on a bare package path, so a
	// longer input is the only thing that can have carried an object name.
	Forms.bInputCarriedObjectName = Normalized.Len() > Forms.PackagePath.Len();

	if (Forms.bInputCarriedObjectName)
	{
		Forms.ObjectPath = Normalized;
		Forms.AssetName = FPackageName::ObjectPathToObjectName(Normalized);
	}
	else
	{
		// A package holds its asset under the package's own leaf name. This is
		// the convention every content asset follows and the form
		// UEditorAssetLibrary::LoadAsset builds internally.
		if (!Forms.PackagePath.Split(TEXT("/"), nullptr, &Forms.AssetName,
			ESearchCase::CaseSensitive, ESearchDir::FromEnd))
		{
			Forms.AssetName = Forms.PackagePath;
		}
		Forms.ObjectPath = Forms.AssetName.IsEmpty()
			? Forms.PackagePath
			: Forms.PackagePath + TEXT(".") + Forms.AssetName;
	}
	return Forms;
}

/** Load the asset at `AssetPath`, the way asset(read) does.
 *
 *  UEditorAssetLibrary::LoadAsset is the usual entry point, but it validates
 *  the path through EditorScriptingHelpers before it loads anything and
 *  answers null for path forms it does not accept, and for any call made
 *  while the editor is in play-in-editor. asset(read) has always had a
 *  LoadObject fallback for exactly that reason, and the type-specific readers
 *  did not: read_datatable, get_datatable_row and export all reported
 *  "Asset not found" or "Asset is not a DataTable" for assets that
 *  asset(read) opened and correctly named as DataTables (#930).
 *
 *  This lives here rather than as a copy per handler file: the asset handlers
 *  share one unity blob, and a second copy would either collide at compile
 *  time or drift into resolving differently from its neighbours. */
inline UObject* MCPLoadAssetObject(const FString& AssetPath)
{
	if (AssetPath.IsEmpty()) return nullptr;

	const FMCPAssetPathForms Forms = MCPAssetPathForms(AssetPath);
	if (Forms.ObjectPath.IsEmpty()) return nullptr;

	// An object already in memory is the answer, and running a path validator
	// over it can only turn a good answer into a null and an error log. The
	// object path is what this step needs: a path with no "." names a package,
	// and returning the UPackage in place of the asset would be a worse answer
	// than not looking, so the derived object path stands in for it.
	if (UObject* Loaded = FindObject<UObject>(nullptr, *Forms.ObjectPath))
	{
		if (!Loaded->IsA<UPackage>()) return Loaded;
	}

	if (UObject* ViaEditorLibrary = UEditorAssetLibrary::LoadAsset(AssetPath))
	{
		return ViaEditorLibrary;
	}

	// #957: the caller's own form is tried first so nothing that used to work
	// stops working, and the derived object path second. A bare package path
	// for an asset that is not loaded yet is the case that used to answer
	// "Asset not found" for an asset asset(search) had just reported by that
	// exact string, because the load stopped at a form the loader would not
	// resolve on its own.
	if (UObject* ViaInput = LoadObject<UObject>(nullptr, *AssetPath))
	{
		if (!ViaInput->IsA<UPackage>()) return ViaInput;
	}
	if (!Forms.bInputCarriedObjectName)
	{
		if (UObject* ViaObjectPath = LoadObject<UObject>(nullptr, *Forms.ObjectPath))
		{
			if (!ViaObjectPath->IsA<UPackage>()) return ViaObjectPath;
		}
	}
	return nullptr;
}

/** Look an asset path up in the Asset Registry without loading anything.
 *  Tries the exact object path first, then any asset the registry holds in
 *  that package, which is what distinguishes "the package has an asset under
 *  a different name" from "there is nothing there at all". */
inline FAssetData MCPFindAssetDataForPath(const FMCPAssetPathForms& Forms)
{
	if (Forms.PackagePath.IsEmpty()) return FAssetData();

	FAssetRegistryModule* Module = FModuleManager::GetModulePtr<FAssetRegistryModule>(TEXT("AssetRegistry"));
	if (!Module) return FAssetData();
	IAssetRegistry& Registry = Module->Get();

	if (!Forms.ObjectPath.IsEmpty())
	{
		const FAssetData Exact = Registry.GetAssetByObjectPath(FSoftObjectPath(Forms.ObjectPath));
		if (Exact.IsValid()) return Exact;
	}

	TArray<FAssetData> InPackage;
	Registry.GetAssetsByPackageName(FName(*Forms.PackagePath), InPackage);
	for (const FAssetData& Candidate : InPackage)
	{
		if (Candidate.IsValid()) return Candidate;
	}
	return FAssetData();
}

/** The answer for a path MCPLoadAssetObject could not resolve.
 *
 *  A bare "Asset not found" is the same sentence for a path that names nothing,
 *  a path whose shape the loader rejects, and an asset whose package will not
 *  open, and #957 and #913 were both reported as missing assets that were not
 *  missing at all. This names the forms that were tried, says whether the
 *  package is on disk, reports what the Asset Registry knows without loading
 *  anything, and, when the registry holds the asset under a different object
 *  path, names the form that would have worked. */
inline TSharedPtr<FJsonValue> MCPAssetNotFoundError(const FString& AssetPath, const FString& Context = FString())
{
	const FMCPAssetPathForms Forms = MCPAssetPathForms(AssetPath);
	const FAssetData Found = MCPFindAssetDataForPath(Forms);
	const bool bPackageOnDisk = !Forms.PackagePath.IsEmpty()
		&& FPackageName::IsValidLongPackageName(Forms.PackagePath)
		&& FPackageName::DoesPackageExist(Forms.PackagePath);

	// Context, when given, names what the path was supposed to be, so the
	// sentence reads "Source asset not found: ..." rather than the generic form.
	const FString Prefix = FString::Printf(
		TEXT("%s not found: '%s'."), Context.IsEmpty() ? TEXT("Asset") : *Context, *AssetPath);

	FString RegistryObjectPath;
	FString RegistryClass;
	if (Found.IsValid())
	{
		RegistryObjectPath = Found.GetSoftObjectPath().ToString();
		RegistryClass = Found.AssetClassPath.GetAssetName().ToString();
	}

	FString Reason;
	FString Suggestion;
	FString Message;
	if (Found.IsValid() && !RegistryObjectPath.Equals(Forms.ObjectPath, ESearchCase::IgnoreCase))
	{
		// The package holds an asset, just not under the name the path form
		// implies. Naming it is the difference between a dead end and a fix.
		Reason = TEXT("pathShape");
		Suggestion = RegistryObjectPath;
		Message = FString::Printf(
			TEXT("%s The Asset Registry has '%s' (%s) in package '%s'. Pass that object path."),
			*Prefix, *RegistryObjectPath, *RegistryClass, *Forms.PackagePath);
	}
	else if (Found.IsValid())
	{
		Reason = TEXT("loadFailed");
		Message = FString::Printf(
			TEXT("%s The Asset Registry lists '%s' (%s), so the asset exists but the package would not open. ")
			TEXT("It may be corrupt, or reference a class the editor cannot resolve."),
			*Prefix, *RegistryObjectPath, *RegistryClass);
	}
	else if (bPackageOnDisk)
	{
		Reason = TEXT("notIndexed");
		Suggestion = Forms.ObjectPath;
		Message = FString::Printf(
			TEXT("%s A package file exists at '%s' but the Asset Registry holds no asset in it, ")
			TEXT("so it may still be scanning. Tried '%s' and '%s'."),
			*Prefix, *Forms.PackagePath, *AssetPath, *Forms.ObjectPath);
	}
	else
	{
		Reason = TEXT("missing");
		Message = FString::Printf(
			TEXT("%s No package exists at '%s' and the Asset Registry has no entry for it. ")
			TEXT("Tried '%s' and '%s'."),
			*Prefix, *Forms.PackagePath, *AssetPath, *Forms.ObjectPath);
	}

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), false);
	Obj->SetStringField(TEXT("error"), Message);
	Obj->SetStringField(TEXT("assetPath"), AssetPath);
	Obj->SetStringField(TEXT("packagePath"), Forms.PackagePath);
	Obj->SetStringField(TEXT("objectPath"), Forms.ObjectPath);
	Obj->SetBoolField(TEXT("packageExistsOnDisk"), bPackageOnDisk);
	Obj->SetBoolField(TEXT("registryMatched"), Found.IsValid());
	Obj->SetStringField(TEXT("reason"), Reason);
	if (Found.IsValid())
	{
		Obj->SetStringField(TEXT("registryObjectPath"), RegistryObjectPath);
		Obj->SetStringField(TEXT("registryClass"), RegistryClass);
	}
	if (!Suggestion.IsEmpty())
	{
		Obj->SetStringField(TEXT("suggestedPath"), Suggestion);
	}
	return MakeShared<FJsonValueObject>(Obj);
}

/** Resolve an asset path or hand back the diagnostic error. Returns nullptr
 *  with OutError set on a miss, so a handler reads as
 *  `if (!Asset) return OutError;`. */
inline UObject* MCPRequireAssetObject(
	const FString& AssetPath,
	TSharedPtr<FJsonValue>& OutError,
	const FString& Context = FString())
{
	UObject* Asset = MCPLoadAssetObject(AssetPath);
	if (!Asset)
	{
		OutError = MCPAssetNotFoundError(AssetPath, Context);
	}
	return Asset;
}

/** The answer for a path that resolved to something of the wrong type.
 *  Distinct from a miss on purpose: "not a DataTable" used to be the sentence
 *  a caller saw when the path resolved to nothing at all. */
inline TSharedPtr<FJsonValue> MCPAssetWrongTypeError(
	const FString& AssetPath,
	const UObject* Found,
	const TCHAR* ExpectedType)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), false);
	Obj->SetStringField(TEXT("error"), FString::Printf(
		TEXT("Asset is not a %s: '%s' (found a %s)."),
		ExpectedType, *AssetPath,
		Found ? *Found->GetClass()->GetName() : TEXT("null")));
	Obj->SetStringField(TEXT("assetPath"), AssetPath);
	Obj->SetStringField(TEXT("expectedClass"), ExpectedType);
	if (Found)
	{
		Obj->SetStringField(TEXT("foundClass"), Found->GetClass()->GetName());
		Obj->SetStringField(TEXT("objectPath"), Found->GetPathName());
	}
	return MakeShared<FJsonValueObject>(Obj);
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

/** The refusal a protected mount produces. Beside the rule it enforces, so a
 *  handler cannot pair MCPIsProtectedAssetPath with a message of its own that
 *  says something slightly different. */
inline TSharedPtr<FJsonValue> MCPProtectedPathError(const FString& Path)
{
	return MCPError(FString::Printf(
		TEXT("Refusing to mutate protected mount: %s. Engine, /Script/, /Memory/, /Temp/ are read-only via the bridge."),
		*Path));
}

/** Emit the standard delete_asset rollback record on a create result. */
inline void MCPSetDeleteAssetRollback(TSharedPtr<FJsonObject> Result, const FString& AssetPath)
{
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	MCPSetRollback(Result, TEXT("delete_asset"), Payload);
}

// ── Actor selection ──────────────────────────────────────────────────────────
//
// Editor labels are NOT unique. A copy-pasted Blueprint gives every copy the
// same label, and a label lookup that answers with "the first actor the
// iterator reached" is a coin flip decided by streaming order. #983 is what
// that costs: several actors labelled BP_SnappyRoad2, a write aimed at the one
// the user had selected, and the edit landing on a road at the other end of
// the map with a success response and nothing to suggest a choice was made.
//
// So there is one resolver, and it refuses rather than guesses:
//
//   * 'actorPath' is the unambiguous selector and wins whenever it is given.
//   * A label naming more than one actor is an error listing every candidate
//     and its actorPath, so the caller can retry precisely.
//   * There is no "just pick one" override. The precise selector already
//     exists, so a caller who wants a specific one of the duplicates has a
//     correct answer, and a caller who wants all of them is asking for a
//     different, plural action. A flag that picks an arbitrary actor out of a
//     set the caller could not tell apart is the same silent wrong write with
//     a name on it.
//
// The plural need is served by MCPCollectActorsByToken, which returns every
// match and is what the ignore-list and reference-list parameters use.

/** How a selector token is allowed to match an actor. */
enum class EMCPActorMatch : uint8
{
	/** Editor label only. */
	Label,
	/** Editor label, then the internal UObject name. */
	LabelOrName,
	/** Editor label, then internal name, then the full object path. */
	LabelNameOrPath,
};

/** The actor in World whose full object path is Path, or nullptr. Paths are
 *  unique, so there is never a choice to make. Accepts the export-text form
 *  (Actor'/Game/...') and falls back to a case-insensitive compare, because a
 *  path that came back from one action and was pasted into another is the
 *  whole point of having it. */
inline AActor* MCPFindActorByPath(UWorld* World, const FString& Path)
{
	if (!World || Path.IsEmpty()) return nullptr;
	FString Wanted = FPackageName::ExportTextPathToObjectPath(Path);
	Wanted.TrimStartAndEndInline();
	if (Wanted.IsEmpty()) return nullptr;
	AActor* CaseInsensitive = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* A = *It;
		if (!IsValid(A)) continue;
		const FString Actual = A->GetPathName();
		if (Actual.Equals(Wanted, ESearchCase::CaseSensitive)) return A;
		if (!CaseInsensitive && Actual.Equals(Wanted, ESearchCase::IgnoreCase)) CaseInsensitive = A;
	}
	return CaseInsensitive;
}

/** Every actor the token names under Match, sorted by object path so the order
 *  is the same on every run rather than whatever the actor iterator happened
 *  to produce that time.
 *
 *  The passes do not blend: a token that is one actor's label and another
 *  actor's internal name resolves to the label match alone, because the label
 *  is what the outliner shows and what a caller types. Name and path answer
 *  only when the label pass found nothing (#806). */
inline void MCPCollectActorsByToken(
	UWorld* World,
	const FString& Token,
	EMCPActorMatch Match,
	TArray<AActor*>& OutMatches)
{
	OutMatches.Reset();
	if (!World || Token.IsEmpty()) return;

	TArray<AActor*> ByName;
	TArray<AActor*> ByPath;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* A = *It;
		if (!IsValid(A)) continue;
		if (A->GetActorLabel() == Token) { OutMatches.Add(A); continue; }
		if (Match != EMCPActorMatch::Label && A->GetName() == Token) { ByName.Add(A); continue; }
		if (Match == EMCPActorMatch::LabelNameOrPath && A->GetPathName() == Token) { ByPath.Add(A); }
	}
	if (OutMatches.Num() == 0) OutMatches = MoveTemp(ByName);
	if (OutMatches.Num() == 0) OutMatches = MoveTemp(ByPath);

	OutMatches.Sort([](const AActor& A, const AActor& B)
	{
		return A.GetPathName().Compare(B.GetPathName()) < 0;
	});
}

/** Which pass of MCPCollectActorsByToken produced a match, for the message. */
inline const TCHAR* MCPDescribeActorMatchTier(const FString& Token, AActor* Match)
{
	if (Match && Match->GetActorLabel() == Token) return TEXT("editor label");
	if (Match && Match->GetName() == Token) return TEXT("internal object name");
	return TEXT("object path");
}

/** One candidate row in an ambiguity refusal: enough to tell two same-labelled
 *  actors apart without a follow-up call, plus the actorPath to retry with. */
inline TSharedPtr<FJsonObject> MCPDescribeActorCandidate(AActor* Actor)
{
	TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
	if (!Actor) return Row;
	Row->SetStringField(TEXT("actorPath"), Actor->GetPathName());
	Row->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
	Row->SetStringField(TEXT("actorName"), Actor->GetName());
	Row->SetStringField(TEXT("actorClass"), Actor->GetClass()->GetName());
	Row->SetStringField(TEXT("folderPath"), Actor->GetFolderPath().ToString());
	const FVector Loc = Actor->GetActorLocation();
	TSharedPtr<FJsonObject> LocObj = MakeShared<FJsonObject>();
	LocObj->SetNumberField(TEXT("x"), Loc.X);
	LocObj->SetNumberField(TEXT("y"), Loc.Y);
	LocObj->SetNumberField(TEXT("z"), Loc.Z);
	Row->SetObjectField(TEXT("location"), LocObj);
	return Row;
}

/** The refusal an ambiguous selector produces. Lists every candidate and its
 *  actorPath, so the retry is a copy of one field rather than a hunt back
 *  through get_outliner. */
inline TSharedPtr<FJsonValue> MCPAmbiguousActorError(
	const FString& Token,
	const TCHAR* LabelKey,
	const TCHAR* PathKey,
	const TCHAR* MatchedBy,
	const TArray<AActor*>& Candidates)
{
	const int32 Cap = 25;
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), false);
	Obj->SetStringField(TEXT("error"), FString::Printf(
		TEXT("Ambiguous actor selector: '%s' is the %s of %d actors. Editor labels are not unique, so this call refuses rather than picking one of them. Retry with '%s' set to one of the candidate paths below."),
		*Token, MatchedBy, Candidates.Num(), PathKey));
	Obj->SetBoolField(TEXT("ambiguous"), true);
	Obj->SetStringField(TEXT("selector"), LabelKey);
	Obj->SetStringField(TEXT("selectorValue"), Token);
	Obj->SetStringField(TEXT("matchedBy"), MatchedBy);
	Obj->SetNumberField(TEXT("matchCount"), Candidates.Num());
	TArray<TSharedPtr<FJsonValue>> Rows;
	for (int32 Index = 0; Index < Candidates.Num() && Index < Cap; ++Index)
	{
		Rows.Add(MakeShared<FJsonValueObject>(MCPDescribeActorCandidate(Candidates[Index])));
	}
	Obj->SetArrayField(TEXT("candidates"), Rows);
	if (Candidates.Num() > Cap) Obj->SetBoolField(TEXT("candidatesTruncated"), true);
	return MakeShared<FJsonValueObject>(Obj);
}

/** True when a resolver failure was a refusal to choose rather than a miss.
 *  An action that treats an absent actor as a no-op still has to fail on an
 *  ambiguous one: "already deleted" is the wrong answer when three actors
 *  carry the label and none of them was touched. */
inline bool MCPIsAmbiguousActorError(const TSharedPtr<FJsonValue>& Error)
{
	if (!Error.IsValid() || Error->Type != EJson::Object) return false;
	const TSharedPtr<FJsonObject> Obj = Error->AsObject();
	bool bAmbiguous = false;
	return Obj.IsValid() && Obj->TryGetBoolField(TEXT("ambiguous"), bAmbiguous) && bAmbiguous;
}

// FindActorByLabel, FindActorByLabelOrName, FindActorByLabelOrPath and
// FindActorByLabelNameOrPath each answered a duplicate label with the first
// match the actor iterator produced, which is the silent wrong write #983
// reported, and four spellings of one search is how the rules drift apart.
// Nothing in THIS plugin calls them any more: core goes through
// MCPResolveActor, or MCPCollectActorsByToken where the plural answer is the
// correct one.
//
// They survive as compatibility shims because this is a PUBLIC header shipped
// to plugin authors, and deleting them outright is a breaking change for every
// plugin built against it. PIE_Studio calls FindActorByLabelOrName today and
// broke the moment they went; a third-party plugin nobody here can see would
// have broken the same way, and only after publishing.
//
// They now share the consolidated search, so they cannot drift from it, and
// they keep first-match semantics because that is the contract callers already
// have. New code should use MCPResolveActor, which refuses an ambiguous label
// rather than picking one.

/** Legacy: first actor whose editor label matches. Prefer MCPResolveActor. */
inline AActor* FindActorByLabel(UWorld* World, const FString& Label)
{
	TArray<AActor*> Matches;
	MCPCollectActorsByToken(World, Label, EMCPActorMatch::Label, Matches);
	return Matches.Num() > 0 ? Matches[0] : nullptr;
}

/** Legacy: first actor matching an editor label or internal name.
 *  Prefer MCPResolveActor. */
inline AActor* FindActorByLabelOrName(UWorld* World, const FString& LabelOrName)
{
	TArray<AActor*> Matches;
	MCPCollectActorsByToken(World, LabelOrName, EMCPActorMatch::LabelOrName, Matches);
	return Matches.Num() > 0 ? Matches[0] : nullptr;
}

/** Legacy: an actor by label, or by full object path when the label is empty.
 *  Prefer MCPResolveActor. */
inline AActor* FindActorByLabelOrPath(UWorld* World, const FString& Label, const FString& Path)
{
	if (!Path.IsEmpty())
	{
		if (AActor* ByPath = MCPFindActorByPath(World, Path)) return ByPath;
	}
	if (Label.IsEmpty()) return nullptr;
	return FindActorByLabel(World, Label);
}

/** Legacy: first actor matching a label, internal name or object path.
 *  Prefer MCPResolveActor. */
inline AActor* FindActorByLabelNameOrPath(UWorld* World, const FString& Token)
{
	TArray<AActor*> Matches;
	MCPCollectActorsByToken(World, Token, EMCPActorMatch::LabelNameOrPath, Matches);
	return Matches.Num() > 0 ? Matches[0] : nullptr;
}

/** Build the "no such actor" message for a failed label/name/path lookup.
 *  Names what was searched and offers the labels that contain the token, so a
 *  caller that guessed a label sees the real one instead of a bare miss. */
inline FString MCPDescribeActorLookupMiss(
	UWorld* World,
	const FString& Token,
	const FString& WorldLabel,
	EMCPActorMatch Match = EMCPActorMatch::LabelNameOrPath)
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
	// Name what was actually searched. Claiming a name and path sweep that a
	// label-only action never ran sends a caller looking for a typo in the
	// wrong field.
	const TCHAR* Searched =
		Match == EMCPActorMatch::Label ? TEXT("by editor label")
		: Match == EMCPActorMatch::LabelOrName ? TEXT("by editor label, then by internal object name")
		: TEXT("by editor label, then by internal object name, then by full object path");
	FString Msg = FString::Printf(
		TEXT("Actor '%s' not found in the %s world. Searched every placed actor %s (%d actors). Pass actorPath for an exact object path when a label is ambiguous or absent."),
		*Token, *WorldLabel, Searched, ActorCount);
	if (Near.Num() > 0)
	{
		Msg += FString::Printf(TEXT(" Labels containing that text: [%s]."), *FString::Join(Near, TEXT(", ")));
	}
	Msg += TEXT(" List the real labels with level(get_outliner).");
	return Msg;
}

/** Which parameters carry the actor selector for one action, and how far the
 *  label token is allowed to reach.
 *
 *  Handlers that name their actor something other than 'actorLabel' pass the
 *  pair explicitly. The convention is that the path key is the label key with
 *  its "Label" suffix swapped for "Path" (childLabel / childPath), so a caller
 *  can guess it correctly. */
struct FMCPActorSelector
{
	/** Parameter carrying the editor label (or the label/name/path token). */
	const TCHAR* LabelKey = TEXT("actorLabel");
	/** Parameter carrying the unambiguous full object path. */
	const TCHAR* PathKey = TEXT("actorPath");
	/** A second spelling of the label parameter, for actions that shipped
	 *  with two (get_relative_transform takes 'target' or 'targetLabel').
	 *  Read only when LabelKey is absent. */
	const TCHAR* AltLabelKey = nullptr;
	/** How far LabelKey's value is allowed to reach. */
	EMCPActorMatch Match = EMCPActorMatch::Label;
	/** When false, an absent selector is not an error: the resolver returns
	 *  nullptr with OutError left unset and the caller decides what that
	 *  means (an optional target, or a second selection route). */
	bool bRequired = true;
	/** Names the world in the miss message: "editor", "PIE". */
	const TCHAR* WorldLabel = TEXT("editor");
};

/** Resolve one actor from an already-extracted token. Returns nullptr and
 *  writes OutError on a miss or on ambiguity; the caller returns OutError
 *  unchanged. Used where the token did not come from a parameter of its own
 *  (a list entry, a fixed label). */
inline AActor* MCPResolveActorToken(
	UWorld* World,
	const FString& Token,
	TSharedPtr<FJsonValue>& OutError,
	const FMCPActorSelector& Selector = FMCPActorSelector())
{
	OutError.Reset();
	if (!World)
	{
		OutError = MCPError(TEXT("Editor world not available"));
		return nullptr;
	}
	TArray<AActor*> Matches;
	MCPCollectActorsByToken(World, Token, Selector.Match, Matches);
	if (Matches.Num() == 1) return Matches[0];
	if (Matches.Num() > 1)
	{
		OutError = MCPAmbiguousActorError(
			Token, Selector.LabelKey, Selector.PathKey,
			MCPDescribeActorMatchTier(Token, Matches[0]), Matches);
		return nullptr;
	}
	OutError = MCPError(MCPDescribeActorLookupMiss(World, Token, Selector.WorldLabel, Selector.Match));
	return nullptr;
}

/** THE actor resolver. Reads the unambiguous path selector first, then the
 *  label, and refuses when the label names more than one actor.
 *
 *  A path that names nothing is an error rather than a quiet fall-through to
 *  the label: the path is the precise selector, and demoting a precise miss to
 *  a fuzzy hit is how the wrong actor gets edited in the first place.
 *
 *  Returns nullptr on every failure with OutError carrying the response to
 *  return. When Selector.bRequired is false and neither key was supplied,
 *  returns nullptr with OutError unset. */
inline AActor* MCPResolveActor(
	UWorld* World,
	const TSharedPtr<FJsonObject>& Params,
	TSharedPtr<FJsonValue>& OutError,
	const FMCPActorSelector& Selector = FMCPActorSelector())
{
	OutError.Reset();

	FString Path;
	FString Token;
	if (Params.IsValid())
	{
		Params->TryGetStringField(Selector.PathKey, Path);
		Params->TryGetStringField(Selector.LabelKey, Token);
		if (Token.IsEmpty() && Selector.AltLabelKey)
		{
			Params->TryGetStringField(Selector.AltLabelKey, Token);
		}
	}
	Path.TrimStartAndEndInline();
	Token.TrimStartAndEndInline();

	if (Path.IsEmpty() && Token.IsEmpty())
	{
		if (Selector.bRequired)
		{
			OutError = MCPError(FString::Printf(
				TEXT("Missing required parameter '%s' (or '%s'). Editor labels are not unique, so '%s' is the selector to prefer when you have one."),
				Selector.LabelKey, Selector.PathKey, Selector.PathKey));
		}
		return nullptr;
	}

	if (!World)
	{
		OutError = MCPError(TEXT("Editor world not available"));
		return nullptr;
	}

	if (!Path.IsEmpty())
	{
		if (AActor* ByPath = MCPFindActorByPath(World, Path)) return ByPath;
		OutError = MCPError(FString::Printf(
			TEXT("No actor at '%s' in the %s world. Object paths look like /Game/Maps/Map.Map:PersistentLevel.Actor_0; level(get_outliner) reports the real one for every actor."),
			*Path, Selector.WorldLabel));
		return nullptr;
	}

	return MCPResolveActorToken(World, Token, OutError, Selector);
}

/** Spawn-by-label idempotency check. If World already has an actor with the
 *  given Label, returns a fully-formed "already existed" result the caller
 *  can return directly (or an MCPError when OnConflict == "error"). When
 *  Label is empty or no match exists, returns an unset shared pointer so the
 *  caller proceeds to spawn. Mirrors MCPCheckAssetExists's contract for
 *  in-world actors.
 *
 *  #983: this asks "does this label already name something", so several
 *  matches is an answer rather than a refusal, and refusing here would break
 *  a rerun of a spawn that is meant to be idempotent. But it hands back an
 *  actorPath the caller may then write to, so it must not be an arbitrary
 *  one: the search is the shared, path-sorted one, and when the label names
 *  several actors the result says so and lists them all instead of presenting
 *  one as though it were the only. */
inline TSharedPtr<FJsonValue> MCPCheckActorLabelExists(
	UWorld* World,
	const FString& Label,
	const FString& OnConflict,
	const FString& FriendlyType = TEXT("Actor"))
{
	if (!World || Label.IsEmpty()) return TSharedPtr<FJsonValue>();
	TArray<AActor*> Matches;
	MCPCollectActorsByToken(World, Label, EMCPActorMatch::Label, Matches);
	if (Matches.Num() == 0) return TSharedPtr<FJsonValue>();

	if (OnConflict == TEXT("error"))
	{
		return MCPError(FString::Printf(TEXT("%s '%s' already exists"), *FriendlyType, *Label));
	}

	auto Existing = MCPSuccess();
	MCPSetExisted(Existing);
	Existing->SetStringField(TEXT("actorLabel"), Label);
	Existing->SetStringField(TEXT("actorPath"), Matches[0]->GetPathName());
	Existing->SetNumberField(TEXT("existingCount"), Matches.Num());
	if (Matches.Num() > 1)
	{
		// Deliberately NOT the 'ambiguous' key: that one marks a refusal, and
		// this is a success. A consumer branching on 'ambiguous' must not read
		// an idempotent no-op as a call that did nothing because it refused.
		Existing->SetBoolField(TEXT("labelIsAmbiguous"), true);
		TArray<TSharedPtr<FJsonValue>> Rows;
		for (AActor* Match : Matches)
		{
			Rows.Add(MakeShared<FJsonValueObject>(MCPDescribeActorCandidate(Match)));
		}
		Existing->SetArrayField(TEXT("candidates"), Rows);
		Existing->SetStringField(TEXT("note"), FString::Printf(
			TEXT("%d actors already carry the label '%s'. actorPath names the first by object path; address any of them with the actorPath from candidates."),
			Matches.Num(), *Label));
	}
	return MCPResult(Existing);
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
//
// Every helper below treats an unset Params the same way it treats an empty
// one: a required key is missing, an optional key falls back to its default.
//
// From the socket an unset pointer cannot arrive, because ProcessMessage
// substitutes a fresh FJsonObject on every path. This header is public and
// shipped, though, and its neighbours here - ResolveWorldFromParams,
// MCPResolveActor, ReadPageRequest - all test validity first. A caller that
// reads those and concludes the file guards, then hands an unset pointer to a
// nested dispatch or a test, was one line from a null dereference on the game
// thread. One rule for the whole header is the thing that stops that.

/** Extract a required string parameter.  Returns error JSON on failure, nullptr on success. */
inline TSharedPtr<FJsonValue> RequireString(
	const TSharedPtr<FJsonObject>& Params,
	const TCHAR* Key,
	FString& OutValue)
{
	if (Params.IsValid() && Params->TryGetStringField(Key, OutValue) && !OutValue.IsEmpty())
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
	if (Params.IsValid())
	{
		if (Params->TryGetStringField(Key1, OutValue) && !OutValue.IsEmpty())
			return nullptr;
		if (Params->TryGetStringField(Key2, OutValue) && !OutValue.IsEmpty())
			return nullptr;
	}
	return MCPError(FString::Printf(TEXT("Missing required parameter '%s' (or '%s')"), Key1, Key2));
}

/** Extract an optional string, returning DefaultValue if absent. */
inline FString OptionalString(
	const TSharedPtr<FJsonObject>& Params,
	const TCHAR* Key,
	const FString& DefaultValue = TEXT(""))
{
	FString Value;
	return (Params.IsValid() && Params->TryGetStringField(Key, Value)) ? Value : DefaultValue;
}

/** Extract an optional int32, returning DefaultValue if absent. */
inline int32 OptionalInt(
	const TSharedPtr<FJsonObject>& Params,
	const TCHAR* Key,
	int32 DefaultValue = 0)
{
	int32 Value;
	return (Params.IsValid() && Params->TryGetNumberField(Key, Value)) ? Value : DefaultValue;
}

/** Extract an optional double, returning DefaultValue if absent. */
inline double OptionalNumber(
	const TSharedPtr<FJsonObject>& Params,
	const TCHAR* Key,
	double DefaultValue = 0.0)
{
	double Value;
	return (Params.IsValid() && Params->TryGetNumberField(Key, Value)) ? Value : DefaultValue;
}

/** Extract an optional bool, returning DefaultValue if absent. */
inline bool OptionalBool(
	const TSharedPtr<FJsonObject>& Params,
	const TCHAR* Key,
	bool DefaultValue = false)
{
	bool Value;
	return (Params.IsValid() && Params->TryGetBoolField(Key, Value)) ? Value : DefaultValue;
}

/**
 * Why an actor filter matched nothing, in terms of what it WOULD have matched.
 *
 * Level actions do not agree on filter semantics and their parameter names do
 * not warn you: get_outliner's nameFilter is a case-insensitive substring over
 * the label OR the internal name, while delete_actors' labelPrefix is a
 * case-sensitive prefix over the label only. The same string selects different
 * sets, and the losing call returns success with matched:0, which a caller
 * reasonably reads as "nothing to do".
 *
 * So a zero match reports the counts under the OTHER semantics rather than
 * leaving the caller to discover them. Returns an unset pointer when the
 * string would have matched nothing under any of them, because then a zero
 * really does mean zero.
 */
inline TSharedPtr<FJsonObject> MCPDescribeZeroActorMatch(UWorld* World, const FString& Needle)
{
	if (!World || Needle.IsEmpty())
	{
		return nullptr;
	}

	int32 LabelContains = 0;
	int32 NameContains = 0;
	int32 PrefixIgnoringCase = 0;
	TArray<TSharedPtr<FJsonValue>> Samples;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor) continue;
		const FString Label = Actor->GetActorLabel();
		const FString Name = Actor->GetName();
		const bool bLabelContains = Label.Contains(Needle, ESearchCase::IgnoreCase);
		const bool bNameContains = Name.Contains(Needle, ESearchCase::IgnoreCase);
		const bool bPrefix = Label.StartsWith(Needle, ESearchCase::IgnoreCase);
		if (bLabelContains) ++LabelContains;
		if (bNameContains) ++NameContains;
		if (bPrefix) ++PrefixIgnoringCase;
		if ((bLabelContains || bNameContains) && Samples.Num() < 5)
		{
			Samples.Add(MakeShared<FJsonValueString>(
				FString::Printf(TEXT("%s (internal name %s)"), *Label, *Name)));
		}
	}

	if (LabelContains == 0 && NameContains == 0 && PrefixIgnoringCase == 0)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> Hint = MakeShared<FJsonObject>();
	Hint->SetStringField(TEXT("filter"), Needle);
	Hint->SetNumberField(TEXT("actorsWhoseLabelContainsIt"), LabelContains);
	Hint->SetNumberField(TEXT("actorsWhoseInternalNameContainsIt"), NameContains);
	Hint->SetNumberField(TEXT("actorsWhoseLabelStartsWithItIgnoringCase"), PrefixIgnoringCase);
	Hint->SetArrayField(TEXT("samples"), Samples);
	Hint->SetStringField(TEXT("note"),
		TEXT("labelPrefix is a case-sensitive PREFIX over the EDITOR LABEL. level(get_outliner)'s nameFilter is a case-insensitive SUBSTRING over the label OR the internal name, so the same string selects a different set there. Use labelContains for a substring over the label, or nameContains for one over the internal name."));
	return Hint;
}

/**
 * Note that an actor enumeration only saw the actors that are loaded.
 *
 * Every actor query in this plugin iterates the world, and on a World
 * Partition map that is a real answer but not the whole answer. Saying so
 * turns a silently wrong zero into an actionable one.
 */
inline void MCPNoteLoadedOnlyEnumeration(UWorld* World, TSharedPtr<FJsonObject> Result)
{
	if (!World || !Result.IsValid() || !World->IsPartitionedWorld())
	{
		return;
	}
	Result->SetBoolField(TEXT("partitionedWorld"), true);
	Result->SetStringField(TEXT("enumerationNote"),
		TEXT("This is a World Partition map and only LOADED actors were enumerated. An actor whose cell is not streamed in is invisible to every world query, including this one. Use level(list_actor_descs) to see the unloaded ones and level(load_actor_descs) to pin them first."));
}

/** Render a TArray<FString> as a JSON string array. The inverse of
 *  JsonArrayToStringList, shared so batch handlers that report label lists do
 *  not each define their own file-local copy (unity build: two anonymous
 *  namespaces sharing a blob merge, and the second definition is C2084). */
inline TArray<TSharedPtr<FJsonValue>> MCPStringListToJson(const TArray<FString>& Values)
{
	TArray<TSharedPtr<FJsonValue>> Out;
	Out.Reserve(Values.Num());
	for (const FString& Value : Values)
	{
		Out.Add(MakeShared<FJsonValueString>(Value));
	}
	return Out;
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
	if (!Params.IsValid() || !Params->TryGetObjectField(Key, Obj) || !Obj || !(*Obj).IsValid()) return DefaultValue;
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
	if (!Params.IsValid() || !Params->TryGetObjectField(Key, Obj) || !Obj || !(*Obj).IsValid())
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
	if (!Params.IsValid() || !Params->TryGetObjectField(Key, Obj) || !Obj || !(*Obj).IsValid()) return DefaultValue;
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
	if (!Params.IsValid() || !Params->TryGetObjectField(Key, Obj) || !Obj || !(*Obj).IsValid())
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
	if (!Params.IsValid() || !Params->TryGetObjectField(Key, Obj) || !Obj || !(*Obj).IsValid()) return DefaultValue;
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
	if (!Params.IsValid() || !Params->TryGetObjectField(Key, Obj) || !Obj || !(*Obj).IsValid()) return FTransform::Identity;
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

				// A /Script path names a module, and modules move between them:
				// UPhysicalMaterial is /Script/PhysicsCore.PhysicalMaterial, not
				// /Script/Engine.PhysicalMaterial, which is the spelling older
				// documentation uses and the one this resolver's own "not found"
				// text recommends. The leaf name is added LAST, so an exact path
				// still wins and this is only reached when no spelling of the
				// path matched anything at all.
				if (PackagePart.StartsWith(TEXT("/Script/")))
				{
					Add(ObjectPart);
					const FString StrippedLeaf = StripPrefix(ObjectPart);
					if (StrippedLeaf != ObjectPart) Add(StrippedLeaf);
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

/** Load an asset of a known type by path. Returns nullptr when the path names
 *  nothing, and also when it names something of another type.
 *
 *  #957/#913: this used to run its own two-step resolution, one step short of
 *  the one asset(read) uses, so a short package path for an asset that was not
 *  loaded yet answered nullptr here and resolved fine there. It now defers to
 *  MCPLoadAssetObject so there is exactly one answer to "what does this path
 *  name" in the whole plugin. */
template <typename T>
T* LoadAssetByPath(const FString& AssetPath)
{
	return Cast<T>(MCPLoadAssetObject(AssetPath));
}

/** The answer for a typed load that came back empty: names the class that was
 *  found when the path resolved to the wrong thing, and falls through to the
 *  full path diagnostic when it resolved to nothing. */
inline TSharedPtr<FJsonValue> MCPAssetLoadError(const FString& AssetPath, const TCHAR* ExpectedType)
{
	if (UObject* Found = MCPLoadAssetObject(AssetPath))
	{
		return MCPAssetWrongTypeError(AssetPath, Found, ExpectedType);
	}
	return MCPAssetNotFoundError(AssetPath);
}

/** Load an asset or return an error response.  Assigns to OutVar. */
#define REQUIRE_ASSET(Type, OutVar, AssetPath) \
	Type* OutVar = LoadAssetByPath<Type>(AssetPath); \
	if (!OutVar) return MCPAssetLoadError(AssetPath, TEXT(#Type));

/** Export a property's value as text, honouring C-style fixed arrays.
 *
 *  A UPROPERTY declared as `int32 Foo[3]` is ONE FProperty with ArrayDim == 3,
 *  not three properties. ExportTextItem_Direct exports a single element, so a
 *  caller that passes ContainerPtrToValuePtr<void>(Container) with no index
 *  gets element 0 and nothing else, and the value reads as a plain scalar.
 *
 *  That is how #927 hid two thirds of RecastNavMesh's NavMeshResolutionParams:
 *  lint-prose-allow: tier  RecastNavMesh's own name for its three generation tiers
 *  the Low tier was reported as if it were the whole property while the engine
 *  was generating from Default and High, so a navmesh diagnosis was performed
 *  against numbers the engine was not using.
 *
 *  Returns a JSON string for a normal property, and a JSON array of one string
 *  per element for a fixed array, so a caller can tell the two apart. */
inline TSharedPtr<FJsonValue> MCPExportPropertyValue(const FProperty* Prop, const void* Container)
{
	if (!Prop || !Container) return MakeShared<FJsonValueString>(FString());

	auto ExportOne = [Prop, Container](int32 Index) -> FString
	{
		FString Text;
		Prop->ExportTextItem_Direct(
			Text, Prop->ContainerPtrToValuePtr<void>(Container, Index), nullptr, nullptr, PPF_None);
		return Text;
	};

	if (Prop->ArrayDim <= 1)
	{
		return MakeShared<FJsonValueString>(ExportOne(0));
	}

	TArray<TSharedPtr<FJsonValue>> Elements;
	Elements.Reserve(Prop->ArrayDim);
	for (int32 Index = 0; Index < Prop->ArrayDim; ++Index)
	{
		Elements.Add(MakeShared<FJsonValueString>(ExportOne(Index)));
	}
	return MakeShared<FJsonValueArray>(Elements);
}

/** True when a property is a C-style fixed array, so callers that must emit a
 *  scalar can say the value was truncated rather than silently truncating. */
inline bool MCPPropertyIsFixedArray(const FProperty* Prop)
{
	return Prop != nullptr && Prop->ArrayDim > 1;
}

// ── Package save ─────────────────────────────────────────────────────────────

/** True when the package is a map package, i.e. one whose on-disk form is a
 *  ".umap" rather than a ".uasset".
 *
 *  #949: writing a world package with the asset extension does not fail. It
 *  creates a second file that claims the same long package name, so the level
 *  then exists twice on disk and the two copies diverge silently as different
 *  save paths write different files. Unreal resolves the ".uasset" first, so
 *  the stale fork is the one that wins.
 *
 *  ContainsMap is the package flag Unreal sets on world packages and is the
 *  same test editor(save_dirty) branches on. FindWorldInPackage is the backstop
 *  for a world built in memory whose flag has not been stamped yet. One-file-
 *  per-actor packages under __ExternalActors__ hold an AActor and no UWorld, so
 *  both tests say false and they keep the ".uasset" extension OFPA expects. */
inline bool IsMapPackage(UPackage* Package)
{
	if (!Package) return false;
	return Package->ContainsMap() || UWorld::FindWorldInPackage(Package) != nullptr;
}

/** On-disk file extension for a package, dot included. ".umap" for world
 *  packages, ".uasset" for everything else. */
inline const FString& PackageFileExtension(UPackage* Package)
{
	return IsMapPackage(Package)
		? FPackageName::GetMapPackageExtension()
		: FPackageName::GetAssetPackageExtension();
}

/** Resolve the on-disk filename a package must be written to, extension
 *  included. Returns false when the package name has no mounted root, which
 *  keeps callers off FPackageName::LongPackageNameToFilename - that one is
 *  fatal rather than recoverable when the name does not resolve. */
inline bool ResolvePackageFileName(UPackage* Package, FString& OutFileName)
{
	if (!Package) return false;
	return FPackageName::TryConvertLongPackageNameToFilename(
		Package->GetName(), OutFileName, PackageFileExtension(Package));
}

/** Why an asset's package cannot be written, answered BEFORE the save is
 *  attempted. Returns true when the write is blocked, with OutReason carrying
 *  the sentence to hand the caller.
 *
 *  #932: blueprint(reparent) saved as part of the operation, and a read-only
 *  .uasset (a file never checked out of source control) turned the failed save
 *  into a FATAL engine error that took the whole editor process down. The asset
 *  was undamaged and the call replayed cleanly after a checkout, but no handler
 *  may answer a routine, foreseeable condition with a crash.
 *
 *  Asking the file system first is what turns that into an ordinary failure,
 *  and it is the same order asset(set_property) already uses (#931). It lives
 *  here, as one function, because "can this package be written" has to have a
 *  single answer: the protected-mount guardrail had four copies once and two of
 *  them enforced a weaker rule than the others. */
inline bool MCPPackageWriteBlocked(UObject* Asset, FString& OutReason)
{
	OutReason.Reset();

	UPackage* Package = Asset ? Asset->GetOutermost() : nullptr;
	if (!Package)
	{
		OutReason = TEXT("The asset has no package, so there is nothing to write.");
		return true;
	}

	const FString PackageName = Package->GetName();
	if (MCPIsProtectedAssetPath(PackageName))
	{
		OutReason = FString::Printf(
			TEXT("'%s' is on a protected mount, which the bridge never writes to."),
			*PackageName);
		return true;
	}

	FString PackageFileName;
	if (!ResolvePackageFileName(Package, PackageFileName))
	{
		OutReason = FString::Printf(
			TEXT("'%s' has no mounted content root, so there is no file to write it to."),
			*PackageName);
		return true;
	}

	// Only a file that already exists can be read-only. A package saved for the
	// first time has nothing on disk to check, and asking about a missing file
	// answers "not read-only", which is the right answer for a create.
	if (IFileManager::Get().FileExists(*PackageFileName)
		&& IFileManager::Get().IsReadOnly(*PackageFileName))
	{
		OutReason = FString::Printf(
			TEXT("'%s' is read-only on disk. Check it out of source control or clear the read-only flag, then retry."),
			*PackageFileName);
		return true;
	}

	return false;
}

/** Mark the asset's package dirty and save it to disk. Used by every create/
 *  mutate handler that wants changes persisted across editor restarts.
 *  No-op if Asset or its package is null. Returns true on successful save.
 *
 *  Refuses before the engine is asked to write a file it cannot open (#932),
 *  so the worst outcome of a read-only or protected package is a false return
 *  rather than a fatal error. Callers that want the sentence explaining the
 *  false use SaveAssetPackageChecked. */
inline bool SaveAssetPackage(UObject* Asset)
{
	if (!Asset) return false;
	UPackage* Package = Asset->GetOutermost();
	if (!Package) return false;
	Package->MarkPackageDirty();

	FString BlockedReason;
	if (MCPPackageWriteBlocked(Asset, BlockedReason)) return false;

	// The extension has to follow the package, not the call site. Any handler
	// that mutates an actor or component in the open level reaches this with a
	// world package as the outermost (#949).
	FString PackageFileName;
	if (!ResolvePackageFileName(Package, PackageFileName)) return false;
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Standalone;
	return UPackage::SavePackage(Package, nullptr, *PackageFileName, SaveArgs);
}

/** SaveAssetPackage, with the reason when it did not write. A handler that
 *  reports its own persistence uses this so a refusal reads as a named cause
 *  rather than a bare false. */
inline bool SaveAssetPackageChecked(UObject* Asset, FString& OutReason)
{
	if (MCPPackageWriteBlocked(Asset, OutReason)) return false;
	if (SaveAssetPackage(Asset)) return true;

	UPackage* Package = Asset ? Asset->GetOutermost() : nullptr;
	OutReason = FString::Printf(
		TEXT("The editor refused to write '%s'. The output log carries the reason."),
		Package ? *Package->GetName() : TEXT("(no package)"));
	return false;
}

/** The refusal an action that saves as a side effect returns when the package
 *  cannot be written. Returns nullptr when the write may go ahead, so a handler
 *  reads as `if (auto Blocked = MCPAssetWriteBlockedError(...)) return Blocked;`
 *  placed BEFORE the first mutation.
 *
 *  Operation names what the caller asked for, in the imperative, so the message
 *  reads "Cannot reparent this Blueprint: ...". */
inline TSharedPtr<FJsonValue> MCPAssetWriteBlockedError(
	UObject* Asset,
	const FString& AssetPath,
	const TCHAR* Operation)
{
	FString Reason;
	if (!MCPPackageWriteBlocked(Asset, Reason)) return nullptr;

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), false);
	Obj->SetStringField(TEXT("error"), FString::Printf(
		TEXT("Cannot %s: %s Nothing was changed."), Operation, *Reason));
	Obj->SetStringField(TEXT("assetPath"), AssetPath);
	Obj->SetStringField(TEXT("path"), AssetPath);
	Obj->SetStringField(TEXT("reason"), TEXT("package_not_writable"));
	Obj->SetBoolField(TEXT("saved"), false);
	if (UPackage* Package = Asset ? Asset->GetOutermost() : nullptr)
	{
		Obj->SetStringField(TEXT("packageName"), Package->GetName());
		FString PackageFileName;
		if (ResolvePackageFileName(Package, PackageFileName))
		{
			Obj->SetStringField(TEXT("packageFile"), PackageFileName);
		}
	}
	return MakeShared<FJsonValueObject>(Obj);
}

/** Record on a result whether the side-effect save reached disk, and why not
 *  when it did not. A save that did not happen is a failure, not a success with
 *  a footnote: the caller's next read comes off the in-memory object and looks
 *  correct right up until the editor restarts (#931). */
inline void MCPNoteSaveOutcome(
	const TSharedPtr<FJsonObject>& Result,
	const FString& AssetPath,
	bool bSaved,
	const FString& Reason)
{
	if (!Result.IsValid()) return;
	Result->SetBoolField(TEXT("saved"), bSaved);
	if (bSaved) return;

	Result->SetBoolField(TEXT("success"), false);
	Result->SetStringField(TEXT("saveError"), Reason);
	Result->SetStringField(TEXT("error"), FString::Printf(
		TEXT("The change was applied in memory but '%s' was not written: %s"),
		*AssetPath, *Reason));
}

// ── GC root RAII ─────────────────────────────────────────────────────────────

/** RAII: root a UObject on construction, unroot on scope exit. Prevents the
 *  AddToRoot/RemoveFromRoot pairs from leaking when an early return (validation
 *  error, import failure) sneaks into the middle of the pair.
 *
 *  Clears only what it set. The root set is a single flag, not a reference
 *  count: UObjectBaseUtility::AddToRoot is SetRootSet() and RemoveFromRoot is
 *  ClearRootSet(), each one atomic set or clear of
 *  EInternalObjectFlags::RootSet. So a scope that unconditionally cleared on
 *  exit would unroot an object some other party had rooted for its own
 *  reasons, and two nested scopes over the same object would leave it
 *  collectable the moment the INNER one exited while the outer one still
 *  believed it was protected. Recording whether this scope was the one that
 *  set the flag makes both cases correct: the inner scope is a no-op and the
 *  outer keeps its guarantee, and an object rooted elsewhere is left alone.
 *
 *  What it does not promise: if another party clears the flag during the
 *  scope, the object is unrooted from that moment. A single flag cannot
 *  express two owners, and no version of this class ever could. */
class FGCRootScope
{
public:
	explicit FGCRootScope(UObject* InObject) : Object(InObject)
	{
		if (Object && !Object->IsRooted())
		{
			Object->AddToRoot();
			bRootedHere = true;
		}
	}
	~FGCRootScope()
	{
		if (bRootedHere && Object && Object->IsRooted()) Object->RemoveFromRoot();
	}
	FGCRootScope(const FGCRootScope&) = delete;
	FGCRootScope& operator=(const FGCRootScope&) = delete;
private:
	UObject* Object = nullptr;
	bool bRootedHere = false;
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

// ── Object graph ─────────────────────────────────────────────────────────────

/** Objects an outer owns directly, skipping nested subobjects and anything
 *  already garbage. Spelled the way each engine wants it: 5.8 deprecated the
 *  bool form of GetObjectsWithOuter in favour of EGetObjectsFlags, and the enum
 *  does not exist before it. */
inline void MCPGetDirectSubobjects(const UObjectBase* Outer, TArray<UObject*>& OutObjects)
{
	if (!Outer)
	{
		return;
	}
#if UE_MCP_HAS_5_8_API
	GetObjectsWithOuter(Outer, OutObjects, EGetObjectsFlags::None,
		RF_NoFlags, EInternalObjectFlags::Garbage);
#else
	GetObjectsWithOuter(Outer, OutObjects, /*bIncludeNestedObjects*/ false,
		RF_NoFlags, EInternalObjectFlags::Garbage);
#endif
}

/** Every object under an outer, descending through nested subobjects, skipping
 *  anything already garbage. The twin of MCPGetDirectSubobjects for the callers
 *  that need the whole tree - a Control Rig's RigVM models, for instance, hang
 *  off collapsed nodes and the function library rather than off the blueprint
 *  directly, so a direct-only walk finds none of them.
 *
 *  Both spellings live here and nowhere else. This module is a unity build, so
 *  a file-local copy in a second .cpp is a C2084 redefinition the moment the
 *  adaptive-unity working set puts the two files in one blob. */
inline void MCPGetNestedSubobjects(const UObjectBase* Outer, TArray<UObject*>& OutObjects)
{
	if (!Outer)
	{
		return;
	}
#if UE_MCP_HAS_5_8_API
	GetObjectsWithOuter(Outer, OutObjects, EGetObjectsFlags::IncludeNestedObjects,
		RF_NoFlags, EInternalObjectFlags::Garbage);
#else
	GetObjectsWithOuter(Outer, OutObjects, /*bIncludeNestedObjects*/ true,
		RF_NoFlags, EInternalObjectFlags::Garbage);
#endif
}

// ── Component bounds ─────────────────────────────────────────────────────────

/** A scene component's cached world-space bounds, spelled the way each engine
 *  wants it. 5.8 added USceneComponent::GetBounds(), an inline getter that
 *  returns a const reference to the public Bounds member; 5.7 and earlier offer
 *  the member alone. Both branches read the same field, so every supported
 *  engine returns the same value at the same freshness - the accessor is a
 *  rename, and UpdateBounds is still what refreshes what either one reads.
 *
 *  Bounds is public on 5.8 too, so the gate decides which spelling is used
 *  rather than which one compiles. Preferring the accessor there is what keeps
 *  these call sites correct on the engine that eventually makes the member
 *  private, which is the reason Epic added the getter.
 *
 *  Both spellings live here and nowhere else. This module is a unity build, so
 *  a file-local copy in a second .cpp is a C2084 redefinition the moment the
 *  adaptive-unity working set puts the two files in one blob. */
inline const FBoxSphereBounds& MCPComponentWorldBounds(const USceneComponent& Component)
{
#if UE_MCP_HAS_5_8_API
	return Component.GetBounds();
#else
	return Component.Bounds;
#endif
}

// ── Widget variable GUID metadata (#728, #799) ───────────────────────────────
//
// A UWidgetBlueprint keeps WidgetVariableNameToGuidMap so external references
// to a widget variable survive a rename of that widget. The
// WidgetBlueprintCompiler validates the map on every compile and raises
//
//   "Widget [X] was added but did not get a GUID"
//
// for any widget variable it is about to generate that owns no entry. That is
// an engine ensure, so the editor survives it, but the asset is left in a state
// the UMG editor does not consider valid and every later compile repeats it.
//
// The set the compiler validates is the blueprint's SOURCE widgets, which
// UBaseWidgetBlueprint gathers without walking the hierarchy. Its own header
// says the accessor "avoids calling virtual functions on instances and is
// therefore safe to use throughout compilation", so the set is every UWidget
// the WidgetTree still OWNS, not the widgets a walk down from RootWidget
// reaches. The two part company the moment a widget is detached from its parent
// without being moved out of the tree: the walk stops seeing it while the
// compiler still generates a variable for it. Bookkeeping driven by the walk
// alone therefore deletes the GUID of a widget that still needs one, which is
// how the ensure fires on a widget that was just removed. The enumeration below
// is by outer for that reason, with the walk folded in as a union rather than
// used on its own.
//
// The map is editor-only data that landed in 5.5, so its presence is detected
// at compile time here rather than tracked with a hand-maintained version
// window.
namespace MCPWidgetGuidMap
{
	template <typename T, typename = void>
	struct THasMap : std::false_type {};

	template <typename T>
	struct THasMap<T, std::void_t<decltype(T::WidgetVariableNameToGuidMap)>> : std::true_type {};

	/** What a sync changed, and what it could not fix. */
	struct FSyncReport
	{
		/** False when this engine has no WidgetVariableNameToGuidMap at all. */
		bool bSupported = false;
		/** True once CompileChecked has actually run the compile. */
		bool bCompiled = false;
		int32 Added = 0;
		int32 Pruned = 0;
		/** Widgets moved out of the tree because nothing reached them. */
		int32 Evicted = 0;
		/** Entries whose stored GUID was not a valid GUID and was replaced. */
		int32 Repaired = 0;
		/** Variables whose map entry the compiler will refuse, after the sync
		 *  has done everything it can. A non-empty list means the next compile
		 *  of this asset ensures. */
		TArray<FName> Unusable;
		/** What is actually wrong, one clause per defect, for the message. */
		TArray<FString> Defects;

		bool IsClean() const { return Unusable.Num() == 0; }

		FString DefectList() const
		{
			return Defects.Num() ? FString::Join(Defects, TEXT("; ")) : FString(TEXT("(none)"));
		}

		FString UnusableList() const
		{
			TArray<FString> Names;
			Names.Reserve(Unusable.Num());
			for (const FName& Name : Unusable)
			{
				Names.Add(Name.ToString());
			}
			return Names.Num() ? FString::Join(Names, TEXT(", ")) : FString(TEXT("(none)"));
		}
	};

	/** Every name the compiler will generate a variable for: each widget the
	 *  WidgetTree owns (reachable from the root or not), plus each animation. */
	template <typename TWidgetBP>
	TSet<FName> RequiredNames(TWidgetBP* WidgetBP)
	{
		TSet<FName> Names;
		if (!WidgetBP)
		{
			return Names;
		}

		// UWidgetTree and UWidget named without naming them. This header is
		// included by every handler and must not drag the UMG headers into all
		// of them, and a type spelled off the template parameter stays
		// dependent, so the lookup happens where UMG is included.
		using FTreeType = std::remove_reference_t<decltype(*WidgetBP->WidgetTree)>;
		using FWidgetType = std::remove_reference_t<decltype(*WidgetBP->WidgetTree->RootWidget)>;

		if (FTreeType* Tree = WidgetBP->WidgetTree)
		{
			TArray<UObject*> Owned;
			MCPGetDirectSubobjects(Tree, Owned);
			for (UObject* Object : Owned)
			{
				if (FWidgetType* Widget = Cast<FWidgetType>(Object))
				{
					Names.Add(Widget->GetFName());
				}
			}

			Tree->ForEachWidget([&Names](FWidgetType* Widget)
			{
				if (Widget)
				{
					Names.Add(Widget->GetFName());
				}
			});
		}

		// TObjectPtr::GetFName reads the name off the object handle, so it does
		// not need UWidgetAnimation defined. WidgetBlueprint.h only forward
		// declares that class, and dereferencing the pointer here would force
		// every translation unit that includes this header to pull in UMG.
		for (const auto& Animation : WidgetBP->Animations)
		{
			const FName AnimationName = Animation.GetFName();
			if (!AnimationName.IsNone())
			{
				Names.Add(AnimationName);
			}
		}
		return Names;
	}

	/**
	 * Make the map say exactly what the compiler is about to check: give every
	 * required name the GUID it lacks, and drop every entry no live name backs.
	 * Call immediately before a compile, and again after, because the compile
	 * itself can rename a widget whose name collided.
	 */
	template <typename TWidgetBP>
	FSyncReport Sync(TWidgetBP* WidgetBP)
	{
		FSyncReport Report;
		if constexpr (THasMap<TWidgetBP>::value)
		{
			if (!WidgetBP)
			{
				return Report;
			}
			Report.bSupported = true;

			const TSet<FName> Required = RequiredNames(WidgetBP);
			for (const FName& Name : Required)
			{
				if (Name.IsNone())
				{
					continue;
				}
				if (!WidgetBP->WidgetVariableNameToGuidMap.Contains(Name))
				{
					WidgetBP->WidgetVariableNameToGuidMap.Add(Name, FGuid::NewGuid());
					++Report.Added;
				}
			}

			// Pruning is measured against a wider set: an entry naming one of
			// the blueprint's own declared variables is not proof of drift, so
			// it is left alone rather than dropped and re-added.
			TSet<FName> Known = Required;
			for (const auto& Variable : WidgetBP->NewVariables)
			{
				Known.Add(Variable.VarName);
			}

			TArray<FName> Stale;
			for (const auto& Entry : WidgetBP->WidgetVariableNameToGuidMap)
			{
				if (!Known.Contains(Entry.Key))
				{
					Stale.Add(Entry.Key);
				}
			}
			for (const FName& Name : Stale)
			{
				WidgetBP->WidgetVariableNameToGuidMap.Remove(Name);
			}
			Report.Pruned = Stale.Num();

			// What the compiler actually checks, and which half of it the fill
			// above cannot cover.
			//
			// FWidgetBlueprintCompilerContext::ValidateAndFixUpVariableGuids
			// raises ensureAlways on three conditions:
			//   1. a source widget or animation with no entry;
			//   2. an entry whose stored GUID is not valid;
			//   3. two entries carrying the same GUID.
			//
			// Condition 1 cannot survive the fill loop above, which adds a
			// fresh GUID for every required name and prunes nothing from that
			// set, so re-testing it here would be a guard that can never fire.
			// The two the fill loop says nothing about are the ones worth
			// checking, because both arrive with the asset rather than with
			// the mutation:
			//
			// Condition 2 is repairable. An invalid GUID references nothing, so
			// replacing it breaks nothing; the engine replaces it too, after
			// ensuring.
			//
			// Condition 3 is not. The GUID is how external assets refer to a
			// variable across a rename, so reassigning one of a colliding pair
			// silently breaks whichever references chose that one. The engine
			// does not repair it either - its own message tells the user to
			// delete and recreate one of the two variables - so this is the
			// state in which the map cannot be made to match what the compiler
			// checks, and it is what stops the compile.
			TMap<FGuid, FName> GuidOwners;
			for (auto& Entry : WidgetBP->WidgetVariableNameToGuidMap)
			{
				if (!Entry.Value.IsValid())
				{
					Entry.Value = FGuid::NewGuid();
					++Report.Repaired;
				}
				if (const FName* Owner = GuidOwners.Find(Entry.Value))
				{
					Report.Unusable.Add(Entry.Key);
					Report.Defects.Add(FString::Printf(
						TEXT("'%s' carries the same variable GUID as '%s'"),
						*Entry.Key.ToString(), *Owner->ToString()));
					continue;
				}
				GuidOwners.Add(Entry.Value, Entry.Key);
			}
		}
		return Report;
	}

	/**
	 * Move every widget the tree no longer reaches out of the WidgetTree and
	 * into the transient package, and report the ones that would not move.
	 *
	 * Detaching a widget from its parent panel does not end its membership of
	 * the blueprint: ownership is what makes the compiler generate a variable
	 * for it, so a detached widget still outered to the tree is still compiled,
	 * still needs a GUID, and still holds its name against a later add. Only a
	 * handler whose contract is removal may call this, since it is destructive
	 * by design.
	 */
	template <typename TWidgetBP>
	TArray<FName> EvictUnreachableWidgets(TWidgetBP* WidgetBP, int32& OutEvicted)
	{
		OutEvicted = 0;
		TArray<FName> Stuck;
		if (!WidgetBP || !WidgetBP->WidgetTree)
		{
			return Stuck;
		}

		using FTreeType = std::remove_reference_t<decltype(*WidgetBP->WidgetTree)>;
		using FWidgetType = std::remove_reference_t<decltype(*WidgetBP->WidgetTree->RootWidget)>;

		FTreeType* Tree = WidgetBP->WidgetTree;

		TSet<const UObject*> Reachable;
		Tree->ForEachWidget([&Reachable](FWidgetType* Widget)
		{
			if (Widget)
			{
				Reachable.Add(Widget);
			}
		});
		// Named slot content hangs off the tree rather than off the root, so it
		// is reachable in every sense that matters even where the root walk
		// never visits it. Evicting it would delete authored content.
		for (const auto& Binding : Tree->NamedSlotBindings)
		{
			if (Binding.Value)
			{
				Reachable.Add(Binding.Value);
				FTreeType::ForWidgetAndChildren(Binding.Value, [&Reachable](FWidgetType* Widget)
				{
					if (Widget)
					{
						Reachable.Add(Widget);
					}
				});
			}
		}

		TArray<UObject*> Owned;
		MCPGetDirectSubobjects(Tree, Owned);
		for (UObject* Object : Owned)
		{
			FWidgetType* Widget = Cast<FWidgetType>(Object);
			if (!Widget || Reachable.Contains(Widget))
			{
				continue;
			}

			// A unique name rather than the one it has: the transient package is
			// shared, and the same widget name is evicted from the same asset
			// on every run of a script that adds and removes it.
			const FName EvictedName = Widget->GetFName();
			const FName ParkedName = MakeUniqueObjectName(
				GetTransientPackage(), Widget->GetClass(), EvictedName);
			Widget->Rename(*ParkedName.ToString(), GetTransientPackage(),
				REN_DontCreateRedirectors | REN_NonTransactional);
			if (Widget->GetOuter() == Tree)
			{
				Stuck.Add(EvictedName);
			}
			else
			{
				++OutEvicted;
			}
		}
		return Stuck;
	}

	/**
	 * Sync, compile, sync again.
	 *
	 * When the map cannot be made to match what the compiler checks - which in
	 * practice means two variables carrying one GUID, the one defect neither
	 * the sync nor the engine can repair without picking which set of external
	 * references to break - the compile does NOT run and bCompiled stays false.
	 * The ensure inside the compiler is the thing this exists to prevent, and a
	 * handler that compiled anyway would be reporting success on an asset it
	 * had just broken.
	 */
	template <typename TWidgetBP>
	FSyncReport CompileChecked(TWidgetBP* WidgetBP)
	{
		FSyncReport Report = Sync(WidgetBP);
		if (!Report.IsClean())
		{
			return Report;
		}

		FKismetEditorUtilities::CompileBlueprint(WidgetBP);

		const FSyncReport After = Sync(WidgetBP);
		Report.Added += After.Added;
		Report.Pruned += After.Pruned;
		Report.Repaired += After.Repaired;
		Report.Unusable = After.Unusable;
		Report.Defects = After.Defects;
		Report.bCompiled = true;
		return Report;
	}

	/** The error a handler returns when CompileChecked refused to compile. */
	inline TSharedPtr<FJsonValue> BlockedError(const FString& AssetPath, const FSyncReport& Report)
	{
		return MCPError(FString::Printf(
			TEXT("Refusing to compile '%s': %s. Two widget variables cannot share a GUID - the UMG ")
			TEXT("compiler reports it and external assets that reference either one by GUID resolve to ")
			TEXT("whichever it reaches first, so the bridge will not reassign one behind your back. ")
			TEXT("Nothing was compiled or saved. Open the asset in the UMG editor and delete and ")
			TEXT("recreate one of %s."),
			*AssetPath, *Report.DefectList(), *Report.UnusableList()));
	}
}

/** Stamp GUID bookkeeping onto a widget mutation result, and withdraw the
 *  success claim when a variable the compiler generates was left with a map
 *  entry the compiler will refuse. Silence there is what turns into an editor
 *  ensure the next time anything compiles the asset. */
inline void MCPSetWidgetGuidOutcome(
	const TSharedPtr<FJsonObject>& Result,
	const MCPWidgetGuidMap::FSyncReport& Report,
	const FString& AssetPath)
{
	if (!Result.IsValid())
	{
		return;
	}

	// Only when there was something to report: a property write that changed no
	// metadata should not carry three zeroes describing the metadata it left
	// alone.
	if (Report.Pruned > 0)
	{
		Result->SetNumberField(TEXT("prunedGuidEntries"), Report.Pruned);
	}
	if (Report.Added > 0)
	{
		Result->SetNumberField(TEXT("widgetGuidEntriesAdded"), Report.Added);
	}
	if (Report.Evicted > 0)
	{
		Result->SetNumberField(TEXT("evictedWidgets"), Report.Evicted);
	}
	if (Report.Repaired > 0)
	{
		Result->SetNumberField(TEXT("repairedGuidEntries"), Report.Repaired);
	}
	if (Report.IsClean())
	{
		return;
	}

	TArray<TSharedPtr<FJsonValue>> Missing;
	for (const FName& Name : Report.Unusable)
	{
		Missing.Add(MakeShared<FJsonValueString>(Name.ToString()));
	}
	Result->SetArrayField(TEXT("widgetsWithUnusableGuid"), Missing);
	Result->SetBoolField(TEXT("success"), false);
	Result->SetStringField(TEXT("error"), FString::Printf(
		TEXT("'%s' was changed, but its widget variable GUID bookkeeping is not in a state the UMG ")
		TEXT("compiler accepts: %s. The bridge does not reassign a shared GUID on its own, because ")
		TEXT("external assets reference these variables by GUID across renames. This asset reports the ")
		TEXT("failure on every compile until one of %s is deleted and recreated in the UMG editor."),
		*AssetPath, *Report.DefectList(), *Report.UnusableList()));
}
