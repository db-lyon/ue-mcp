#include "PhysicsHandlers.h"
#include "HandlerRegistry.h"
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
}

TSharedPtr<FJsonValue> FPhysicsHandlers::SetCollisionProfile(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString ActorLabel;
	if (!Params->TryGetStringField(TEXT("actorLabel"), ActorLabel))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'actorLabel' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString ProfileName;
	if (!Params->TryGetStringField(TEXT("profileName"), ProfileName))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'profileName' parameter (e.g. BlockAll, OverlapAll, BlockAllDynamic, NoCollision, Pawn, PhysicsActor)"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		Result->SetStringField(TEXT("error"), TEXT("Editor world not available"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

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
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Actor not found: %s"), *ActorLabel));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Set collision profile on all PrimitiveComponents
	int32 ComponentsModified = 0;
	TArray<UPrimitiveComponent*> PrimitiveComponents;
	Actor->GetComponents<UPrimitiveComponent>(PrimitiveComponents);

	for (UPrimitiveComponent* PrimComp : PrimitiveComponents)
	{
		if (!PrimComp) continue;
		PrimComp->SetCollisionProfileName(FName(*ProfileName));
		ComponentsModified++;
	}

	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("profileName"), ProfileName);
	Result->SetNumberField(TEXT("componentsModified"), ComponentsModified);
	Result->SetBoolField(TEXT("success"), ComponentsModified > 0);

	if (ComponentsModified == 0)
	{
		Result->SetStringField(TEXT("warning"), TEXT("No PrimitiveComponents found on actor"));
	}

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FPhysicsHandlers::SetPhysicsEnabled(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString ActorLabel;
	if (!Params->TryGetStringField(TEXT("actorLabel"), ActorLabel))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'actorLabel' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	bool bEnabled = true;
	if (!Params->TryGetBoolField(TEXT("enabled"), bEnabled))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'enabled' parameter (true/false)"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		Result->SetStringField(TEXT("error"), TEXT("Editor world not available"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

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
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Actor not found: %s"), *ActorLabel));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Enable/disable physics on all PrimitiveComponents
	int32 ComponentsModified = 0;
	TArray<UPrimitiveComponent*> PrimitiveComponents;
	Actor->GetComponents<UPrimitiveComponent>(PrimitiveComponents);

	for (UPrimitiveComponent* PrimComp : PrimitiveComponents)
	{
		if (!PrimComp) continue;
		PrimComp->SetSimulatePhysics(bEnabled);
		ComponentsModified++;
	}

	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetBoolField(TEXT("enabled"), bEnabled);
	Result->SetNumberField(TEXT("componentsModified"), ComponentsModified);
	Result->SetBoolField(TEXT("success"), ComponentsModified > 0);

	if (ComponentsModified == 0)
	{
		Result->SetStringField(TEXT("warning"), TEXT("No PrimitiveComponents found on actor"));
	}

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FPhysicsHandlers::SetCollisionEnabled(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString ActorLabel;
	if (!Params->TryGetStringField(TEXT("actorLabel"), ActorLabel))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'actorLabel' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString CollisionType;
	if (!Params->TryGetStringField(TEXT("collisionType"), CollisionType))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'collisionType' parameter (NoCollision, QueryOnly, PhysicsOnly, QueryAndPhysics)"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
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
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Unknown collision type: '%s'. Use NoCollision, QueryOnly, PhysicsOnly, or QueryAndPhysics."), *CollisionType));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		Result->SetStringField(TEXT("error"), TEXT("Editor world not available"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

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
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Actor not found: %s"), *ActorLabel));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Set collision enabled on all PrimitiveComponents
	int32 ComponentsModified = 0;
	TArray<UPrimitiveComponent*> PrimitiveComponents;
	Actor->GetComponents<UPrimitiveComponent>(PrimitiveComponents);

	for (UPrimitiveComponent* PrimComp : PrimitiveComponents)
	{
		if (!PrimComp) continue;
		PrimComp->SetCollisionEnabled(CollisionEnabled);
		ComponentsModified++;
	}

	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("collisionType"), CollisionType);
	Result->SetNumberField(TEXT("componentsModified"), ComponentsModified);
	Result->SetBoolField(TEXT("success"), ComponentsModified > 0);

	if (ComponentsModified == 0)
	{
		Result->SetStringField(TEXT("warning"), TEXT("No PrimitiveComponents found on actor"));
	}

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FPhysicsHandlers::SetBodyProperties(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString ActorLabel;
	if (!Params->TryGetStringField(TEXT("actorLabel"), ActorLabel))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'actorLabel' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		Result->SetStringField(TEXT("error"), TEXT("Editor world not available"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

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
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Actor not found: %s"), *ActorLabel));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Get all PrimitiveComponents
	int32 ComponentsModified = 0;
	TArray<UPrimitiveComponent*> PrimitiveComponents;
	Actor->GetComponents<UPrimitiveComponent>(PrimitiveComponents);

	// Track which properties were set
	TArray<FString> PropertiesSet;

	for (UPrimitiveComponent* PrimComp : PrimitiveComponents)
	{
		if (!PrimComp) continue;

		FBodyInstance* BodyInstance = PrimComp->GetBodyInstance();
		if (!BodyInstance) continue;

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

	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetArrayField(TEXT("propertiesSet"), PropsArray);
	Result->SetNumberField(TEXT("componentsModified"), ComponentsModified);
	Result->SetBoolField(TEXT("success"), ComponentsModified > 0);

	if (ComponentsModified == 0)
	{
		Result->SetStringField(TEXT("warning"), TEXT("No PrimitiveComponents with BodyInstance found on actor"));
	}

	return MakeShared<FJsonValueObject>(Result);
}
