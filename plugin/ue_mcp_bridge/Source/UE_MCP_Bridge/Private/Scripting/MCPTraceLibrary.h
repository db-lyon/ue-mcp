#pragma once

// Line traces callable from editor Python (#1283). A run_python_file script has
// no binding of the engine trace functions that converts a collision channel,
// so this exposes the level(line_trace) query as unreal.MCPTraceLibrary.

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MCPTraceLibrary.generated.h"

/** One line trace's answer, flattened so a script reads plain fields. */
USTRUCT(BlueprintType)
struct FMCPTraceHit
{
	GENERATED_BODY()

	/** True when the ray hit something that blocks the channel. */
	UPROPERTY(BlueprintReadOnly, Category = "MCP|Trace")
	bool bHit = false;

	UPROPERTY(BlueprintReadOnly, Category = "MCP|Trace")
	FVector Location = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "MCP|Trace")
	FVector ImpactPoint = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "MCP|Trace")
	FVector ImpactNormal = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "MCP|Trace")
	double Distance = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "MCP|Trace")
	FString ActorPath;

	UPROPERTY(BlueprintReadOnly, Category = "MCP|Trace")
	FString ActorLabel;

	UPROPERTY(BlueprintReadOnly, Category = "MCP|Trace")
	FString ComponentName;

	UPROPERTY(BlueprintReadOnly, Category = "MCP|Trace")
	bool bStartPenetrating = false;

	UPROPERTY(BlueprintReadOnly, Category = "MCP|Trace")
	FString ActorClass;

	UPROPERTY(BlueprintReadOnly, Category = "MCP|Trace")
	FString ComponentClass;

	UPROPERTY(BlueprintReadOnly, Category = "MCP|Trace")
	FVector Normal = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "MCP|Trace")
	int32 FaceIndex = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "MCP|Trace")
	FString BoneName;

	/** Path of the physical material the hit reported, empty when none. */
	UPROPERTY(BlueprintReadOnly, Category = "MCP|Trace")
	FString PhysicalMaterial;

	/** Path of the world that answered. */
	UPROPERTY(BlueprintReadOnly, Category = "MCP|Trace")
	FString World;

	/** Why no trace ran (no PIE world, unknown channel); empty when it ran. */
	UPROPERTY(BlueprintReadOnly, Category = "MCP|Trace")
	FString Error;
};

UCLASS()
class UMCPTraceLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Line trace by collision channel name against the PIE world (default) or
	 * the editor world. World is pie | editor | auto; PieInstance picks a PIE
	 * world (-1 for the first). Channel takes the project's channel names.
	 */
	UFUNCTION(BlueprintCallable, Category = "MCP|Trace")
	static FMCPTraceHit LineTrace(FVector Start, FVector End, const FString& World = TEXT("pie"),
		int32 PieInstance = -1, const FString& Channel = TEXT("Visibility"), bool bTraceComplex = false);

	/** Sphere, axis-aligned box or vertical capsule sweep. Multi returns overlaps
	 * through the first blocker (64 maximum). Errors return one entry with Error.
	 * A single miss returns one entry with bHit false; a multi miss is empty.
	 * IgnoreActors accepts labels or full actor paths in the selected world. */
	UFUNCTION(BlueprintCallable, Category = "MCP|Trace", meta = (AutoCreateRefTerm = "IgnoreActors"))
	static TArray<FMCPTraceHit> ShapeTrace(FVector Start, FVector End, const FString& Shape,
		const TArray<FString>& IgnoreActors, double Radius = 0.0, FVector HalfExtent = FVector(0, 0, 0),
		double HalfHeight = 0.0, const FString& World = TEXT("pie"), int32 PieInstance = -1,
		const FString& Channel = TEXT("Visibility"), bool bTraceComplex = false, bool bMulti = false);
};
