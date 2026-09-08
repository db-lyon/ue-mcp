#include "PhysicsHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Handlers/BlueprintHandlers_Internal.h"
#include "Components/PrimitiveComponent.h"
#include "PhysicsEngine/BodyInstance.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Blueprint.h"
#include "Engine/World.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "EditorAssetLibrary.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "GameFramework/Actor.h"
#include "EngineUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

void FPhysicsHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	Registry.RegisterHandler(TEXT("set_collision_profile"), &SetCollisionProfile);
	Registry.RegisterHandler(TEXT("set_collision_enabled"), &SetCollisionEnabled);
	Registry.RegisterHandler(TEXT("set_collision"), &SetCollision);
	Registry.RegisterHandler(TEXT("set_simulate_physics"), &SetPhysicsEnabled);
	Registry.RegisterHandler(TEXT("set_physics_properties"), &SetBodyProperties);
	// #676: perturb a physics body (impulse or force) for over-time observation.
	Registry.RegisterHandler(TEXT("add_impulse"), &AddImpulse);
	Registry.RegisterHandler(TEXT("add_force"), &AddImpulse);
}

namespace
{
	// Resolve a collision channel by canonical enum name ("ECC_Visibility",
	// "Visibility", "WorldStatic", "Pawn", "GameTraceChannel1"). Bare names are
	// retried with the ECC_ prefix the enum uses.
	bool ResolveCollisionChannel(const FString& Name, ECollisionChannel& Out)
	{
		if (UEnum* E = StaticEnum<ECollisionChannel>())
		{
			int64 V = E->GetValueByNameString(Name);
			if (V == INDEX_NONE && !Name.StartsWith(TEXT("ECC_")))
			{
				V = E->GetValueByNameString(FString(TEXT("ECC_")) + Name);
			}
			if (V != INDEX_NONE) { Out = static_cast<ECollisionChannel>(V); return true; }
		}
		return false;
	}

	bool ResolveCollisionResponse(const FString& Name, ECollisionResponse& Out)
	{
		if (Name.Equals(TEXT("Block"), ESearchCase::IgnoreCase) || Name.Equals(TEXT("ECR_Block"), ESearchCase::IgnoreCase)) { Out = ECR_Block; return true; }
		if (Name.Equals(TEXT("Overlap"), ESearchCase::IgnoreCase) || Name.Equals(TEXT("ECR_Overlap"), ESearchCase::IgnoreCase)) { Out = ECR_Overlap; return true; }
		if (Name.Equals(TEXT("Ignore"), ESearchCase::IgnoreCase) || Name.Equals(TEXT("ECR_Ignore"), ESearchCase::IgnoreCase)) { Out = ECR_Ignore; return true; }
		return false;
	}

	bool ResolveCollisionEnabled(const FString& Name, ECollisionEnabled::Type& Out)
	{
		if (Name.Equals(TEXT("NoCollision"), ESearchCase::IgnoreCase)) { Out = ECollisionEnabled::NoCollision; return true; }
		if (Name.Equals(TEXT("QueryOnly"), ESearchCase::IgnoreCase)) { Out = ECollisionEnabled::QueryOnly; return true; }
		if (Name.Equals(TEXT("PhysicsOnly"), ESearchCase::IgnoreCase)) { Out = ECollisionEnabled::PhysicsOnly; return true; }
		if (Name.Equals(TEXT("QueryAndPhysics"), ESearchCase::IgnoreCase)) { Out = ECollisionEnabled::QueryAndPhysics; return true; }
		return false;
	}
}

