#include "Scripting/MCPTraceLibrary.h"

#include "CollisionQueryParams.h"
#include "HandlerUtils.h"
#include "Handlers/Level/LevelTrace.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HandlerObjectUtils.h"
#include "HandlerWorld.h"
#include "PhysicalMaterials/PhysicalMaterial.h"

FMCPTraceHit UMCPTraceLibrary::LineTrace(FVector Start, FVector End, const FString& World, int32 PieInstance,
	const FString& Channel, bool bTraceComplex)
{
	FMCPTraceHit Out;
	UWorld* TraceWorld = ResolveWorldScope(World, PieInstance);
	if (!TraceWorld)
	{
		Out.Error = FString::Printf(TEXT("No world for scope '%s'%s"), *World,
			World.Equals(TEXT("editor"), ESearchCase::IgnoreCase) ? TEXT("") : TEXT(": is PIE running?"));
		return Out;
	}
	Out.World = TraceWorld->GetPathName();

	ECollisionChannel TraceChannel = ECC_Visibility;
	FString ResolvedChannel;
	if (!MCPResolveCollisionChannel(Channel, TraceChannel, ResolvedChannel))
	{
		Out.Error = FString::Printf(TEXT("Unknown collision channel '%s'"), *Channel);
		return Out;
	}

	FCollisionQueryParams Query(SCENE_QUERY_STAT(MCPPythonLineTrace), bTraceComplex);
	Query.bReturnPhysicalMaterial = true;
	Query.bReturnFaceIndex = bTraceComplex;
	FHitResult Hit;
	Out.bHit = TraceWorld->LineTraceSingleByChannel(Hit, Start, End, TraceChannel, Query);
	if (!Out.bHit) return Out;

	Out.bStartPenetrating = Hit.bStartPenetrating;
	Out.Normal = Hit.Normal;
	Out.FaceIndex = Hit.FaceIndex;
	if (!Hit.BoneName.IsNone()) Out.BoneName = Hit.BoneName.ToString();
	Out.Location = Hit.Location;
	Out.ImpactPoint = Hit.ImpactPoint;
	Out.ImpactNormal = Hit.ImpactNormal;
	Out.Distance = Hit.Distance;
	if (AActor* HitActor = Hit.GetActor())
	{
		Out.ActorPath = HitActor->GetPathName();
		Out.ActorLabel = HitActor->GetActorLabel();
		Out.ActorClass = HitActor->GetClass()->GetName();
	}
	if (UPrimitiveComponent* HitComponent = Hit.GetComponent())
	{
		Out.ComponentName = HitComponent->GetName();
		Out.ComponentClass = HitComponent->GetClass()->GetName();
	}
	if (Hit.PhysMaterial.IsValid())
	{
		Out.PhysicalMaterial = Hit.PhysMaterial->GetPathName();
	}
	return Out;
}

TArray<FMCPTraceHit> UMCPTraceLibrary::ShapeTrace(FVector Start, FVector End, const FString& Shape,
	const TArray<FString>& IgnoreActors, double Radius, FVector HalfExtent, double HalfHeight,
	const FString& World, int32 PieInstance, const FString& Channel, bool bTraceComplex, bool bMulti)
{
	auto Params = MakeShared<FJsonObject>();
	Params->SetObjectField(TEXT("start"), MCPVec3ToJsonObject(Start));
	Params->SetObjectField(TEXT("end"), MCPVec3ToJsonObject(End));
	Params->SetStringField(TEXT("shape"), Shape);
	Params->SetNumberField(TEXT("radius"), Radius);
	Params->SetObjectField(TEXT("halfExtent"), MCPVec3ToJsonObject(HalfExtent));
	Params->SetNumberField(TEXT("halfHeight"), HalfHeight);
	Params->SetStringField(TEXT("world"), World);
	Params->SetNumberField(TEXT("pieInstance"), PieInstance);
	Params->SetStringField(TEXT("channel"), Channel);
	Params->SetBoolField(TEXT("traceComplex"), bTraceComplex);
	Params->SetBoolField(TEXT("multi"), bMulti);
	TArray<TSharedPtr<FJsonValue>> Ignore;
	for (const FString& Actor : IgnoreActors) Ignore.Add(MakeShared<FJsonValueString>(Actor));
	Params->SetArrayField(TEXT("ignoreActors"), Ignore);
	TSharedPtr<FJsonValue> Error;
	UWorld* TraceWorld = MCPTrace::ResolveTraceWorld(Params, Error);
	const auto Result = TraceWorld ? MCPTrace::Execute(TraceWorld, Params, true)->AsObject() : Error->AsObject();
	TArray<FMCPTraceHit> Out;
	if (!Result->GetBoolField(TEXT("success")))
	{
		FMCPTraceHit Failure;
		Failure.Error = Result->GetStringField(TEXT("error"));
		Out.Add(Failure);
		return Out;
	}
	TArray<TSharedPtr<FJsonValue>> Rows;
	if (bMulti) Rows = Result->GetArrayField(TEXT("hits"));
	else Rows.Add(MakeShared<FJsonValueObject>(Result));
	for (const auto& Value : Rows)
	{
		const auto Row = Value->AsObject();
		FMCPTraceHit Hit;
		Hit.bHit = Row->GetBoolField(TEXT("hit"));
		Hit.World = TraceWorld->GetPathName();
		Row->TryGetBoolField(TEXT("bStartPenetrating"), Hit.bStartPenetrating);
		Hit.Location = OptionalVec3(Row, TEXT("location"));
		Hit.ImpactPoint = OptionalVec3(Row, TEXT("impactPoint"));
		Hit.ImpactNormal = OptionalVec3(Row, TEXT("impactNormal"));
		Hit.Normal = OptionalVec3(Row, TEXT("normal"));
		Hit.Distance = OptionalNumber(Row, TEXT("distance"), 0);
		Hit.FaceIndex = OptionalInt(Row, TEXT("faceIndex"), INDEX_NONE);
		Hit.ActorPath = OptionalString(Row, TEXT("actorPath"));
		Hit.ActorLabel = OptionalString(Row, TEXT("actorLabel"));
		Hit.ActorClass = OptionalString(Row, TEXT("actorClass"));
		Hit.ComponentName = OptionalString(Row, TEXT("componentName"));
		Hit.ComponentClass = OptionalString(Row, TEXT("componentClass"));
		Hit.PhysicalMaterial = OptionalString(Row, TEXT("physicalMaterial"));
		Hit.BoneName = OptionalString(Row, TEXT("boneName"));
		Out.Add(Hit);
	}
	return Out;
}
