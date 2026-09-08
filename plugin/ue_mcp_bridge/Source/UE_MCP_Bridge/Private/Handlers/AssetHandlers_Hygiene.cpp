// Project-wide asset hygiene: what nothing references, what references nothing,
// what is duplicated, and what is named wrong. Plus the batched fix-up for the
// two of those a machine can safely act on.
//
// All functions below are members of FAssetHandlers: a translation-unit
// partition the way AssetHandlers_UV.cpp is, not a new class. Declarations go
// in AssetHandlers.h, registration in AssetHandlers.cpp::RegisterHandlers.
//
// WHAT WAS ALREADY REACHABLE, AND IS NOT REBUILT HERE:
//
//   * asset(get_referencers) / asset(get_dependencies) answer both
//     questions for ONE named package. Neither can sweep a project, which is
//     the thing an agent actually needs: "which of these 4,000 assets is dead"
//     is not 4,000 calls.
//   * asset(fixup_redirectors) already rewrites and removes redirector stubs
//     with its own preflight. The audit REPORTS redirectors and points at it;
//     fix_asset_hygiene refuses that fix rather than shipping a second, weaker
//     implementation of it.
//   * Bulk property writes across many assets (an attenuation, a sound class, a
//     submix, a compression setting) are asset(bulk_set_properties): every one
//     of those is a plain UPROPERTY and the batch action already carries the
//     preflight and the replayable rollback. This file adds no property setter.
//   * asset(bulk_rename) renames a list somebody else computed, and
//     asset(delete_batch) deletes a list somebody else computed. What is
//     missing is the computing: which names violate a convention, and which
//     assets are genuinely unreferenced at the moment of deletion.
//
// TWO RULES THIS FILE HOLDS ITSELF TO:
//
//   1. The destructive action's default is QUARANTINE, not delete. Unreferenced
//      assets are moved into a folder where they are still recoverable, and the
//      inverse of that move is an exact bulk_rename that this handler emits. A
//      real delete is opt-in and says out loud that it cannot be undone.
//   2. dryRun DEFAULTS TO TRUE on the fix action, and the dry run lists every
//      asset it would touch with its exact destination. A batch that renames or
//      removes hundreds of assets should never be one typo away.
//
// UNITY BUILD: every file-local symbol below is prefixed MCPHyg / FMCPHyg. The
// asset handlers share one unity blob, so a helper named the same as one in
// AssetHandlers.cpp (which has its own IsWorldAsset, for instance) would be a
// redefinition (C2084) on whichever machine happens to group them together.

#include "AssetHandlers.h"

#include "HandlerRegistry.h"
#include "HandlerUtils.h"

#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"

#include "EditorScriptingUtilities/Public/EditorAssetLibrary.h"
#include "Engine/AssetManager.h"
#include "Engine/AssetManagerTypes.h"

#include "IO/IoHash.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/ObjectRedirector.h"
#include "UObject/TopLevelAssetPath.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{

// ─── Limits ──────────────────────────────────────────────────────────────────

/** How many assets one audit will walk. A sweep is O(assets) registry queries
 *  on the game thread, so an unbounded one on a large project stalls the
 *  editor. Over this the audit reports what it covered and says it truncated,
 *  which is a usable answer; a frozen editor is not. */
constexpr int32 MCPHygDefaultMaxAssets = 20000;
constexpr int32 MCPHygMaxMaxAssets = 200000;

/** How many findings of one kind come back in the response. The counts are
 *  always complete; only the listing is capped. */
constexpr int32 MCPHygDefaultMaxIssues = 200;
constexpr int32 MCPHygMaxMaxIssues = 5000;

/** How many assets one fix call will rename, move or delete. Deliberately far
 *  below the audit's ceiling: reading 20,000 assets is cheap and reversible,
 *  moving 20,000 assets is neither. */
constexpr int32 MCPHygDefaultMaxFixes = 100;
constexpr int32 MCPHygMaxMaxFixes = 2000;

// ─── Per-item outcome vocabulary ─────────────────────────────────────────────
//
// Every submitted or planned item gets exactly one of these back, so a caller
// can account for all N entries it was told about. Same idiom as
// asset(bulk_set_properties), deliberately: two batch actions in one category
// that disagree about how they report per-item status is a trap.

const TCHAR* const MCPHygStatusOk          = TEXT("ok");
const TCHAR* const MCPHygStatusProtected   = TEXT("protected");
const TCHAR* const MCPHygStatusDuplicate   = TEXT("duplicate");
const TCHAR* const MCPHygStatusNotFound    = TEXT("not_found");
const TCHAR* const MCPHygStatusCompliant   = TEXT("compliant");
const TCHAR* const MCPHygStatusCollision   = TEXT("collision");
const TCHAR* const MCPHygStatusRefused     = TEXT("refused");
const TCHAR* const MCPHygStatusRenamed     = TEXT("renamed");
const TCHAR* const MCPHygStatusMoved       = TEXT("moved");
const TCHAR* const MCPHygStatusDeleted     = TEXT("deleted");
const TCHAR* const MCPHygStatusFailed      = TEXT("failed");
const TCHAR* const MCPHygStatusSkipped     = TEXT("skipped");

// ─── Naming convention ───────────────────────────────────────────────────────

/**
 * One naming rule: a class, the prefix its assets should carry, and optionally
 * a suffix.
 *
 * Matching is on the asset's CLASS NAME rather than on a class hierarchy walk,
 * because that is what a convention actually keys on: a WidgetBlueprint wants
 * WBP_ and an AnimBlueprint wants ABP_, and both are UBlueprint subclasses, so
 * a hierarchy walk would give them all BP_.
 */
struct FMCPHygNamingRule
{
	FString ClassName;
	FString Prefix;
	FString Suffix;
};

/**
 * The default convention. This is the widely used Unreal community table, and
 * it is a DEFAULT rather than a law: `namingRules` replaces or extends it, and
 * a class with no rule is never a violation.
 */
const TArray<FMCPHygNamingRule>& MCPHygDefaultNamingRules()
{
	static const TArray<FMCPHygNamingRule> Rules = {
		// Meshes and skeletons
		{ TEXT("StaticMesh"),                  TEXT("SM_"),      TEXT("") },
		{ TEXT("SkeletalMesh"),                TEXT("SK_"),      TEXT("") },
		{ TEXT("Skeleton"),                    TEXT("SKEL_"),    TEXT("") },
		{ TEXT("PhysicsAsset"),                TEXT("PHYS_"),    TEXT("") },
		{ TEXT("GeometryCollection"),          TEXT("GC_"),      TEXT("") },

		// Materials and textures
		{ TEXT("Material"),                    TEXT("M_"),       TEXT("") },
		{ TEXT("MaterialInstanceConstant"),    TEXT("MI_"),      TEXT("") },
		{ TEXT("MaterialFunction"),            TEXT("MF_"),      TEXT("") },
		{ TEXT("MaterialParameterCollection"), TEXT("MPC_"),     TEXT("") },
		{ TEXT("PhysicalMaterial"),            TEXT("PM_"),      TEXT("") },
		{ TEXT("SubsurfaceProfile"),           TEXT("SSP_"),     TEXT("") },
		{ TEXT("Texture2D"),                   TEXT("T_"),       TEXT("") },
		{ TEXT("TextureCube"),                 TEXT("TC_"),      TEXT("") },
		{ TEXT("TextureRenderTarget2D"),       TEXT("RT_"),      TEXT("") },

		// Blueprints and UI
		{ TEXT("Blueprint"),                   TEXT("BP_"),      TEXT("") },
		{ TEXT("WidgetBlueprint"),             TEXT("WBP_"),     TEXT("") },
		{ TEXT("AnimBlueprint"),               TEXT("ABP_"),     TEXT("") },
		{ TEXT("Font"),                        TEXT("Font_"),    TEXT("") },

		// Animation
		{ TEXT("AnimSequence"),                TEXT("AS_"),      TEXT("") },
		{ TEXT("AnimMontage"),                 TEXT("AM_"),      TEXT("") },
		{ TEXT("BlendSpace"),                  TEXT("BS_"),      TEXT("") },
		{ TEXT("AimOffsetBlendSpace"),         TEXT("AO_"),      TEXT("") },
		{ TEXT("ControlRigBlueprint"),         TEXT("CR_"),      TEXT("") },
		{ TEXT("IKRigDefinition"),             TEXT("IK_"),      TEXT("") },
		{ TEXT("IKRetargeter"),                TEXT("RTG_"),     TEXT("") },

		// Data
		{ TEXT("DataTable"),                   TEXT("DT_"),      TEXT("") },
		{ TEXT("CurveTable"),                  TEXT("CT_"),      TEXT("") },
		{ TEXT("CurveFloat"),                  TEXT("Curve_"),   TEXT("") },
		{ TEXT("CurveVector"),                 TEXT("Curve_"),   TEXT("") },
		{ TEXT("CurveLinearColor"),            TEXT("Curve_"),   TEXT("") },
		{ TEXT("DataAsset"),                   TEXT("DA_"),      TEXT("") },
		{ TEXT("UserDefinedEnum"),             TEXT("E_"),       TEXT("") },
		{ TEXT("UserDefinedStruct"),           TEXT("F_"),       TEXT("") },
		{ TEXT("StringTable"),                 TEXT("ST_"),      TEXT("") },

		// Audio
		{ TEXT("SoundWave"),                   TEXT("SW_"),      TEXT("") },
		{ TEXT("SoundCue"),                    TEXT("SC_"),      TEXT("") },
		{ TEXT("SoundClass"),                  TEXT("SCL_"),     TEXT("") },
		{ TEXT("SoundMix"),                    TEXT("SMIX_"),    TEXT("") },
		{ TEXT("SoundAttenuation"),            TEXT("ATT_"),     TEXT("") },
		{ TEXT("SoundSubmix"),                 TEXT("SUBMIX_"),  TEXT("") },
		{ TEXT("SoundConcurrency"),            TEXT("CONC_"),    TEXT("") },
		{ TEXT("MetaSoundSource"),             TEXT("MS_"),      TEXT("") },

		// Effects
		{ TEXT("NiagaraSystem"),               TEXT("NS_"),      TEXT("") },
		{ TEXT("NiagaraEmitter"),              TEXT("NE_"),      TEXT("") },
		{ TEXT("ParticleSystem"),              TEXT("PS_"),      TEXT("") },

		// AI and gameplay
		{ TEXT("BehaviorTree"),                TEXT("BT_"),      TEXT("") },
		{ TEXT("BlackboardData"),              TEXT("BB_"),      TEXT("") },
		{ TEXT("EnvQuery"),                    TEXT("EQS_"),     TEXT("") },
		{ TEXT("StateTree"),                   TEXT("STT_"),     TEXT("") },
		{ TEXT("InputAction"),                 TEXT("IA_"),      TEXT("") },
		{ TEXT("InputMappingContext"),         TEXT("IMC_"),     TEXT("") },

		// World building and cinematics
		{ TEXT("World"),                       TEXT("L_"),       TEXT("") },
		{ TEXT("LevelSequence"),               TEXT("LS_"),      TEXT("") },
		{ TEXT("FoliageType_InstancedStaticMesh"), TEXT("FT_"),  TEXT("") },
		{ TEXT("LandscapeGrassType"),          TEXT("LG_"),      TEXT("") },
		{ TEXT("PCGGraph"),                    TEXT("PCG_"),     TEXT("") },
	};
	return Rules;
}

