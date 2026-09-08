// Split from EditorHandlers.cpp to keep that file under 3k lines.
// All functions below are still members of FEditorHandlers - this file is a
// translation-unit partition, not a new class. Handler registration
// stays in EditorHandlers.cpp::RegisterHandlers.

#include "EditorHandlers.h"
#include "UE_MCP_BridgeModule.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"

#include "UObject/GCObjectScopeGuard.h"
#include "HandlerFunctionCall.h"
#include "HandlerJsonProperty.h"
#include "JsonSerializer.h"
#include "Containers/Ticker.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Engine/Blueprint.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Modules/ModuleManager.h"
#include "UObject/Script.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"
#include "UObject/Package.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Settings/LevelEditorPlaySettings.h"

namespace
{
	static FDelegateHandle IgnoreErrorsPostPIEHandle;
	static FDelegateHandle IgnoreErrorsEndPIEHandle;
	static FTSTicker::FDelegateHandle IgnoreErrorsTimeoutHandle;
	static bool bIgnoreErrorsBypassArmed = false;
	static TArray<TWeakObjectPtr<UBlueprint>> SuppressedBlueprintWarnings;

	static void RestoreIgnoreBlueprintErrorsState(bool bFromTimeoutTicker = false)
	{
		if (!bIgnoreErrorsBypassArmed)
		{
			return;
		}

		for (const TWeakObjectPtr<UBlueprint>& Blueprint : SuppressedBlueprintWarnings)
		{
			if (Blueprint.IsValid())
			{
				Blueprint->bDisplayCompilePIEWarning = true;
			}
		}
		SuppressedBlueprintWarnings.Reset();
		bIgnoreErrorsBypassArmed = false;

		if (IgnoreErrorsPostPIEHandle.IsValid())
		{
			FEditorDelegates::PostPIEStarted.Remove(IgnoreErrorsPostPIEHandle);
			IgnoreErrorsPostPIEHandle.Reset();
		}
		if (IgnoreErrorsEndPIEHandle.IsValid())
		{
			FEditorDelegates::EndPIE.Remove(IgnoreErrorsEndPIEHandle);
			IgnoreErrorsEndPIEHandle.Reset();
		}
		if (!bFromTimeoutTicker && IgnoreErrorsTimeoutHandle.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(IgnoreErrorsTimeoutHandle);
		}
		IgnoreErrorsTimeoutHandle.Reset();
	}

	static void ArmIgnoreBlueprintErrorsForNextPIELaunch(const TArray<UBlueprint*>& Blueprints)
	{
		SuppressedBlueprintWarnings.Reset(Blueprints.Num());
		for (UBlueprint* Blueprint : Blueprints)
		{
			Blueprint->bDisplayCompilePIEWarning = false;
			SuppressedBlueprintWarnings.Add(Blueprint);
		}
		bIgnoreErrorsBypassArmed = true;

		IgnoreErrorsPostPIEHandle = FEditorDelegates::PostPIEStarted.AddLambda(
			[](bool) { RestoreIgnoreBlueprintErrorsState(); });
		IgnoreErrorsEndPIEHandle = FEditorDelegates::EndPIE.AddLambda(
			[](bool) { RestoreIgnoreBlueprintErrorsState(); });

		const double Deadline = FPlatformTime::Seconds() + 30.0;
		IgnoreErrorsTimeoutHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateLambda([Deadline](float)
			{
				if (FPlatformTime::Seconds() < Deadline)
				{
					return true;
				}
				RestoreIgnoreBlueprintErrorsState(true);
				return false;
			}));
	}

	// The server decides *whether* a caller may bypass the Blueprint-error
	// prompt (standing config opt-in, or a live approval prompt answered by
	// the user) and names the source here. The bridge cannot re-derive user
	// consent, so it records the source and refuses anything it does not
	// recognize rather than pretending to re-check it.
	static bool IsRecognizedBypassAuthorization(const FString& Source)
	{
		return Source == TEXT("user_approval") || Source == TEXT("config");
	}

	static ULevelEditorPlaySettings* GetPlaySettingsForRW()
	{
		return GetMutableDefault<ULevelEditorPlaySettings>();
	}

	static bool SetIntPropOn(UObject* Obj, const TCHAR* Name, int32 NewVal)
	{
		FIntProperty* P = CastField<FIntProperty>(Obj->GetClass()->FindPropertyByName(FName(Name)));
		if (!P) return false;
		P->SetPropertyValue_InContainer(Obj, NewVal);
		return true;
	}
	static bool SetBoolPropOn(UObject* Obj, const TCHAR* Name, bool NewVal)
	{
		FBoolProperty* P = CastField<FBoolProperty>(Obj->GetClass()->FindPropertyByName(FName(Name)));
		if (!P) return false;
		P->SetPropertyValue_InContainer(Obj, NewVal);
		return true;
	}
	static bool SetEnumPropOn(UObject* Obj, const TCHAR* Name, int64 NewVal)
	{
		FProperty* Prop = Obj->GetClass()->FindPropertyByName(FName(Name));
		if (!Prop) return false;
		if (FEnumProperty* EP = CastField<FEnumProperty>(Prop))
		{
			EP->GetUnderlyingProperty()->SetIntPropertyValue(EP->ContainerPtrToValuePtr<void>(Obj), NewVal);
			return true;
		}
		if (FByteProperty* BP = CastField<FByteProperty>(Prop))
		{
			BP->SetPropertyValue_InContainer(Obj, static_cast<uint8>(NewVal));
			return true;
		}
		return false;
	}
	static int32 GetIntPropOn(const UObject* Obj, const TCHAR* Name, int32 Default = 0)
	{
		if (FIntProperty* P = CastField<FIntProperty>(Obj->GetClass()->FindPropertyByName(FName(Name))))
		{
			return P->GetPropertyValue_InContainer(Obj);
		}
		return Default;
	}
	static bool GetBoolPropOn(const UObject* Obj, const TCHAR* Name, bool Default = false)
	{
		if (FBoolProperty* P = CastField<FBoolProperty>(Obj->GetClass()->FindPropertyByName(FName(Name))))
		{
			return P->GetPropertyValue_InContainer(Obj);
		}
		return Default;
	}
	static int64 GetEnumPropOn(const UObject* Obj, const TCHAR* Name)
	{
		FProperty* Prop = Obj->GetClass()->FindPropertyByName(FName(Name));
		if (!Prop) return 0;
		if (FEnumProperty* EP = CastField<FEnumProperty>(Prop))
		{
			return EP->GetUnderlyingProperty()->GetSignedIntPropertyValue(EP->ContainerPtrToValuePtr<void>(Obj));
		}
		if (FByteProperty* BP = CastField<FByteProperty>(Prop))
		{
			return BP->GetPropertyValue_InContainer(Obj);
		}
		return 0;
	}

	static const TCHAR* NetModeNameFromValue(int64 V)
	{
		switch (V)
		{
		case static_cast<int64>(EPlayNetMode::PIE_ListenServer): return TEXT("ListenServer");
		case static_cast<int64>(EPlayNetMode::PIE_Client):       return TEXT("Client");
		default: return TEXT("Standalone");
		}
	}
}


