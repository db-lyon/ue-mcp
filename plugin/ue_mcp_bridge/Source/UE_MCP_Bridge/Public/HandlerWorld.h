#pragma once

// Which world a handler acts on: the editor world, a play-in-editor world by
// instance, and the refusal an editor-world action gives while play is running.

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"
#include "UnrealEngine.h"
#include "FileHelpers.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Templates/UniquePtr.h"
#include "HandlerResult.h"
#include "HandlerParams.h"

/** Is a play-in-editor or simulate session running right now?
 *
 *  The editor's asset loader refuses every call in play mode and logs the
 *  reason where a bridge caller cannot see it (#1065). */
inline bool MCPIsPlayInEditorActive()
{
	return GEditor && (GEditor->PlayWorld != nullptr || GEditor->bIsSimulatingInEditor);
}

/** Appended to a load failure while PIE is running, empty otherwise. One
 *  wording, shared by every resolver. */
inline FString MCPPlayInEditorLoadNote()
{
	if (!MCPIsPlayInEditorActive()) return FString();
	return TEXT(" A play-in-editor session is running, which changes how assets load: ")
		TEXT("the editor's own asset loader refuses every call while in play mode. ")
		TEXT("Stop play with editor(action=\"play_in_editor\", pieAction=\"stop\") and retry before treating this as an asset problem.");
}

/** A precondition refusal for an editor-world action while PIE is running,
 *  nullptr otherwise. Without it the load fails as "not found" (#1098). */
inline TSharedPtr<FJsonValue> MCPRefuseDuringPlayInEditor(const TCHAR* ActionName)
{
	if (!MCPIsPlayInEditorActive()) return nullptr;
	return MCPError(FString::Printf(
		TEXT("%s cannot run while a play-in-editor session is running: the editor's asset loader refuses every call in play mode, ")
		TEXT("so the sequence would read as missing and captures would not follow the playhead. ")
		TEXT("Stop play with editor(action=\"play_in_editor\", pieAction=\"stop\") and retry."),
		ActionName));
}

/** Get the editor world, or nullptr if not available. */
inline UWorld* GetEditorWorld()
{
	if (!GEditor) return nullptr;
	return GEditor->GetEditorWorldContext().World();
}

/** A level path as its package name: "/Game/Maps/L_X.L_X" and "/Game/Maps/L_X" both give "/Game/Maps/L_X". */
inline FString MCPMapPackageName(FString Map)
{
	Map.TrimStartAndEndInline();
	if (int32 Dot = INDEX_NONE; Map.FindChar(TEXT('.'), Dot))
	{
		Map.LeftInline(Dot);
	}
	return Map;
}

/** Does Map name a level asset on disk? An empty error means yes. */
inline FString MCPCheckMapExists(const FString& Map)
{
	const FString Package = MCPMapPackageName(Map);
	FString File;
	if (Package.IsEmpty() || !FPackageName::TryConvertLongPackageNameToFilename(Package, File, FPackageName::GetMapPackageExtension()) || !FPaths::FileExists(File))
	{
		return FString::Printf(TEXT("map %s does not exist"), *Package);
	}
	return FString();
}

/** Opens Map in the editor unless it is already the editor world. Never prompts:
 *  unsaved changes in the open level, or a running PIE session, are errors. */
inline bool MCPLoadMapIfNeeded(const FString& InMap, FString& OutError)
{
	const FString Map = MCPMapPackageName(InMap);
	UWorld* EditorWorld = GetEditorWorld();
	UPackage* Current = EditorWorld ? EditorWorld->GetOutermost() : nullptr;
	if (Current && Current->GetName() == Map)
	{
		return true;
	}
	if (MCPIsPlayInEditorActive())
	{
		OutError = FString::Printf(TEXT("play-in-editor is running in %s; stop it so the run can open %s"),
			Current ? *Current->GetName() : TEXT("another level"), *Map);
		return false;
	}
	if (Current && Current->IsDirty())
	{
		OutError = FString::Printf(TEXT("map %s has unsaved changes; save or discard them before a run opens %s"), *Current->GetName(), *Map);
		return false;
	}
	OutError = MCPCheckMapExists(Map);
	if (!OutError.IsEmpty())
	{
		return false;
	}
	FString File;
	FPackageName::TryConvertLongPackageNameToFilename(Map, File, FPackageName::GetMapPackageExtension());
	if (!FEditorFileUtils::LoadMap(File, /*LoadAsTemplate*/ false, /*bShowProgress*/ false))
	{
		OutError = FString::Printf(TEXT("map %s failed to load"), *Map);
		return false;
	}
	return true;
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
	MCPNoteParamRead(Params, TEXT("pieInstance"));
	if (Params.IsValid() && Params->TryGetNumberField(TEXT("pieInstance"), Raw))
	{
		PIEInstance = FMath::RoundToInt(Raw);
	}
	return ResolveWorldScope(Scope, PIEInstance);
}

/** A world's kind as the short string responses report: editor, pie, game,
 *  editorPreview, gamePreview, inactive or none. */
