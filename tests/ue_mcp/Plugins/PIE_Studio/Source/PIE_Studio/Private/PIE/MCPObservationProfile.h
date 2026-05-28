#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MCPObservationProfile.generated.h"

USTRUCT(BlueprintType)
struct FMCPTrackedValueEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tracking")
	FString Path;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tracking", meta=(ClampMin="0"))
	float DriftThreshold = 0.f;
};

USTRUCT(BlueprintType)
struct FMCPTrackedActorEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tracking")
	FString ActorId;
};

UCLASS(BlueprintType)
class UMCPObservationProfile : public UDataAsset
{
	GENERATED_BODY()
public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Values",
		meta=(TitleProperty="Path"))
	TArray<FMCPTrackedValueEntry> TrackedValues;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Actors",
		meta=(TitleProperty="ActorId"))
	TArray<FMCPTrackedActorEntry> TrackedActors;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sampling")
	bool bCapturePawnState = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sampling")
	bool bCaptureMontage = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Thresholds")
	float PositionThresholdCm = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Thresholds")
	float RotationThresholdDeg = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Thresholds")
	float VelocityThresholdCms = 25.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Thresholds")
	float TrackedValueDefaultThreshold = 0.f;
};