TSharedPtr<FJsonValue> FEditorHandlers::PieControl(const TSharedPtr<FJsonObject>& Params)
{
	FString Action;
	if (auto Err = RequireString(Params, TEXT("action"), Action)) return Err;

	if (!GEditor)
	{
		return MCPError(TEXT("Editor not available"));
	}

	auto Result = MCPSuccess();

	if (Action == TEXT("status"))
	{
		bool bIsPlaying = (GEditor->PlayWorld != nullptr);
		Result->SetBoolField(TEXT("isPlaying"), bIsPlaying);
		Result->SetStringField(TEXT("action"), Action);
		// action=status only reads. Stated rather than left blank, because the
		// same handler mutates under the other two actions.
		Result->SetBoolField(TEXT("changed"), false);
		Result->SetBoolField(TEXT("rollbackPossible"), false);
	}
	else if (Action == TEXT("start"))
	{
		if (GEditor->PlayWorld != nullptr)
		{
			// No session was started, so the verdict is false. `alreadyRunning`
			// is the reason as a field rather than as prose: a caller has to
			// tell "there was nothing to start" from "the launch broke", and
			// that is the whole job the marker does. It does not turn the
			// answer into a yes. A flow step that expects this outcome says so
			// on itself with ignore_failure: true, which is the flow's decision
			// to make and not this handler's to hide.
			TSharedPtr<FJsonObject> Already = MakeShared<FJsonObject>();
			Already->SetBoolField(TEXT("success"), false);
			Already->SetStringField(TEXT("error"),
				TEXT("PIE session already active, so none was started. This call changed nothing; stop the session with pie_control(action=stop) before starting a different one."));
			Already->SetBoolField(TEXT("alreadyRunning"), true);
			Already->SetBoolField(TEXT("isPlaying"), true);
			Already->SetStringField(TEXT("action"), Action);
			Already->SetBoolField(TEXT("changed"), false);
			Already->SetBoolField(TEXT("rollbackPossible"), false);
			return MCPResult(Already);
		}

		// AssetRegistry must be done with its initial scan before PIE can start.
		// Cold editor starts spend 30-90s scanning; during that window
		// RequestPlaySession silently no-ops and the caller sees isPlaying=false
		// with no diagnostic (#406). Either wait it out (default) or return a
		// structured error if waitForAssetRegistry=false.
		FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		IAssetRegistry& Reg = ARM.Get();
		if (Reg.IsLoadingAssets())
		{
			bool bWait = true;
			Params->TryGetBoolField(TEXT("waitForAssetRegistry"), bWait);
			double TimeoutSec = 180.0;
			if (double Override; Params->TryGetNumberField(TEXT("assetRegistryTimeoutSeconds"), Override))
			{
				TimeoutSec = Override;
			}
			if (!bWait)
			{
				auto Err = MCPError(TEXT("AssetRegistry initial scan still in progress; PIE cannot start yet. Retry shortly or pass waitForAssetRegistry=true (default) to block until ready."));
				return Err;
			}
			const double Deadline = FPlatformTime::Seconds() + TimeoutSec;
			while (Reg.IsLoadingAssets() && FPlatformTime::Seconds() < Deadline)
			{
				FPlatformProcess::Sleep(0.25);
			}
			if (Reg.IsLoadingAssets())
			{
				return MCPError(FString::Printf(TEXT("AssetRegistry still loading after %.1fs; PIE start aborted. Pass assetRegistryTimeoutSeconds to extend the wait, or retry later."), TimeoutSec));
			}
			Result->SetBoolField(TEXT("waitedForAssetRegistry"), true);
		}

		bool bIgnoreBlueprintErrors = false;
		Params->TryGetBoolField(TEXT("ignoreBlueprintErrors"), bIgnoreBlueprintErrors);
		if (bIgnoreBlueprintErrors)
		{
			if (bIgnoreErrorsBypassArmed)
			{
				return MCPError(TEXT("A Blueprint-error bypass PIE launch is already pending"));
			}

			FString AuthorizationSource;
			Params->TryGetStringField(TEXT("authorizationSource"), AuthorizationSource);
			if (!IsRecognizedBypassAuthorization(AuthorizationSource))
			{
				return MCPError(TEXT("Blueprint-error bypass requires authorizationSource=user_approval (an approval prompt the user accepted) or authorizationSource=config (a standing ue-mcp.pie.allowIgnoreBlueprintErrors opt-in). Call editor(play_in_editor_ignore_blueprint_errors) rather than pie_control directly."));
			}

			TArray<UBlueprint*> BlueprintsToSuppress;
			TArray<TSharedPtr<FJsonValue>> ErroredBlueprints;
			TArray<TSharedPtr<FJsonValue>> MustCompileBlueprints;
			for (TObjectIterator<UBlueprint> It; It; ++It)
			{
				UBlueprint* Blueprint = *It;
				if (!IsValid(Blueprint))
				{
					continue;
				}

				// PIE recompiles every non-data Blueprint that IsPossiblyDirty()
				// reports, which covers BS_Unknown as well as BS_Dirty. Those
				// compiles can produce brand new errors after this handler has
				// already decided what to suppress, so refuse them instead of
				// arming a bypass that would not cover the result.
				const bool bWillCompileBecauseDirty =
					!FBlueprintEditorUtils::IsDataOnlyBlueprint(Blueprint)
					&& Blueprint->IsPossiblyDirty();
				const bool bErroredLevelBlueprint =
					FBlueprintEditorUtils::IsLevelScriptBlueprint(Blueprint)
					&& Blueprint->Status == BS_Error;
				if (bWillCompileBecauseDirty || bErroredLevelBlueprint)
				{
					MustCompileBlueprints.Add(MakeShared<FJsonValueString>(Blueprint->GetPathName()));
				}
				else if (Blueprint->Status == BS_Error && Blueprint->bDisplayCompilePIEWarning)
				{
					ErroredBlueprints.Add(MakeShared<FJsonValueString>(Blueprint->GetPathName()));
					BlueprintsToSuppress.Add(Blueprint);
				}
			}

			if (!MustCompileBlueprints.IsEmpty())
			{
				TSharedPtr<FJsonObject> Blocked = MakeShared<FJsonObject>();
				Blocked->SetBoolField(TEXT("success"), false);
				Blocked->SetBoolField(TEXT("blocked"), true);
				Blocked->SetStringField(TEXT("code"), TEXT("blueprints_require_compile"));
				Blocked->SetStringField(
					TEXT("error"),
					TEXT("Blueprint-error bypass only applies to already-compiled, saved Blueprint errors. Compile and save the listed dirty or errored Level Blueprints first."));
				Blocked->SetArrayField(TEXT("blueprints"), MustCompileBlueprints);
				return MCPResult(Blocked);
			}

			// Nothing errored: this is an ordinary PIE start. Arming anyway
			// would hold the single-bypass slot for 30s and claim a launch ran
			// on broken Blueprints when none exist.
			const bool bArmed = !BlueprintsToSuppress.IsEmpty();
			if (bArmed)
			{
				ArmIgnoreBlueprintErrorsForNextPIELaunch(BlueprintsToSuppress);
				UE_LOG(LogMCPBridge, Warning,
					TEXT("PIE starting with the Blueprint compiler-error prompt suppressed for %d Blueprint(s) (authorization: %s). PIE may run stale or invalid Blueprint bytecode."),
					BlueprintsToSuppress.Num(), *AuthorizationSource);
				for (const UBlueprint* Blueprint : BlueprintsToSuppress)
				{
					UE_LOG(LogMCPBridge, Warning, TEXT("  suppressed Blueprint error: %s"), *Blueprint->GetPathName());
				}
			}

			Result->SetBoolField(TEXT("ignoredBlueprintErrors"), bArmed);
			Result->SetNumberField(TEXT("ignoredBlueprintErrorCount"), BlueprintsToSuppress.Num());
			Result->SetStringField(TEXT("authorizationSource"), AuthorizationSource);
			Result->SetArrayField(TEXT("loadedErroredBlueprints"), ErroredBlueprints);
			Result->SetStringField(
				TEXT("warning"),
				bArmed
					? TEXT("PIE may run stale or invalid Blueprint bytecode. Suppressed warning flags are restored after this PIE launch is processed.")
					: TEXT("No loaded Blueprint was in an error state, so nothing was suppressed. This was an ordinary PIE start."));
		}
		else if (bIgnoreErrorsBypassArmed)
		{
			// An earlier bypass never reached PIE (the launch was refused
			// somewhere else) and its watchdog has not fired yet. Restore the
			// warning flags now so this ordinary start cannot silently inherit
			// a suppression nobody authorized it to use.
			RestoreIgnoreBlueprintErrorsState();
		}

		// Read before requesting. The guard above tests PlayWorld only, and the
		// request is deferred, so two starts in the same tick both pass it and
		// the second silently overwrites PlaySessionRequest. This is the signal
		// that makes the answer below true rather than assumed.
		const bool bAlreadyQueued = GEditor->IsPlaySessionRequestQueued();

		FRequestPlaySessionParams SessionParams;
		GEditor->RequestPlaySession(SessionParams);
		Result->SetStringField(TEXT("action"), Action);
		// A REQUEST: UEditorEngine::RequestPlaySession stores the params to be
		// acted on next tick, so PIE is not running yet and isPlaying still
		// answers false. Queuing over a request that was already queued
		// replaces it and starts nothing extra, which is a no-op worth saying.
		Result->SetBoolField(TEXT("changed"), !bAlreadyQueued);
		Result->SetBoolField(TEXT("alreadyQueued"), bAlreadyQueued);
		Result->SetBoolField(TEXT("isPlaying"), false);
		Result->SetBoolField(TEXT("playSessionRequested"), true);
		Result->SetStringField(TEXT("startNote"), bAlreadyQueued
			? TEXT("A play session was already queued and this call replaced that request rather than adding one. It begins on a later editor tick; poll pie_control(action=status) until isPlaying is true before doing anything that needs the PIE world.")
			: TEXT("The play session is queued and begins on a later editor tick. Poll pie_control(action=status) until isPlaying is true before doing anything that needs the PIE world."));

		// No rollback, and stopping is not one. The request is deferred, so at
		// the moment a rollback would run PlayWorld is still null and
		// pie_control(action=stop) reports alreadyStopped on exactly that - and
		// PIE then starts anyway a tick later, leaving the editor in play after
		// a rollback that reported it had finished. An inverse that can leave
		// the thing it was meant to undo running is worse than none, and the
		// idempotent report makes that reading MORE convincing, not less: the
		// undo would come back clean while play carried on.
		//
		// The queued request IS cancellable: UEditorEngine has
		// CancelRequestPlaySession, which drops it before the tick acts on it.
		// The bridge exposes no action that calls it, so there is no method
		// name to put in a rollback record. That is a one-line gap in the
		// surface, not an engine limitation.
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("Starting play is a deferred request. pie_control(action=stop) is not its inverse: at rollback time there is no session yet, so it reports alreadyStopped while the queued one starts regardless. The engine can drop a queued request through CancelRequestPlaySession, but no bridge action calls it, so no rollback can name one. Stop it deliberately once pie_control(action=status) reports isPlaying."));
	}
	else if (Action == TEXT("stop"))
	{
		if (GEditor->PlayWorld == nullptr)
		{
			// The mirror of the start above: nothing was ended, so the verdict
			// is false, and `alreadyStopped` says the reason was an absent
			// session rather than a stop that failed to take.
			TSharedPtr<FJsonObject> Already = MakeShared<FJsonObject>();
			Already->SetBoolField(TEXT("success"), false);
			Already->SetStringField(TEXT("error"),
				TEXT("No PIE session active, so nothing was asked to end. Play is already stopped."));
			Already->SetBoolField(TEXT("alreadyStopped"), true);
			Already->SetBoolField(TEXT("isPlaying"), false);
			Already->SetStringField(TEXT("action"), Action);
			Already->SetBoolField(TEXT("changed"), false);
			Already->SetBoolField(TEXT("rollbackPossible"), false);
			return MCPResult(Already);
		}

		GEditor->RequestEndPlayMap();
		Result->SetStringField(TEXT("action"), Action);
		// Stopping is refused above when no PlayWorld exists, so reaching here
		// always requested the end of a session that was running. Like the
		// start, this is queued: RequestEndPlayMap flags the tick loop and the
		// worlds are torn down on a later tick.
		Result->SetBoolField(TEXT("changed"), true);
		Result->SetBoolField(TEXT("endPlayRequested"), true);
		Result->SetStringField(TEXT("stopNote"),
			TEXT("The session ends on a later editor tick. Poll pie_control(action=status) until isPlaying is false before doing anything that needs the PIE world gone."));
		// No rollback. Starting PIE again is not the inverse of stopping it: it
		// is a fresh session from the map's saved state, with none of the world
		// the stop tore down. A rollback here would also run after play had
		// already ended, which is the point at which restarting is a new action
		// rather than an undo.
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("Ending play destroys the PIE worlds and everything spawned in them. pie_control(action=start) launches a NEW session from the saved map rather than restoring this one."));
	}
	else
	{
		return MCPError(FString::Printf(TEXT("Unknown action: %s. Expected 'status', 'start', or 'stop'"), *Action));
	}

	return MCPResult(Result);
}