TSharedPtr<FJsonValue> FPhysicsHandlers::SetCollisionProfile(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorLabel;
	if (auto Err = RequireStringAlt(Params, TEXT("actorLabel"), TEXT("actorPath"), ActorLabel)) return Err;

	FString ProfileName;
	if (auto Err = RequireString(Params, TEXT("profileName"), ProfileName)) return Err;

	REQUIRE_EDITOR_WORLD(World);

	TSharedPtr<FJsonValue> ActorErr;
	AActor* Actor = MCPResolveActor(World, Params, ActorErr);
	if (!Actor) return ActorErr;
	ActorLabel = Actor->GetActorLabel();

	// Capture previous profile from first component (for rollback)
	TArray<UPrimitiveComponent*> PrimitiveComponents;
	Actor->GetComponents<UPrimitiveComponent>(PrimitiveComponents);
	FString PrevProfile;
	bool bAllAlreadyMatch = !PrimitiveComponents.IsEmpty();
	for (UPrimitiveComponent* PrimComp : PrimitiveComponents)
	{
		if (!PrimComp) continue;
		const FString CompProfile = PrimComp->GetCollisionProfileName().ToString();
		if (PrevProfile.IsEmpty()) PrevProfile = CompProfile;
		if (CompProfile != ProfileName) bAllAlreadyMatch = false;
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("actorPath"), Actor->GetPathName());
	Result->SetStringField(TEXT("profileName"), ProfileName);

	if (bAllAlreadyMatch)
	{
		MCPSetExisted(Result);
		Result->SetBoolField(TEXT("updated"), false);
		Result->SetBoolField(TEXT("unchanged"), true);
		Result->SetNumberField(TEXT("componentsModified"), 0);
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("Every primitive component already used this collision profile, so nothing changed and there is "
				 "nothing to undo."));
		return MCPResult(Result);
	}

	int32 ComponentsModified = 0;
	for (UPrimitiveComponent* PrimComp : PrimitiveComponents)
	{
		if (!PrimComp) continue;
		PrimComp->SetCollisionProfileName(FName(*ProfileName));
		ComponentsModified++;
	}

	Result->SetNumberField(TEXT("componentsModified"), ComponentsModified);
	Result->SetBoolField(TEXT("success"), ComponentsModified > 0);
	Result->SetBoolField(TEXT("unchanged"), ComponentsModified == 0);

	if (ComponentsModified == 0)
	{
		Result->SetStringField(TEXT("warning"), TEXT("No PrimitiveComponents found on actor"));
		Result->SetBoolField(TEXT("updated"), false);
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("The actor has no PrimitiveComponent to profile, so nothing was written and there is nothing to "
				 "undo."));
		return MCPResult(Result);
	}

	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("previousProfileName"), PrevProfile);
	// Two spellings that cannot be replayed. An EMPTY name exports to nothing
	// and then fails this handler's own RequireString at replay time, and the
	// "Custom" sentinel names no profile the engine can look up - a component
	// reports it once it carries per-channel overrides, and writing it back
	// would restore nothing. Either way the record is withheld rather than
	// emitted knowing it would be refused.
	const bool bPrevWasCustom =
		(FName(*PrevProfile) == UCollisionProfile::CustomCollisionProfileName);
	if (PrevProfile.IsEmpty() || bPrevWasCustom)
	{
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"), bPrevWasCustom
			? TEXT("The components' previous profile read as the 'Custom' sentinel, which names no profile the engine "
				   "can look up: a component reports it once it carries per-channel response overrides. Restoring "
				   "that behaviour needs gameplay(set_collision) with collisionEnabled, objectType and the full "
				   "response table, which this action has no parameters for, so no inverse is emitted.")
			: TEXT("No component reported a previous collision profile name, and gameplay(set_collision_profile) "
				   "requires a non-empty profileName, so no inverse can be expressed."));
		return MCPResult(Result);
	}

	// Addressed by actorPath, which is unique, rather than by a label that can
	// match several actors.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("actorPath"), Actor->GetPathName());
	Payload->SetStringField(TEXT("profileName"), PrevProfile);
	MCPSetRollback(Result, TEXT("set_collision_profile"), Payload);
	Result->SetBoolField(TEXT("rollbackLossy"), ComponentsModified > 1);
	if (ComponentsModified > 1)
	{
		Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
			TEXT("The record carries the profile read from the FIRST of %d primitive component(s) written. Components "
				 "that used different profiles before this call all come back with that one's profile; "
				 "gameplay(set_collision) with a componentName is the per-component form."),
			ComponentsModified));
	}

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPhysicsHandlers::SetPhysicsEnabled(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorLabel;
	if (auto Err = RequireStringAlt(Params, TEXT("actorLabel"), TEXT("actorPath"), ActorLabel)) return Err;

	// #721: the published TS schema names this parameter "simulate" while the
	// handler historically read only "enabled", so a schema-conformant call
	// silently no-opped. Accept either spelling (simulate | enabled) and error
	// explicitly when neither is present rather than succeeding silently.
	bool bEnabled = true;
	if (!Params->TryGetBoolField(TEXT("simulate"), bEnabled) &&
		!Params->TryGetBoolField(TEXT("enabled"), bEnabled))
	{
		return MCPError(TEXT("Missing 'simulate' (aka 'enabled') parameter (true/false)"));
	}

	REQUIRE_EDITOR_WORLD(World);

	TSharedPtr<FJsonValue> ActorErr;
	AActor* Actor = MCPResolveActor(World, Params, ActorErr);
	if (!Actor) return ActorErr;
	ActorLabel = Actor->GetActorLabel();

	// Capture previous state for rollback / idempotency check
	TArray<UPrimitiveComponent*> PrimitiveComponents;
	Actor->GetComponents<UPrimitiveComponent>(PrimitiveComponents);
	bool bPrev = false;
	bool bAnySim = false;
	bool bAllAlready = !PrimitiveComponents.IsEmpty();
	for (UPrimitiveComponent* PrimComp : PrimitiveComponents)
	{
		if (!PrimComp) continue;
		const bool bCompSim = PrimComp->IsSimulatingPhysics();
		if (!bAnySim) { bPrev = bCompSim; bAnySim = true; }
		if (bCompSim != bEnabled) bAllAlready = false;
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("actorPath"), Actor->GetPathName());
	Result->SetBoolField(TEXT("enabled"), bEnabled);

	if (bAllAlready)
	{
		MCPSetExisted(Result);
		Result->SetBoolField(TEXT("updated"), false);
		Result->SetBoolField(TEXT("unchanged"), true);
		Result->SetNumberField(TEXT("componentsModified"), 0);
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("Every primitive component was already simulating in this state, so nothing changed and there is "
				 "nothing to undo."));
		return MCPResult(Result);
	}

	int32 ComponentsModified = 0;
	for (UPrimitiveComponent* PrimComp : PrimitiveComponents)
	{
		if (!PrimComp) continue;
		PrimComp->SetSimulatePhysics(bEnabled);
		ComponentsModified++;
	}

	Result->SetNumberField(TEXT("componentsModified"), ComponentsModified);
	Result->SetBoolField(TEXT("success"), ComponentsModified > 0);
	Result->SetBoolField(TEXT("unchanged"), ComponentsModified == 0);

	if (ComponentsModified == 0)
	{
		Result->SetStringField(TEXT("warning"), TEXT("No PrimitiveComponents found on actor"));
		Result->SetBoolField(TEXT("updated"), false);
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("The actor has no PrimitiveComponent to simulate, so nothing was written and there is nothing to "
				 "undo."));
	}
	else
	{
		MCPSetUpdated(Result);
		// The inverse is this same action with the previous flag. The method
		// name is the REGISTERED one - gameplay(set_simulate_physics) - and the
		// parameter is 'enabled', which is the spelling this handler reads
		// alongside 'simulate'. Addressed by actorPath, which is unique, rather
		// than by a label that can match several actors.
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("actorPath"), Actor->GetPathName());
		Payload->SetBoolField(TEXT("enabled"), bPrev);
		MCPSetRollback(Result, TEXT("set_simulate_physics"), Payload);
		// The previous flag is read from the FIRST simulating component, and
		// this writes every one of them, so an actor whose components differed
		// all come back with that one's value.
		Result->SetBoolField(TEXT("rollbackLossy"), ComponentsModified > 1);
		if (ComponentsModified > 1)
		{
			Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
				TEXT("The record carries the simulate flag read from the FIRST of %d primitive component(s) written. "
					 "Components that differed from each other before this call all come back with that one's value."),
				ComponentsModified));
		}
	}

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPhysicsHandlers::SetCollisionEnabled(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorLabel;
	if (auto Err = RequireStringAlt(Params, TEXT("actorLabel"), TEXT("actorPath"), ActorLabel)) return Err;

	// The published TS schema documents this parameter as 'collisionEnabled',
	// which is also what gameplay(set_collision) calls it, while this handler
	// historically read only 'collisionType'. A schema-conformant call therefore
	// failed on a parameter the caller had supplied under the documented name.
	// Both spellings are read, and the error below names both.
	FString CollisionType;
	if (!Params->TryGetStringField(TEXT("collisionType"), CollisionType) || CollisionType.IsEmpty())
	{
		if (!Params->TryGetStringField(TEXT("collisionEnabled"), CollisionType) || CollisionType.IsEmpty())
		{
			return MCPError(TEXT(
				"Missing 'collisionEnabled' (aka 'collisionType'). Pass one of NoCollision, QueryOnly, PhysicsOnly "
				"or QueryAndPhysics."));
		}
	}

	// Map string to ECollisionEnabled
	ECollisionEnabled::Type CollisionEnabled;
	if (CollisionType.Equals(TEXT("NoCollision"), ESearchCase::IgnoreCase))
	{
		CollisionEnabled = ECollisionEnabled::NoCollision;
	}
	else if (CollisionType.Equals(TEXT("QueryOnly"), ESearchCase::IgnoreCase))
	{
		CollisionEnabled = ECollisionEnabled::QueryOnly;
	}
	else if (CollisionType.Equals(TEXT("PhysicsOnly"), ESearchCase::IgnoreCase))
	{
		CollisionEnabled = ECollisionEnabled::PhysicsOnly;
	}
	else if (CollisionType.Equals(TEXT("QueryAndPhysics"), ESearchCase::IgnoreCase))
	{
		CollisionEnabled = ECollisionEnabled::QueryAndPhysics;
	}
	else
	{
		return MCPError(FString::Printf(TEXT("Unknown collision type: '%s'. Use NoCollision, QueryOnly, PhysicsOnly, or QueryAndPhysics."), *CollisionType));
	}

	REQUIRE_EDITOR_WORLD(World);

	TSharedPtr<FJsonValue> ActorErr;
	AActor* Actor = MCPResolveActor(World, Params, ActorErr);
	if (!Actor) return ActorErr;
	ActorLabel = Actor->GetActorLabel();

	// Capture previous state
	TArray<UPrimitiveComponent*> PrimitiveComponents;
	Actor->GetComponents<UPrimitiveComponent>(PrimitiveComponents);
	ECollisionEnabled::Type PrevType = ECollisionEnabled::NoCollision;
	bool bAnyFound = false;
	bool bAllAlready = !PrimitiveComponents.IsEmpty();
	for (UPrimitiveComponent* PrimComp : PrimitiveComponents)
	{
		if (!PrimComp) continue;
		const ECollisionEnabled::Type CompCol = PrimComp->GetCollisionEnabled();
		if (!bAnyFound) { PrevType = CompCol; bAnyFound = true; }
		if (CompCol != CollisionEnabled) bAllAlready = false;
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("actorPath"), Actor->GetPathName());
	Result->SetStringField(TEXT("collisionType"), CollisionType);

	if (bAllAlready)
	{
		MCPSetExisted(Result);
		Result->SetBoolField(TEXT("updated"), false);
		Result->SetBoolField(TEXT("unchanged"), true);
		Result->SetNumberField(TEXT("componentsModified"), 0);
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("Every primitive component already held this collision mode, so nothing changed and there is nothing "
				 "to undo."));
		return MCPResult(Result);
	}

	int32 ComponentsModified = 0;
	for (UPrimitiveComponent* PrimComp : PrimitiveComponents)
	{
		if (!PrimComp) continue;
		PrimComp->SetCollisionEnabled(CollisionEnabled);
		ComponentsModified++;
	}

	Result->SetNumberField(TEXT("componentsModified"), ComponentsModified);
	Result->SetBoolField(TEXT("success"), ComponentsModified > 0);
	Result->SetBoolField(TEXT("unchanged"), ComponentsModified == 0);

	if (ComponentsModified == 0)
	{
		Result->SetStringField(TEXT("warning"), TEXT("No PrimitiveComponents found on actor"));
		Result->SetBoolField(TEXT("updated"), false);
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("The actor has no PrimitiveComponent to set collision on, so nothing was written and there is "
				 "nothing to undo."));
	}
	else
	{
		MCPSetUpdated(Result);
		FString PrevTypeStr;
		switch (PrevType)
		{
		case ECollisionEnabled::NoCollision: PrevTypeStr = TEXT("NoCollision"); break;
		case ECollisionEnabled::QueryOnly: PrevTypeStr = TEXT("QueryOnly"); break;
		case ECollisionEnabled::PhysicsOnly: PrevTypeStr = TEXT("PhysicsOnly"); break;
		case ECollisionEnabled::QueryAndPhysics: PrevTypeStr = TEXT("QueryAndPhysics"); break;
		default: PrevTypeStr = TEXT("NoCollision"); break;
		}
		// Addressed by actorPath, which is unique, rather than by a label that
		// can match several actors. 'collisionType' is this handler's own
		// parameter name and is read before the 'collisionEnabled' alias, so the
		// record replays regardless of which spelling the original call used.
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("actorPath"), Actor->GetPathName());
		Payload->SetStringField(TEXT("collisionType"), PrevTypeStr);
		MCPSetRollback(Result, TEXT("set_collision_enabled"), Payload);
		Result->SetBoolField(TEXT("rollbackLossy"), ComponentsModified > 1);
		if (ComponentsModified > 1)
		{
			Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
				TEXT("The record carries the collision mode read from the FIRST of %d primitive component(s) written. "
					 "Components that differed from each other before this call all come back with that one's mode; "
					 "gameplay(set_collision) with a componentName is the per-component form."),
				ComponentsModified));
		}
	}

	return MCPResult(Result);
}

