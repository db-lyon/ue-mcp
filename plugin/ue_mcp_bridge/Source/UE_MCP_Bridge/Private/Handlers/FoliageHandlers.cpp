#include "FoliageHandlers.h"
#include "HandlerRegistry.h"
#include "InstancedFoliageActor.h"
#include "FoliageType.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

void FFoliageHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	Registry.RegisterHandler(TEXT("list_foliage_types"), &ListFoliageTypes);
	Registry.RegisterHandler(TEXT("sample_foliage"), &SampleFoliage);
}

TSharedPtr<FJsonValue> FFoliageHandlers::ListFoliageTypes(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		Result->SetStringField(TEXT("error"), TEXT("No editor world available"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	TArray<TSharedPtr<FJsonValue>> FoliageTypesArray;

	for (TActorIterator<AInstancedFoliageActor> It(World); It; ++It)
	{
		AInstancedFoliageActor* FoliageActor = *It;
		if (!FoliageActor) continue;

		const auto& FoliageInfoMap = FoliageActor->GetFoliageInfos();
		for (const auto& Pair : FoliageInfoMap)
		{
			UFoliageType* FoliageType = Pair.Key;
			const FFoliageInfo& FoliageInfo = *Pair.Value;

			if (!FoliageType) continue;

			TSharedPtr<FJsonObject> TypeObj = MakeShared<FJsonObject>();
			TypeObj->SetStringField(TEXT("name"), FoliageType->GetName());
			TypeObj->SetStringField(TEXT("path"), FoliageType->GetPathName());
			TypeObj->SetNumberField(TEXT("instanceCount"), FoliageInfo.Instances.Num());

			// Get source mesh info if available
			UStaticMesh* Mesh = FoliageType->GetStaticMesh();
			if (Mesh)
			{
				TypeObj->SetStringField(TEXT("staticMesh"), Mesh->GetPathName());
			}

			FoliageTypesArray.Add(MakeShared<FJsonValueObject>(TypeObj));
		}
	}

	Result->SetArrayField(TEXT("foliageTypes"), FoliageTypesArray);
	Result->SetNumberField(TEXT("count"), FoliageTypesArray.Num());
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FFoliageHandlers::SampleFoliage(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	// Parse center point
	const TSharedPtr<FJsonObject>* CenterObj = nullptr;
	if (!Params->TryGetObjectField(TEXT("center"), CenterObj) || !CenterObj || !(*CenterObj).IsValid())
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'center' parameter (object with x, y, z)"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	double CenterX = 0, CenterY = 0, CenterZ = 0;
	(*CenterObj)->TryGetNumberField(TEXT("x"), CenterX);
	(*CenterObj)->TryGetNumberField(TEXT("y"), CenterY);
	(*CenterObj)->TryGetNumberField(TEXT("z"), CenterZ);
	FVector Center(CenterX, CenterY, CenterZ);

	double Radius = 1000.0;
	Params->TryGetNumberField(TEXT("radius"), Radius);
	double RadiusSq = Radius * Radius;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		Result->SetStringField(TEXT("error"), TEXT("No editor world available"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	TMap<FString, int32> TypeCounts;
	int32 TotalCount = 0;

	for (TActorIterator<AInstancedFoliageActor> It(World); It; ++It)
	{
		AInstancedFoliageActor* FoliageActor = *It;
		if (!FoliageActor) continue;

		const auto& FoliageInfoMap = FoliageActor->GetFoliageInfos();
		for (const auto& Pair : FoliageInfoMap)
		{
			UFoliageType* FoliageType = Pair.Key;
			const FFoliageInfo& FoliageInfo = *Pair.Value;

			if (!FoliageType) continue;

			FString TypeName = FoliageType->GetName();
			int32 MatchCount = 0;

			for (const FFoliageInstance& Instance : FoliageInfo.Instances)
			{
				FVector InstanceLocation = Instance.Location;
				double DistSq = FVector::DistSquared(Center, InstanceLocation);
				if (DistSq <= RadiusSq)
				{
					MatchCount++;
				}
			}

			if (MatchCount > 0)
			{
				TypeCounts.FindOrAdd(TypeName) += MatchCount;
				TotalCount += MatchCount;
			}
		}
	}

	TArray<TSharedPtr<FJsonValue>> TypesArray;
	for (const auto& Pair : TypeCounts)
	{
		TSharedPtr<FJsonObject> TypeObj = MakeShared<FJsonObject>();
		TypeObj->SetStringField(TEXT("type"), Pair.Key);
		TypeObj->SetNumberField(TEXT("count"), Pair.Value);
		TypesArray.Add(MakeShared<FJsonValueObject>(TypeObj));
	}

	TSharedPtr<FJsonObject> CenterResult = MakeShared<FJsonObject>();
	CenterResult->SetNumberField(TEXT("x"), CenterX);
	CenterResult->SetNumberField(TEXT("y"), CenterY);
	CenterResult->SetNumberField(TEXT("z"), CenterZ);

	Result->SetObjectField(TEXT("center"), CenterResult);
	Result->SetNumberField(TEXT("radius"), Radius);
	Result->SetNumberField(TEXT("totalCount"), TotalCount);
	Result->SetArrayField(TEXT("types"), TypesArray);
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}