TSharedPtr<FJsonValue> FEditorHandlers::PieGetRuntimeValue(const TSharedPtr<FJsonObject>& Params)
{
	if (!GEditor)
	{
		return MCPError(TEXT("Editor not available"));
	}

	// Check if PIE is active
	if (GEditor->PlayWorld == nullptr)
	{
		return MCPError(TEXT("PIE is not active. Start a PIE session first."));
	}

	// This action shipped before #983 with 'actorPath' as a label|name|path
	// token, not a strict path, and with 'actorLabel' as its fallback spelling.
	// The shared resolver treats a path as precise, so the loose reading is
	// kept here explicitly rather than silently dropped: a caller who has been
	// passing a label in 'actorPath' for two releases must not start getting
	// "no actor at that path".
	FString ActorPath;
	if (!Params->TryGetStringField(TEXT("actorPath"), ActorPath))
	{
		// Also accept actorLabel as a fallback
		if (!Params->TryGetStringField(TEXT("actorLabel"), ActorPath))
		{
			return MCPError(TEXT("Missing 'actorPath' parameter"));
		}
	}

	FString PropertyName;
	if (auto Err = RequireString(Params, TEXT("propertyName"), PropertyName)) return Err;

	// Search for the actor in the PIE world (accept label, name, or full path).
	// #778: honour pieInstance so a client world is reachable.
	// #983: through the shared resolver, so actorPath wins over actorLabel and
	// a duplicated label is refused rather than read off one of the copies.
	UWorld* PIEWorld = ResolveWorldFromParams(Params, TEXT("pie"));
	FMCPActorSelector PieSel;
	PieSel.Match = EMCPActorMatch::LabelNameOrPath;
	PieSel.WorldLabel = TEXT("PIE");
	TSharedPtr<FJsonValue> PieActorErr;
	AActor* TargetActor = MCPResolveActor(PIEWorld, Params, PieActorErr, PieSel);
	if (!TargetActor && MCPIsAmbiguousActorError(PieActorErr)) return PieActorErr;

	// The legacy tolerance: retry the value of 'actorPath' as a label / name
	// token. Ambiguity is still refused, so the loose reading cannot bring the
	// silent wrong pick back with it.
	if (!TargetActor && !ActorPath.IsEmpty())
	{
		TSharedPtr<FJsonValue> LegacyErr;
		TargetActor = MCPResolveActorToken(PIEWorld, ActorPath, LegacyErr, PieSel);
		if (!TargetActor && MCPIsAmbiguousActorError(LegacyErr)) return LegacyErr;
	}

	if (!TargetActor)
	{
		// Collect available actor names for the error message
		TArray<TSharedPtr<FJsonValue>> AvailableActors;
		int32 Count = 0;
		for (TActorIterator<AActor> It(PIEWorld); It && Count < 20; ++It, ++Count)
		{
			AvailableActors.Add(MakeShared<FJsonValueString>((*It)->GetActorLabel()));
		}
		TSharedPtr<FJsonObject> ErrResult = MakeShared<FJsonObject>();
		ErrResult->SetBoolField(TEXT("success"), false);
		ErrResult->SetStringField(TEXT("error"), FString::Printf(TEXT("Actor '%s' not found in PIE world"), *ActorPath));
		ErrResult->SetArrayField(TEXT("availableActors"), AvailableActors);
		return MCPResult(ErrResult);
	}

	// #344/#381: dotted-path resolution. propertyName "Inventory.Slots" or
	// "StatsComponent.CurrentHP" descends through component subobjects /
	// struct fields. Previously only flat property names worked, forcing
	// execute_python for every nested read on a UActorComponent subobject.
	TArray<FString> PathParts;
	PropertyName.ParseIntoArray(PathParts, TEXT("."));

	UStruct* CurrentStruct = TargetActor->GetClass();
	const void* CurrentContainer = TargetActor;
	FProperty* Property = nullptr;

	for (int32 i = 0; i < PathParts.Num(); ++i)
	{
		FProperty* Seg = CurrentStruct->FindPropertyByName(FName(*PathParts[i]));

		// Bare component name at the head of the path resolves against the
		// actor's component list when no property of that name matches - so
		// "StatsComponent.CurrentHP" reaches the component subobject without
		// the caller having to know the C++ UPROPERTY name.
		if (!Seg && i == 0)
		{
			UActorComponent* MatchedComp = nullptr;
			for (UActorComponent* Comp : TargetActor->GetComponents())
			{
				if (Comp && Comp->GetName() == PathParts[i]) { MatchedComp = Comp; break; }
			}
			if (MatchedComp)
			{
				CurrentContainer = MatchedComp;
				CurrentStruct = MatchedComp->GetClass();
				continue;
			}
		}

		if (!Seg)
		{
			return MCPError(FString::Printf(
				TEXT("Property '%s' not found at '%s' (segment %d)"),
				*PathParts[i], *PropertyName, i));
		}
		if (i < PathParts.Num() - 1)
		{
			if (FStructProperty* SP = CastField<FStructProperty>(Seg))
			{
				CurrentContainer = SP->ContainerPtrToValuePtr<void>(const_cast<void*>(CurrentContainer));
				CurrentStruct = SP->Struct;
			}
			else if (FObjectProperty* OP = CastField<FObjectProperty>(Seg))
			{
				UObject* SubObject = OP->GetObjectPropertyValue(
					OP->ContainerPtrToValuePtr<void>(const_cast<void*>(CurrentContainer)));
				if (!SubObject)
				{
					return MCPError(FString::Printf(
						TEXT("Cannot descend into '%s' - reference is null"), *PathParts[i]));
				}
				CurrentContainer = SubObject;
				CurrentStruct = SubObject->GetClass();
			}
			else
			{
				return MCPError(FString::Printf(
					TEXT("'%s' is not a struct or sub-object - cannot descend"), *PathParts[i]));
			}
		}
		else
		{
			Property = Seg;
		}
	}

	if (!Property)
	{
		return MCPError(FString::Printf(TEXT("Property path '%s' did not resolve to a leaf property"), *PropertyName));
	}

	// Read property value and serialize based on type
	auto Result = MCPSuccess();
	const void* PropertyValue = Property->ContainerPtrToValuePtr<void>(const_cast<void*>(CurrentContainer));

	if (FStrProperty* StrProp = CastField<FStrProperty>(Property))
	{
		FString Value = StrProp->GetPropertyValue(PropertyValue);
		Result->SetStringField(TEXT("value"), Value);
		Result->SetStringField(TEXT("type"), TEXT("String"));
	}
	else if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Property))
	{
		bool Value = BoolProp->GetPropertyValue(PropertyValue);
		Result->SetBoolField(TEXT("value"), Value);
		Result->SetStringField(TEXT("type"), TEXT("Bool"));
	}
	else if (FIntProperty* IntProp = CastField<FIntProperty>(Property))
	{
		int32 Value = IntProp->GetPropertyValue(PropertyValue);
		Result->SetNumberField(TEXT("value"), Value);
		Result->SetStringField(TEXT("type"), TEXT("Int"));
	}
	else if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Property))
	{
		float Value = FloatProp->GetPropertyValue(PropertyValue);
		Result->SetNumberField(TEXT("value"), Value);
		Result->SetStringField(TEXT("type"), TEXT("Float"));
	}
	else if (FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Property))
	{
		double Value = DoubleProp->GetPropertyValue(PropertyValue);
		Result->SetNumberField(TEXT("value"), Value);
		Result->SetStringField(TEXT("type"), TEXT("Double"));
	}
	else if (FNameProperty* NameProp = CastField<FNameProperty>(Property))
	{
		FName Value = NameProp->GetPropertyValue(PropertyValue);
		Result->SetStringField(TEXT("value"), Value.ToString());
		Result->SetStringField(TEXT("type"), TEXT("Name"));
	}
	else if (FTextProperty* TextProp = CastField<FTextProperty>(Property))
	{
		FText Value = TextProp->GetPropertyValue(PropertyValue);
		Result->SetStringField(TEXT("value"), Value.ToString());
		Result->SetStringField(TEXT("type"), TEXT("Text"));
	}
	else if (FStructProperty* StructProp = CastField<FStructProperty>(Property))
	{
		// Handle common struct types: FVector, FRotator, FLinearColor
		if (StructProp->Struct == TBaseStructure<FVector>::Get())
		{
			const FVector* Vec = reinterpret_cast<const FVector*>(PropertyValue);
			TSharedPtr<FJsonObject> VecObj = MakeShared<FJsonObject>();
			VecObj->SetNumberField(TEXT("x"), Vec->X);
			VecObj->SetNumberField(TEXT("y"), Vec->Y);
			VecObj->SetNumberField(TEXT("z"), Vec->Z);
			Result->SetObjectField(TEXT("value"), VecObj);
			Result->SetStringField(TEXT("type"), TEXT("Vector"));
		}
		else if (StructProp->Struct == TBaseStructure<FRotator>::Get())
		{
			const FRotator* Rot = reinterpret_cast<const FRotator*>(PropertyValue);
			TSharedPtr<FJsonObject> RotObj = MakeShared<FJsonObject>();
			RotObj->SetNumberField(TEXT("pitch"), Rot->Pitch);
			RotObj->SetNumberField(TEXT("yaw"), Rot->Yaw);
			RotObj->SetNumberField(TEXT("roll"), Rot->Roll);
			Result->SetObjectField(TEXT("value"), RotObj);
			Result->SetStringField(TEXT("type"), TEXT("Rotator"));
		}
		else if (StructProp->Struct == TBaseStructure<FLinearColor>::Get())
		{
			const FLinearColor* Color = reinterpret_cast<const FLinearColor*>(PropertyValue);
			TSharedPtr<FJsonObject> ColorObj = MakeShared<FJsonObject>();
			ColorObj->SetNumberField(TEXT("r"), Color->R);
			ColorObj->SetNumberField(TEXT("g"), Color->G);
			ColorObj->SetNumberField(TEXT("b"), Color->B);
			ColorObj->SetNumberField(TEXT("a"), Color->A);
			Result->SetObjectField(TEXT("value"), ColorObj);
			Result->SetStringField(TEXT("type"), TEXT("LinearColor"));
		}
		else
		{
			// Generic struct: export to string
			FString ExportedValue;
			Property->ExportTextItem_Direct(ExportedValue, PropertyValue, nullptr, nullptr, PPF_None);
			Result->SetStringField(TEXT("value"), ExportedValue);
			Result->SetStringField(TEXT("type"), StructProp->Struct->GetName());
		}
	}
	else
	{
		// Fallback: export property value as string
		FString ExportedValue;
		Property->ExportTextItem_Direct(ExportedValue, PropertyValue, nullptr, nullptr, PPF_None);
		Result->SetStringField(TEXT("value"), ExportedValue);
		Result->SetStringField(TEXT("type"), Property->GetCPPType());
	}

	Result->SetStringField(TEXT("actorPath"), ActorPath);
	Result->SetStringField(TEXT("propertyName"), PropertyName);
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// get_runtime_values -- bulk read across the active world. classFilter +
// paths array, with each path supporting UObject-pointer hops AND zero-arg
// BlueprintCallable getter dispatch (#414).
//
// Closes the recurring "walk every actor with class X, dump GetRequired() /
// GetSupplied() / IsPowered() off a sub-object" Python escape hatch.
// ---------------------------------------------------------------------------
namespace
{
	// Serialize a property value into a JSON value field. Returns false if the
	// property type is unsupported.
	static bool WritePropertyValue(TSharedPtr<FJsonObject> Out, const TCHAR* Field, FProperty* Prop, const void* ValuePtr)
	{
		if (!Prop || !ValuePtr) { Out->SetField(Field, MakeShared<FJsonValueNull>()); return true; }
		if (FStrProperty* SP = CastField<FStrProperty>(Prop))     { Out->SetStringField(Field, SP->GetPropertyValue(ValuePtr)); return true; }
		if (FBoolProperty* BP = CastField<FBoolProperty>(Prop))   { Out->SetBoolField(Field, BP->GetPropertyValue(ValuePtr)); return true; }
		if (FIntProperty* IP = CastField<FIntProperty>(Prop))     { Out->SetNumberField(Field, IP->GetPropertyValue(ValuePtr)); return true; }
		if (FFloatProperty* FP = CastField<FFloatProperty>(Prop)) { Out->SetNumberField(Field, FP->GetPropertyValue(ValuePtr)); return true; }
		if (FDoubleProperty* DP = CastField<FDoubleProperty>(Prop)) { Out->SetNumberField(Field, DP->GetPropertyValue(ValuePtr)); return true; }
		if (FNameProperty* NP = CastField<FNameProperty>(Prop))   { Out->SetStringField(Field, NP->GetPropertyValue(ValuePtr).ToString()); return true; }
		if (FTextProperty* TP = CastField<FTextProperty>(Prop))   { Out->SetStringField(Field, TP->GetPropertyValue(ValuePtr).ToString()); return true; }
		// #598: soft refs derive from FObjectPropertyBase but GetObjectPropertyValue
		// returns null when the target is not loaded - reporting a false "None".
		// Read the soft path string instead so unloaded soft refs report their path.
		if (FSoftObjectProperty* SoftObjP = CastField<FSoftObjectProperty>(Prop))
		{
			const FSoftObjectPtr& Ptr = SoftObjP->GetPropertyValue(ValuePtr);
			const FString PathStr = Ptr.ToString();
			if (PathStr.IsEmpty()) { Out->SetField(Field, MakeShared<FJsonValueNull>()); }
			else { Out->SetStringField(Field, PathStr); }
			return true;
		}
		if (FSoftClassProperty* SoftClsP = CastField<FSoftClassProperty>(Prop))
		{
			const FString PathStr = SoftClsP->GetPropertyValue(ValuePtr).ToString();
			if (PathStr.IsEmpty()) { Out->SetField(Field, MakeShared<FJsonValueNull>()); }
			else { Out->SetStringField(Field, PathStr); }
			return true;
		}
		if (FObjectPropertyBase* OP = CastField<FObjectPropertyBase>(Prop))
		{
			UObject* Obj = OP->GetObjectPropertyValue(ValuePtr);
			if (!Obj) { Out->SetField(Field, MakeShared<FJsonValueNull>()); return true; }
			Out->SetStringField(Field, Obj->GetPathName());
			return true;
		}
		if (FStructProperty* StP = CastField<FStructProperty>(Prop))
		{
			if (StP->Struct == TBaseStructure<FVector>::Get())
			{
				const FVector* V = reinterpret_cast<const FVector*>(ValuePtr);
				TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
				O->SetNumberField(TEXT("x"), V->X); O->SetNumberField(TEXT("y"), V->Y); O->SetNumberField(TEXT("z"), V->Z);
				Out->SetObjectField(Field, O); return true;
			}
		}
		// #885: a container is real JSON here too. The export-text fallback
		// below renders a TArray<FString> as an empty string, so a path whose
		// leaf is an array read back as though it held nothing.
		if (MCPFunctionCall::IsContainerProperty(Prop))
		{
			Out->SetField(Field, MCPFunctionCall::ValueToJson(Prop, ValuePtr, nullptr));
			return true;
		}
		FString Exported;
		Prop->ExportTextItem_Direct(Exported, ValuePtr, nullptr, nullptr, PPF_None);
		Out->SetStringField(Field, Exported);
		return true;
	}