/** Read the caller's rules, merged over the defaults or replacing them.
 *  Returns an error value on a malformed entry, having built nothing. */
TSharedPtr<FJsonValue> MCPHygBuildNamingRules(
	const TSharedPtr<FJsonObject>& Params,
	TMap<FString, FMCPHygNamingRule>& OutRules,
	FString& OutMode)
{
	OutMode = OptionalString(Params, TEXT("namingRuleMode"), TEXT("merge")).ToLower();
	if (OutMode != TEXT("merge") && OutMode != TEXT("replace"))
	{
		return MCPError(FString::Printf(
			TEXT("Unknown namingRuleMode '%s'. Use merge (the caller's rules on top of the built-in table, ")
				TEXT("the default) or replace (the caller's rules and nothing else)."),
			*OutMode));
	}

	OutRules.Reset();
	if (OutMode == TEXT("merge"))
	{
		for (const FMCPHygNamingRule& Rule : MCPHygDefaultNamingRules())
		{
			OutRules.Add(Rule.ClassName, Rule);
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* Custom = nullptr;
	if (Params->TryGetArrayField(TEXT("namingRules"), Custom) && Custom)
	{
		for (int32 Index = 0; Index < Custom->Num(); ++Index)
		{
			const TSharedPtr<FJsonObject>* Entry = nullptr;
			if (!(*Custom)[Index].IsValid() || !(*Custom)[Index]->TryGetObject(Entry) || !Entry || !Entry->IsValid())
			{
				return MCPError(FString::Printf(
					TEXT("namingRules[%d] must be an object of the form ")
						TEXT("{\"class\": \"StaticMesh\", \"prefix\": \"SM_\", \"suffix\": \"\"}."),
					Index));
			}
			FMCPHygNamingRule Rule;
			if (!(*Entry)->TryGetStringField(TEXT("class"), Rule.ClassName) || Rule.ClassName.IsEmpty())
			{
				return MCPError(FString::Printf(
					TEXT("namingRules[%d].class must be a non-empty asset class NAME, such as StaticMesh, ")
						TEXT("WidgetBlueprint or NiagaraSystem. It is matched against the class name the asset ")
						TEXT("registry reports, not against a class path."),
					Index));
			}
			(*Entry)->TryGetStringField(TEXT("prefix"), Rule.Prefix);
			(*Entry)->TryGetStringField(TEXT("suffix"), Rule.Suffix);
			if (Rule.Prefix.IsEmpty() && Rule.Suffix.IsEmpty())
			{
				return MCPError(FString::Printf(
					TEXT("namingRules[%d] for class '%s' has neither a prefix nor a suffix, so it asserts ")
						TEXT("nothing. Give it one, or drop the entry."),
					Index, *Rule.ClassName));
			}
			OutRules.Add(Rule.ClassName, Rule);
		}
	}

	if (OutRules.Num() == 0)
	{
		return MCPError(TEXT("namingRuleMode='replace' was given with no namingRules, so there is no ")
			TEXT("convention left to check against. Supply namingRules, or use merge."));
	}
	return nullptr;
}

/** Every prefix in the rule set, so a wrongly-prefixed asset can have its old
 *  prefix stripped rather than stacked ("MI_SM_Rock"). */
TArray<FString> MCPHygAllPrefixes(const TMap<FString, FMCPHygNamingRule>& Rules)
{
	TArray<FString> Prefixes;
	for (const TPair<FString, FMCPHygNamingRule>& Pair : Rules)
	{
		if (!Pair.Value.Prefix.IsEmpty()) Prefixes.AddUnique(Pair.Value.Prefix);
	}
	// Longest first: "SUBMIX_" has to be tested before "S" would ever match.
	Prefixes.Sort([](const FString& A, const FString& B) { return A.Len() > B.Len(); });
	return Prefixes;
}

/** The name this asset should have under `Rule`, or an empty string when the
 *  name it already has is compliant. */
FString MCPHygCompliantName(
	const FString& CurrentName,
	const FMCPHygNamingRule& Rule,
	const TArray<FString>& AllPrefixes)
{
	FString Core = CurrentName;
	const bool bHasPrefix = Rule.Prefix.IsEmpty() || Core.StartsWith(Rule.Prefix, ESearchCase::CaseSensitive);
	const bool bHasSuffix = Rule.Suffix.IsEmpty() || Core.EndsWith(Rule.Suffix, ESearchCase::CaseSensitive);
	if (bHasPrefix && bHasSuffix) return FString();

	if (!bHasPrefix)
	{
		// Strip a prefix that belongs to a DIFFERENT rule before adding the
		// right one, so a texture named MI_Rock becomes T_Rock rather than
		// T_MI_Rock. A prefix that is not in the rule set is left alone,
		// because it is probably part of the name.
		for (const FString& Other : AllPrefixes)
		{
			if (Other != Rule.Prefix && Core.StartsWith(Other, ESearchCase::CaseSensitive))
			{
				Core = Core.RightChop(Other.Len());
				break;
			}
		}
		Core = Rule.Prefix + Core;
	}
	if (!bHasSuffix)
	{
		Core = Core + Rule.Suffix;
	}
	return Core == CurrentName ? FString() : Core;
}

// ─── Scope ───────────────────────────────────────────────────────────────────

/** Which assets an audit or a fix looks at. */
struct FMCPHygScope
{
	TArray<FString> Directories;
	bool bRecursive = true;
	int32 MaxAssets = MCPHygDefaultMaxAssets;
	TArray<FString> ExcludePrefixes;
	TArray<FString> ClassFilter;
};

TSharedPtr<FJsonValue> MCPHygParseScope(const TSharedPtr<FJsonObject>& Params, FMCPHygScope& Out)
{
	const TArray<TSharedPtr<FJsonValue>>* Dirs = nullptr;
	if (Params->TryGetArrayField(TEXT("directories"), Dirs) && Dirs)
	{
		Out.Directories = JsonArrayToStringList(Dirs);
	}
	const FString Single = OptionalString(Params, TEXT("directory"));
	if (!Single.IsEmpty()) Out.Directories.AddUnique(Single);
	if (Out.Directories.Num() == 0) Out.Directories.Add(TEXT("/Game"));

	for (FString& Dir : Out.Directories)
	{
		Dir.TrimStartAndEndInline();
		if (!Dir.StartsWith(TEXT("/")))
		{
			return MCPError(FString::Printf(
				TEXT("'%s' is not a content path. Directories are mount-rooted, such as /Game or ")
					TEXT("/Game/Characters, not a path on disk."),
				*Dir));
		}
		// A trailing slash makes the registry's path match fail outright, and
		// it is the single most common way to write one of these.
		while (Dir.Len() > 1 && Dir.EndsWith(TEXT("/"))) Dir.LeftChopInline(1);
	}

	Out.bRecursive = OptionalBool(Params, TEXT("recursive"), true);
	Out.MaxAssets = FMath::Clamp(
		OptionalInt(Params, TEXT("maxAssets"), MCPHygDefaultMaxAssets), 1, MCPHygMaxMaxAssets);

	const TArray<TSharedPtr<FJsonValue>>* Excludes = nullptr;
	if (Params->TryGetArrayField(TEXT("excludePaths"), Excludes) && Excludes)
	{
		Out.ExcludePrefixes = JsonArrayToStringList(Excludes);
	}
	const TArray<TSharedPtr<FJsonValue>>* Classes = nullptr;
	if (Params->TryGetArrayField(TEXT("classNames"), Classes) && Classes)
	{
		Out.ClassFilter = JsonArrayToStringList(Classes);
	}
	return nullptr;
}

bool MCPHygIsExcluded(const FMCPHygScope& Scope, const FString& PackageName)
{
	for (const FString& Prefix : Scope.ExcludePrefixes)
	{
		if (!Prefix.IsEmpty() && PackageName.StartsWith(Prefix, ESearchCase::IgnoreCase)) return true;
	}
	return false;
}

/** World Partition puts every actor in its own package under these folders.
 *  They are referenced by their world through the streaming system rather than
 *  by a package dependency, so a hygiene sweep that treated them as ordinary
 *  assets would report an entire level's actors as dead and offer to move
 *  them. */
bool MCPHygIsExternalPackage(const FString& PackageName)
{
	return PackageName.Contains(TEXT("/__ExternalActors__/"))
		|| PackageName.Contains(TEXT("/__ExternalObjects__/"));
}

bool MCPHygIsWorldAsset(const FAssetData& Data)
{
	return Data.AssetClassPath.GetAssetName() == FName(TEXT("World"));
}

/** Gather the assets in scope, capped. Reports whether the cap bit. */
void MCPHygGatherAssets(
	IAssetRegistry& Registry,
	const FMCPHygScope& Scope,
	TArray<FAssetData>& OutAssets,
	int32& OutTotalInScope,
	bool& bOutTruncated)
{
	FARFilter Filter;
	Filter.bRecursivePaths = Scope.bRecursive;
	for (const FString& Dir : Scope.Directories)
	{
		Filter.PackagePaths.Add(FName(*Dir));
	}
	// The class filter is applied below rather than through FARFilter::ClassPaths:
	// that field wants a full class path, and a caller naming "NiagaraSystem"
	// would silently match nothing at all rather than being told the spelling is
	// wrong. Comparing the short name the registry reports is what a caller can
	// actually predict.

	TArray<FAssetData> All;
	Registry.GetAssets(Filter, All);

	OutTotalInScope = 0;
	bOutTruncated = false;
	OutAssets.Reserve(FMath::Min(All.Num(), Scope.MaxAssets));

	for (const FAssetData& Data : All)
	{
		const FString PackageName = Data.PackageName.ToString();
		if (MCPHygIsExcluded(Scope, PackageName)) continue;
		if (Scope.ClassFilter.Num() > 0)
		{
			const FString ClassName = Data.AssetClassPath.GetAssetName().ToString();
			bool bMatch = false;
			for (const FString& Wanted : Scope.ClassFilter)
			{
				if (ClassName.Equals(Wanted, ESearchCase::IgnoreCase)) { bMatch = true; break; }
			}
			if (!bMatch) continue;
		}
		++OutTotalInScope;
		if (OutAssets.Num() >= Scope.MaxAssets)
		{
			bOutTruncated = true;
			continue;
		}
		OutAssets.Add(Data);
	}
}

/** The package names the Asset Manager knows as primary assets. Those are
 *  loaded by id rather than by a hard reference, so every one of them looks
 *  unreferenced to the dependency graph and none of them is. */
TSet<FName> MCPHygPrimaryAssetPackages()
{
	TSet<FName> Out;
	UAssetManager* Manager = UAssetManager::GetIfInitialized();
	if (!Manager) return Out;

	TArray<FPrimaryAssetTypeInfo> TypeInfos;
	Manager->GetPrimaryAssetTypeInfoList(TypeInfos);
	for (const FPrimaryAssetTypeInfo& Info : TypeInfos)
	{
		TArray<FPrimaryAssetId> Ids;
		Manager->GetPrimaryAssetIdList(FPrimaryAssetType(Info.PrimaryAssetType), Ids);
		for (const FPrimaryAssetId& Id : Ids)
		{
			const FSoftObjectPath Path = Manager->GetPrimaryAssetPath(Id);
			if (Path.IsValid())
			{
				Out.Add(FName(*Path.GetLongPackageName()));
			}
		}
	}
	return Out;
}

/** Does this dependency name a package that actually exists?
 *  Cached, because a dependency graph revisits the same packages constantly. */
bool MCPHygPackageResolves(IAssetRegistry& Registry, const FName PackageName, TMap<FName, bool>& Cache)
{
	if (const bool* Cached = Cache.Find(PackageName)) return *Cached;

	const FString AsString = PackageName.ToString();
	bool bResolves = false;

	if (FPackageName::IsScriptPackage(AsString))
	{
		// /Script/Foo is code, not content. It cannot be a broken asset
		// reference, and reporting it as one would bury the real findings.
		bResolves = true;
	}
	else if (AsString.StartsWith(TEXT("/Temp/")) || AsString.StartsWith(TEXT("/Memory/")))
	{
		bResolves = true;
	}
	else if (FPackageName::DoesPackageExist(AsString))
	{
		bResolves = true;
	}
	else
	{
		// A package the registry knows about but which is not on disk yet is a
		// package created this session and not saved. That is not a broken
		// reference; it is unsaved work.
		TArray<FAssetData> InRegistry;
		Registry.GetAssetsByPackageName(PackageName, InRegistry, /*bIncludeOnlyOnDiskAssets*/ false);
		bResolves = InRegistry.Num() > 0;
	}

	Cache.Add(PackageName, bResolves);
	return bResolves;
}

/** Referencers of a package, minus the package itself and minus anything the
 *  audit is deliberately ignoring. */
int32 MCPHygRealReferencerCount(
	IAssetRegistry& Registry,
	const FName PackageName,
	bool bIgnoreRedirectors,
	TArray<FString>* OutReferencers)
{
	TArray<FName> Referencers;
	Registry.GetReferencers(PackageName, Referencers, UE::AssetRegistry::EDependencyCategory::Package);

	int32 Count = 0;
	for (const FName& Referencer : Referencers)
	{
		if (Referencer == PackageName) continue;

		if (bIgnoreRedirectors)
		{
			// A redirector stub pointing at this asset is not a user of it: it
			// is the leftover of a rename. Counting it keeps an otherwise dead
			// asset alive forever, which is exactly the state a project ends up
			// in after a few hundred moves.
			TArray<FAssetData> AtReferencer;
			Registry.GetAssetsByPackageName(Referencer, AtReferencer);
			bool bAllRedirectors = AtReferencer.Num() > 0;
			for (const FAssetData& Data : AtReferencer)
			{
				if (!Data.IsRedirector()) { bAllRedirectors = false; break; }
			}
			if (bAllRedirectors) continue;
		}

		++Count;
		if (OutReferencers) OutReferencers->Add(Referencer.ToString());
	}
	return Count;
}

TSharedPtr<FJsonObject> MCPHygAssetJson(const FAssetData& Data)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetStringField(TEXT("assetPath"), Data.GetSoftObjectPath().ToString());
	Obj->SetStringField(TEXT("packageName"), Data.PackageName.ToString());
	Obj->SetStringField(TEXT("name"), Data.AssetName.ToString());
	Obj->SetStringField(TEXT("class"), Data.AssetClassPath.GetAssetName().ToString());
	return Obj;
}

// ─── The planned fix ─────────────────────────────────────────────────────────

/** One asset a fix intends to touch, with its preflight verdict. */
struct FMCPHygPlannedFix
{
	FString AssetPath;
	FString PackageName;
	FString AssetName;
	FString ClassName;

	/** For a rename or a move. */
	FString NewPackagePath;
	FString NewName;
	FString DestinationPath;

	FString Status;
	FString Error;
	bool bEligible = false;

	UObject* Asset = nullptr;

	TSharedPtr<FJsonObject> ToJson() const
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("assetPath"), AssetPath);
		Obj->SetStringField(TEXT("name"), AssetName);
		Obj->SetStringField(TEXT("class"), ClassName);
		if (!DestinationPath.IsEmpty()) Obj->SetStringField(TEXT("destinationPath"), DestinationPath);
		Obj->SetStringField(TEXT("status"), Status);

		// A skip carries a note, not a fault: "this one already has a referencer"
		// is the action working correctly. Reporting it as ok=false with an
		// `error` would make a healthy run read as a batch of failures, so only
		// the statuses that really are problems say so.
		const bool bIsProblem =
			Status == MCPHygStatusFailed
			|| Status == MCPHygStatusNotFound
			|| Status == MCPHygStatusProtected
			|| Status == MCPHygStatusDuplicate
			|| Status == MCPHygStatusCollision
			|| Status == MCPHygStatusRefused;
		Obj->SetBoolField(TEXT("ok"), !bIsProblem);
		if (!Error.IsEmpty())
		{
			Obj->SetStringField(bIsProblem ? TEXT("error") : TEXT("reason"), Error);
		}
		return Obj;
	}
};

} // namespace

