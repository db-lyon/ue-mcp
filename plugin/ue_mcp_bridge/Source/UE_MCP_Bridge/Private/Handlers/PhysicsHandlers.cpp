#include "PhysicsHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Components/PrimitiveComponent.h"
#include "PhysicsEngine/BodyInstance.h"
#include "Engine/World.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "GameFramework/Actor.h"
#include "EngineUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

void FPhysicsHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	Registry.RegisterHandler(TEXT("set_collision_profile"), &SetCollisionProfile);
	Registry.RegisterHandler(TEXT("set_physics_enabled"), &SetPhysicsEnabled);
	Registry.RegisterHandler(TEXT("set_collision_enabled"), &SetCollisionEnabled);
	Registry.RegisterHandler(TEXT("set_body_properties"), &SetBodyProperties);
	// Aliases
	Registry.RegisterHandler(TEXT("set_simulate_physics"), &SetPhysicsEnabled);
	Registry.RegisterHandler(TEXT("set_physics_properties"), &SetBodyProperties);
}

TSharedPtr<FJsonValue> FPhysicsHandlers::SetCollisionProfile(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorLabel;
	if (auto Err = RequireString(Params, TEXT("actorLabel"), ActorLabel)) return Err;

	FString ProfileName;
	if (auto Err = RequireString(Params, TEXT("profileName"), ProfileName)) return Err;

	REQUIRE_EDITOR_WORLD(World);

	// Find actor by label
	AActor* Actor = nullptr;
	for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
	{
		if ((*ActorIt)->GetActorLabel() == ActorLabel)
		{
			Actor = *ActorIt;
			break;
		}
	}

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

	// Find actor by label
	AActor* Actor = nullptr;
	for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
	{
		if ((*ActorIt)->GetActorLabel() == ActorLabel)
		{
			Actor = *ActorIt;
			break;
		}
	}

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

	// Find actor by label
	AActor* Actor = nullptr;
	for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
	{
		if ((*ActorIt)->GetActorLabel() == ActorLabel)
		{
			Actor = *ActorIt;
			break;
		}
	}

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

TSharedPtr<FJsonValue> FPhysicsHandlers::SetBodyProperties(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorLabel;
	if (auto Err = RequireString(Params, TEXT("actorLabel"), ActorLabel)) return Err;

	REQUIRE_EDITOR_WORLD(World);

	// Find actor by label
	AActor* Actor = nullptr;
	for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
	{
		if ((*ActorIt)->GetActorLabel() == ActorLabel)
		{
			Actor = *ActorIt;
			break;
		}
	}

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