	/**
	 * #969: evaluate a path segment that resolved to a UFUNCTION, binding the
	 * literal arguments the segment carried.
	 *
	 * A read-only accessor keyed by an id (a tally by option id, a balance by
	 * currency id, an attribute by tag) is a very common shape and used to be
	 * unreachable here: anything taking a parameter was refused, and the
	 * workaround was one invoke_object_function per key with an exact
	 * objectPath, which loses the multi-instance table this action exists for.
	 *
	 * The literals are coerced by MCPJsonProperty::SetJsonOnProperty, the same
	 * setter invoke_object_function's `args` go through. There is deliberately
	 * no second coercion: a string reads into an FName, an FString, an integer,
	 * a float, a bool or an enum there already, so a keyed accessor behaves the
	 * same whichever action reaches it.
	 */
	static bool CallPathGetter(
		UObject* Target,
		UFunction* Fn,
		const TArray<FString>& ArgLiterals,
		TSharedPtr<FJsonObject> Out,
		const TCHAR* FieldKey,
		FString& OutErr)
	{
		FProperty* RetProp = Fn->GetReturnProperty();
		if (!RetProp)
		{
			OutErr = FString::Printf(TEXT("UFUNCTION '%s' has no return value"), *Fn->GetName());
			return false;
		}

		// Inputs are the parameters a caller can supply: not the return, and not
		// a plain out param, which the function writes rather than reads.
		TArray<FProperty*> Inputs;
		for (TFieldIterator<FProperty> It(Fn); It && (It->PropertyFlags & CPF_Parm); ++It)
		{
			FProperty* Parm = *It;
			if (Parm->PropertyFlags & CPF_ReturnParm) continue;
			if ((Parm->PropertyFlags & CPF_OutParm) && !(Parm->PropertyFlags & CPF_ReferenceParm)) continue;
			Inputs.Add(Parm);
		}

		if (ArgLiterals.Num() != Inputs.Num())
		{
			TArray<FString> Signature;
			for (FProperty* Parm : Inputs)
			{
				Signature.Add(FString::Printf(TEXT("%s %s"), *Parm->GetCPPType(), *Parm->GetName()));
			}
			OutErr = FString::Printf(
				TEXT("UFUNCTION '%s' takes %d argument(s) (%s) but the path supplied %d. Write them into the path, e.g. '%s(key)'."),
				*Fn->GetName(), Inputs.Num(), *FString::Join(Signature, TEXT(", ")), ArgLiterals.Num(), *Fn->GetName());
			return false;
		}

		// ParmsSize, not PropertiesSize: a Blueprint function's locals live past
		// the parameter block and initialising them would run off the frame.
		TArray<uint8> Frame;
		Frame.SetNumZeroed(Fn->ParmsSize);
		for (TFieldIterator<FProperty> It(Fn); It && (It->PropertyFlags & CPF_Parm); ++It)
		{
			It->InitializeValue_InContainer(Frame.GetData());
		}

		bool bBound = true;
		for (int32 ArgIndex = 0; ArgIndex < Inputs.Num(); ++ArgIndex)
		{
			FProperty* Parm = Inputs[ArgIndex];
			const TSharedPtr<FJsonValue> AsJson = MakeShared<FJsonValueString>(ArgLiterals[ArgIndex]);
			FString BindErr;
			if (!MCPJsonProperty::SetJsonOnProperty(
					Parm, Parm->ContainerPtrToValuePtr<void>(Frame.GetData()), AsJson, BindErr))
			{
				OutErr = FString::Printf(
					TEXT("UFUNCTION '%s' argument '%s': %s"), *Fn->GetName(), *Parm->GetName(), *BindErr);
				bBound = false;
				break;
			}
		}

		if (bBound)
		{
			{
				// #806: without this guard an actor getter called against the
				// editor world is skipped and the zeroed frame reads back as a
				// real value.
				FEditorScriptExecutionGuard ScriptGuard;
				Target->ProcessEvent(Fn, Frame.GetData());
			}
			WritePropertyValue(Out, FieldKey, RetProp, RetProp->ContainerPtrToValuePtr<void>(Frame.GetData()));
		}

		MCPFunctionCall::DestroyFrame(Fn, Frame.GetData());
		return bBound;
	}

	// Walk one dotted path starting at Root. Per segment: property hop, sub-object
	// hop, or - at the leaf - a UFUNCTION call, which may carry literal arguments
	// as 'GetTallyWeight(overclock)'. Writes the result onto Out.
	static void ResolvePath(UObject* Root, const FString& Path, TSharedPtr<FJsonObject> Out, const TCHAR* FieldKey, FString& OutErr)
	{
		if (!Root) { OutErr = TEXT("null root"); return; }
		TArray<FString> Parts;
		// Split on dots that separate segments, not on a dot inside an argument
		// list: 'GetWeightAt(1.5)' is one segment, and splitting it would look
		// up a property named 'GetWeightAt(1'.
		MCPFunctionCall::SplitPathSegments(Path, Parts);
		if (Parts.Num() == 0) { OutErr = TEXT("empty path"); return; }

		UStruct* CurStruct = Root->GetClass();
		void* CurContainer = Root;
		UObject* CurObject = Root;

		for (int32 i = 0; i < Parts.Num(); ++i)
		{
			const FString& RawSeg = Parts[i];
			const bool bLast = (i == Parts.Num() - 1);

			// A segment may be 'Name' or 'Name(literal, literal)'. Reading the
			// argument list off the front makes the rest of this loop the same
			// as it always was.
			FString Seg;
			TArray<FString> CallArgs;
			bool bHasArgList = false;
			if (!MCPFunctionCall::ParseCallSegment(RawSeg, Seg, CallArgs, bHasArgList, OutErr))
			{
				return;
			}

			// An argument list says "call this", so no property or component of
			// that name is considered: silently reading a property while the
			// caller asked for a call would answer a question nobody asked.
			FProperty* Prop = bHasArgList ? nullptr : CurStruct->FindPropertyByName(FName(*Seg));

			if (bLast && Prop)
			{
				void* Val = Prop->ContainerPtrToValuePtr<void>(CurContainer);
				WritePropertyValue(Out, FieldKey, Prop, Val);
				return;
			}

			if (Prop)
			{
				if (FStructProperty* SP = CastField<FStructProperty>(Prop))
				{
					CurContainer = SP->ContainerPtrToValuePtr<void>(CurContainer);
					CurStruct = SP->Struct;
					continue;
				}
				if (FObjectProperty* OP = CastField<FObjectProperty>(Prop))
				{
					UObject* Sub = OP->GetObjectPropertyValue(OP->ContainerPtrToValuePtr<void>(CurContainer));
					if (!Sub) { OutErr = FString::Printf(TEXT("'%s' is null"), *Seg); return; }
					CurContainer = Sub;
					CurStruct = Sub->GetClass();
					CurObject = Sub;
					continue;
				}
				OutErr = FString::Printf(TEXT("'%s' is not a struct/sub-object - cannot descend"), *Seg);
				return;
			}

			// No property by that name. At the head of the path, try matching an
			// actor component by name (mirrors get_runtime_value behavior). At
			// any later segment OR the leaf, try a UFUNCTION call - that covers
			// GetRequired() / IsPowered() and, since #969, GetTally(overclock).
			if (i == 0 && !bHasArgList)
			{
				if (AActor* AsActor = Cast<AActor>(CurObject))
				{
					for (UActorComponent* Comp : AsActor->GetComponents())
					{
						if (Comp && Comp->GetName() == Seg)
						{
							CurContainer = Comp;
							CurStruct = Comp->GetClass();
							CurObject = Comp;
							goto NextSegment;
						}
					}
				}
			}

			// UFUNCTION getter at this segment, with or without arguments.
			if (UFunction* Fn = CurObject ? CurObject->FindFunction(FName(*Seg)) : nullptr)
			{
				if (!bLast)
				{
					OutErr = FString::Printf(TEXT("UFUNCTION '%s' must be the leaf segment - cannot descend into its return"), *Seg);
					return;
				}
				CallPathGetter(CurObject, Fn, CallArgs, Out, FieldKey, OutErr);
				return;
			}

			OutErr = FString::Printf(
				TEXT("Segment '%s' is neither a property, component, nor a UFUNCTION on %s. A UFUNCTION that takes arguments is written '%s(value)'."),
				*Seg, *CurStruct->GetName(), *Seg);
			return;

		NextSegment:;
		}
	}
}


TSharedPtr<FJsonValue> FEditorHandlers::GetRuntimeValues(const TSharedPtr<FJsonObject>& Params)
{
	if (!GEditor) return MCPError(TEXT("Editor not available"));

	const FString ClassFilter = OptionalString(Params, TEXT("classFilter"));
	const TArray<TSharedPtr<FJsonValue>>* PathsArr = nullptr;
	if (!Params->TryGetArrayField(TEXT("paths"), PathsArr) || !PathsArr || PathsArr->Num() == 0)
	{
		return MCPError(TEXT("Missing 'paths' (array of dotted property/function paths)"));
	}

	TArray<FString> Paths;
	for (const TSharedPtr<FJsonValue>& V : *PathsArr)
	{
		FString P;
		if (V->TryGetString(P) && !P.IsEmpty()) Paths.Add(P);
	}
	if (Paths.Num() == 0) return MCPError(TEXT("'paths' must contain at least one non-empty string"));

	// #778: was GEditor->PlayWorld, which is always the primary/server world.
	const FString WorldHint = OptionalString(Params, TEXT("world"), TEXT("auto"));
	UWorld* World = ResolveWorldFromParams(Params, *WorldHint);
	if (!World) World = (UWorld*)GEditor->GetEditorWorldContext().World();
	if (!World) return MCPError(TEXT("No world available"));

	TArray<TSharedPtr<FJsonValue>> Rows;
	int32 Matched = 0;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor) continue;

		// classFilter empty => every actor. #569: match by substring (not exact)
		// against actor class OR any component class, walking the parent-class
		// chain, so a filter like "Character" matches "BP_MyCharacter_C" and a
		// base-class filter matches subclasses. Exact match never hit PIE _C names.
		auto ClassMatches = [&ClassFilter](UClass* Cls) -> bool
		{
			if (ClassFilter.IsEmpty()) return true;
			for (UClass* C = Cls; C; C = C->GetSuperClass())
			{
				if (C->GetName().Contains(ClassFilter)) return true;
			}
			return false;
		};
		bool bActorMatch = ClassFilter.IsEmpty() || ClassMatches(Actor->GetClass());
		UActorComponent* ComponentMatch = nullptr;
		if (!bActorMatch)
		{
			for (UActorComponent* Comp : Actor->GetComponents())
			{
				if (Comp && ClassMatches(Comp->GetClass()))
				{
					ComponentMatch = Comp;
					bActorMatch = true;
					break;
				}
			}
		}
		if (!bActorMatch) continue;

		++Matched;
		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
		Row->SetStringField(TEXT("actorClass"), Actor->GetClass()->GetName());
		if (ComponentMatch)
		{
			Row->SetStringField(TEXT("componentName"), ComponentMatch->GetName());
			Row->SetStringField(TEXT("componentClass"), ComponentMatch->GetClass()->GetName());
		}

		TSharedPtr<FJsonObject> Values = MakeShared<FJsonObject>();
		TSharedPtr<FJsonObject> Errors = MakeShared<FJsonObject>();
		UObject* ResolveRoot = ComponentMatch ? (UObject*)ComponentMatch : (UObject*)Actor;
		for (const FString& Path : Paths)
		{
			FString Err;
			ResolvePath(ResolveRoot, Path, Values, *Path, Err);
			if (!Err.IsEmpty()) Errors->SetStringField(Path, Err);
		}
		Row->SetObjectField(TEXT("values"), Values);
		if (Errors->Values.Num() > 0) Row->SetObjectField(TEXT("errors"), Errors);
		Rows.Add(MakeShared<FJsonValueObject>(Row));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("world"), World == (UWorld*)GEditor->PlayWorld ? TEXT("pie") : TEXT("editor"));
	Result->SetStringField(TEXT("classFilter"), ClassFilter);
	Result->SetNumberField(TEXT("matched"), Matched);
	Result->SetArrayField(TEXT("rows"), Rows);
	return MCPResult(Result);
}


