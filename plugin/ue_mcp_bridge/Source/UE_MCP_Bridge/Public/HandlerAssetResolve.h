#pragma once

// Resolving an asset path to a loaded object, the diagnostic a miss returns, and
// the protected-mount rule every write checks.

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "EditorAssetLibrary.h"
#include "Engine/Blueprint.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "HandlerResult.h"
#include "HandlerWorld.h"

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

/** Is this candidate an asset the editor still consults?
 *
 *  RF_NewerVersionExists marks an object a package reload replaced. It stays
 *  reachable, and resolving to one is silent: reads report stale values and
 *  writes land where nothing looks (#972, #1074). */
inline bool MCPIsLiveAssetObject(const UObject* Candidate)
{
	if (!IsValid(Candidate)) return false;
	if (Candidate->HasAnyFlags(RF_NewerVersionExists)) return false;
	if (Candidate->IsA<UPackage>()) return false;
	return true;
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

/** Is there really an asset at this path, without loading anything?
 *
 *  Both sources, because an unsaved asset has no file and the registry is its
 *  only witness. Shared so the resolvers cannot disagree about it. */
inline bool MCPAssetExistsWithoutLoading(const FMCPAssetPathForms& Forms)
{
	if (!Forms.PackagePath.IsEmpty()
		&& FPackageName::IsValidLongPackageName(Forms.PackagePath)
		&& FPackageName::DoesPackageExist(Forms.PackagePath))
	{
		return true;
	}
	return MCPFindAssetDataForPath(Forms).IsValid();
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
 *  Every candidate is revalidated by MCPIsLiveAssetObject above (#1074).
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
		if (MCPIsLiveAssetObject(Loaded)) return Loaded;
	}

	if (UObject* ViaEditorLibrary = UEditorAssetLibrary::LoadAsset(AssetPath))
	{
		if (MCPIsLiveAssetObject(ViaEditorLibrary)) return ViaEditorLibrary;
	}

	// #957: the caller's own form is tried first so nothing that used to work
	// stops working, and the derived object path second. A bare package path
	// for an asset that is not loaded yet is the case that used to answer
	// "Asset not found" for an asset asset(search) had just reported by that
	// exact string, because the load stopped at a form the loader would not
	// resolve on its own.
	// Gated for the same reason as the package load below: LoadObject on a path
	// with nothing behind it can force a blocking package search.
	if (!MCPAssetExistsWithoutLoading(Forms)) return nullptr;

	if (UObject* ViaInput = LoadObject<UObject>(nullptr, *AssetPath))
	{
		if (MCPIsLiveAssetObject(ViaInput)) return ViaInput;
	}
	if (!Forms.bInputCarriedObjectName)
	{
		if (UObject* ViaObjectPath = LoadObject<UObject>(nullptr, *Forms.ObjectPath))
		{
			if (MCPIsLiveAssetObject(ViaObjectPath)) return ViaObjectPath;
		}
	}

	// Every step above rejected its candidate. A full load re-reads the package,
	// gated on the asset existing so LoadPackage cannot force a blind search.
	const FString PackageName = FPackageName::ObjectPathToPackageName(Forms.ObjectPath);
	if (!PackageName.IsEmpty())
	{
		if (UPackage* Package = LoadPackage(nullptr, *PackageName, LOAD_None))
		{
			Package->FullyLoad();
			const FString ObjectName = FPackageName::ObjectPathToObjectName(Forms.ObjectPath);
			if (UObject* InPackage = FindObject<UObject>(Package, *ObjectName))
			{
				if (MCPIsLiveAssetObject(InPackage)) return InPackage;
			}
		}
	}
	return nullptr;
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

	// Appended, not substituted: what the registry and filesystem report stays
	// true and stays first.
	const FString PlayNote = MCPPlayInEditorLoadNote();
	if (!PlayNote.IsEmpty()) Message += PlayNote;

	TSharedPtr<FJsonObject> Obj = MCPErrorObject(Message);
	Obj->SetBoolField(TEXT("playInEditorActive"), !PlayNote.IsEmpty());
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
	TSharedPtr<FJsonObject> Obj = MCPErrorObject(FString::Printf(
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

/** Load a Blueprint by path and return its CDO cast to T. Returns nullptr
 *  on miss; writes a structured error to OutError. The path goes through
 *  the shared asset resolver, so it accepts every form asset(read) does. */
template <typename T = AActor>
inline T* LoadBlueprintCDO(const FString& BlueprintPath, TSharedPtr<FJsonValue>& OutError)
{
	UBlueprint* Blueprint = LoadAssetByPath<UBlueprint>(BlueprintPath);
	if (!Blueprint)
	{
		OutError = MCPAssetLoadError(BlueprintPath, TEXT("Blueprint"));
		return nullptr;
	}
	if (!Blueprint->GeneratedClass)
	{
		OutError = MCPError(FString::Printf(TEXT("Blueprint has no generated class: %s"), *BlueprintPath));
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
