#include "PhysicsHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Handlers/BlueprintHandlers_Internal.h"
#include "Components/PrimitiveComponent.h"
#include "PhysicsEngine/BodyInstance.h"
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
	if (auto Err = RequireString(Params, TEXT("actorLabel"), ActorLabel)) return Err;

	FString ProfileName;
	if (auto Err = RequireString(Params, TEXT("profileName"), ProfileName)) return Err;

	REQUIRE_EDITOR_WORLD(World);

	AActor* Actor = FindActorByLabel(World, ActorLabel);
	if (!Actor)
	{
		return MCPError(FString::Printf(TEXT("Actor not found: %s"), *ActorLabel));
	}

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
	Result->SetStringField(TEXT("profileName"), ProfileName);

	if (bAllAlreadyMatch)
	{
		MCPSetExisted(Result);
		Result->SetBoolField(TEXT("updated"), false);
		Result->SetNumberField(TEXT("componentsModified"), 0);
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

	if (ComponentsModified == 0)
	{
		Result->SetStringField(TEXT("warning"), TEXT("No PrimitiveComponents found on actor"));
	}
	else
	{
		MCPSetUpdated(Result);
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("actorLabel"), ActorLabel);
		Payload->SetStringField(TEXT("profileName"), PrevProfile);
		MCPSetRollback(Result, TEXT("set_collision_profile"), Payload);
	}

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPhysicsHandlers::SetPhysicsEnabled(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorLabel;
	if (auto Err = RequireString(Params, TEXT("actorLabel"), ActorLabel)) return Err;

	bool bEnabled = true;
	if (!Params->TryGetBoolField(TEXT("enabled"), bEnabled))
	{
		return MCPError(TEXT("Missing 'enabled' parameter (true/false)"));
	}

	REQUIRE_EDITOR_WORLD(World);

	AActor* Actor = FindActorByLabel(World, ActorLabel);
	if (!Actor)
	{
		return MCPError(FString::Printf(TEXT("Actor not found: %s"), *ActorLabel));
	}

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
	Result->SetBoolField(TEXT("enabled"), bEnabled);

	if (bAllAlready)
	{
		MCPSetExisted(Result);
		Result->SetBoolField(TEXT("updated"), false);
		Result->SetNumberField(TEXT("componentsModified"), 0);
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

	if (ComponentsModified == 0)
	{
		Result->SetStringField(TEXT("warning"), TEXT("No PrimitiveComponents found on actor"));
	}
	else
	{
		MCPSetUpdated(Result);
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("actorLabel"), ActorLabel);
		Payload->SetBoolField(TEXT("enabled"), bPrev);
		MCPSetRollback(Result, TEXT("set_physics_enabled"), Payload);
	}

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPhysicsHandlers::SetCollisionEnabled(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorLabel;
	if (auto Err = RequireString(Params, TEXT("actorLabel"), ActorLabel)) return Err;

	FString CollisionType;
	if (auto Err = RequireString(Params, TEXT("collisionType"), CollisionType)) return Err;

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

	AActor* Actor = FindActorByLabel(World, ActorLabel);
	if (!Actor)
	{
		return MCPError(FString::Printf(TEXT("Actor not found: %s"), *ActorLabel));
	}

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
	Result->SetStringField(TEXT("collisionType"), CollisionType);

	if (bAllAlready)
	{
		MCPSetExisted(Result);
		Result->SetBoolField(TEXT("updated"), false);
		Result->SetNumberField(TEXT("componentsModified"), 0);
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

	if (ComponentsModified == 0)
	{
		Result->SetStringField(TEXT("warning"), TEXT("No PrimitiveComponents found on actor"));
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
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("actorLabel"), ActorLabel);
		Payload->SetStringField(TEXT("collisionType"), PrevTypeStr);
		MCPSetRollback(Result, TEXT("set_collision_enabled"), Payload);
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
	else if (Params->TryGetStringField(TEXT("actorLabel"), ActorLabel) && !ActorLabel.IsEmpty())
	{
		UWorld* World = GetEditorWorld();
		if (!World) return MCPError(TEXT("No editor world available"));
		Actor = FindActorByLabel(World, ActorLabel);
		if (!Actor) return MCPError(FString::Printf(TEXT("Actor not found: %s"), *ActorLabel));

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

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("target"), TargetDesc);
	Result->SetNumberField(TEXT("componentsModified"), Targets.Num());
	TArray<TSharedPtr<FJsonValue>> AppliedArr;
	for (const FString& A : Applied) AppliedArr.Add(MakeShared<FJsonValueString>(A));
	Result->SetArrayField(TEXT("applied"), AppliedArr);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPhysicsHandlers::SetBodyProperties(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorLabel;
	if (auto Err = RequireString(Params, TEXT("actorLabel"), ActorLabel)) return Err;

	REQUIRE_EDITOR_WORLD(World);

	AActor* Actor = FindActorByLabel(World, ActorLabel);
	if (!Actor)
	{
		return MCPError(FString::Printf(TEXT("Actor not found: %s"), *ActorLabel));
	}

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
	Result->SetArrayField(TEXT("propertiesSet"), PropsArray);
	Result->SetNumberField(TEXT("componentsModified"), ComponentsModified);
	Result->SetBoolField(TEXT("success"), ComponentsModified > 0);

	if (ComponentsModified == 0)
	{
		Result->SetStringField(TEXT("warning"), TEXT("No PrimitiveComponents with BodyInstance found on actor"));
	}
	else if (PropertiesSet.Num() > 0)
	{
		MCPSetUpdated(Result);
		MCPSetRollback(Result, TEXT("set_body_properties"), PrevPayload);
	}
	else
	{
		// No properties were actually requested
		MCPSetExisted(Result);
		Result->SetBoolField(TEXT("updated"), false);
	}

	return MCPResult(Result);
}