// #126: Fast-forward PIE game time. Raises WorldSettings dilation caps and calls SetGlobalTimeDilation.
TSharedPtr<FJsonValue> FEditorHandlers::SetPieTimeScale(const TSharedPtr<FJsonObject>& Params)
{
	double Factor = 1.0;
	if (!Params->TryGetNumberField(TEXT("factor"), Factor))
	{
		return MCPError(TEXT("Missing 'factor' (number) parameter"));
	}
	if (Factor <= 0.0)
	{
		return MCPError(TEXT("'factor' must be > 0"));
	}

	UWorld* World = GetPIEWorld();
	if (!World)
	{
		return MCPError(TEXT("No PIE/Game world active - start PIE first"));
	}

	AWorldSettings* WS = World->GetWorldSettings();
	if (!WS)
	{
		return MCPError(TEXT("WorldSettings not available on PIE world"));
	}

	// Read the dilation that is about to be replaced. This is the only value an
	// inverse call could restore, and it is unreadable once the write lands.
	const float PreviousFactor = UGameplayStatics::GetGlobalTimeDilation(World);
	const float PreviousMaxCap = WS->MaxGlobalTimeDilation;
	const float PreviousMinCap = WS->MinGlobalTimeDilation;

	// Raise dilation caps so Factor isn't clamped.
	const float CapHigh = FMath::Max(1000.0f, (float)Factor * 2.0f);
	WS->MaxGlobalTimeDilation = FMath::Max(WS->MaxGlobalTimeDilation, CapHigh);
	WS->MinGlobalTimeDilation = FMath::Min(WS->MinGlobalTimeDilation, 0.0001f);

	UGameplayStatics::SetGlobalTimeDilation(World, (float)Factor);

	auto Result = MCPSuccess();
	Result->SetNumberField(TEXT("factor"), Factor);
	Result->SetNumberField(TEXT("maxCap"), WS->MaxGlobalTimeDilation);
	Result->SetNumberField(TEXT("minCap"), WS->MinGlobalTimeDilation);
	Result->SetStringField(TEXT("world"), World->GetName());
	Result->SetNumberField(TEXT("previousFactor"), PreviousFactor);
	// SetGlobalTimeDilation clamps, so compare what the world holds now against
	// what it held before rather than against what was asked for.
	const float AppliedFactor = UGameplayStatics::GetGlobalTimeDilation(World);
	Result->SetNumberField(TEXT("appliedFactor"), AppliedFactor);
	const bool bChanged = !FMath::IsNearlyEqual(AppliedFactor, PreviousFactor)
		|| !FMath::IsNearlyEqual(PreviousMaxCap, WS->MaxGlobalTimeDilation)
		|| !FMath::IsNearlyEqual(PreviousMinCap, WS->MinGlobalTimeDilation);
	Result->SetBoolField(TEXT("changed"), bChanged);
	if (bChanged) MCPSetUpdated(Result);

	// Self-inverse: the same handler with the dilation this call replaced. Only
	// when that value is one this handler would accept back - it refuses a
	// non-positive factor, so a world already sitting at zero has no inverse
	// call rather than a rollback that would be rejected when replayed.
	if (PreviousFactor > 0.0f)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetNumberField(TEXT("factor"), PreviousFactor);
		MCPSetRollback(Result, TEXT("set_pie_time_scale"), Payload);
		// The caps are not part of the inverse: this handler only ever widens
		// them, and it takes no parameter that would narrow them back.
		const bool bCapsWidened = !FMath::IsNearlyEqual(PreviousMaxCap, WS->MaxGlobalTimeDilation)
			|| !FMath::IsNearlyEqual(PreviousMinCap, WS->MinGlobalTimeDilation);
		Result->SetBoolField(TEXT("rollbackLossy"), true);
		Result->SetStringField(TEXT("rollbackNote"), bCapsWidened
			? TEXT("Restores the previous time dilation only. The Min/Max GlobalTimeDilation caps this call widened on WorldSettings stay widened, because this handler has no parameter that narrows them. The rollback also needs the same PIE session to still be running: once play ends the world it would target is gone and the call reports that instead.")
			: TEXT("Restores the previous time dilation only. The rollback needs the same PIE session to still be running: once play ends the world it would target is gone and the call reports that instead."));
	}
	else
	{
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
			TEXT("The world's time dilation before this call was %g, and this handler refuses a factor that is not greater than zero. Replaying it with that value would be rejected, so no rollback is offered."),
			PreviousFactor));
	}
	return MCPResult(Result);
}

// ─── #148 capture_scene_png ────────────────────────────────────────
// Headless PNG screenshot via a reusable hidden SceneCapture2D actor.
// Works when the editor window is not focused (unlike viewport
// screenshots) and guarantees RGBA8 LDR PNG output suitable for image
// readers that reject HDR/EXR.


// #228/#229: PIE pawn lookup. Resolves the controlled pawn for a given
// player index against the live PIE world; PIE pawns spawn with runtime
// labels not addressable through the editor outliner.
TSharedPtr<FJsonValue> FEditorHandlers::GetPiePawn(const TSharedPtr<FJsonObject>& Params)
{
	if (!GEditor) return MCPError(TEXT("Editor not available"));

	FWorldContext* PieCtx = GEditor->GetPIEWorldContext();
	UWorld* PieWorld = PieCtx ? PieCtx->World() : nullptr;
	if (!PieWorld)
	{
		return MCPError(TEXT("PIE not running - start PIE before resolving the player pawn"));
	}

	const int32 PlayerIndex = OptionalInt(Params, TEXT("playerIndex"), 0);
	APlayerController* PC = UGameplayStatics::GetPlayerController(PieWorld, PlayerIndex);
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (!Pawn) return MCPError(FString::Printf(TEXT("No controlled pawn for player index %d"), PlayerIndex));

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), Pawn->GetActorLabel());
	Result->SetStringField(TEXT("actorName"), Pawn->GetName());
	Result->SetStringField(TEXT("class"), Pawn->GetClass()->GetPathName());
	Result->SetStringField(TEXT("path"), Pawn->GetPathName());
	const FVector L = Pawn->GetActorLocation();
	TSharedPtr<FJsonObject> LocO = MakeShared<FJsonObject>();
	LocO->SetNumberField(TEXT("x"), L.X);
	LocO->SetNumberField(TEXT("y"), L.Y);
	LocO->SetNumberField(TEXT("z"), L.Z);
	Result->SetObjectField(TEXT("location"), LocO);
	const FRotator R = Pawn->GetActorRotation();
	TSharedPtr<FJsonObject> RotO = MakeShared<FJsonObject>();
	RotO->SetNumberField(TEXT("pitch"), R.Pitch);
	RotO->SetNumberField(TEXT("yaw"), R.Yaw);
	RotO->SetNumberField(TEXT("roll"), R.Roll);
	Result->SetObjectField(TEXT("rotation"), RotO);
	return MCPResult(Result);
}

// #228/#229: invoke a BlueprintCallable / Exec UFUNCTION on a target.
// Target resolution: actorLabel against the chosen world (editor by
// default; world="pie" for PIE). The 'args' object maps parameter names
// to JSON values which are converted via FProperty ImportText. Out
// parameters and return values are read back via the same export path.