inline const TCHAR* MCPDescribeWorldType(const UWorld* World)
{
	if (!World) return TEXT("none");
	switch (World->WorldType)
	{
		case EWorldType::Editor:        return TEXT("editor");
		case EWorldType::PIE:           return TEXT("pie");
		case EWorldType::Game:          return TEXT("game");
		case EWorldType::EditorPreview: return TEXT("editorPreview");
		case EWorldType::GamePreview:   return TEXT("gamePreview");
		case EWorldType::Inactive:      return TEXT("inactive");
		default:                        return TEXT("none");
	}
}

/** The play-in-editor world Object lives in, or nullptr for an editor-world or unowned object. */
inline UWorld* MCPPlayWorldOf(const UObject* Object)
{
	UWorld* World = Object ? Object->GetWorld() : nullptr;
	return (World && World->IsPlayInEditor()) ? World : nullptr;
}

/**
 * Runs the enclosed code as the PIE instance that owns World: GWorld and the
 * PIE id point at that instance, the way the engine switches them for script
 * running in it. A no-op for an editor world or when World is null.
 */
class FMCPScopedPlayWorldContext : public FNoncopyable
{
public:
	explicit FMCPScopedPlayWorldContext(UWorld* World)
	{
		if (!World || !World->IsPlayInEditor() || !GEngine) return;
		const FWorldContext* Context = GEngine->GetWorldContextFromWorld(World);
		if (!Context) return;
		PieInstance = Context->PIEInstance;
		IdOverride = MakeUnique<FTemporaryPlayInEditorIDOverride>(Context->PIEInstance);
		Switcher = MakeUnique<FScopedConditionalWorldSwitcher>(World);
	}

	/** The PIE instance the scope switched to, or INDEX_NONE when it did nothing. */
	int32 GetPieInstance() const { return PieInstance; }

private:
	int32 PieInstance = INDEX_NONE;
	// Declared in construction order so the world is restored before the id.
	TUniquePtr<FTemporaryPlayInEditorIDOverride> IdOverride;
	TUniquePtr<FScopedConditionalWorldSwitcher> Switcher;
};

/**
 * Tear down a world a test or a handler created for its own use (#1488).
 *
 * DestroyWorld leaves the world's package dirty and the world alive, so a
 * /Temp/Untitled_N package lands on the editor's dirty world list, level(load)
 * refuses and shutdown prompts to save it. This marks the world garbage and
 * clears its package and its actors' external packages, as ending PIE does.
 */
inline void MCPDestroyDisposableWorld(UWorld* World, bool bInformEngineOfWorld = false)
{
	if (!World) return;
	TArray<TWeakObjectPtr<UPackage>> Packages;
	Packages.Add(World->GetPackage());
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (UPackage* External = It->GetExternalPackage()) Packages.AddUnique(External);
	}
	World->DestroyWorld(bInformEngineOfWorld);
	if (World->IsRooted()) World->RemoveFromRoot();
	World->MarkObjectsPendingKill();
	for (const TWeakObjectPtr<UPackage>& Package : Packages)
	{
		if (Package.IsValid() && Package.Get() != GetTransientPackage()) Package->SetDirtyFlag(false);
	}
}

/** Is World the current world of any engine world context (the editor world, a PIE or preview world)? */
inline bool MCPIsLiveWorld(const UWorld* World)
{
	if (!World || !GEngine) return false;
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		if (Context.World() == World) return true;
	}
	return false;
}

/** Names of the packages on the editor's dirty world list. */
inline TSet<FName> MCPDirtyWorldPackageNames()
{
	TArray<UPackage*> Dirty;
	FEditorFileUtils::GetDirtyWorldPackages(Dirty);
	TSet<FName> Names;
	for (const UPackage* Package : Dirty)
	{
		if (Package) Names.Add(Package->GetFName());
	}
	return Names;
}

/**
 * Clear the dirty flag on /Temp world packages that became dirty after Before
 * was taken and whose world no context is using: what a test leaves when it
 * tears its world down without MCPDestroyDisposableWorld (#1488). Nothing is
 * saved or deleted. Returns the cleared package names.
 */
inline TArray<FString> MCPSweepAbandonedTempWorlds(const TSet<FName>& Before)
{
	TArray<FString> Cleared;
	TArray<UPackage*> Dirty;
	FEditorFileUtils::GetDirtyWorldPackages(Dirty);
	for (UPackage* Package : Dirty)
	{
		if (!Package || Before.Contains(Package->GetFName())) continue;
		const FString Name = Package->GetName();
		if (!Name.StartsWith(TEXT("/Temp/"))) continue;
		bool bLive = false;
		ForEachObjectWithPackage(Package, [&bLive](UObject* Object)
		{
			const UWorld* Owner = Object ? (Object->IsA<UWorld>() ? Cast<UWorld>(Object) : Object->GetTypedOuter<UWorld>()) : nullptr;
			if (MCPIsLiveWorld(Owner)) { bLive = true; return false; }
			return true;
		});
		if (bLive) continue;
		Package->SetDirtyFlag(false);
		Cleared.Add(Name);
	}
	return Cleared;
}

/** Get the editor world or return an error response. */
#define REQUIRE_EDITOR_WORLD(WorldVar) \
	UWorld* WorldVar = GetEditorWorld(); \
	if (!WorldVar) return MCPError(TEXT("Editor world not available"));