// set_collision -- unified collision authoring for placed level instances AND
// Blueprint component templates. Applies any of: collisionProfile,
// collisionEnabled, objectType, responseToAllChannels, and a per-channel
// responses map {channel: Block|Overlap|Ignore}. Targets a specific component
// by name, or every primitive component when omitted. (#545)
TSharedPtr<FJsonValue> FPhysicsHandlers::SetCollision(const TSharedPtr<FJsonObject>& Params)
{
	const FString ComponentName = OptionalString(Params, TEXT("componentName"));

	// Gather the target primitive components from either a placed actor
	// (actorLabel) or a Blueprint component template (assetPath).
	TArray<UPrimitiveComponent*> Targets;
	UBlueprint* Blueprint = nullptr;   // set when editing a BP template
	AActor* Actor = nullptr;           // set when editing a placed actor
	FString TargetDesc;

	FString AssetPath;
	FString ActorLabel;
	if (Params->TryGetStringField(TEXT("assetPath"), AssetPath) && !AssetPath.IsEmpty())
	{
		Blueprint = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(AssetPath));
		if (!Blueprint)
		{
			return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
		}
		if (ComponentName.IsEmpty())
		{
			return MCPError(TEXT("componentName is required when targeting a Blueprint component template"));
		}
		bool bInherited = false;
		TArray<FString> Available;
		UActorComponent* Template = ResolveComponentTemplate(Blueprint, ComponentName, /*bForWrite=*/true, bInherited, Available);
		UPrimitiveComponent* Prim = Cast<UPrimitiveComponent>(Template);
		if (!Prim)
		{
			return MCPError(FString::Printf(TEXT("Component '%s' is not a PrimitiveComponent (or not found). Available: [%s]"), *ComponentName, *FString::Join(Available, TEXT(", "))));
		}
		Targets.Add(Prim);
		TargetDesc = FString::Printf(TEXT("%s:%s"), *AssetPath, *ComponentName);
	}
	else if ((Params->TryGetStringField(TEXT("actorLabel"), ActorLabel) && !ActorLabel.IsEmpty())
		|| Params->HasField(TEXT("actorPath")))
	{
		UWorld* World = GetEditorWorld();
		if (!World) return MCPError(TEXT("No editor world available"));
		TSharedPtr<FJsonValue> ActorErr;
		Actor = MCPResolveActor(World, Params, ActorErr);
		if (!Actor) return ActorErr;
		ActorLabel = Actor->GetActorLabel();

		TArray<UPrimitiveComponent*> AllPrims;
		Actor->GetComponents<UPrimitiveComponent>(AllPrims);
		for (UPrimitiveComponent* Prim : AllPrims)
		{
			if (!Prim) continue;
			if (ComponentName.IsEmpty() ||
				Prim->GetName().Equals(ComponentName, ESearchCase::IgnoreCase) ||
				Prim->GetName().StartsWith(ComponentName, ESearchCase::IgnoreCase))
			{
				Targets.Add(Prim);
			}
		}
		if (Targets.IsEmpty())
		{
			return MCPError(FString::Printf(TEXT("No matching PrimitiveComponent on actor '%s'"), *ActorLabel));
		}
		TargetDesc = ActorLabel;
	}
	else
	{
		return MCPError(TEXT("Provide either actorLabel (placed instance) or assetPath (Blueprint component template)"));
	}

	// Decode the requested settings once.
	const FString Profile = OptionalString(Params, TEXT("collisionProfile"));
	const FString EnabledStr = OptionalString(Params, TEXT("collisionEnabled"));
	const FString ObjectTypeStr = OptionalString(Params, TEXT("objectType"));
	const FString AllResponseStr = OptionalString(Params, TEXT("responseToAllChannels"));

	ECollisionEnabled::Type EnabledVal = ECollisionEnabled::QueryAndPhysics;
	const bool bSetEnabled = !EnabledStr.IsEmpty();
	if (bSetEnabled && !ResolveCollisionEnabled(EnabledStr, EnabledVal))
	{
		return MCPError(FString::Printf(TEXT("Unknown collisionEnabled '%s'. Use NoCollision, QueryOnly, PhysicsOnly, QueryAndPhysics."), *EnabledStr));
	}

	ECollisionChannel ObjectType = ECC_WorldStatic;
	const bool bSetObjectType = !ObjectTypeStr.IsEmpty();
	if (bSetObjectType && !ResolveCollisionChannel(ObjectTypeStr, ObjectType))
	{
		return MCPError(FString::Printf(TEXT("Unknown objectType channel '%s'"), *ObjectTypeStr));
	}

	ECollisionResponse AllResponse = ECR_Block;
	const bool bSetAllResponse = !AllResponseStr.IsEmpty();
	if (bSetAllResponse && !ResolveCollisionResponse(AllResponseStr, AllResponse))
	{
		return MCPError(FString::Printf(TEXT("Unknown responseToAllChannels '%s'. Use Block, Overlap, Ignore."), *AllResponseStr));
	}

	// Per-channel responses: pre-resolve the map so a bad channel/response name
	// fails before any mutation.
	TArray<TPair<ECollisionChannel, ECollisionResponse>> ChannelResponses;
	const TSharedPtr<FJsonObject>* ResponsesObj = nullptr;
	if (Params->TryGetObjectField(TEXT("responses"), ResponsesObj) && ResponsesObj && (*ResponsesObj).IsValid())
	{
		for (const auto& Pair : (*ResponsesObj)->Values)
		{
			const FString ChannelName(*Pair.Key);
			ECollisionChannel Ch;
			if (!ResolveCollisionChannel(ChannelName, Ch))
			{
				return MCPError(FString::Printf(TEXT("Unknown response channel '%s'"), *ChannelName));
			}
			FString RespStr;
			Pair.Value->TryGetString(RespStr);
			ECollisionResponse Resp;
			if (!ResolveCollisionResponse(RespStr, Resp))
			{
				return MCPError(FString::Printf(TEXT("Unknown response '%s' for channel '%s'. Use Block, Overlap, Ignore."), *RespStr, *ChannelName));
			}
			ChannelResponses.Emplace(Ch, Resp);
		}
	}

	if (!bSetEnabled && !bSetObjectType && !bSetAllResponse && Profile.IsEmpty() && ChannelResponses.Num() == 0)
	{
		return MCPError(TEXT("Nothing to set. Provide collisionProfile, collisionEnabled, objectType, responseToAllChannels, and/or responses."));
	}

	// What the first target looked like before anything was written. Reading one
	// component mirrors what set_collision_profile and set_collision_enabled
	// already do for their own rollbacks, and the note below says plainly that
	// components which differed from each other all come back with this one's
	// settings.
	TSharedPtr<FJsonObject> RollbackPayload = MakeShared<FJsonObject>();
	FName PreviousProfileName;
	bool bPreviousProfileWasCustom = false;
	bool bCapturedEveryResponse = false;
	FString PreviousStateDigest;
	// Every collision field this action can write, in one string. Only ever
	// compared against itself, so the format is irrelevant as long as it covers
	// the same ground the writes below do.
	const auto DescribeCollisionState = [](UPrimitiveComponent* Component) -> FString
	{
		FString Digest = Component->GetCollisionProfileName().ToString();
		Digest += FString::Printf(TEXT("|%d|%d"),
			(int32)Component->GetCollisionEnabled(), (int32)Component->GetCollisionObjectType());
		for (int32 Channel = 0; Channel < (int32)ECC_MAX; ++Channel)
		{
			Digest += FString::Printf(TEXT("|%d"),
				(int32)Component->GetCollisionResponseToChannel((ECollisionChannel)Channel));
		}
		return Digest;
	};
	{
		const auto EnabledToString = [](ECollisionEnabled::Type Value) -> const TCHAR*
		{
			switch (Value)
			{
			case ECollisionEnabled::NoCollision:     return TEXT("NoCollision");
			case ECollisionEnabled::QueryOnly:       return TEXT("QueryOnly");
			case ECollisionEnabled::PhysicsOnly:     return TEXT("PhysicsOnly");
			case ECollisionEnabled::QueryAndPhysics: return TEXT("QueryAndPhysics");
			default:                                 return TEXT("NoCollision");
			}
		};
		const auto ResponseToString = [](ECollisionResponse Value) -> const TCHAR*
		{
			switch (Value)
			{
			case ECR_Block:   return TEXT("Block");
			case ECR_Overlap: return TEXT("Overlap");
			default:          return TEXT("Ignore");
			}
		};

		UPrimitiveComponent* First = Targets[0];
		UEnum* ChannelEnum = StaticEnum<ECollisionChannel>();
		PreviousProfileName = First->GetCollisionProfileName();

		// Writing a profile RESETS every channel response on the component (the
		// apply loop below relies on exactly that ordering), and so does
		// responseToAllChannels. Either one therefore has to carry the whole
		// response table, not just the channels the caller named, or the
		// overrides it wiped are gone with nothing to put them back.
		const bool bWritesEveryResponse = !Profile.IsEmpty() || bSetAllResponse;

		// "Custom" is the sentinel UPrimitiveComponent reports once a component
		// carries per-channel overrides. It names no profile the engine can look
		// up, so restoring it would restore nothing. The state is fully
		// described by collisionEnabled + objectType + the response table
		// instead, and those are carried unconditionally whenever a profile is
		// written for exactly that reason.
		bPreviousProfileWasCustom =
			(PreviousProfileName == UCollisionProfile::CustomCollisionProfileName);
		if (!Profile.IsEmpty() && !bPreviousProfileWasCustom)
		{
			RollbackPayload->SetStringField(TEXT("collisionProfile"), PreviousProfileName.ToString());
		}
		if (bSetEnabled || !Profile.IsEmpty())
		{
			RollbackPayload->SetStringField(TEXT("collisionEnabled"), EnabledToString(First->GetCollisionEnabled()));
		}
		if ((bSetObjectType || !Profile.IsEmpty()) && ChannelEnum)
		{
			RollbackPayload->SetStringField(TEXT("objectType"),
				ChannelEnum->GetNameStringByValue((int64)First->GetCollisionObjectType()));
		}

		if (bWritesEveryResponse || ChannelResponses.Num() > 0)
		{
			TSharedPtr<FJsonObject> PreviousResponses = MakeShared<FJsonObject>();
			if (bWritesEveryResponse && ChannelEnum)
			{
				for (int32 Index = 0; Index < ChannelEnum->NumEnums(); ++Index)
				{
					const int64 Value = ChannelEnum->GetValueByIndex(Index);
					if (Value < 0 || Value >= (int64)ECC_MAX) continue;
					const FString ChannelName = ChannelEnum->GetNameStringByIndex(Index);
					if (ChannelName.IsEmpty() || ChannelName.EndsWith(TEXT("_MAX"))) continue;
					PreviousResponses->SetStringField(ChannelName,
						ResponseToString(First->GetCollisionResponseToChannel((ECollisionChannel)Value)));
				}
			}
			// Named channels last, so an explicit entry wins over the sweep
			// above when both apply. They agree, but the order makes that
			// independent of enum iteration.
			for (const TPair<ECollisionChannel, ECollisionResponse>& CR : ChannelResponses)
			{
				const FString ChannelName = ChannelEnum
					? ChannelEnum->GetNameStringByValue((int64)CR.Key) : FString();
				if (ChannelName.IsEmpty()) continue;
				PreviousResponses->SetStringField(ChannelName,
					ResponseToString(First->GetCollisionResponseToChannel(CR.Key)));
			}
			if (PreviousResponses->Values.Num() > 0)
			{
				RollbackPayload->SetObjectField(TEXT("responses"), PreviousResponses);
			}
			bCapturedEveryResponse = bWritesEveryResponse;
		}

		// The whole collision state of the first target as one comparable
		// string, so the idempotency marker below reports a real before/after
		// rather than "the caller asked for something".
		PreviousStateDigest = DescribeCollisionState(First);
	}

	TArray<FString> Applied;
	for (UPrimitiveComponent* Prim : Targets)
	{
		Prim->Modify();
		// Apply profile first (it resets channel responses), then overrides.
		if (!Profile.IsEmpty()) { Prim->SetCollisionProfileName(FName(*Profile)); }
		if (bSetEnabled) { Prim->SetCollisionEnabled(EnabledVal); }
		if (bSetObjectType) { Prim->SetCollisionObjectType(ObjectType); }
		if (bSetAllResponse) { Prim->SetCollisionResponseToAllChannels(AllResponse); }
		for (const TPair<ECollisionChannel, ECollisionResponse>& CR : ChannelResponses)
		{
			Prim->SetCollisionResponseToChannel(CR.Key, CR.Value);
		}
		Prim->MarkPackageDirty();
	}

	if (!Profile.IsEmpty()) Applied.Add(TEXT("collisionProfile"));
	if (bSetEnabled) Applied.Add(TEXT("collisionEnabled"));
	if (bSetObjectType) Applied.Add(TEXT("objectType"));
	if (bSetAllResponse) Applied.Add(TEXT("responseToAllChannels"));
	if (ChannelResponses.Num() > 0) Applied.Add(TEXT("responses"));

	// Persist Blueprint template edits with a recompile + save.
	if (Blueprint)
	{
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		SaveAssetPackage(Blueprint);
	}

	// Read the same digest back off the same component. A call that wrote the
	// settings the component already had moved nothing, and saying so is what
	// keeps a replayed flow step from reading as an edit.
	const bool bChanged = DescribeCollisionState(Targets[0]) != PreviousStateDigest;

	auto Result = MCPSuccess();
	if (bChanged) MCPSetUpdated(Result); else Result->SetBoolField(TEXT("updated"), false);
	Result->SetBoolField(TEXT("unchanged"), !bChanged);
	Result->SetStringField(TEXT("target"), TargetDesc);
	Result->SetNumberField(TEXT("componentsModified"), Targets.Num());
	TArray<TSharedPtr<FJsonValue>> AppliedArr;
	for (const FString& A : Applied) AppliedArr.Add(MakeShared<FJsonValueString>(A));
	Result->SetArrayField(TEXT("applied"), AppliedArr);

	if (!bChanged)
	{
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("The component already held every setting this call wrote, so nothing changed and there is nothing "
				 "to undo."));
		return MCPResult(Result);
	}

	// The inverse is this same action carrying the settings that were there,
	// addressed at the same target. The selector is echoed exactly as it came
	// in, so a Blueprint template edit rolls back on the template and a placed
	// actor rolls back on the actor.
	if (Blueprint)
	{
		RollbackPayload->SetStringField(TEXT("assetPath"), AssetPath);
		RollbackPayload->SetStringField(TEXT("componentName"), ComponentName);
	}
	else
	{
		RollbackPayload->SetStringField(TEXT("actorPath"), Actor->GetPathName());
		if (!ComponentName.IsEmpty()) RollbackPayload->SetStringField(TEXT("componentName"), ComponentName);
	}
	MCPSetRollback(Result, TEXT("set_collision"), RollbackPayload);
	Result->SetBoolField(TEXT("rollbackCarriesEveryResponse"), bCapturedEveryResponse);
	Result->SetBoolField(TEXT("previousProfileWasCustom"), bPreviousProfileWasCustom);

	// Two independent ways this can fall short of exact, reported separately
	// rather than folded into one flag.
	const bool bLossy = Targets.Num() > 1 || bPreviousProfileWasCustom;
	Result->SetBoolField(TEXT("rollbackLossy"), bLossy);
	if (bLossy)
	{
		FString Note;
		if (Targets.Num() > 1)
		{
			Note += FString::Printf(
				TEXT("The record carries the settings read from the FIRST of %d component(s) written. Components that "
					 "differed from each other before this call all come back with that one's values. Name a single "
					 "'componentName' to get an exact inverse. "),
				Targets.Num());
		}
		if (bPreviousProfileWasCustom)
		{
			Note += TEXT("The component's profile NAME was the 'Custom' sentinel, which names no profile the engine "
						 "can look up, so no collisionProfile is carried. Its behaviour is restored in full from the "
						 "collision mode, object type and the complete per-channel response table, and the profile "
						 "reads as Custom again afterwards.");
		}
		Result->SetStringField(TEXT("rollbackNote"), Note.TrimEnd());
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPhysicsHandlers::SetBodyProperties(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorLabel;
	if (auto Err = RequireStringAlt(Params, TEXT("actorLabel"), TEXT("actorPath"), ActorLabel)) return Err;

	REQUIRE_EDITOR_WORLD(World);

	TSharedPtr<FJsonValue> ActorErr;
	AActor* Actor = MCPResolveActor(World, Params, ActorErr);
	if (!Actor) return ActorErr;
	ActorLabel = Actor->GetActorLabel();

	// Get all PrimitiveComponents
	int32 ComponentsModified = 0;
	TArray<UPrimitiveComponent*> PrimitiveComponents;
	Actor->GetComponents<UPrimitiveComponent>(PrimitiveComponents);

	// Track which properties were set
	TArray<FString> PropertiesSet;

	// Capture previous values from first component for rollback payload
	TSharedPtr<FJsonObject> PrevPayload = MakeShared<FJsonObject>();
	PrevPayload->SetStringField(TEXT("actorLabel"), ActorLabel);
	bool bCapturedPrev = false;

	for (UPrimitiveComponent* PrimComp : PrimitiveComponents)
	{
		if (!PrimComp) continue;

		FBodyInstance* BodyInstance = PrimComp->GetBodyInstance();
		if (!BodyInstance) continue;

		if (!bCapturedPrev)
		{
			double Mass = 0.0;
			if (Params->TryGetNumberField(TEXT("mass"), Mass))
			{
				PrevPayload->SetNumberField(TEXT("mass"), BodyInstance->GetMassOverride());
			}
			double LinearDamping = 0.0;
			if (Params->TryGetNumberField(TEXT("linearDamping"), LinearDamping))
			{
				PrevPayload->SetNumberField(TEXT("linearDamping"), BodyInstance->LinearDamping);
			}
			double AngularDamping = 0.0;
			if (Params->TryGetNumberField(TEXT("angularDamping"), AngularDamping))
			{
				PrevPayload->SetNumberField(TEXT("angularDamping"), BodyInstance->AngularDamping);
			}
			bool bEnableGravity = true;
			if (Params->TryGetBoolField(TEXT("enableGravity"), bEnableGravity))
			{
				PrevPayload->SetBoolField(TEXT("enableGravity"), BodyInstance->bEnableGravity);
			}
			bCapturedPrev = true;
		}

		// Set mass override if provided
		double Mass = 0.0;
		if (Params->TryGetNumberField(TEXT("mass"), Mass))
		{
			BodyInstance->SetMassOverride(Mass);
			if (ComponentsModified == 0) PropertiesSet.Add(TEXT("mass"));
		}

		// Set linear damping if provided
		double LinearDamping = 0.0;
		if (Params->TryGetNumberField(TEXT("linearDamping"), LinearDamping))
		{
			BodyInstance->LinearDamping = LinearDamping;
			PrimComp->SetLinearDamping(LinearDamping);
			if (ComponentsModified == 0) PropertiesSet.Add(TEXT("linearDamping"));
		}

		// Set angular damping if provided
		double AngularDamping = 0.0;
		if (Params->TryGetNumberField(TEXT("angularDamping"), AngularDamping))
		{
			BodyInstance->AngularDamping = AngularDamping;
			PrimComp->SetAngularDamping(AngularDamping);
			if (ComponentsModified == 0) PropertiesSet.Add(TEXT("angularDamping"));
		}

		// Set gravity enabled if provided
		bool bEnableGravity = true;
		if (Params->TryGetBoolField(TEXT("enableGravity"), bEnableGravity))
		{
			PrimComp->SetEnableGravity(bEnableGravity);
			if (ComponentsModified == 0) PropertiesSet.Add(TEXT("enableGravity"));
		}

		ComponentsModified++;
	}

	// Build properties set list
	TArray<TSharedPtr<FJsonValue>> PropsArray;
	for (const FString& PropName : PropertiesSet)
	{
		PropsArray.Add(MakeShared<FJsonValueString>(PropName));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("actorPath"), Actor->GetPathName());
	Result->SetArrayField(TEXT("propertiesSet"), PropsArray);
	Result->SetNumberField(TEXT("componentsModified"), ComponentsModified);
	Result->SetBoolField(TEXT("success"), ComponentsModified > 0);

	if (ComponentsModified == 0)
	{
		Result->SetStringField(TEXT("warning"), TEXT("No PrimitiveComponents with BodyInstance found on actor"));
		Result->SetBoolField(TEXT("updated"), false);
		Result->SetBoolField(TEXT("unchanged"), true);
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("The actor has no PrimitiveComponent with a BodyInstance, so nothing was written and there is "
				 "nothing to undo."));
	}
	else if (PropertiesSet.Num() > 0)
	{
		MCPSetUpdated(Result);
		Result->SetBoolField(TEXT("unchanged"), false);
		// The inverse is this same action carrying the values that were there,
		// under the REGISTERED method name - gameplay(set_physics_properties).
		// The payload's mass/linearDamping/angularDamping/enableGravity are that
		// handler's own parameter names, and only the ones this call was asked
		// to write are in it.
		PrevPayload->SetStringField(TEXT("actorPath"), Actor->GetPathName());
		MCPSetRollback(Result, TEXT("set_physics_properties"), PrevPayload);
		// The previous values are read from the FIRST component with a body, and
		// every one of them is written, so components that differed all come back
		// with that one's numbers.
		Result->SetBoolField(TEXT("rollbackLossy"), ComponentsModified > 1);
		if (ComponentsModified > 1)
		{
			Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
				TEXT("The record carries the values read from the FIRST of %d component body(s) written. Components "
					 "that differed from each other before this call all come back with that one's values."),
				ComponentsModified));
		}
	}
	else
	{
		// No properties were actually requested
		MCPSetExisted(Result);
		Result->SetBoolField(TEXT("updated"), false);
		Result->SetBoolField(TEXT("unchanged"), true);
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("None of mass, linearDamping, angularDamping or enableGravity was passed, so nothing was written "
				 "and there is nothing to undo."));
	}

	return MCPResult(Result);
}