// ---------------------------------------------------------------------------
// asset(audit_hygiene)
//
// One read-only sweep answering the four questions a project accumulates
// answers to over time and never gets asked:
//
//   unreferenced      what nothing points at
//   brokenReferences  what points at nothing
//   duplicates        what exists twice
//   naming            what breaks the convention
//   redirectors       what renames left behind
//
// Every check is opt-out rather than opt-in, because the useful call is the one
// that runs them all; `checks` narrows it when only one matters.
//
// This is the read half that makes asset(bulk_fix_hygiene) safe to run: the fix
// action recomputes its own findings rather than trusting a stale audit, but a
// caller that has not looked at an audit first has no business running the fix.
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FAssetHandlers::AuditAssetHygiene(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	FMCPHygScope Scope;
	if (auto Err = MCPHygParseScope(Params, Scope)) return Err;

	const int32 MaxIssues = FMath::Clamp(
		OptionalInt(Params, TEXT("maxIssues"), MCPHygDefaultMaxIssues), 1, MCPHygMaxMaxIssues);

	// Which checks to run. An unknown word is refused with the list, rather
	// than quietly producing a report missing the section the caller asked for.
	static const TArray<FString> AllChecks = {
		TEXT("unreferenced"), TEXT("brokenreferences"), TEXT("duplicates"),
		TEXT("naming"), TEXT("redirectors")
	};
	TArray<FString> Checks;
	const TArray<TSharedPtr<FJsonValue>>* RequestedChecks = nullptr;
	if (Params->TryGetArrayField(TEXT("checks"), RequestedChecks) && RequestedChecks)
	{
		for (const FString& Raw : JsonArrayToStringList(RequestedChecks))
		{
			const FString Lower = Raw.ToLower();
			if (!AllChecks.Contains(Lower))
			{
				return MCPError(FString::Printf(
					TEXT("Unknown hygiene check '%s'. Use one or more of: unreferenced, brokenReferences, ")
						TEXT("duplicates, naming, redirectors. Omit 'checks' to run all five."),
					*Raw));
			}
			Checks.AddUnique(Lower);
		}
	}
	if (Checks.Num() == 0) Checks = AllChecks;

	const FString DuplicateMethod = OptionalString(Params, TEXT("duplicateMethod"), TEXT("both")).ToLower();
	if (DuplicateMethod != TEXT("content") && DuplicateMethod != TEXT("name") && DuplicateMethod != TEXT("both"))
	{
		return MCPError(FString::Printf(
			TEXT("Unknown duplicateMethod '%s'. Use content (same class, same file size, same saved hash), ")
				TEXT("name (same asset name and class in more than one folder) or both (the default)."),
			*DuplicateMethod));
	}

	TMap<FString, FMCPHygNamingRule> NamingRules;
	FString NamingRuleMode;
	if (Checks.Contains(TEXT("naming")))
	{
		if (auto Err = MCPHygBuildNamingRules(Params, NamingRules, NamingRuleMode)) return Err;
	}
	const TArray<FString> AllPrefixes = MCPHygAllPrefixes(NamingRules);

	const bool bIncludeWorlds = OptionalBool(Params, TEXT("includeWorlds"), false);
	const bool bIgnoreRedirectorReferencers = OptionalBool(Params, TEXT("ignoreRedirectorReferencers"), true);

	TArray<FString> KeepPrefixes;
	const TArray<TSharedPtr<FJsonValue>>* Keep = nullptr;
	if (Params->TryGetArrayField(TEXT("keepPaths"), Keep) && Keep)
	{
		KeepPrefixes = JsonArrayToStringList(Keep);
	}

	IAssetRegistry& Registry =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

	TArray<FAssetData> Assets;
	int32 TotalInScope = 0;
	bool bTruncatedScan = false;
	MCPHygGatherAssets(Registry, Scope, Assets, TotalInScope, bTruncatedScan);

	const TSet<FName> PrimaryAssetPackages = MCPHygPrimaryAssetPackages();
	TMap<FName, bool> ResolveCache;

	// ── The five findings ───────────────────────────────────────────────────
	TArray<TSharedPtr<FJsonValue>> Unreferenced;
	int32 UnreferencedTotal = 0;

	TArray<TSharedPtr<FJsonValue>> Broken;
	int32 BrokenTotal = 0;
	int32 BrokenLinkTotal = 0;

	TArray<TSharedPtr<FJsonValue>> Redirectors;
	int32 RedirectorTotal = 0;

	TArray<TSharedPtr<FJsonValue>> NamingViolations;
	int32 NamingTotal = 0;
	int32 NamingCheckedTotal = 0;

	// Duplicate grouping runs after the walk, so it collects here.
	TMap<FString, TArray<FAssetData>> ByContentKey;
	TMap<FString, TArray<FAssetData>> ByNameKey;

	const bool bWantUnreferenced = Checks.Contains(TEXT("unreferenced"));
	const bool bWantBroken = Checks.Contains(TEXT("brokenreferences"));
	const bool bWantDuplicates = Checks.Contains(TEXT("duplicates"));
	const bool bWantNaming = Checks.Contains(TEXT("naming"));
	const bool bWantRedirectors = Checks.Contains(TEXT("redirectors"));

	for (const FAssetData& Data : Assets)
	{
		const FString PackageName = Data.PackageName.ToString();
		const FString ClassName = Data.AssetClassPath.GetAssetName().ToString();
		const bool bIsRedirector = Data.IsRedirector();
		const bool bIsWorld = MCPHygIsWorldAsset(Data);
		const bool bIsExternal = MCPHygIsExternalPackage(PackageName);

		bool bKept = false;
		for (const FString& Prefix : KeepPrefixes)
		{
			if (!Prefix.IsEmpty() && PackageName.StartsWith(Prefix, ESearchCase::IgnoreCase))
			{
				bKept = true;
				break;
			}
		}

		// ── redirectors ────────────────────────────────────────────────────
		if (bWantRedirectors && bIsRedirector)
		{
			++RedirectorTotal;
			if (Redirectors.Num() < MaxIssues)
			{
				Redirectors.Add(MakeShared<FJsonValueObject>(MCPHygAssetJson(Data)));
			}
		}

		// ── unreferenced ───────────────────────────────────────────────────
		if (bWantUnreferenced
			&& !bIsRedirector
			&& !bIsExternal
			&& !bKept
			&& (bIncludeWorlds || !bIsWorld)
			&& !PrimaryAssetPackages.Contains(Data.PackageName))
		{
			const int32 ReferencerCount =
				MCPHygRealReferencerCount(Registry, Data.PackageName, bIgnoreRedirectorReferencers, nullptr);
			if (ReferencerCount == 0)
			{
				++UnreferencedTotal;
				if (Unreferenced.Num() < MaxIssues)
				{
					TSharedPtr<FJsonObject> Entry = MCPHygAssetJson(Data);
					Entry->SetNumberField(TEXT("referencerCount"), 0);
					Unreferenced.Add(MakeShared<FJsonValueObject>(Entry));
				}
			}
		}

		// ── broken references ──────────────────────────────────────────────
		if (bWantBroken)
		{
			TArray<FName> Dependencies;
			Registry.GetDependencies(
				Data.PackageName, Dependencies, UE::AssetRegistry::EDependencyCategory::Package);

			TArray<FString> Missing;
			for (const FName& Dependency : Dependencies)
			{
				if (Dependency == Data.PackageName) continue;
				if (!MCPHygPackageResolves(Registry, Dependency, ResolveCache))
				{
					Missing.Add(Dependency.ToString());
				}
			}
			if (Missing.Num() > 0)
			{
				++BrokenTotal;
				BrokenLinkTotal += Missing.Num();
				if (Broken.Num() < MaxIssues)
				{
					TSharedPtr<FJsonObject> Entry = MCPHygAssetJson(Data);
					Entry->SetArrayField(TEXT("missingDependencies"), MCPStringListToJson(Missing));
					Entry->SetNumberField(TEXT("missingCount"), Missing.Num());
					Broken.Add(MakeShared<FJsonValueObject>(Entry));
				}
			}
		}

		// ── naming ─────────────────────────────────────────────────────────
		if (bWantNaming && !bIsRedirector && !bIsExternal)
		{
			if (const FMCPHygNamingRule* Rule = NamingRules.Find(ClassName))
			{
				++NamingCheckedTotal;
				const FString AssetName = Data.AssetName.ToString();
				const FString Suggested = MCPHygCompliantName(AssetName, *Rule, AllPrefixes);
				if (!Suggested.IsEmpty())
				{
					++NamingTotal;
					if (NamingViolations.Num() < MaxIssues)
					{
						TSharedPtr<FJsonObject> Entry = MCPHygAssetJson(Data);
						Entry->SetStringField(TEXT("expectedPrefix"), Rule->Prefix);
						if (!Rule->Suffix.IsEmpty())
						{
							Entry->SetStringField(TEXT("expectedSuffix"), Rule->Suffix);
						}
						Entry->SetStringField(TEXT("suggestedName"), Suggested);
						NamingViolations.Add(MakeShared<FJsonValueObject>(Entry));
					}
				}
			}
		}

		// ── duplicates ─────────────────────────────────────────────────────
		if (bWantDuplicates && !bIsRedirector && !bIsExternal)
		{
			if (DuplicateMethod != TEXT("content"))
			{
				ByNameKey.FindOrAdd(ClassName + TEXT("|") + Data.AssetName.ToString().ToLower()).Add(Data);
			}
			if (DuplicateMethod != TEXT("name"))
			{
				const TOptional<FAssetPackageData> PackageData =
					Registry.GetAssetPackageDataCopy(Data.PackageName);
				if (PackageData.IsSet() && PackageData->DiskSize > 0)
				{
#if WITH_EDITORONLY_DATA
					const FString Hash = LexToString(PackageData->GetPackageSavedHash());
#else
					const FString Hash = TEXT("");
#endif
					if (!Hash.IsEmpty())
					{
						ByContentKey.FindOrAdd(FString::Printf(
							TEXT("%s|%lld|%s"), *ClassName, PackageData->DiskSize, *Hash)).Add(Data);
					}
				}
			}
		}
	}

	// ── Fold the duplicate groups ───────────────────────────────────────────
	TArray<TSharedPtr<FJsonValue>> DuplicateGroups;
	int32 DuplicateGroupTotal = 0;
	int32 DuplicateAssetTotal = 0;

	auto EmitGroups = [&](const TMap<FString, TArray<FAssetData>>& Groups, const TCHAR* Method)
	{
		for (const TPair<FString, TArray<FAssetData>>& Pair : Groups)
		{
			if (Pair.Value.Num() < 2) continue;
			++DuplicateGroupTotal;
			DuplicateAssetTotal += Pair.Value.Num();
			if (DuplicateGroups.Num() >= MaxIssues) continue;

			TSharedPtr<FJsonObject> Group = MakeShared<FJsonObject>();
			Group->SetStringField(TEXT("method"), Method);
			Group->SetStringField(TEXT("class"), Pair.Value[0].AssetClassPath.GetAssetName().ToString());
			Group->SetNumberField(TEXT("count"), Pair.Value.Num());
			TArray<TSharedPtr<FJsonValue>> Members;
			for (const FAssetData& Data : Pair.Value)
			{
				Members.Add(MakeShared<FJsonValueObject>(MCPHygAssetJson(Data)));
			}
			Group->SetArrayField(TEXT("assets"), Members);
			DuplicateGroups.Add(MakeShared<FJsonValueObject>(Group));
		}
	};
	if (bWantDuplicates)
	{
		EmitGroups(ByContentKey, TEXT("content"));
		EmitGroups(ByNameKey, TEXT("name"));
	}

	// ── Result ──────────────────────────────────────────────────────────────
	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("directories"), MCPStringListToJson(Scope.Directories));
	Result->SetBoolField(TEXT("recursive"), Scope.bRecursive);
	Result->SetArrayField(TEXT("checks"), MCPStringListToJson(Checks));
	Result->SetNumberField(TEXT("assetsInScope"), TotalInScope);
	Result->SetNumberField(TEXT("assetsScanned"), Assets.Num());
	Result->SetNumberField(TEXT("maxAssets"), Scope.MaxAssets);
	Result->SetNumberField(TEXT("maxIssues"), MaxIssues);
	Result->SetBoolField(TEXT("scanTruncated"), bTruncatedScan);
	if (bTruncatedScan)
	{
		Result->SetStringField(TEXT("scanTruncatedNote"), FString::Printf(
			TEXT("%d assets are in scope and %d were scanned, so the counts below describe the scanned subset ")
				TEXT("and not the project. Narrow 'directories', add a 'classNames' filter, or raise 'maxAssets'."),
			TotalInScope, Assets.Num()));
	}

	if (bWantUnreferenced)
	{
		TSharedPtr<FJsonObject> Section = MakeShared<FJsonObject>();
		Section->SetNumberField(TEXT("count"), UnreferencedTotal);
		Section->SetArrayField(TEXT("assets"), Unreferenced);
		Section->SetBoolField(TEXT("listTruncated"), UnreferencedTotal > Unreferenced.Num());
		Section->SetBoolField(TEXT("worldsIncluded"), bIncludeWorlds);
		// The single most important sentence in this whole response. A caller
		// that deletes this list without reading it will break the project.
		Section->SetStringField(TEXT("caveat"),
			TEXT("'Unreferenced' means no other PACKAGE depends on it. An asset loaded by name from an INI ")
				TEXT("setting, from C++ with a hardcoded path, from a Blueprint's soft reference resolved at ")
				TEXT("runtime, or from an unloaded plugin has no package dependency and appears here while ")
				TEXT("being very much in use. Maps, World Partition external actors and Asset Manager primary ")
				TEXT("assets are already excluded for exactly this reason. Treat the list as candidates to ")
				TEXT("review, never as a delete list, and prefer asset(bulk_fix_hygiene) with the default ")
				TEXT("quarantine over a delete."));
		Result->SetObjectField(TEXT("unreferenced"), Section);
	}

	if (bWantBroken)
	{
		TSharedPtr<FJsonObject> Section = MakeShared<FJsonObject>();
		Section->SetNumberField(TEXT("assetCount"), BrokenTotal);
		Section->SetNumberField(TEXT("missingLinkCount"), BrokenLinkTotal);
		Section->SetArrayField(TEXT("assets"), Broken);
		Section->SetBoolField(TEXT("listTruncated"), BrokenTotal > Broken.Num());
		Section->SetStringField(TEXT("note"),
			TEXT("Each entry names an asset whose saved dependencies include a package that is neither on disk ")
				TEXT("nor in the registry. Script packages (/Script/...) are code and are never counted. A ")
				TEXT("package created this session and not yet saved is not counted either. There is no ")
				TEXT("automatic fix: a missing dependency is either a file that should be restored from source ")
				TEXT("control or a reference that should be cleared with asset(set_property)."));
		Result->SetObjectField(TEXT("brokenReferences"), Section);
	}

	if (bWantDuplicates)
	{
		TSharedPtr<FJsonObject> Section = MakeShared<FJsonObject>();
		Section->SetStringField(TEXT("method"), DuplicateMethod);
		Section->SetNumberField(TEXT("groupCount"), DuplicateGroupTotal);
		Section->SetNumberField(TEXT("assetCount"), DuplicateAssetTotal);
		Section->SetArrayField(TEXT("groups"), DuplicateGroups);
		Section->SetBoolField(TEXT("listTruncated"), DuplicateGroupTotal > DuplicateGroups.Num());
		Section->SetStringField(TEXT("note"),
			TEXT("method='content' groups assets whose class, file size and saved package hash all match, ")
				TEXT("which finds a file copied verbatim. It does NOT find a copy saved under a different ")
				TEXT("name, because the name is inside the file and changes the hash; method='name' is what ")
				TEXT("catches an asset imported twice into two folders. There is no automatic fix, because ")
				TEXT("deciding which copy survives is a decision about which referencers should be rewritten."));
		Result->SetObjectField(TEXT("duplicates"), Section);
	}

	if (bWantNaming)
	{
		TSharedPtr<FJsonObject> Section = MakeShared<FJsonObject>();
		Section->SetStringField(TEXT("ruleMode"), NamingRuleMode);
		Section->SetNumberField(TEXT("ruleCount"), NamingRules.Num());
		Section->SetNumberField(TEXT("assetsWithARule"), NamingCheckedTotal);
		Section->SetNumberField(TEXT("count"), NamingTotal);
		Section->SetArrayField(TEXT("assets"), NamingViolations);
		Section->SetBoolField(TEXT("listTruncated"), NamingTotal > NamingViolations.Num());

		TArray<TSharedPtr<FJsonValue>> RuleJson;
		for (const TPair<FString, FMCPHygNamingRule>& Pair : NamingRules)
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("class"), Pair.Value.ClassName);
			Entry->SetStringField(TEXT("prefix"), Pair.Value.Prefix);
			if (!Pair.Value.Suffix.IsEmpty()) Entry->SetStringField(TEXT("suffix"), Pair.Value.Suffix);
			RuleJson.Add(MakeShared<FJsonValueObject>(Entry));
		}
		Section->SetArrayField(TEXT("rulesApplied"), RuleJson);
		Section->SetStringField(TEXT("note"),
			TEXT("Rules match on the asset's class NAME, so WidgetBlueprint and AnimBlueprint carry their own ")
				TEXT("prefixes rather than inheriting Blueprint's. A class with no rule is never a violation. ")
				TEXT("asset(bulk_fix_hygiene, fix='naming') applies the suggestedName of every entry here."));
		Result->SetObjectField(TEXT("naming"), Section);
	}

	if (bWantRedirectors)
	{
		TSharedPtr<FJsonObject> Section = MakeShared<FJsonObject>();
		Section->SetNumberField(TEXT("count"), RedirectorTotal);
		Section->SetArrayField(TEXT("assets"), Redirectors);
		Section->SetBoolField(TEXT("listTruncated"), RedirectorTotal > Redirectors.Num());
		Section->SetStringField(TEXT("fixWith"), TEXT("asset(fixup_redirectors)"));
		Section->SetStringField(TEXT("note"),
			TEXT("Every entry is an ObjectRedirector stub left by a rename or a move. asset(fixup_redirectors) ")
				TEXT("loads the referencers, rewrites them and deletes the stubs that come out unreferenced; ")
				TEXT("it has its own preflight and dryRun. asset(bulk_fix_hygiene) deliberately does not duplicate ")
				TEXT("it."));
		Result->SetObjectField(TEXT("redirectors"), Section);
	}

	const int32 TotalFindings =
		UnreferencedTotal + BrokenTotal + DuplicateGroupTotal + NamingTotal + RedirectorTotal;
	Result->SetNumberField(TEXT("totalFindings"), TotalFindings);
	Result->SetBoolField(TEXT("clean"), TotalFindings == 0);
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// asset(bulk_fix_hygiene)
//
// The batched fix-up for the two findings a machine can act on without deciding
// something a person should: a name that breaks the convention, and an asset
// nothing references.
//
// SHAPE, deliberately the same as asset(bulk_set_properties): every candidate
// is preflighted, every candidate gets a status back including the rejected
// ones, and a failed preflight aborts before anything is touched unless
// continueOnError says otherwise. Two batch actions in one category that
// disagree about this would be a trap.
//
// SAFETY, which is not the same as the shape:
//   * dryRun DEFAULTS TO TRUE. The first call always reports and changes
//     nothing, and the report names every asset with its exact destination.
//   * unreferenced defaults to MOVING assets into a quarantine folder, not
//     deleting them. That move has an exact inverse and this handler emits it.
//     unreferencedAction='delete' is opt-in and says it cannot be undone.
//   * Findings are recomputed here rather than taken from the caller. An audit
//     is a snapshot, and an asset that gained a referencer since then must not
//     be deleted on the strength of it.
//   * maxFixes caps the batch far below the audit's scan ceiling.
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FAssetHandlers::FixAssetHygiene(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	FString Fix;
	if (auto Err = RequireString(Params, TEXT("fix"), Fix)) return Err;
	Fix = Fix.ToLower();
	if (Fix != TEXT("naming") && Fix != TEXT("unreferenced"))
	{
		if (Fix == TEXT("redirectors"))
		{
			return MCPError(TEXT("fix='redirectors' is not handled here. asset(fixup_redirectors) already ")
				TEXT("rewrites referencers and deletes the stubs, with its own preflight and dryRun; ")
				TEXT("asset(audit_hygiene) lists the stubs to feed it. Use naming or unreferenced."));
		}
		if (Fix == TEXT("duplicates") || Fix == TEXT("brokenreferences"))
		{
			return MCPError(FString::Printf(
				TEXT("fix='%s' has no safe automatic form and is deliberately not offered. Choosing which ")
					TEXT("duplicate survives rewrites everything that pointed at the others, and a missing ")
					TEXT("dependency is either a file to restore from source control or a reference to clear ")
					TEXT("with asset(set_property). Use naming or unreferenced."),
				*Fix));
		}
		return MCPError(FString::Printf(
			TEXT("Unknown fix '%s'. Use naming (rename assets to the convention) or unreferenced (quarantine ")
				TEXT("or delete assets nothing references)."),
			*Fix));
	}

	FMCPHygScope Scope;
	if (auto Err = MCPHygParseScope(Params, Scope)) return Err;

	// dryRun defaults to TRUE here and nowhere else in this category. This
	// action renames and removes assets in bulk; the default has to be the one
	// that cannot hurt.
	const bool bDryRun = OptionalBool(Params, TEXT("dryRun"), true);
	const bool bContinueOnError = OptionalBool(Params, TEXT("continueOnError"), false);
	const bool bSave = OptionalBool(Params, TEXT("save"), true);
	const int32 MaxFixes = FMath::Clamp(
		OptionalInt(Params, TEXT("maxFixes"), MCPHygDefaultMaxFixes), 1, MCPHygMaxMaxFixes);

	// An explicit list is the safest way to drive this: audit, read the report,
	// then hand back exactly the paths that were approved.
	TArray<FString> ExplicitPaths;
	const TArray<TSharedPtr<FJsonValue>>* PathArray = nullptr;
	if (Params->TryGetArrayField(TEXT("assetPaths"), PathArray) && PathArray)
	{
		ExplicitPaths = JsonArrayToStringList(PathArray);
	}

	TArray<FString> KeepPrefixes;
	const TArray<TSharedPtr<FJsonValue>>* Keep = nullptr;
	if (Params->TryGetArrayField(TEXT("keepPaths"), Keep) && Keep)
	{
		KeepPrefixes = JsonArrayToStringList(Keep);
	}

	TMap<FString, FMCPHygNamingRule> NamingRules;
	FString NamingRuleMode;
	if (Fix == TEXT("naming"))
	{
		if (auto Err = MCPHygBuildNamingRules(Params, NamingRules, NamingRuleMode)) return Err;
	}
	const TArray<FString> AllPrefixes = MCPHygAllPrefixes(NamingRules);

	FString UnreferencedAction = OptionalString(Params, TEXT("unreferencedAction"), TEXT("quarantine")).ToLower();
	FString QuarantineFolder = OptionalString(Params, TEXT("quarantineFolder"), TEXT("/Game/Quarantine"));
	const bool bIgnoreRedirectorReferencers = OptionalBool(Params, TEXT("ignoreRedirectorReferencers"), true);

	if (Fix == TEXT("unreferenced"))
	{
		if (UnreferencedAction != TEXT("quarantine") && UnreferencedAction != TEXT("delete"))
		{
			return MCPError(FString::Printf(
				TEXT("Unknown unreferencedAction '%s'. Use quarantine (move the assets under ")
					TEXT("quarantineFolder, which this call can undo, and the default) or delete (permanent)."),
				*UnreferencedAction));
		}
		while (QuarantineFolder.Len() > 1 && QuarantineFolder.EndsWith(TEXT("/")))
		{
			QuarantineFolder.LeftChopInline(1);
		}
		if (UnreferencedAction == TEXT("quarantine"))
		{
			if (!QuarantineFolder.StartsWith(TEXT("/")))
			{
				return MCPError(FString::Printf(
					TEXT("'%s' is not a content path. quarantineFolder is mount-rooted, such as ")
						TEXT("/Game/Quarantine."),
					*QuarantineFolder));
			}
			if (MCPIsProtectedAssetPath(QuarantineFolder)) return MCPProtectedPathError(QuarantineFolder);
		}
	}

	IAssetRegistry& Registry =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

	// ── Collect the candidates ──────────────────────────────────────────────
	TArray<FAssetData> Candidates;
	TArray<FString> NotFoundPaths;
	const bool bExplicit = ExplicitPaths.Num() > 0;
	int32 TotalInScope = 0;
	bool bTruncatedScan = false;

	if (bExplicit)
	{
		for (const FString& Path : ExplicitPaths)
		{
			const FMCPAssetPathForms Forms = MCPAssetPathForms(Path);
			TArray<FAssetData> Found;
			Registry.GetAssetsByPackageName(FName(*Forms.PackagePath), Found);
			bool bAdded = false;
			for (const FAssetData& Data : Found)
			{
				if (Data.IsRedirector()) continue;
				Candidates.Add(Data);
				bAdded = true;
			}
			// Reported by path rather than as an empty candidate row, so a
			// caller can still account for every path it submitted.
			if (!bAdded) NotFoundPaths.Add(Path);
		}
		TotalInScope = Candidates.Num() + NotFoundPaths.Num();
	}
	else
	{
		MCPHygGatherAssets(Registry, Scope, Candidates, TotalInScope, bTruncatedScan);
	}

	const TSet<FName> PrimaryAssetPackages = MCPHygPrimaryAssetPackages();

	// ── Preflight: plan every fix, validate it, touch nothing ───────────────
	TArray<FMCPHygPlannedFix> Plan;
	TSet<FString> ClaimedDestinations;
	TSet<FString> SeenSources;
	int32 EligibleCount = 0;
	int32 RejectedCount = 0;
	FString FirstPreflightError;

	auto Reject = [&](FMCPHygPlannedFix& Item, const TCHAR* Status, const FString& Message)
	{
		Item.Status = Status;
		Item.Error = Message;
		Item.bEligible = false;
		++RejectedCount;
		if (FirstPreflightError.IsEmpty()) FirstPreflightError = Message;
	};

	for (const FAssetData& Data : Candidates)
	{
		FMCPHygPlannedFix Item;
		Item.AssetPath = Data.GetSoftObjectPath().ToString();
		Item.PackageName = Data.PackageName.ToString();
		Item.AssetName = Data.AssetName.ToString();
		Item.ClassName = Data.AssetClassPath.GetAssetName().ToString();

		if (SeenSources.Contains(Item.PackageName))
		{
			Reject(Item, MCPHygStatusDuplicate, FString::Printf(
				TEXT("'%s' appears more than once in this batch, so the order of the two operations on it ")
					TEXT("would be undefined. Submit it once."),
				*Item.PackageName));
			Plan.Add(MoveTemp(Item));
			continue;
		}
		SeenSources.Add(Item.PackageName);

		if (MCPIsProtectedAssetPath(Item.PackageName))
		{
			Reject(Item, MCPHygStatusProtected, FString::Printf(
				TEXT("Refusing to mutate protected mount: %s. Engine, /Script/, /Memory/ and /Temp/ are ")
					TEXT("read-only through the bridge."),
				*Item.PackageName));
			Plan.Add(MoveTemp(Item));
			continue;
		}

		if (MCPHygIsExternalPackage(Item.PackageName))
		{
			Reject(Item, MCPHygStatusRefused, FString::Printf(
				TEXT("'%s' is a World Partition external package. Its world owns it through the streaming ")
					TEXT("system rather than a package dependency, so renaming or removing it orphans an actor."),
				*Item.PackageName));
			Plan.Add(MoveTemp(Item));
			continue;
		}

		// Refused unconditionally, with no opt-in. Renaming or moving a World has
		// to migrate its __ExternalActors__ and __ExternalObjects__ packages in
		// the same atomic batch, which asset(rename) does and a bulk fix-up does
		// not; doing it here would orphan every actor in the level.
		if (MCPHygIsWorldAsset(Data))
		{
			Reject(Item, MCPHygStatusRefused, FString::Printf(
				TEXT("'%s' is a World and this action never renames or moves one, because doing so without ")
					TEXT("migrating its __ExternalActors__ and __ExternalObjects__ packages in the same batch ")
					TEXT("orphans every actor in the level. Use asset(rename), which migrates them atomically."),
				*Item.PackageName));
			Plan.Add(MoveTemp(Item));
			continue;
		}

		bool bKept = false;
		for (const FString& Prefix : KeepPrefixes)
		{
			if (!Prefix.IsEmpty() && Item.PackageName.StartsWith(Prefix, ESearchCase::IgnoreCase))
			{
				bKept = true;
				break;
			}
		}
		if (bKept)
		{
			Item.Status = MCPHygStatusSkipped;
			Item.Error.Reset();
			Plan.Add(MoveTemp(Item));
			continue;
		}

		// ── The fix-specific plan ──────────────────────────────────────────
		if (Fix == TEXT("naming"))
		{
			const FMCPHygNamingRule* Rule = NamingRules.Find(Item.ClassName);
			if (!Rule)
			{
				Item.Status = MCPHygStatusSkipped;
				Plan.Add(MoveTemp(Item));
				continue;
			}
			const FString Suggested = MCPHygCompliantName(Item.AssetName, *Rule, AllPrefixes);
			if (Suggested.IsEmpty())
			{
				Item.Status = MCPHygStatusCompliant;
				Plan.Add(MoveTemp(Item));
				continue;
			}
			Item.NewPackagePath = FPaths::GetPath(Item.PackageName);
			Item.NewName = Suggested;
		}
		else
		{
			// Recomputed here rather than trusted from an audit. Between the
			// audit and this call somebody may have referenced the asset, and
			// removing it on the strength of a stale snapshot is the failure
			// mode this whole action exists to avoid.
			if (PrimaryAssetPackages.Contains(Data.PackageName))
			{
				Item.Status = MCPHygStatusSkipped;
				Item.Error = FString::Printf(
					TEXT("'%s' is registered with the Asset Manager as a primary asset, so it is loaded by id ")
						TEXT("and has no package referencer by design."),
					*Item.PackageName);
				Plan.Add(MoveTemp(Item));
				continue;
			}

			TArray<FString> Referencers;
			const int32 ReferencerCount = MCPHygRealReferencerCount(
				Registry, Data.PackageName, bIgnoreRedirectorReferencers, &Referencers);
			if (ReferencerCount > 0)
			{
				Item.Status = MCPHygStatusSkipped;
				Item.Error = FString::Printf(
					TEXT("'%s' has %d referencer(s) as of this call, starting with '%s', so it is not ")
						TEXT("unreferenced and was left alone."),
					*Item.PackageName, ReferencerCount, *Referencers[0]);
				Plan.Add(MoveTemp(Item));
				continue;
			}

			if (UnreferencedAction == TEXT("quarantine"))
			{
				// Keep the shape of the source path under the quarantine root,
				// so the move is reversible by construction and two assets with
				// the same name from different folders cannot collide.
				const FString SourceDir = FPaths::GetPath(Item.PackageName);
				FString Relative = SourceDir;
				if (Relative.StartsWith(TEXT("/"))) Relative.RightChopInline(1);
				Item.NewPackagePath = QuarantineFolder + TEXT("/") + Relative;
				Item.NewName = Item.AssetName;
			}
		}

		// ── Destination validation, shared by rename and quarantine ────────
		if (!Item.NewName.IsEmpty())
		{
			Item.DestinationPath = FString::Printf(
				TEXT("%s/%s.%s"), *Item.NewPackagePath, *Item.NewName, *Item.NewName);

			if (MCPIsProtectedAssetPath(Item.NewPackagePath))
			{
				Reject(Item, MCPHygStatusProtected, FString::Printf(
					TEXT("The destination '%s' is on a protected mount."), *Item.NewPackagePath));
				Plan.Add(MoveTemp(Item));
				continue;
			}
			if (ClaimedDestinations.Contains(Item.DestinationPath))
			{
				Reject(Item, MCPHygStatusCollision, FString::Printf(
					TEXT("Two assets in this batch both want to become '%s'. Rename one of them by hand ")
						TEXT("first, or narrow the batch."),
					*Item.DestinationPath));
				Plan.Add(MoveTemp(Item));
				continue;
			}
			if (UEditorAssetLibrary::DoesAssetExist(Item.DestinationPath))
			{
				Reject(Item, MCPHygStatusCollision, FString::Printf(
					TEXT("'%s' already exists, so '%s' cannot take that name. Rename the existing one, or ")
						TEXT("exclude this asset with keepPaths."),
					*Item.DestinationPath, *Item.PackageName));
				Plan.Add(MoveTemp(Item));
				continue;
			}
			ClaimedDestinations.Add(Item.DestinationPath);
		}

		if (Fix == TEXT("unreferenced") && UnreferencedAction == TEXT("delete"))
		{
			Item.DestinationPath.Reset();
		}

		Item.Status = MCPHygStatusOk;
		Item.bEligible = true;
		++EligibleCount;
		Plan.Add(MoveTemp(Item));
	}

	// ── The batch ceiling, before anything runs ─────────────────────────────
	if (EligibleCount > MaxFixes)
	{
		TSharedPtr<FJsonObject> Refusal = MakeShared<FJsonObject>();
		Refusal->SetBoolField(TEXT("success"), false);
		Refusal->SetStringField(TEXT("error"), FString::Printf(
			TEXT("This call would %s %d assets and maxFixes is %d, so nothing was done. Review the audit ")
				TEXT("first, then either narrow 'directories', pass the approved paths in 'assetPaths', or ")
				TEXT("raise 'maxFixes' deliberately."),
			Fix == TEXT("naming") ? TEXT("rename")
				: (UnreferencedAction == TEXT("delete") ? TEXT("delete") : TEXT("quarantine")),
			EligibleCount, MaxFixes));
		Refusal->SetStringField(TEXT("reason"), TEXT("batch_too_large"));
		Refusal->SetNumberField(TEXT("eligibleCount"), EligibleCount);
		Refusal->SetNumberField(TEXT("maxFixes"), MaxFixes);
		return MakeShared<FJsonValueObject>(Refusal);
	}

	// ── The preflight verdict ───────────────────────────────────────────────
	const bool bPreflightPassed = RejectedCount == 0;

	auto BuildItemArray = [&]() -> TArray<TSharedPtr<FJsonValue>>
	{
		TArray<TSharedPtr<FJsonValue>> Items;
		Items.Reserve(Plan.Num());
		for (const FMCPHygPlannedFix& Item : Plan)
		{
			// A caller that named its own paths gets a row for every one of
			// them. A caller that asked for a directory sweep does not want
			// 4,000 rows saying "this one was already fine", so those are
			// folded into the counts instead.
			if (!bExplicit)
			{
				if (Item.Status == MCPHygStatusCompliant) continue;
				if (Item.Status == MCPHygStatusSkipped && Item.Error.IsEmpty()) continue;
			}
			Items.Add(MakeShared<FJsonValueObject>(Item.ToJson()));
		}
		return Items;
	};

	auto DescribeRun = [&](const TSharedPtr<FJsonObject>& Out)
	{
		Out->SetStringField(TEXT("fix"), Fix);
		Out->SetArrayField(TEXT("directories"), MCPStringListToJson(Scope.Directories));
		Out->SetNumberField(TEXT("candidateCount"), Candidates.Num());
		if (NotFoundPaths.Num() > 0)
		{
			Out->SetArrayField(TEXT("notFoundPaths"), MCPStringListToJson(NotFoundPaths));
			Out->SetNumberField(TEXT("notFoundCount"), NotFoundPaths.Num());
		}
		Out->SetNumberField(TEXT("assetsInScope"), TotalInScope);
		Out->SetBoolField(TEXT("scanTruncated"), bTruncatedScan);
		Out->SetNumberField(TEXT("eligibleCount"), EligibleCount);
		Out->SetNumberField(TEXT("rejectedCount"), RejectedCount);
		Out->SetBoolField(TEXT("preflightPassed"), bPreflightPassed);
		Out->SetNumberField(TEXT("maxFixes"), MaxFixes);
		Out->SetBoolField(TEXT("continueOnError"), bContinueOnError);
		if (Fix == TEXT("unreferenced"))
		{
			Out->SetStringField(TEXT("unreferencedAction"), UnreferencedAction);
			if (UnreferencedAction == TEXT("quarantine"))
			{
				Out->SetStringField(TEXT("quarantineFolder"), QuarantineFolder);
			}
		}
		if (Fix == TEXT("naming"))
		{
			Out->SetStringField(TEXT("namingRuleMode"), NamingRuleMode);
			Out->SetNumberField(TEXT("ruleCount"), NamingRules.Num());
		}
	};

	if (!bPreflightPassed && !bContinueOnError)
	{
		TSharedPtr<FJsonObject> Refusal = MakeShared<FJsonObject>();
		Refusal->SetBoolField(TEXT("success"), false);
		Refusal->SetStringField(TEXT("error"), FString::Printf(
			TEXT("%d of %d candidates failed preflight, so nothing was changed. The first problem was: %s ")
				TEXT("Pass continueOnError=true to apply the %d that did pass and keep the rejects reported ")
				TEXT("alongside them."),
			RejectedCount, Plan.Num(), *FirstPreflightError, EligibleCount));
		Refusal->SetStringField(TEXT("reason"), TEXT("preflight_failed"));
		DescribeRun(Refusal);
		Refusal->SetArrayField(TEXT("items"), BuildItemArray());
		return MakeShared<FJsonValueObject>(Refusal);
	}

	// ── Dry run: say exactly what would happen, change nothing ──────────────
	if (bDryRun)
	{
		auto Preview = MCPSuccess();
		DescribeRun(Preview);
		Preview->SetBoolField(TEXT("dryRun"), true);
		Preview->SetBoolField(TEXT("changed"), false);
		Preview->SetArrayField(TEXT("items"), BuildItemArray());
		Preview->SetStringField(TEXT("note"), FString::Printf(
			TEXT("dryRun defaults to true on this action. %d asset(s) would be %s; every one of them is listed ")
				TEXT("above with its destination. Repeat with dryRun=false to apply."),
			EligibleCount,
			Fix == TEXT("naming") ? TEXT("renamed")
				: (UnreferencedAction == TEXT("delete") ? TEXT("deleted permanently") : TEXT("moved to quarantine"))));
		return MCPResult(Preview);
	}

	if (EligibleCount == 0)
	{
		auto Result = MCPSuccess();
		MCPSetExisted(Result);
		DescribeRun(Result);
		Result->SetBoolField(TEXT("dryRun"), false);
		Result->SetBoolField(TEXT("changed"), false);
		Result->SetArrayField(TEXT("items"), BuildItemArray());
		Result->SetStringField(TEXT("unchangedReason"), FString::Printf(
			TEXT("Nothing in scope needs the '%s' fix, so no asset was touched."), *Fix));
		return MCPResult(Result);
	}

	// ── Apply ───────────────────────────────────────────────────────────────
	auto Result = MCPSuccess();
	DescribeRun(Result);
	Result->SetBoolField(TEXT("dryRun"), false);

	int32 Applied = 0;
	int32 Failed = 0;
	TArray<TSharedPtr<FJsonValue>> RollbackRenames;

	if (Fix == TEXT("naming") || UnreferencedAction == TEXT("quarantine"))
	{
		// One IAssetTools::RenameAssets call for the whole batch: that is the
		// operation the Content Browser performs on a drag-move, and it
		// collapses every rename into a single transaction with one
		// redirector-fixup pass. Renaming one at a time forces a project-wide
		// reference update per asset and is what makes a large batch crash.
		TArray<FAssetRenameData> Batch;
		TArray<FMCPHygPlannedFix*> Batched;
		Batch.Reserve(EligibleCount);

		for (FMCPHygPlannedFix& Item : Plan)
		{
			if (!Item.bEligible) continue;
			Item.Asset = MCPLoadAssetObject(Item.AssetPath);
			if (!Item.Asset)
			{
				Item.Status = MCPHygStatusNotFound;
				Item.Error = FString::Printf(
					TEXT("'%s' passed preflight but could not be loaded, so it was not moved."),
					*Item.AssetPath);
				++Failed;
				continue;
			}
			Batch.Emplace(Item.Asset, Item.NewPackagePath, Item.NewName);
			Batched.Add(&Item);
		}

		if (Batch.Num() > 0)
		{
			IAssetTools& AssetTools =
				FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
			AssetTools.RenameAssets(Batch);

			int32 RedirectorsLeft = 0;
			TArray<FString> RedirectorPackages;

			for (FMCPHygPlannedFix* Item : Batched)
			{
				if (UEditorAssetLibrary::DoesAssetExist(Item->DestinationPath))
				{
					Item->Status = Fix == TEXT("naming") ? MCPHygStatusRenamed : MCPHygStatusMoved;
					++Applied;

					TSharedPtr<FJsonObject> Inverse = MakeShared<FJsonObject>();
					Inverse->SetStringField(TEXT("sourcePath"), Item->DestinationPath);
					Inverse->SetStringField(TEXT("destinationPath"), Item->AssetPath);
					RollbackRenames.Add(MakeShared<FJsonValueObject>(Inverse));

					// The rename leaves a redirector wherever a referencer was
					// not loaded. Silence about that is how a project ends up
					// with hundreds of stubs nobody expected (#908).
					const FMCPAssetPathForms SourceForms = MCPAssetPathForms(Item->AssetPath);
					bool bRedirectorLeft = false;
					TArray<FAssetData> AtOldPath;
					Registry.GetAssetsByPackageName(FName(*SourceForms.PackagePath), AtOldPath);
					for (const FAssetData& Data : AtOldPath)
					{
						if (Data.IsRedirector()) { bRedirectorLeft = true; break; }
					}
					if (!bRedirectorLeft
						&& FindObject<UObjectRedirector>(nullptr, *SourceForms.ObjectPath) != nullptr)
					{
						bRedirectorLeft = true;
					}
					if (bRedirectorLeft)
					{
						++RedirectorsLeft;
						RedirectorPackages.Add(SourceForms.PackagePath);
					}
				}
				else
				{
					Item->Status = MCPHygStatusFailed;
					Item->Error = FString::Printf(
						TEXT("The batch rename did not land '%s' at '%s'."),
						*Item->AssetPath, *Item->DestinationPath);
					++Failed;
				}
			}

			Result->SetNumberField(TEXT("redirectorsLeft"), RedirectorsLeft);
			Result->SetArrayField(TEXT("redirectorPackages"), MCPStringListToJson(RedirectorPackages));
			if (RedirectorsLeft > 0)
			{
				Result->SetStringField(TEXT("redirectorNote"), FString::Printf(
					TEXT("%d of %d moved assets left an ObjectRedirector at the old path, still pointed at by ")
						TEXT("packages this call did not load. Pass redirectorPackages to ")
						TEXT("asset(fixup_redirectors) to load exactly those referencers, rewrite them, and ")
						TEXT("delete the stubs that come out unreferenced."),
					RedirectorsLeft, Applied));
			}
		}
	}
	else
	{
		// Permanent delete. Every asset here was re-verified as unreferenced a
		// few lines above, and force is used because that check has already
		// been made properly; UEditorAssetLibrary::DeleteAsset does none of its
		// own.
		for (FMCPHygPlannedFix& Item : Plan)
		{
			if (!Item.bEligible) continue;
			if (UEditorAssetLibrary::DeleteAsset(Item.AssetPath))
			{
				Item.Status = MCPHygStatusDeleted;
				++Applied;
			}
			else
			{
				Item.Status = MCPHygStatusFailed;
				Item.Error = FString::Printf(
					TEXT("The editor refused to delete '%s'. It may be open in an asset editor, its package ")
						TEXT("may be read-only on disk, or it may have unsaved changes. asset(delete, ")
						TEXT("force=true) reports the specific reason."),
					*Item.AssetPath);
				++Failed;
			}
		}
	}

	// IAssetTools::RenameAssets moves the objects and rewrites the referencers it
	// loaded, but a package left dirty in memory is a change that disappears at
	// the next editor start, which reads as a rename that silently did not take.
	int32 SavedCount = 0;
	int32 SaveFailedCount = 0;
	if (bSave && Applied > 0)
	{
		for (const FMCPHygPlannedFix& Item : Plan)
		{
			if (Item.Status != MCPHygStatusRenamed && Item.Status != MCPHygStatusMoved) continue;
			UObject* Moved = MCPLoadAssetObject(Item.DestinationPath);
			if (!Moved) { ++SaveFailedCount; continue; }
			FString SaveReason;
			if (SaveAssetPackageChecked(Moved, SaveReason)) ++SavedCount; else ++SaveFailedCount;
		}
		Result->SetNumberField(TEXT("savedCount"), SavedCount);
		Result->SetNumberField(TEXT("saveFailedCount"), SaveFailedCount);
		if (SaveFailedCount > 0)
		{
			Result->SetStringField(TEXT("saveNote"), FString::Printf(
				TEXT("%d moved package(s) are still dirty in memory. Call asset(save_all_dirty) to flush ")
					TEXT("them, or they are lost at the next editor start."),
				SaveFailedCount));
		}
	}
	else if (!bSave && Applied > 0)
	{
		Result->SetBoolField(TEXT("saved"), false);
		Result->SetStringField(TEXT("saveSkippedReason"),
			TEXT("save=false was requested, so the moved packages are dirty in memory only. Call ")
				TEXT("asset(save_all_dirty) before closing the editor."));
	}

	Result->SetNumberField(TEXT("appliedCount"), Applied);
	Result->SetNumberField(TEXT("failedCount"), Failed);
	Result->SetBoolField(TEXT("changed"), Applied > 0);
	Result->SetArrayField(TEXT("items"), BuildItemArray());
	if (Applied > 0) MCPSetUpdated(Result);
	if (Failed > 0)
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), FString::Printf(
			TEXT("%d of %d eligible assets could not be changed; see items[] for each one. The %d that did ")
				TEXT("change are reported there too."),
			Failed, EligibleCount, Applied));
	}

	// ── Rollback ────────────────────────────────────────────────────────────
	if (RollbackRenames.Num() > 0)
	{
		// A move has an exact inverse: move it back. This is the whole reason
		// quarantine is the default for unreferenced assets.
		Result->SetBoolField(TEXT("rollbackAvailable"), true);
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetArrayField(TEXT("renames"), RollbackRenames);
		MCPSetRollback(Result, TEXT("bulk_rename_assets"), Payload);
	}
	else if (Applied > 0)
	{
		Result->SetBoolField(TEXT("rollbackAvailable"), false);
		Result->SetStringField(TEXT("rollbackUnavailableReason"), FString::Printf(
			TEXT("unreferencedAction='delete' removed %d asset(s) permanently and no call brings them back; ")
				TEXT("recover them from source control. unreferencedAction='quarantine' (the default) moves ")
				TEXT("them instead and emits an exact inverse rename."),
			Applied));
	}

	return MCPResult(Result);
}