// #228/#229: invoke a BlueprintCallable / Exec UFUNCTION on a target.
// Target resolution: actorLabel against the chosen world (editor by
// default; world="pie" for PIE). The 'args' object maps parameter names
// to JSON values which are converted via FProperty ImportText. Out
// parameters and return values are read back via the same export path.
TSharedPtr<FJsonValue> FEditorHandlers::InvokeFunction(const TSharedPtr<FJsonObject>& Params)
{
	FString FunctionName;
	if (auto Err = RequireString(Params, TEXT("functionName"), FunctionName)) return Err;
	FString ActorLabel;
	if (auto Err = RequireStringAlt(Params, TEXT("actorLabel"), TEXT("actorPath"), ActorLabel)) return Err;

	// #778: this used GEditor->GetPIEWorldContext(), which is always the
	// primary (server) context, so 'pieInstance' could never reach it and a
	// client world was unaddressable. Route through the shared resolver.
	const FString WorldScope = OptionalString(Params, TEXT("world"), TEXT("editor")).ToLower();
	UWorld* World = ResolveWorldFromParams(Params, *WorldScope);
	if (!World)
	{
		return MCPError(WorldScope == TEXT("pie")
			? TEXT("PIE not running (or no such pieInstance) - cannot invoke against PIE world. See editor(list_pie_instances).")
			: TEXT("No editor world available"));
	}

	// Describe the world that was actually resolved, not the requested scope:
	// world="auto" resolves to PIE when a session is running.
	const FString WorldLabel = World->IsPlayInEditor() ? TEXT("PIE") : TEXT("editor");

	// #654: also match internal UObject name and full path so unlabeled actors
	// (e.g. AI controllers spawned at runtime, which have no editor label) can
	// be targeted, not just editor-labelled placed actors.
	// #806: the lookup walks the placed instances of the resolved world in a
	// fixed label -> name -> path order, and a miss is an error. There is no
	// class-default fallback here and there must never be one: a default object
	// answers every call with default state, which reads as success.
	// #983: actorPath wins when given, and a label that names more than one
	// actor is refused rather than invoked on whichever came first.
	FMCPActorSelector TargetSel;
	TargetSel.Match = EMCPActorMatch::LabelNameOrPath;
	TargetSel.WorldLabel = *WorldLabel;
	TSharedPtr<FJsonValue> TargetErr;
	AActor* Target = MCPResolveActor(World, Params, TargetErr, TargetSel);
	if (!Target) return TargetErr;
	// Defensive: the lookup iterates placed actors, so neither of these can fire
	// today. They exist so that a future change which lets an archetype or an
	// actor from another world through fails loudly instead of quietly
	// answering from default state, which is the failure this issue reported.
	if (Target->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
	{
		return MCPError(FString::Printf(
			TEXT("'%s' resolved to a class default object, not a placed actor. Refusing the call: a default object would answer from default state and report success."),
			*ActorLabel));
	}
	if (Target->GetWorld() != World)
	{
		return MCPError(FString::Printf(
			TEXT("'%s' resolved to an actor outside the %s world. Refusing the call."),
			*ActorLabel, *WorldLabel));
	}
	// Read now: the call below can destroy the actor, and the response says
	// which instance ran it.
	const FString ResolvedActorLabel = Target->GetActorLabel();
	const FString ResolvedActorPath = Target->GetPathName();

	// #382: optional `component` redirects the call target to a named subobject
	// (e.g. invoke `Server_Deconstruct` on the actor's BuildModeComponent).
	// Without this, RPCs / UFUNCTIONs that live on a component subobject can't
	// be reached via invoke_function and force execute_python.
	UObject* CallTarget = Target;
	FString ComponentName;
	if (Params->TryGetStringField(TEXT("component"), ComponentName) && !ComponentName.IsEmpty())
	{
		UActorComponent* FoundComp = nullptr;
		for (UActorComponent* Comp : Target->GetComponents())
		{
			if (Comp && Comp->GetName() == ComponentName) { FoundComp = Comp; break; }
		}
		if (!FoundComp)
		{
			TArray<FString> CompNames;
			for (UActorComponent* Comp : Target->GetComponents())
			{
				if (Comp) CompNames.Add(Comp->GetName());
			}
			return MCPError(FString::Printf(
				TEXT("Component '%s' not found on actor '%s'. Available: [%s]"),
				*ComponentName, *ActorLabel, *FString::Join(CompNames, TEXT(", "))));
		}
		CallTarget = FoundComp;
	}

	UFunction* Func = CallTarget->FindFunction(FName(*FunctionName));
	if (!Func) return MCPError(FString::Printf(TEXT("Function '%s' not found on %s"), *FunctionName, *CallTarget->GetClass()->GetName()));

	TArray<uint8> ParamBuf;
	ParamBuf.SetNumZeroed(Func->ParmsSize);
	// Initialise default param values
	for (TFieldIterator<FProperty> It(Func); It && (It->PropertyFlags & CPF_Parm); ++It)
	{
		It->InitializeValue_InContainer(ParamBuf.GetData());
	}

	const TSharedPtr<FJsonObject>* ArgObj = nullptr;
	Params->TryGetObjectField(TEXT("args"), ArgObj);

	// #383: optional actorArgs maps UObject* parameters to live actor labels in
	// the active world. LoadObject only resolves asset paths, so any actor-to-
	// actor RPC (Horse->ServerMount(PlayerCharacter)) previously had to be
	// done via execute_python. With actorArgs, the same call becomes:
	//   invoke_function(actorLabel="Horse_01", functionName="ServerMount",
	//                   actorArgs={Player: "PlayerCharacter_0"})
	const TSharedPtr<FJsonObject>* ActorArgObj = nullptr;
	Params->TryGetObjectField(TEXT("actorArgs"), ActorArgObj);

	if (ArgObj && (*ArgObj).IsValid())
	{
		for (TFieldIterator<FProperty> It(Func); It && (It->PropertyFlags & CPF_Parm); ++It)
		{
			FProperty* P = *It;
			if (P->PropertyFlags & CPF_ReturnParm) continue;
			if ((P->PropertyFlags & CPF_OutParm) && !(P->PropertyFlags & CPF_ReferenceParm)) continue;
			TSharedPtr<FJsonValue> Val = (*ArgObj)->TryGetField(P->GetName());
			if (!Val.IsValid()) continue;
			void* PtrAddr = P->ContainerPtrToValuePtr<void>(ParamBuf.GetData());
			FString E;
			if (!MCPJsonProperty::SetJsonOnProperty(P, PtrAddr, Val, E))
			{
				for (TFieldIterator<FProperty> CleanupIt(Func); CleanupIt && (CleanupIt->PropertyFlags & CPF_Parm); ++CleanupIt)
				{
					CleanupIt->DestroyValue_InContainer(ParamBuf.GetData());
				}
				return MCPError(FString::Printf(TEXT("Argument '%s': %s"), *P->GetName(), *E));
			}
		}
	}

	// Apply actor-label resolution AFTER plain args so explicit args[paramName]
	// stays authoritative when both are supplied. Walk every UObject* parm and
	// resolve the matching actorArgs entry.
	if (ActorArgObj && (*ActorArgObj).IsValid())
	{
		for (TFieldIterator<FProperty> It(Func); It && (It->PropertyFlags & CPF_Parm); ++It)
		{
			FProperty* P = *It;
			if (P->PropertyFlags & CPF_ReturnParm) continue;
			if ((P->PropertyFlags & CPF_OutParm) && !(P->PropertyFlags & CPF_ReferenceParm)) continue;

			FObjectProperty* OP = CastField<FObjectProperty>(P);
			if (!OP) continue;
			FString ActorArgLabel;
			if (!(*ActorArgObj)->TryGetStringField(P->GetName(), ActorArgLabel) || ActorArgLabel.IsEmpty())
			{
				continue;
			}

			// #983: an actor argument is an actor selector like any other, so a
			// duplicated label is refused rather than passed as whichever copy
			// the iterator reached first.
			TArray<AActor*> ArgMatches;
			MCPCollectActorsByToken(World, ActorArgLabel, EMCPActorMatch::LabelNameOrPath, ArgMatches);
			if (ArgMatches.Num() > 1)
			{
				for (TFieldIterator<FProperty> CleanupIt(Func); CleanupIt && (CleanupIt->PropertyFlags & CPF_Parm); ++CleanupIt)
				{
					CleanupIt->DestroyValue_InContainer(ParamBuf.GetData());
				}
				return MCPAmbiguousActorError(
					ActorArgLabel, TEXT("actorArgs"), TEXT("actorPath"),
					MCPDescribeActorMatchTier(ActorArgLabel, ArgMatches[0]), ArgMatches);
			}
			AActor* RefActor = ArgMatches.Num() == 1 ? ArgMatches[0] : nullptr;
			if (!RefActor)
			{
				for (TFieldIterator<FProperty> CleanupIt(Func); CleanupIt && (CleanupIt->PropertyFlags & CPF_Parm); ++CleanupIt)
				{
					CleanupIt->DestroyValue_InContainer(ParamBuf.GetData());
				}
				return MCPError(FString::Printf(
					TEXT("actorArgs[%s]: actor '%s' not found in %s world"),
					*P->GetName(), *ActorArgLabel, *WorldLabel));
			}
			if (!RefActor->IsA(OP->PropertyClass))
			{
				// Allow component look-up: if the property expects a UActorComponent
				// subclass, walk the actor's components and pick the first match.
				if (OP->PropertyClass->IsChildOf(UActorComponent::StaticClass()))
				{
					UActorComponent* MatchedComp = nullptr;
					for (UActorComponent* Comp : RefActor->GetComponents())
					{
						if (Comp && Comp->IsA(OP->PropertyClass)) { MatchedComp = Comp; break; }
					}
					if (MatchedComp)
					{
						OP->SetObjectPropertyValue(P->ContainerPtrToValuePtr<void>(ParamBuf.GetData()), MatchedComp);
						continue;
					}
				}
				for (TFieldIterator<FProperty> CleanupIt(Func); CleanupIt && (CleanupIt->PropertyFlags & CPF_Parm); ++CleanupIt)
				{
					CleanupIt->DestroyValue_InContainer(ParamBuf.GetData());
				}
				return MCPError(FString::Printf(
					TEXT("actorArgs[%s]: actor '%s' (%s) is not assignable to expected type %s"),
					*P->GetName(), *ActorArgLabel,
					*RefActor->GetClass()->GetName(), *OP->PropertyClass->GetName()));
			}
			OP->SetObjectPropertyValue(P->ContainerPtrToValuePtr<void>(ParamBuf.GetData()), RefActor);
		}
	}

	// ProcessEvent can run arbitrary game code, including code that tears down
	// the world and collects garbage, and CallTarget is read again below.
	FGCObjectScopeGuard CallTargetGuard(CallTarget);

	// #973: read the callspace BEFORE the guard opens. That is the only moment
	// it is observable: inside the guard GAllowActorScriptExecutionInEditor
	// makes AActor::GetFunctionCallspace answer Local in its first branch, so a
	// UFUNCTION(Server) runs its implementation on this copy instead of being
	// sent, and the result used to say nothing about it.
	FString NaturalCallspace;
	const bool bCallspaceForcedLocal =
		MCPFunctionCall::WouldForceNetCallspaceLocal(CallTarget, Func, NaturalCallspace);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	// #806: report the instance that ran the call. The reported defect was a
	// call that looked successful while answering from somewhere other than the
	// placed actor, and there was nothing in the response to see that with.
	Result->SetStringField(TEXT("resolvedActorLabel"), ResolvedActorLabel);
	Result->SetStringField(TEXT("resolvedActorPath"), ResolvedActorPath);
	Result->SetStringField(TEXT("world"), WorldLabel);
	if (!ComponentName.IsEmpty()) Result->SetStringField(TEXT("component"), ComponentName);
	Result->SetStringField(TEXT("functionName"), FunctionName);

	// Whether the call CAN have written anything is answerable, so answer it
	// rather than asserting a change. A pure or const UFUNCTION is declared to
	// have no side effects, and a deferred call against an editor world is one
	// this handler has already worked out will be skipped outright - the
	// warning below says exactly that, and claiming changed=true beside it
	// would be a marker contradicting its own response.
	const bool bDeferToNextTick = OptionalBool(Params, TEXT("deferToNextTick"), false);
	const bool bPureFunction = Func->HasAnyFunctionFlags(FUNC_BlueprintPure | FUNC_Const);
	const bool bWillBeSkipped = bDeferToNextTick && WorldLabel == TEXT("editor");
	Result->SetBoolField(TEXT("pure"), bPureFunction);
	Result->SetBoolField(TEXT("changed"), !bPureFunction && !bWillBeSkipped);
	if (bPureFunction)
	{
		Result->SetStringField(TEXT("idempotencyNote"),
			TEXT("The function is declared BlueprintPure or const, so it is a query: it reports a value and writes nothing. Calling it again returns the same answer for the same state."));
	}
	// Past purity the effect is opaque: a non-pure UFUNCTION is not required to
	// write anything, and nothing here compares before against after.
	Result->SetBoolField(TEXT("changeDetected"), false);
	// No rollback. What an arbitrary UFUNCTION did is opaque from here: nothing
	// was captured before the call, no inverse function is known, and a great
	// many of them (spawns, damage, sound, RPCs) have none. Inventing one would
	// hand the flow engine a call that could do anything.
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"), bPureFunction
		? TEXT("A pure or const function writes nothing, so there is nothing to undo.")
		: TEXT("A UFUNCTION call has no known inverse. Wrap the sequence in editor(begin_editor_transaction) and roll back with editor(cancel_editor_transaction) when the effects are transactional, or undo the effect with whatever call your project treats as its opposite."));

	// #973: the opt-in escape from the override above. Queued for the next
	// engine tick, the send happens after the guard's scope has ended and the
	// call routes the way it would from game code.
	if (bDeferToNextTick)
	{
		Result->SetBoolField(TEXT("deferred"), true);
		if (!NaturalCallspace.IsEmpty())
		{
			Result->SetStringField(TEXT("netCallspace"), NaturalCallspace);
		}
		Result->SetStringField(TEXT("note"), TEXT(
			"Queued for the next engine tick, outside the editor script-execution guard, so a replicated function "
			"routes through GetFunctionCallspace normally instead of being forced to Local. Return and out parameters "
			"are not reported: the response is written before the call runs. Read the effect back afterwards with "
			"editor(get_runtime_values) or editor(get_object_properties)."));
		if (WorldLabel == TEXT("editor"))
		{
			Result->SetStringField(TEXT("warning"), TEXT(
				"The target is in the editor world, whose actors were never initialised for play. Outside the guard "
				"AActor::ProcessEvent will skip the call entirely. deferToNextTick is for a live PIE session."));
		}
		MCPFunctionCall::DeferProcessEventToNextTick(CallTarget, Func, MoveTemp(ParamBuf));
		return MCPResult(Result);
	}

	// #806: AActor::ProcessEvent refuses to run script in a world whose actors
	// were never initialised for play, which is every editor world, unless the
	// function is marked CallInEditor. The refusal is silent: the parameter
	// frame stays zero-initialised and those zeros are then exported as if they
	// were the answer, so K2_GetActorLocation on a placed actor reported
	// (X=0,Y=0,Z=0) with success. FEditorScriptExecutionGuard opens the same
	// gate the editor itself uses for CallInEditor functions, for this call
	// only, so the placed instance actually runs the function.
	{
		FEditorScriptExecutionGuard ScriptGuard;
		CallTarget->ProcessEvent(Func, ParamBuf.GetData());
	}

	if (bCallspaceForcedLocal)
	{
		Result->SetStringField(TEXT("netCallspace"), NaturalCallspace);
		Result->SetBoolField(TEXT("callspaceForcedLocal"), true);
		Result->SetStringField(TEXT("warning"),
			MCPFunctionCall::DescribeForcedLocalCallspace(FunctionName, NaturalCallspace));
	}

	// #885: containers come back as real JSON. Export text renders a
	// TArray<FString> return as an empty string, which made every
	// array-returning accessor unreadable through the bridge.
	TSharedPtr<FJsonObject> OutVals = MakeShared<FJsonObject>();
	MCPFunctionCall::WriteOutputs(OutVals, Func, ParamBuf.GetData(), CallTarget);
	Result->SetObjectField(TEXT("returnValues"), OutVals);

	MCPFunctionCall::DestroyFrame(Func, ParamBuf.GetData());
	return MCPResult(Result);
}