// #676: apply an impulse or force to an actor's physics body so its motion can
// be observed over time (loop read_actor_motion to sample the response).
// Registered for both add_impulse and add_force; the mode param (or the
// registered name intent) selects. Defaults to impulse.
TSharedPtr<FJsonValue> FPhysicsHandlers::AddImpulse(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorLabel;
	if (auto Err = RequireStringAlt(Params, TEXT("actorLabel"), TEXT("actorPath"), ActorLabel)) return Err;

	const FString WorldScope = OptionalString(Params, TEXT("world"), TEXT("auto"));
	UWorld* World = ResolveWorldFromParams(Params, *WorldScope);
	if (!World) return MCPError(TEXT("World not available"));

	FMCPActorSelector ActorSel;
	ActorSel.Match = EMCPActorMatch::LabelNameOrPath;
	ActorSel.WorldLabel = World->IsGameWorld() ? TEXT("PIE") : TEXT("editor");
	TSharedPtr<FJsonValue> ActorErr;
	AActor* Actor = MCPResolveActor(World, Params, ActorErr, ActorSel);
	if (!Actor) return ActorErr;
	ActorLabel = Actor->GetActorLabel();

	// Resolve the primitive component: named, or the root.
	const FString ComponentName = OptionalString(Params, TEXT("componentName"));
	UPrimitiveComponent* Prim = nullptr;
	if (!ComponentName.IsEmpty())
	{
		for (UActorComponent* C : Actor->GetComponents())
		{
			if (C->GetName() == ComponentName) { Prim = Cast<UPrimitiveComponent>(C); break; }
		}
	}
	else
	{
		Prim = Cast<UPrimitiveComponent>(Actor->GetRootComponent());
		if (!Prim) Prim = Actor->FindComponentByClass<UPrimitiveComponent>();
	}
	if (!Prim) return MCPError(FString::Printf(TEXT("No PrimitiveComponent found on '%s'"), *ActorLabel));
	if (!Prim->IsSimulatingPhysics())
	{
		return MCPError(FString::Printf(TEXT("Component '%s' is not simulating physics; call set_simulate_physics first"), *Prim->GetName()));
	}

	FVector Vec = FVector::ZeroVector;
	if (auto Err = RequireVec3(Params, TEXT("impulse"), Vec))
	{
		// Accept 'force' or 'vector' as aliases.
		Vec = OptionalVec3(Params, TEXT("force"), OptionalVec3(Params, TEXT("vector")));
		if (Vec.IsNearlyZero()) return Err;
	}

	const FString Mode = OptionalString(Params, TEXT("mode"), TEXT("impulse")).ToLower();
	const FString BoneName = OptionalString(Params, TEXT("boneName"));
	const FName Bone = BoneName.IsEmpty() ? NAME_None : FName(*BoneName);

	// Read before the push, so the response can report a MEASURED effect rather
	// than the request that was made.
	const FVector VelocityBefore = Prim->GetPhysicsLinearVelocity();
	const FVector AngularBefore = Prim->GetPhysicsAngularVelocityInDegrees();

	const TSharedPtr<FJsonObject>* AtLoc = nullptr;
	if (Params->TryGetObjectField(TEXT("location"), AtLoc) && AtLoc)
	{
		FVector Loc = FVector::ZeroVector; ReadVec3Fields(*AtLoc, Loc);
		Prim->AddImpulseAtLocation(Vec, Loc, Bone);
	}
	else if (Mode == TEXT("force"))
	{
		Prim->AddForce(Vec, Bone, OptionalBool(Params, TEXT("accelChange"), false));
	}
	else
	{
		Prim->AddImpulse(Vec, Bone, OptionalBool(Params, TEXT("velChange"), false));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
	Result->SetStringField(TEXT("actorPath"), Actor->GetPathName());
	Result->SetStringField(TEXT("component"), Prim->GetName());
	Result->SetStringField(TEXT("mode"), Mode);
	Result->SetObjectField(TEXT("vector"), MCPVec3ToJsonObject(Vec));
	const FVector VelocityAfter = Prim->GetPhysicsLinearVelocity();
	const FVector AngularAfter = Prim->GetPhysicsAngularVelocityInDegrees();
	Result->SetObjectField(TEXT("linearVelocity"), MCPVec3ToJsonObject(VelocityAfter));
	Result->SetObjectField(TEXT("previousLinearVelocity"), MCPVec3ToJsonObject(VelocityBefore));
	Result->SetObjectField(TEXT("angularVelocity"), MCPVec3ToJsonObject(AngularAfter));
	Result->SetObjectField(TEXT("previousAngularVelocity"), MCPVec3ToJsonObject(AngularBefore));

	// The idempotency marker is MEASURED, not asserted: the body's linear and
	// angular velocity are read either side of the push. A force in 'force'
	// mode is integrated over the next substep rather than immediately, and a
	// vector the solver rejects as below its own threshold moves nothing, so
	// both legitimately report unchanged. What this does NOT claim is that
	// repeating the call is safe: two impulses are two impulses, which is why
	// there is no already* flag and why the note below says so.
	const bool bMotionChanged =
		!VelocityAfter.Equals(VelocityBefore, UE_KINDA_SMALL_NUMBER)
		|| !AngularAfter.Equals(AngularBefore, UE_KINDA_SMALL_NUMBER);
	Result->SetBoolField(TEXT("unchanged"), !bMotionChanged);
	if (!bMotionChanged)
	{
		Result->SetStringField(TEXT("unchangedNote"),
			TEXT("Neither the linear nor the angular velocity moved. In 'force' mode that is expected: a force is "
				 "integrated over the next physics substep rather than applied instantly, so sample the motion with "
				 "level(read_actor_motion) instead of reading it back here. In 'impulse' mode it means the solver "
				 "absorbed the vector - check that it is large enough for the body's mass."));
	}

	// No inverse. This injects a one-shot push into a simulating body. The
	// solver has already integrated it, so the body has moved and collided; an
	// equal and opposite impulse cancels the velocity it added but restores
	// neither the position the body would have had nor anything it hit on the
	// way. Two calls are two impulses, not a repeated state change, so nothing
	// here is an already* flag.
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("An impulse is applied to a live physics body and the solver integrates it immediately. There is no call "
			 "that un-applies one: an equal and opposite impulse cancels the velocity but not the motion that already "
			 "happened. Read the actor's transform before the impulse if it has to be put back."));
	return MCPResult(Result);
}