// Call a static UFUNCTION on a UBlueprintFunctionLibrary, with no actor
// instance. invoke_function can only reach functions that live on an actor or
// component; the static *_BlueprintOnly libraries (Voxel sculpt/query/stamp,
// GeometryScript, Kismet math, etc.) have no instance to target, which
// previously forced execute_python. This resolves the library class, runs the
// call on its CDO, and reuses the same arg/return marshalling as InvokeFunction.
//
// Params: className (short name or /Script/Module.Class path), functionName,
// args? (name -> JSON value), actorArgs? (name -> actor label, for UObject*
// params that are actors), worldContextParam? (name of a UObject* param to fill
// with the editor/PIE world; auto-detected for params named WorldContextObject),
// world? (editor|pie). Returns return/out params under returnValues.
TSharedPtr<FJsonValue> FEditorHandlers::InvokeStaticFunction(const TSharedPtr<FJsonObject>& Params)
{
	FString ClassName;
	if (auto Err = RequireString(Params, TEXT("className"), ClassName)) return Err;
	FString FunctionName;
	if (auto Err = RequireString(Params, TEXT("functionName"), FunctionName)) return Err;

	// #971: route through the shared resolver, so world=editor|pie|game|auto
	// and pieInstance select here exactly as they do for invoke_function. The
	// old code special-cased the literal string "pie" and sent everything else
	// to the editor world, which has no GameInstance: a server-authoritative
	// entry point implemented as a static that looks a UGameInstanceSubsystem up
	// off its world context could not be exercised in PIE at all.
	const FString WorldScope = OptionalString(Params, TEXT("world"), TEXT("editor")).ToLower();
	UWorld* World = ResolveWorldFromParams(Params, *WorldScope);
	if (!World)
	{
		return MCPError(WorldScope == TEXT("pie") || WorldScope == TEXT("game")
			? TEXT("PIE not running (or no such pieInstance) - cannot invoke against a PIE world. See editor(list_pie_instances).")
			: TEXT("No editor world available"));
	}
	// Describe the world that was actually resolved, not the requested scope:
	// world="auto" resolves to PIE when a session is running.
	const FString WorldLabel = World->IsPlayInEditor() ? TEXT("PIE") : TEXT("editor");

	// Resolve the function-library class: a /Script/Module.Class path, or a bare
	// class name (with or without the leading U).
	UClass* LibClass = nullptr;
	if (ClassName.Contains(TEXT(".")) || ClassName.StartsWith(TEXT("/")))
	{
		LibClass = LoadObject<UClass>(nullptr, *ClassName);
	}
	if (!LibClass)
	{
		FString Bare = ClassName;
		Bare.RemoveFromStart(TEXT("U"));
		LibClass = FindFirstObject<UClass>(*Bare, EFindFirstObjectOptions::NativeFirst);
	}
	if (!LibClass)
	{
		return MCPError(FString::Printf(TEXT("Function library class not found: %s"), *ClassName));
	}

	UObject* CDO = LibClass->GetDefaultObject();
	if (!CDO) return MCPError(FString::Printf(TEXT("No CDO for class %s"), *LibClass->GetName()));

	UFunction* Func = LibClass->FindFunctionByName(FName(*FunctionName));
	if (!Func) return MCPError(FString::Printf(TEXT("Static function '%s' not found on %s"), *FunctionName, *LibClass->GetName()));

	TArray<uint8> ParamBuf;
	ParamBuf.SetNumZeroed(Func->ParmsSize);
	for (TFieldIterator<FProperty> It(Func); It && (It->PropertyFlags & CPF_Parm); ++It)
	{
		It->InitializeValue_InContainer(ParamBuf.GetData());
	}

	auto Cleanup = [&]()
	{
		for (TFieldIterator<FProperty> It(Func); It && (It->PropertyFlags & CPF_Parm); ++It)
		{
			It->DestroyValue_InContainer(ParamBuf.GetData());
		}
	};

	const TSharedPtr<FJsonObject>* ArgObj = nullptr;
	Params->TryGetObjectField(TEXT("args"), ArgObj);
	if (ArgObj && (*ArgObj).IsValid())
	{
		for (TFieldIterator<FProperty> It(Func); It && (It->PropertyFlags & CPF_Parm); ++It)
		{
			FProperty* P = *It;
			if (P->PropertyFlags & CPF_ReturnParm) continue;
			if ((P->PropertyFlags & CPF_OutParm) && !(P->PropertyFlags & CPF_ReferenceParm)) continue;
			TSharedPtr<FJsonValue> Val = (*ArgObj)->TryGetField(P->GetName());
			if (!Val.IsValid()) continue;
			void* PtrAddr = P->ContainerPtrToValuePtr<void>(ParamBuf.GetData());
			FString E;
			if (!MCPJsonProperty::SetJsonOnProperty(P, PtrAddr, Val, E))
			{
				Cleanup();
				return MCPError(FString::Printf(TEXT("Argument '%s': %s"), *P->GetName(), *E));
			}
		}
	}

	// actorArgs: UObject* params resolved from live actor labels (e.g. the sculpt
	// actor that a Voxel sculpt op operates on).
	const TSharedPtr<FJsonObject>* ActorArgObj = nullptr;
	Params->TryGetObjectField(TEXT("actorArgs"), ActorArgObj);
	if (ActorArgObj && (*ActorArgObj).IsValid())
	{
		for (TFieldIterator<FProperty> It(Func); It && (It->PropertyFlags & CPF_Parm); ++It)
		{
			FProperty* P = *It;
			if (P->PropertyFlags & CPF_ReturnParm) continue;
			FObjectProperty* OP = CastField<FObjectProperty>(P);
			if (!OP) continue;
			FString ActorArgLabel;
			if (!(*ActorArgObj)->TryGetStringField(P->GetName(), ActorArgLabel) || ActorArgLabel.IsEmpty()) continue;
			// #983: refuse a duplicated label rather than passing whichever
			// copy the actor iterator reached first.
			TArray<AActor*> ArgMatches;
			MCPCollectActorsByToken(World, ActorArgLabel, EMCPActorMatch::LabelNameOrPath, ArgMatches);
			if (ArgMatches.Num() > 1)
			{
				Cleanup();
				return MCPAmbiguousActorError(
					ActorArgLabel, TEXT("actorArgs"), TEXT("actorPath"),
					MCPDescribeActorMatchTier(ActorArgLabel, ArgMatches[0]), ArgMatches);
			}
			AActor* RefActor = ArgMatches.Num() == 1 ? ArgMatches[0] : nullptr;
			if (!RefActor)
			{
				Cleanup();
				return MCPError(FString::Printf(TEXT("actorArgs[%s]: actor '%s' not found in %s world"), *P->GetName(), *ActorArgLabel, *WorldLabel));
			}
			if (!RefActor->IsA(OP->PropertyClass))
			{
				if (OP->PropertyClass->IsChildOf(UActorComponent::StaticClass()))
				{
					UActorComponent* MatchedComp = nullptr;
					for (UActorComponent* Comp : RefActor->GetComponents())
					{
						if (Comp && Comp->IsA(OP->PropertyClass)) { MatchedComp = Comp; break; }
					}
					if (MatchedComp)
					{
						OP->SetObjectPropertyValue(P->ContainerPtrToValuePtr<void>(ParamBuf.GetData()), MatchedComp);
						continue;
					}
				}
				Cleanup();
				return MCPError(FString::Printf(TEXT("actorArgs[%s]: actor '%s' (%s) is not assignable to expected type %s"), *P->GetName(), *ActorArgLabel, *RefActor->GetClass()->GetName(), *OP->PropertyClass->GetName()));
			}
			OP->SetObjectPropertyValue(P->ContainerPtrToValuePtr<void>(ParamBuf.GetData()), RefActor);
		}
	}

	// World-context injection: static library functions commonly take a UObject*
	// WorldContextObject. Fill any unset object param matching worldContextParam
	// (or the name the function's own WorldContext metadata gives, or a name
	// containing WorldContext) with the world resolved above.
	//
	// #971: worldContextParam only ever chose WHICH parameter to fill. What goes
	// into it is the selected world, so the parameter that decides PIE versus
	// editor is `world`, and the metadata lookup means a library that names its
	// context parameter something else is still recognised.
	const FString WcParam = OptionalString(Params, TEXT("worldContextParam"), TEXT(""));
	FString MetaWcParam;
#if WITH_METADATA
	if (const FString* Declared = Func->FindMetaData(TEXT("WorldContext")))
	{
		MetaWcParam = *Declared;
	}
#endif
	FString FilledWcParam;
	for (TFieldIterator<FProperty> It(Func); It && (It->PropertyFlags & CPF_Parm); ++It)
	{
		FProperty* P = *It;
		if (P->PropertyFlags & CPF_ReturnParm) continue;
		FObjectProperty* OP = CastField<FObjectProperty>(P);
		if (!OP) continue;
		const FString PName = P->GetName();
		const bool bIsWc =
			(!WcParam.IsEmpty() && PName == WcParam) ||
			(!MetaWcParam.IsEmpty() && PName == MetaWcParam) ||
			PName.Contains(TEXT("WorldContext"));
		if (!bIsWc) continue;
		void* Addr = P->ContainerPtrToValuePtr<void>(ParamBuf.GetData());
		if (OP->GetObjectPropertyValue(Addr) != nullptr) continue;
		if (World->IsA(OP->PropertyClass) || OP->PropertyClass == UObject::StaticClass())
		{
			OP->SetObjectPropertyValue(Addr, World);
			FilledWcParam = PName;
		}
	}

	FGCObjectScopeGuard CDOGuard(CDO);
	CDO->ProcessEvent(Func, ParamBuf.GetData());

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("className"), LibClass->GetName());
	Result->SetStringField(TEXT("functionName"), FunctionName);
	// #971: say which world the call actually ran against and which parameter
	// carried it. "subsystem unavailable" from a static that looks a
	// GameInstance subsystem up off its context is unreadable without this.
	Result->SetStringField(TEXT("world"), WorldLabel);
	Result->SetStringField(TEXT("worldPath"), World->GetPathName());
	Result->SetStringField(TEXT("netMode"), DescribePIENetMode(World));
	if (!FilledWcParam.IsEmpty())
	{
		Result->SetStringField(TEXT("worldContextParam"), FilledWcParam);
	}

	// #885: containers come back as real JSON, scalars and structs keep their
	// export-text spelling. See MCPFunctionCall::OutputToJson.
	TSharedPtr<FJsonObject> OutVals = MakeShared<FJsonObject>();
	MCPFunctionCall::WriteOutputs(OutVals, Func, ParamBuf.GetData(), CDO);
	Result->SetObjectField(TEXT("returnValues"), OutVals);

	// Whether the call CAN have written anything is answerable, so answer it.
	// Most of what these libraries expose (Kismet math, GeometryScript queries)
	// is declared BlueprintPure or const, which means it reports a value and
	// writes nothing.
	const bool bPureFunction = Func->HasAnyFunctionFlags(FUNC_BlueprintPure | FUNC_Const);
	Result->SetBoolField(TEXT("pure"), bPureFunction);
	Result->SetBoolField(TEXT("changed"), !bPureFunction);
	if (bPureFunction)
	{
		Result->SetStringField(TEXT("idempotencyNote"),
			TEXT("The function is declared BlueprintPure or const, so it is a query: it reports a value and writes nothing. Calling it again returns the same answer for the same state."));
	}
	// Past purity the effect is opaque: nothing here compares before to after.
	Result->SetBoolField(TEXT("changeDetected"), false);
	// No rollback. What an arbitrary static library function did is opaque from
	// here: nothing was captured before the call and no inverse function is
	// known, and the non-pure ones (GeometryScript edits, file writes, spawns)
	// have no opposite call at all.
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"), bPureFunction
		? TEXT("A pure or const function writes nothing, so there is nothing to undo.")
		: TEXT("A static library call has no known inverse. Wrap the sequence in editor(begin_editor_transaction) and roll back with editor(cancel_editor_transaction) when the effects are transactional."));

	Cleanup();
	return MCPResult(Result);
}

// #384: read/write ULevelEditorPlaySettings via reflection. Direct member
// access is blocked because the fields are private; FProperty lookup gives
// us the same access path Python's set_editor_property uses.


TSharedPtr<FJsonValue> FEditorHandlers::ConfigurePie(const TSharedPtr<FJsonObject>& Params)
{
	ULevelEditorPlaySettings* Settings = GetPlaySettingsForRW();
	if (!Settings) return MCPError(TEXT("LevelEditorPlaySettings CDO not available"));

	// Read every field this handler can write before writing any of them. These
	// are the only values an inverse call could restore, and SaveConfig below
	// persists the new ones over them.
	const int32 PrevNumClients = GetIntPropOn(Settings, TEXT("PlayNumberOfClients"), 1);
	const FString PrevNetMode = NetModeNameFromValue(GetEnumPropOn(Settings, TEXT("PlayNetMode")));
	const bool bPrevRunUnderOneProcess = GetBoolPropOn(Settings, TEXT("RunUnderOneProcess"), true);
	const bool bPrevLaunchSeparateServer = GetBoolPropOn(Settings, TEXT("bLaunchSeparateServer"), false);
	const int32 PrevNewWindowWidth = GetIntPropOn(Settings, TEXT("NewWindowWidth"), 0);
	const int32 PrevNewWindowHeight = GetIntPropOn(Settings, TEXT("NewWindowHeight"), 0);

	bool bAny = false;
	int32 NumClients = 0;
	if (Params->TryGetNumberField(TEXT("numClients"), NumClients) && NumClients > 0)
	{
		if (!SetIntPropOn(Settings, TEXT("PlayNumberOfClients"), NumClients))
		{
			return MCPError(TEXT("PlayNumberOfClients property not found - engine drift?"));
		}
		bAny = true;
	}

	FString NetMode;
	if (Params->TryGetStringField(TEXT("netMode"), NetMode) && !NetMode.IsEmpty())
	{
		int64 Resolved = static_cast<int64>(EPlayNetMode::PIE_Standalone);
		const FString N = NetMode.ToLower();
		if      (N == TEXT("standalone"))    Resolved = static_cast<int64>(EPlayNetMode::PIE_Standalone);
		else if (N == TEXT("listen") || N == TEXT("listenserver")) Resolved = static_cast<int64>(EPlayNetMode::PIE_ListenServer);
		else if (N == TEXT("client"))        Resolved = static_cast<int64>(EPlayNetMode::PIE_Client);
		else
		{
			return MCPError(FString::Printf(
				TEXT("Unknown netMode '%s'. Use standalone | listen | client."), *NetMode));
		}
		if (!SetEnumPropOn(Settings, TEXT("PlayNetMode"), Resolved))
		{
			return MCPError(TEXT("PlayNetMode property not found - engine drift?"));
		}
		bAny = true;
	}

	bool BVal = false;
	if (Params->TryGetBoolField(TEXT("runUnderOneProcess"), BVal))
	{
		SetBoolPropOn(Settings, TEXT("RunUnderOneProcess"), BVal);
		bAny = true;
	}
	if (Params->TryGetBoolField(TEXT("launchSeparateServer"), BVal))
	{
		SetBoolPropOn(Settings, TEXT("bLaunchSeparateServer"), BVal);
		bAny = true;
	}

	// #671: Play-in-New-Window resolution.
	int32 WinW = 0, WinH = 0;
	if (Params->TryGetNumberField(TEXT("newWindowWidth"), WinW) && WinW > 0)
	{
		SetIntPropOn(Settings, TEXT("NewWindowWidth"), WinW);
		bAny = true;
	}
	if (Params->TryGetNumberField(TEXT("newWindowHeight"), WinH) && WinH > 0)
	{
		SetIntPropOn(Settings, TEXT("NewWindowHeight"), WinH);
		bAny = true;
	}

	if (!bAny)
	{
		return MCPError(TEXT("Nothing to configure - provide numClients / netMode / runUnderOneProcess / launchSeparateServer / newWindowWidth / newWindowHeight"));
	}

	Settings->SaveConfig();

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetNumberField(TEXT("numClients"), GetIntPropOn(Settings, TEXT("PlayNumberOfClients"), 1));
	Result->SetStringField(TEXT("netMode"), NetModeNameFromValue(GetEnumPropOn(Settings, TEXT("PlayNetMode"))));
	Result->SetBoolField(TEXT("runUnderOneProcess"), GetBoolPropOn(Settings, TEXT("RunUnderOneProcess"), true));
	Result->SetBoolField(TEXT("launchSeparateServer"), GetBoolPropOn(Settings, TEXT("bLaunchSeparateServer"), false));
	Result->SetNumberField(TEXT("newWindowWidth"), GetIntPropOn(Settings, TEXT("NewWindowWidth"), 0));
	Result->SetNumberField(TEXT("newWindowHeight"), GetIntPropOn(Settings, TEXT("NewWindowHeight"), 0));

	Result->SetNumberField(TEXT("previousNumClients"), PrevNumClients);
	Result->SetStringField(TEXT("previousNetMode"), PrevNetMode);
	Result->SetBoolField(TEXT("previousRunUnderOneProcess"), bPrevRunUnderOneProcess);
	Result->SetBoolField(TEXT("previousLaunchSeparateServer"), bPrevLaunchSeparateServer);
	Result->SetNumberField(TEXT("previousNewWindowWidth"), PrevNewWindowWidth);
	Result->SetNumberField(TEXT("previousNewWindowHeight"), PrevNewWindowHeight);
	const bool bChanged =
		GetIntPropOn(Settings, TEXT("PlayNumberOfClients"), 1) != PrevNumClients
		|| PrevNetMode != NetModeNameFromValue(GetEnumPropOn(Settings, TEXT("PlayNetMode")))
		|| GetBoolPropOn(Settings, TEXT("RunUnderOneProcess"), true) != bPrevRunUnderOneProcess
		|| GetBoolPropOn(Settings, TEXT("bLaunchSeparateServer"), false) != bPrevLaunchSeparateServer
		|| GetIntPropOn(Settings, TEXT("NewWindowWidth"), 0) != PrevNewWindowWidth
		|| GetIntPropOn(Settings, TEXT("NewWindowHeight"), 0) != PrevNewWindowHeight;
	Result->SetBoolField(TEXT("changed"), bChanged);

	// Self-inverse: the same handler with every field's previous value. The
	// net-mode spelling NetModeNameFromValue produces is one this handler
	// parses back (Standalone / ListenServer / Client all lowercase-match).
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetNumberField(TEXT("numClients"), PrevNumClients);
	Payload->SetStringField(TEXT("netMode"), PrevNetMode);
	Payload->SetBoolField(TEXT("runUnderOneProcess"), bPrevRunUnderOneProcess);
	Payload->SetBoolField(TEXT("launchSeparateServer"), bPrevLaunchSeparateServer);
	Payload->SetNumberField(TEXT("newWindowWidth"), PrevNewWindowWidth);
	Payload->SetNumberField(TEXT("newWindowHeight"), PrevNewWindowHeight);
	MCPSetRollback(Result, TEXT("configure_pie"), Payload);
	// This handler ignores a non-positive numClients / newWindowWidth /
	// newWindowHeight, so a previous value of zero cannot be written back and
	// that field keeps whatever this call left in it.
	TArray<FString> Unrestorable;
	if (PrevNumClients <= 0) Unrestorable.Add(TEXT("numClients"));
	if (PrevNewWindowWidth <= 0) Unrestorable.Add(TEXT("newWindowWidth"));
	if (PrevNewWindowHeight <= 0) Unrestorable.Add(TEXT("newWindowHeight"));
	Result->SetBoolField(TEXT("rollbackLossy"), Unrestorable.Num() > 0);
	if (Unrestorable.Num() > 0)
	{
		Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
			TEXT("Restores every field except %s, which was zero or negative before this call. This handler only applies those three when they are greater than zero, so replaying the previous value is a no-op for them and they keep the value written here."),
			*FString::Join(Unrestorable, TEXT(", "))));
	}
	return MCPResult(Result);
}

// #671: point the PIE player's view (control rotation) at a pitch/yaw so a
// capture frames the intended direction without manual mouse-look.
TSharedPtr<FJsonValue> FEditorHandlers::PieSetPlayerView(const TSharedPtr<FJsonObject>& Params)
{
	FWorldContext* PieCtx = GEditor ? GEditor->GetPIEWorldContext() : nullptr;
	UWorld* World = PieCtx ? PieCtx->World() : nullptr;
	if (!World) return MCPError(TEXT("PIE not running"));
	APlayerController* PC = World->GetFirstPlayerController();
	if (!PC) return MCPError(TEXT("No PIE player controller"));

	const FRotator PreviousRot = PC->GetControlRotation();
	FRotator Rot = PreviousRot;
	double Tmp;
	if (Params->TryGetNumberField(TEXT("pitch"), Tmp)) Rot.Pitch = Tmp;
	if (Params->TryGetNumberField(TEXT("yaw"), Tmp))   Rot.Yaw = Tmp;
	if (Params->TryGetNumberField(TEXT("roll"), Tmp))  Rot.Roll = Tmp;
	PC->SetControlRotation(Rot);

	auto Result = MCPSuccess();
	Result->SetObjectField(TEXT("controlRotation"), MCPRotatorToJsonObject(Rot));
	Result->SetObjectField(TEXT("previousControlRotation"), MCPRotatorToJsonObject(PreviousRot));
	Result->SetBoolField(TEXT("changed"), !Rot.Equals(PreviousRot));

	// Self-inverse: the same handler with the control rotation this call
	// replaced. All three axes are sent, so a partial write still restores the
	// whole rotation.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetNumberField(TEXT("pitch"), PreviousRot.Pitch);
	Payload->SetNumberField(TEXT("yaw"), PreviousRot.Yaw);
	Payload->SetNumberField(TEXT("roll"), PreviousRot.Roll);
	MCPSetRollback(Result, TEXT("pie_set_player_view"), Payload);
	Result->SetBoolField(TEXT("rollbackLossy"), false);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("The rollback needs the same PIE session and player controller to still exist. Once play ends it reports that PIE is not running rather than restoring anything, and a live controller keeps turning under player or AI input after the write either way."));
	return MCPResult(Result);
}

// #671: stage input for the running game (Game-only input mode + focus the
// game viewport) so injected/simulated input reaches the pawn.
TSharedPtr<FJsonValue> FEditorHandlers::StageGameInput(const TSharedPtr<FJsonObject>& Params)
{
	FWorldContext* PieCtx = GEditor ? GEditor->GetPIEWorldContext() : nullptr;
	UWorld* World = PieCtx ? PieCtx->World() : nullptr;
	if (!World) return MCPError(TEXT("PIE not running"));
	APlayerController* PC = World->GetFirstPlayerController();
	if (!PC) return MCPError(TEXT("No PIE player controller"));

	const bool bPreviousShowMouseCursor = PC->bShowMouseCursor != 0;
	const FString Mode = OptionalString(Params, TEXT("inputMode"), TEXT("gameOnly")).ToLower();
	if (Mode == TEXT("uionly"))
	{
		FInputModeUIOnly M;
		PC->SetInputMode(M);
	}
	else if (Mode == TEXT("gameandui"))
	{
		FInputModeGameAndUI M;
		PC->SetInputMode(M);
	}
	else
	{
		FInputModeGameOnly M;
		PC->SetInputMode(M);
	}
	const bool bShowCursor = OptionalBool(Params, TEXT("showMouseCursor"), Mode != TEXT("gameonly"));
	PC->SetShowMouseCursor(bShowCursor);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("inputMode"), Mode);
	Result->SetBoolField(TEXT("showMouseCursor"), bShowCursor);
	Result->SetBoolField(TEXT("previousShowMouseCursor"), bPreviousShowMouseCursor);
	// Half the write is comparable and half is not: the cursor flag is readable,
	// the active input mode is not, so the mode is always re-applied. Reporting
	// changed unconditionally rather than from the cursor flag alone avoids
	// claiming a no-op for a call that did reset input routing.
	Result->SetBoolField(TEXT("changed"), true);
	// No rollback. APlayerController exposes no getter for the input mode it is
	// currently in - SetInputMode writes into the viewport client and keeps no
	// readable record - so the mode this call replaced was never captured.
	// Restoring only showMouseCursor would put the cursor back while leaving
	// input routed the way this call left it, which is worse than not trying.
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("The previous input mode is unreadable: Unreal keeps no accessor for the mode a player controller is in, so there is nothing to restore it to. previousShowMouseCursor is reported for the cursor half; call stage_game_input again with the mode the game expects to put input routing back deliberately."));
	return MCPResult(Result);
}


TSharedPtr<FJsonValue> FEditorHandlers::GetPieConfig(const TSharedPtr<FJsonObject>& Params)
{
	const ULevelEditorPlaySettings* Settings = GetDefault<ULevelEditorPlaySettings>();
	if (!Settings) return MCPError(TEXT("LevelEditorPlaySettings CDO not available"));

	auto Result = MCPSuccess();
	Result->SetNumberField(TEXT("numClients"), GetIntPropOn(Settings, TEXT("PlayNumberOfClients"), 1));
	Result->SetStringField(TEXT("netMode"), NetModeNameFromValue(GetEnumPropOn(Settings, TEXT("PlayNetMode"))));
	Result->SetBoolField(TEXT("runUnderOneProcess"), GetBoolPropOn(Settings, TEXT("RunUnderOneProcess"), true));
	Result->SetBoolField(TEXT("launchSeparateServer"), GetBoolPropOn(Settings, TEXT("bLaunchSeparateServer"), false));
	return MCPResult(Result);
}
