#include "LandscapeHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Landscape.h"
#include "LandscapeDataAccess.h"
#include "LandscapeEditTypes.h"
#include "LandscapeProxy.h"
#include "LandscapeStreamingProxy.h"
#include "LandscapeInfo.h"
#include "LandscapeComponent.h"
#include "LandscapeHeightfieldCollisionComponent.h"
#include "LandscapeSplineActor.h"
#include "LandscapeSplinesComponent.h"
#include "LandscapeSplineControlPoint.h"
#include "LandscapeSplineSegment.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/FileHelper.h"
#include "Misc/SecureHash.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInterface.h"
#include "EditorScriptingUtilities/Public/EditorAssetLibrary.h"
#include "RenderingThread.h"
#include "Components/PrimitiveComponent.h"
#include "PhysicsEngine/BodyInstance.h"
#include "LandscapeLayerInfoObject.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

#if UE_MCP_HAS_5_8_API

namespace
{
	constexpr float HeightCollisionToleranceCm = 0.25f;

	struct FLandscapeHeightCollisionSignature
	{
		ULandscapeHeightfieldCollisionComponent* Collision = nullptr;
		FString ComponentPath;
		int64 RawElementCount = 0;
		int32 ComplexSampleCount = 0;
		int32 CollisionSizeVerts = 0;
		int32 SimpleSampleCount = 0;
		int32 SimpleCollisionSizeVerts = 0;
		FSHAHash RawHash;
		FSHAHash LiveHash;
		FTransform ComponentTransform;
		float RawMinWorldZ = 0.0f;
		float RawMaxWorldZ = 0.0f;
		float LiveMinWorldZ = 0.0f;
		float LiveMaxWorldZ = 0.0f;
	};

	bool ReadCollisionSampleInfo(
		const ULandscapeHeightfieldCollisionComponent* Collision,
		ULandscapeHeightfieldCollisionComponent::FCollisionSampleInfo& OutInfo,
		FString& OutError)
	{
		// GetCollisionSampleInfo is public but not LANDSCAPE_API, so calling it
		// from an external plugin compiles and then fails to link. Reproduce its
		// four-line calculation from the reflected size properties instead.
		static const FIntProperty* CollisionSizeQuadsProperty =
			FindFProperty<FIntProperty>(
				ULandscapeHeightfieldCollisionComponent::StaticClass(),
				TEXT("CollisionSizeQuads"));
		static const FIntProperty* SimpleCollisionSizeQuadsProperty =
			FindFProperty<FIntProperty>(
				ULandscapeHeightfieldCollisionComponent::StaticClass(),
				TEXT("SimpleCollisionSizeQuads"));
		if (!CollisionSizeQuadsProperty || !SimpleCollisionSizeQuadsProperty)
		{
			OutError = TEXT("Landscape collision size properties are unavailable");
			return false;
		}

		const int32 CollisionSizeQuads =
			CollisionSizeQuadsProperty->GetPropertyValue_InContainer(Collision);
		const int32 SimpleCollisionSizeQuads =
			SimpleCollisionSizeQuadsProperty->GetPropertyValue_InContainer(Collision);
		if (CollisionSizeQuads <= 0 || SimpleCollisionSizeQuads < 0)
		{
			OutError = FString::Printf(
				TEXT("Invalid landscape collision quad dimensions: complex=%d, simple=%d"),
				CollisionSizeQuads, SimpleCollisionSizeQuads);
			return false;
		}

		OutInfo.CollisionSizeVerts = CollisionSizeQuads + 1;
		OutInfo.SimpleCollisionSizeVerts =
			SimpleCollisionSizeQuads > 0 ? SimpleCollisionSizeQuads + 1 : 0;
		OutInfo.NumSamples = FMath::Square(OutInfo.CollisionSizeVerts);
		OutInfo.NumSimpleSamples = FMath::Square(OutInfo.SimpleCollisionSizeVerts);
		return true;
	}

	bool BuildHeightCollisionSignature(
		TConstArrayView<uint16> RawHeights,
		int32 ComplexSampleCount,
		int32 CollisionSizeVerts,
		int32 SimpleSampleCount,
		int32 SimpleCollisionSizeVerts,
		TConstArrayView<float> LiveWorldHeights,
		const FTransform& ComponentTransform,
		FLandscapeHeightCollisionSignature& OutSignature,
		FString& OutError)
	{
		if (ComplexSampleCount <= 0 || CollisionSizeVerts <= 1 ||
			ComplexSampleCount != CollisionSizeVerts * CollisionSizeVerts)
		{
			OutError = TEXT("Invalid landscape collision sample dimensions");
			return false;
		}
		if (SimpleSampleCount < 0 || SimpleCollisionSizeVerts < 0 ||
			(SimpleSampleCount == 0) != (SimpleCollisionSizeVerts == 0) ||
			(SimpleSampleCount > 0 && SimpleSampleCount != SimpleCollisionSizeVerts * SimpleCollisionSizeVerts))
		{
			OutError = TEXT("Invalid simple landscape collision sample dimensions");
			return false;
		}
		const int32 TotalSampleCount = ComplexSampleCount + SimpleSampleCount;
		if (RawHeights.Num() != TotalSampleCount)
		{
			OutError = FString::Printf(
				TEXT("Raw collision height data has %d element(s), expected %d complex + simple samples"),
				RawHeights.Num(), TotalSampleCount);
			return false;
		}
		if (LiveWorldHeights.Num() != TotalSampleCount)
		{
			OutError = FString::Printf(
				TEXT("Live collision heightfields returned %d sample(s), expected %d"),
				LiveWorldHeights.Num(), TotalSampleCount);
			return false;
		}

		OutSignature.RawElementCount = RawHeights.Num();
		OutSignature.ComplexSampleCount = ComplexSampleCount;
		OutSignature.CollisionSizeVerts = CollisionSizeVerts;
		OutSignature.SimpleSampleCount = SimpleSampleCount;
		OutSignature.SimpleCollisionSizeVerts = SimpleCollisionSizeVerts;
		OutSignature.ComponentTransform = ComponentTransform;
		OutSignature.RawHash = FSHA1::HashBuffer(
			RawHeights.GetData(), static_cast<uint64>(RawHeights.Num()) * sizeof(uint16));

		FSHA1 LiveHasher;
		OutSignature.RawMinWorldZ = TNumericLimits<float>::Max();
		OutSignature.RawMaxWorldZ = TNumericLimits<float>::Lowest();
		OutSignature.LiveMinWorldZ = TNumericLimits<float>::Max();
		OutSignature.LiveMaxWorldZ = TNumericLimits<float>::Lowest();
		for (int32 Index = 0; Index < TotalSampleCount; ++Index)
		{
			const float ExpectedWorldZ = static_cast<float>(ComponentTransform.TransformPosition(
				FVector(0.0, 0.0, LandscapeDataAccess::GetLocalHeight(RawHeights[Index]))).Z);
			const float LiveWorldZ = LiveWorldHeights[Index];
			if (!FMath::IsFinite(ExpectedWorldZ) || !FMath::IsFinite(LiveWorldZ) ||
				!FMath::IsNearlyEqual(ExpectedWorldZ, LiveWorldZ, HeightCollisionToleranceCm))
			{
				OutError = FString::Printf(
					TEXT("Raw/live collision height mismatch at sample %d: raw predicts %.3f cm, live heightfield reports %.3f cm"),
					Index, ExpectedWorldZ, LiveWorldZ);
				return false;
			}

			OutSignature.RawMinWorldZ = FMath::Min(OutSignature.RawMinWorldZ, ExpectedWorldZ);
			OutSignature.RawMaxWorldZ = FMath::Max(OutSignature.RawMaxWorldZ, ExpectedWorldZ);
			OutSignature.LiveMinWorldZ = FMath::Min(OutSignature.LiveMinWorldZ, LiveWorldZ);
			OutSignature.LiveMaxWorldZ = FMath::Max(OutSignature.LiveMaxWorldZ, LiveWorldZ);

			// Hash at 0.1 cm precision. A recreated Chaos heightfield can differ by a
			// tiny floating-point amount while still representing the same uint16
			// source height, but a flattened or shifted field must never compare equal.
			const int64 QuantizedHeight = FMath::RoundToInt64(static_cast<double>(LiveWorldZ) * 10.0);
			LiveHasher.Update(QuantizedHeight);
		}
		OutSignature.LiveHash = LiveHasher.Finalize();
		return true;
	}

	bool CaptureHeightCollisionSignature(
		ULandscapeHeightfieldCollisionComponent* Collision,
		FLandscapeHeightCollisionSignature& OutSignature,
		FString& OutError)
	{
		if (!Collision)
		{
			OutError = TEXT("Landscape collision component is null");
			return false;
		}
		const FBodyInstance* BodyInstance = Collision->GetBodyInstance();
		if (!Collision->IsRegistered() || !Collision->IsPhysicsStateCreated() ||
			!BodyInstance || !BodyInstance->IsValidBodyInstance())
		{
			OutError = FString::Printf(
				TEXT("%s does not have a registered, valid physics body"), *Collision->GetPathName());
			return false;
		}

		ULandscapeHeightfieldCollisionComponent::FCollisionSampleInfo SampleInfo;
		if (!ReadCollisionSampleInfo(Collision, SampleInfo, OutError))
		{
			return false;
		}
		const int64 ExpectedRawElements = static_cast<int64>(SampleInfo.NumSamples) + SampleInfo.NumSimpleSamples;
		const int64 RawElementCount = Collision->CollisionHeightData.GetElementCount();
		if (ExpectedRawElements <= 0 || RawElementCount != ExpectedRawElements)
		{
			OutError = FString::Printf(
				TEXT("%s has %lld raw collision height element(s), expected %lld"),
				*Collision->GetPathName(), RawElementCount, ExpectedRawElements);
			return false;
		}

		TArray<float> LiveWorldHeights;
		LiveWorldHeights.SetNumUninitialized(static_cast<int32>(ExpectedRawElements));
		if (!Collision->FillHeightTile(
			MakeArrayView(LiveWorldHeights.GetData(), SampleInfo.NumSamples),
			0,
			SampleInfo.CollisionSizeVerts))
		{
			OutError = FString::Printf(
				TEXT("%s has no readable live complex collision heightfield"), *Collision->GetPathName());
			return false;
		}
		if (SampleInfo.NumSimpleSamples > 0)
		{
			const FTransform& WorldTransform = Collision->GetComponentTransform();
			for (int32 Y = 0; Y < SampleInfo.SimpleCollisionSizeVerts; ++Y)
			{
				for (int32 X = 0; X < SampleInfo.SimpleCollisionSizeVerts; ++X)
				{
					const TOptional<float> SimpleLocalHeight =
						Collision->GetHeight(X, Y, EHeightfieldSource::Simple);
					if (!SimpleLocalHeight.IsSet())
					{
						OutError = FString::Printf(
							TEXT("%s has no readable live simple collision heightfield"),
							*Collision->GetPathName());
						return false;
					}
					const int32 SampleIndex = SampleInfo.NumSamples +
						Y * SampleInfo.SimpleCollisionSizeVerts + X;
					LiveWorldHeights[SampleIndex] = static_cast<float>(
						WorldTransform.TransformPositionNoScale(
							FVector(0.0, 0.0, SimpleLocalHeight.GetValue())).Z);
				}
			}
		}

		const uint16* RawHeightData = static_cast<const uint16*>(Collision->CollisionHeightData.LockReadOnly());
		if (!RawHeightData)
		{
			Collision->CollisionHeightData.Unlock();
			OutError = FString::Printf(
				TEXT("%s raw collision height data could not be locked"), *Collision->GetPathName());
			return false;
		}

		OutSignature.Collision = Collision;
		OutSignature.ComponentPath = Collision->GetPathName();
		const bool bBuilt = BuildHeightCollisionSignature(
			MakeArrayView(RawHeightData, static_cast<int32>(RawElementCount)),
			SampleInfo.NumSamples,
			SampleInfo.CollisionSizeVerts,
			SampleInfo.NumSimpleSamples,
			SampleInfo.SimpleCollisionSizeVerts,
			LiveWorldHeights,
			Collision->GetComponentTransform(),
			OutSignature,
			OutError);
		Collision->CollisionHeightData.Unlock();
		return bBuilt;
	}

	bool MatchesHeightCollisionSignature(
		const FLandscapeHeightCollisionSignature& Baseline,
		const FLandscapeHeightCollisionSignature& Current,
		FString& OutError)
	{
		if (Baseline.RawElementCount != Current.RawElementCount || Baseline.RawHash != Current.RawHash)
		{
			OutError = FString::Printf(
				TEXT("%s raw collision height data changed"), *Baseline.ComponentPath);
			return false;
		}
		if (Baseline.ComplexSampleCount != Current.ComplexSampleCount ||
			Baseline.CollisionSizeVerts != Current.CollisionSizeVerts ||
			Baseline.SimpleSampleCount != Current.SimpleSampleCount ||
			Baseline.SimpleCollisionSizeVerts != Current.SimpleCollisionSizeVerts ||
			!Baseline.ComponentTransform.Equals(Current.ComponentTransform, 1.e-6f))
		{
			OutError = FString::Printf(
				TEXT("%s collision dimensions or transform changed"), *Baseline.ComponentPath);
			return false;
		}
		if (Baseline.LiveHash != Current.LiveHash ||
			!FMath::IsNearlyEqual(Baseline.LiveMinWorldZ, Current.LiveMinWorldZ, HeightCollisionToleranceCm) ||
			!FMath::IsNearlyEqual(Baseline.LiveMaxWorldZ, Current.LiveMaxWorldZ, HeightCollisionToleranceCm))
		{
			OutError = FString::Printf(
				TEXT("%s live collision heightfield changed (baseline %.3f..%.3f cm, current %.3f..%.3f cm)"),
				*Baseline.ComponentPath,
				Baseline.LiveMinWorldZ, Baseline.LiveMaxWorldZ,
				Current.LiveMinWorldZ, Current.LiveMaxWorldZ);
			return false;
		}
		return true;
	}

	bool ValidateLandscapeCollisionPreflight(
		int32 LandscapeComponents,
		int32 CollisionComponents,
		int32 CapturedHeightComponents,
		int32 PendingLayerUpdateComponents,
		bool bLandscapeLayersUpToDate,
		bool bTextureResourcesReady,
		FString& OutError)
	{
		if (CollisionComponents == 0)
		{
			OutError = TEXT("No registered landscape collision components were available to refresh");
			return false;
		}
		if (CollisionComponents != LandscapeComponents ||
			CapturedHeightComponents != CollisionComponents)
		{
			OutError = FString::Printf(
				TEXT("Loaded proxy coverage is incomplete: %d of %d landscape component(s) have registered, captured collision"),
				CapturedHeightComponents, LandscapeComponents);
			return false;
		}
		if (!bLandscapeLayersUpToDate || PendingLayerUpdateComponents > 0)
		{
			OutError = TEXT("The parent landscape has pending edit-layer updates. A later PreSave could replace collision heights; let the landscape finish updating and validate it before retrying.");
			return false;
		}
		if (!bTextureResourcesReady)
		{
			OutError = TEXT("The parent landscape could not make its texture resources resident");
			return false;
		}
		return true;
	}

	class FScopedLandscapePhysicalMaterialRecreateMode
	{
	public:
		FScopedLandscapePhysicalMaterialRecreateMode()
		{
			Variable = IConsoleManager::Get().FindConsoleVariable(
				TEXT("landscape.ApplyPhysicalMaterialChangesImmediately"));
			if (Variable)
			{
				Variable->Set(0, ECVF_SetByTemp, OverrideTag);
				bEffective = Variable->GetInt() == 0;
			}
		}

		~FScopedLandscapePhysicalMaterialRecreateMode()
		{
			if (Variable)
			{
				Variable->Unset(ECVF_SetByTemp, OverrideTag);
			}
		}

		bool IsValid() const { return Variable != nullptr && bEffective; }

	private:
		static inline const FName OverrideTag = TEXT("UE_MCP_LandscapePhysicalMaterialCollision");
		IConsoleVariable* Variable = nullptr;
		bool bEffective = false;
	};
}

#endif // UE_MCP_HAS_5_8_API

void FLandscapeHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	Registry.RegisterHandler(TEXT("get_landscape_info"), &GetLandscapeInfo);
	Registry.RegisterHandler(TEXT("list_landscape_layers"), &ListLandscapeLayers);
	Registry.RegisterHandler(TEXT("sample_landscape"), &SampleLandscape);
	Registry.RegisterHandler(TEXT("list_landscape_splines"), &ListLandscapeSplines);
	Registry.RegisterHandler(TEXT("get_landscape_component"), &GetLandscapeComponent);
	Registry.RegisterHandler(TEXT("set_landscape_material"), &SetLandscapeMaterial);
	Registry.RegisterHandler(TEXT("add_landscape_layer_info"), &AddLandscapeLayerInfo);
	Registry.RegisterHandler(TEXT("create_landscape"), &CreateLandscape);
	Registry.RegisterHandler(TEXT("create_landscape_layer_info"), &CreateLandscapeLayerInfo);
	Registry.RegisterHandler(TEXT("get_landscape_material_usage_summary"), &GetMaterialUsageSummary);
	// #733: World Partition landscape streaming-proxy enumeration + spatial lookup.
	Registry.RegisterHandler(TEXT("list_landscape_proxies"), &ListLandscapeProxies);
	Registry.RegisterHandler(TEXT("find_landscape_proxy_at"), &FindLandscapeProxyAt);
	Registry.RegisterHandlerWithTimeout(TEXT("refresh_landscape_physical_material_collision"), &RefreshPhysicalMaterialCollision, 600.0f);
	Registry.RegisterHandlerWithTimeout(TEXT("sculpt_landscape"), &Sculpt, 120.0f);
	Registry.RegisterHandlerWithTimeout(TEXT("paint_landscape_layer"), &PaintLayer, 120.0f);
}

TSharedPtr<FJsonValue> FLandscapeHandlers::GetLandscapeInfo(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	// Find landscape proxies in the world
	TArray<TSharedPtr<FJsonValue>> LandscapeArray;
	for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
	{
		ALandscapeProxy* Landscape = *It;
		if (!Landscape) continue;

		TSharedPtr<FJsonObject> LandscapeObj = MakeShared<FJsonObject>();
		LandscapeObj->SetStringField(TEXT("name"), Landscape->GetName());
		LandscapeObj->SetStringField(TEXT("class"), Landscape->GetClass()->GetName());

		// Get component count
		TArray<ULandscapeComponent*> LandscapeComponents;
		Landscape->GetComponents<ULandscapeComponent>(LandscapeComponents);
		LandscapeObj->SetNumberField(TEXT("componentCount"), LandscapeComponents.Num());

		// Get bounds
		FBox Bounds = Landscape->GetComponentsBoundingBox();
		if (Bounds.IsValid)
		{
			TSharedPtr<FJsonObject> BoundsObj = MakeShared<FJsonObject>();
			BoundsObj->SetNumberField(TEXT("minX"), Bounds.Min.X);
			BoundsObj->SetNumberField(TEXT("minY"), Bounds.Min.Y);
			BoundsObj->SetNumberField(TEXT("minZ"), Bounds.Min.Z);
			BoundsObj->SetNumberField(TEXT("maxX"), Bounds.Max.X);
			BoundsObj->SetNumberField(TEXT("maxY"), Bounds.Max.Y);
			BoundsObj->SetNumberField(TEXT("maxZ"), Bounds.Max.Z);

			FVector Size = Bounds.GetSize();
			BoundsObj->SetNumberField(TEXT("sizeX"), Size.X);
			BoundsObj->SetNumberField(TEXT("sizeY"), Size.Y);
			BoundsObj->SetNumberField(TEXT("sizeZ"), Size.Z);
			LandscapeObj->SetObjectField(TEXT("bounds"), BoundsObj);
		}

		// Get location
		FVector Location = Landscape->GetActorLocation();
		LandscapeObj->SetNumberField(TEXT("locationX"), Location.X);
		LandscapeObj->SetNumberField(TEXT("locationY"), Location.Y);
		LandscapeObj->SetNumberField(TEXT("locationZ"), Location.Z);

		LandscapeArray.Add(MakeShared<FJsonValueObject>(LandscapeObj));
	}

	auto Result = MCPSuccess();
	if (LandscapeArray.Num() == 0)
	{
		Result->SetStringField(TEXT("landscape"), TEXT("none"));
	}
	else
	{
		Result->SetArrayField(TEXT("landscapes"), LandscapeArray);
	}

	Result->SetNumberField(TEXT("count"), LandscapeArray.Num());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLandscapeHandlers::ListLandscapeLayers(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	TArray<TSharedPtr<FJsonValue>> LayerArray;
	for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
	{
		ALandscapeProxy* Landscape = *It;
		if (!Landscape) continue;

		ULandscapeInfo* LandscapeInfo = Landscape->GetLandscapeInfo();
		if (LandscapeInfo)
		{
			for (const FLandscapeInfoLayerSettings& LayerSettings : LandscapeInfo->Layers)
			{
				if (LayerSettings.LayerInfoObj)
				{
					TSharedPtr<FJsonObject> LayerObj = MakeShared<FJsonObject>();
					LayerObj->SetStringField(TEXT("name"), LayerSettings.GetLayerName().ToString());
					LayerObj->SetStringField(TEXT("landscapeName"), Landscape->GetName());
					LayerArray.Add(MakeShared<FJsonValueObject>(LayerObj));
				}
			}
		}
	}

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("layers"), LayerArray);
	Result->SetNumberField(TEXT("count"), LayerArray.Num());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLandscapeHandlers::SampleLandscape(const TSharedPtr<FJsonObject>& Params)
{
	const TSharedPtr<FJsonObject>* PointObj = nullptr;
	if (!Params->TryGetObjectField(TEXT("point"), PointObj))
	{
		return MCPError(TEXT("Missing 'point' parameter"));
	}

	FVector Point;
	Point.X = (*PointObj)->GetNumberField(TEXT("x"));
	Point.Y = (*PointObj)->GetNumberField(TEXT("y"));
	Point.Z = (*PointObj)->GetNumberField(TEXT("z"));

	REQUIRE_EDITOR_WORLD(World);

	// Find the first landscape and sample height
	for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
	{
		ALandscapeProxy* Landscape = *It;
		if (!Landscape) continue;

		// Use line trace to get the landscape height at the given point
		FVector TraceStart(Point.X, Point.Y, Point.Z + 100000.0f);
		FVector TraceEnd(Point.X, Point.Y, Point.Z - 100000.0f);

		FHitResult HitResult;
		FCollisionQueryParams QueryParams;
		QueryParams.bTraceComplex = true;

		if (World->LineTraceSingleByChannel(HitResult, TraceStart, TraceEnd, ECC_WorldStatic, QueryParams))
		{
			if (HitResult.GetActor() && HitResult.GetActor()->IsA(ALandscapeProxy::StaticClass()))
			{
				auto Result = MCPSuccess();
				Result->SetNumberField(TEXT("height"), HitResult.Location.Z);
				TSharedPtr<FJsonObject> HitPoint = MakeShared<FJsonObject>();
				HitPoint->SetNumberField(TEXT("x"), HitResult.Location.X);
				HitPoint->SetNumberField(TEXT("y"), HitResult.Location.Y);
				HitPoint->SetNumberField(TEXT("z"), HitResult.Location.Z);
				Result->SetObjectField(TEXT("hitLocation"), HitPoint);

				TSharedPtr<FJsonObject> Normal = MakeShared<FJsonObject>();
				Normal->SetNumberField(TEXT("x"), HitResult.Normal.X);
				Normal->SetNumberField(TEXT("y"), HitResult.Normal.Y);
				Normal->SetNumberField(TEXT("z"), HitResult.Normal.Z);
				Result->SetObjectField(TEXT("normal"), Normal);

				Result->SetBoolField(TEXT("hit"), true);
				return MCPResult(Result);
			}
		}
	}

	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("hit"), false);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLandscapeHandlers::ListLandscapeSplines(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	TArray<TSharedPtr<FJsonValue>> SplineArray;

	for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
	{
		ALandscapeProxy* Landscape = *It;
		if (!Landscape) continue;

		ULandscapeSplinesComponent* SplinesComp = Landscape->GetSplinesComponent();
		if (!SplinesComp) continue;

		const TArray<TObjectPtr<ULandscapeSplineControlPoint>>& ControlPoints = SplinesComp->GetControlPoints();
		for (const TObjectPtr<ULandscapeSplineControlPoint>& CP : ControlPoints)
		{
			if (!CP) continue;

			TSharedPtr<FJsonObject> PointObj = MakeShared<FJsonObject>();
			FVector Location = CP->Location;
			PointObj->SetNumberField(TEXT("x"), Location.X);
			PointObj->SetNumberField(TEXT("y"), Location.Y);
			PointObj->SetNumberField(TEXT("z"), Location.Z);
			PointObj->SetStringField(TEXT("landscapeName"), Landscape->GetName());
			SplineArray.Add(MakeShared<FJsonValueObject>(PointObj));
		}
	}

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("controlPoints"), SplineArray);
	Result->SetNumberField(TEXT("count"), SplineArray.Num());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLandscapeHandlers::GetLandscapeComponent(const TSharedPtr<FJsonObject>& Params)
{
	int32 ComponentIndex = 0;
	if (Params->HasField(TEXT("componentIndex")))
	{
		ComponentIndex = static_cast<int32>(Params->GetNumberField(TEXT("componentIndex")));
	}

	REQUIRE_EDITOR_WORLD(World);

	// Collect all landscape components across all landscape proxies
	TArray<ULandscapeComponent*> AllComponents;
	for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
	{
		ALandscapeProxy* Landscape = *It;
		if (!Landscape) continue;

		TArray<ULandscapeComponent*> LandscapeComponents;
		Landscape->GetComponents<ULandscapeComponent>(LandscapeComponents);
		AllComponents.Append(LandscapeComponents);
	}

	if (ComponentIndex < 0 || ComponentIndex >= AllComponents.Num())
	{
		return MCPError(FString::Printf(TEXT("Component index %d out of range (0-%d)"), ComponentIndex, AllComponents.Num() - 1));
	}

	ULandscapeComponent* Comp = AllComponents[ComponentIndex];
	if (!Comp)
	{
		return MCPError(TEXT("Component is null"));
	}

	auto Result = MCPSuccess();
	Result->SetNumberField(TEXT("componentIndex"), ComponentIndex);
	Result->SetStringField(TEXT("name"), Comp->GetName());

	FVector CompLocation = Comp->GetComponentLocation();
	Result->SetNumberField(TEXT("locationX"), CompLocation.X);
	Result->SetNumberField(TEXT("locationY"), CompLocation.Y);
	Result->SetNumberField(TEXT("locationZ"), CompLocation.Z);

	Result->SetNumberField(TEXT("sectionBaseX"), Comp->SectionBaseX);
	Result->SetNumberField(TEXT("sectionBaseY"), Comp->SectionBaseY);
	Result->SetNumberField(TEXT("componentSizeQuads"), Comp->ComponentSizeQuads);
	Result->SetNumberField(TEXT("subSections"), Comp->NumSubsections);

	FBox CompBounds = Comp->Bounds.GetBox();
	if (CompBounds.IsValid)
	{
		TSharedPtr<FJsonObject> BoundsObj = MakeShared<FJsonObject>();
		BoundsObj->SetNumberField(TEXT("minX"), CompBounds.Min.X);
		BoundsObj->SetNumberField(TEXT("minY"), CompBounds.Min.Y);
		BoundsObj->SetNumberField(TEXT("minZ"), CompBounds.Min.Z);
		BoundsObj->SetNumberField(TEXT("maxX"), CompBounds.Max.X);
		BoundsObj->SetNumberField(TEXT("maxY"), CompBounds.Max.Y);
		BoundsObj->SetNumberField(TEXT("maxZ"), CompBounds.Max.Z);
		Result->SetObjectField(TEXT("bounds"), BoundsObj);
	}

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLandscapeHandlers::SetLandscapeMaterial(const TSharedPtr<FJsonObject>& Params)
{
	FString MaterialPath;
	if (!Params->TryGetStringField(TEXT("materialPath"), MaterialPath) && !Params->TryGetStringField(TEXT("path"), MaterialPath) && !Params->TryGetStringField(TEXT("assetPath"), MaterialPath))
	{
		return MCPError(TEXT("Missing 'materialPath', 'path', or 'assetPath' parameter"));
	}

	REQUIRE_EDITOR_WORLD(World);

	// Find the target landscape
	ALandscapeProxy* TargetLandscape = nullptr;
	FString LandscapeName = OptionalString(Params, TEXT("landscapeName"));

	for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
	{
		ALandscapeProxy* Landscape = *It;
		if (!Landscape) continue;

		if (LandscapeName.IsEmpty() || Landscape->GetName() == LandscapeName)
		{
			TargetLandscape = Landscape;
			break;
		}
	}

	if (!TargetLandscape)
	{
		return MCPError(TEXT("No landscape found in the current level"));
	}

	// Load the material
	UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *MaterialPath);
	if (!Material)
	{
		return MCPError(FString::Printf(TEXT("Material not found: %s"), *MaterialPath));
	}

	// Capture previous material for rollback and idempotency
	UMaterialInterface* PrevMaterial = TargetLandscape->LandscapeMaterial;
	if (PrevMaterial == Material)
	{
		auto Noop = MCPSuccess();
		MCPSetExisted(Noop);
		Noop->SetStringField(TEXT("landscapeName"), TargetLandscape->GetName());
		Noop->SetStringField(TEXT("materialPath"), MaterialPath);
		return MCPResult(Noop);
	}

	// Set the landscape material
	TargetLandscape->LandscapeMaterial = Material;

	// Update all landscape components to use the new material
	TArray<ULandscapeComponent*> LandscapeComponents;
	TargetLandscape->GetComponents<ULandscapeComponent>(LandscapeComponents);
	for (ULandscapeComponent* Comp : LandscapeComponents)
	{
		if (Comp)
		{
			Comp->SetMaterial(0, Material);
			Comp->MarkRenderStateDirty();
		}
	}

	// Mark the landscape as modified
	TargetLandscape->MarkPackageDirty();

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("landscapeName"), TargetLandscape->GetName());
	Result->SetStringField(TEXT("materialPath"), MaterialPath);
	Result->SetStringField(TEXT("materialName"), Material->GetName());
	Result->SetNumberField(TEXT("componentsUpdated"), LandscapeComponents.Num());

	// Rollback: restore previous material path if any
	if (PrevMaterial)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("landscapeName"), TargetLandscape->GetName());
		Payload->SetStringField(TEXT("materialPath"), PrevMaterial->GetPathName());
		MCPSetRollback(Result, TEXT("set_landscape_material"), Payload);
	}

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLandscapeHandlers::AddLandscapeLayerInfo(const TSharedPtr<FJsonObject>& Params)
{
	FString LayerName;
	if (auto Err = RequireString(Params, TEXT("layerName"), LayerName)) return Err;

	REQUIRE_EDITOR_WORLD(World);

	// Find the target landscape
	ALandscapeProxy* TargetLandscape = nullptr;
	FString LandscapeName = OptionalString(Params, TEXT("landscapeName"));

	for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
	{
		ALandscapeProxy* Landscape = *It;
		if (!Landscape) continue;

		if (LandscapeName.IsEmpty() || Landscape->GetName() == LandscapeName)
		{
			TargetLandscape = Landscape;
			break;
		}
	}

	if (!TargetLandscape)
	{
		return MCPError(TEXT("No landscape found in the current level"));
	}

	ULandscapeInfo* LandscapeInfo = TargetLandscape->GetLandscapeInfo();
	if (!LandscapeInfo)
	{
		return MCPError(TEXT("Failed to get landscape info"));
	}

	// Check if a layer with this name already exists
	for (const FLandscapeInfoLayerSettings& ExistingLayer : LandscapeInfo->Layers)
	{
		if (ExistingLayer.LayerInfoObj && ExistingLayer.GetLayerName().ToString() == LayerName)
		{
			auto Result = MCPSuccess();
			Result->SetStringField(TEXT("layerName"), LayerName);
			Result->SetStringField(TEXT("path"), ExistingLayer.LayerInfoObj->GetPathName());
			Result->SetStringField(TEXT("note"), TEXT("Layer already exists on this landscape"));
			return MCPResult(Result);
		}
	}

	// Create a new ULandscapeLayerInfoObject asset
	FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/Landscape/LayerInfos"));

	FString AssetName = FString::Printf(TEXT("LI_%s"), *LayerName);
	FString PackageFullPath = PackagePath / AssetName;

	// Check if the asset already exists
	ULandscapeLayerInfoObject* LayerInfoObj = LoadObject<ULandscapeLayerInfoObject>(nullptr, *(PackageFullPath + TEXT(".") + AssetName));
	if (!LayerInfoObj)
	{
		UPackage* Package = CreatePackage(*PackageFullPath);
		if (!Package)
		{
			return MCPError(FString::Printf(TEXT("Failed to create package: %s"), *PackageFullPath));
		}

		LayerInfoObj = NewObject<ULandscapeLayerInfoObject>(Package, *AssetName, RF_Public | RF_Standalone);
		if (!LayerInfoObj)
		{
			return MCPError(TEXT("Failed to create LandscapeLayerInfoObject"));
		}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
		LayerInfoObj->LayerName = FName(*LayerName);
PRAGMA_ENABLE_DEPRECATION_WARNINGS

		// There is no weight-blend toggle to set here any more: bNoWeightBlend
		// was removed in 5.7 and blending is controlled per-layer through
		// landscape settings. The old 'weightBlended' param read into a unused
		// local and the response hardcoded true, so both are gone rather than
		// left implying a setting that is not being applied.

		// Notify asset registry and save
		FAssetRegistryModule::AssetCreated(LayerInfoObj);
		Package->MarkPackageDirty();
		UEditorAssetLibrary::SaveAsset(PackageFullPath, false);
	}

	// Register the layer info with the landscape
	int32 LayerIndex = LandscapeInfo->Layers.Num();
	FLandscapeInfoLayerSettings NewLayerSettings(LayerInfoObj, TargetLandscape);
	LandscapeInfo->Layers.Add(NewLayerSettings);

	// Mark the landscape as dirty
	TargetLandscape->MarkPackageDirty();

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("layerName"), LayerName);
	Result->SetStringField(TEXT("path"), LayerInfoObj->GetPathName());
	Result->SetStringField(TEXT("landscapeName"), TargetLandscape->GetName());
	Result->SetNumberField(TEXT("layerIndex"), LayerIndex);

	return MCPResult(Result);
}

// ─── #150 get_landscape_material_usage_summary ──────────────────────
// Compact per-proxy dump: class, label, material paths, grass / Nanite /
// landscape component counts. Avoids the big "get all components" blob
// get_actor_details produces when you only need materials + counts.
// #303: spawn an ALandscape with a default flat heightmap at mid-elevation
// (uint16 32768 = no offset). Section/quad defaults match the Editor's
// Landscape Mode "create new" form: 63 quads/subsection, 2 subsections/component
// = 127 quads/component. ComponentCount X/Y default to 8x8 producing a
// 1016x1016 quad landscape (~1 km at default actor scale 100,100,100).
TSharedPtr<FJsonValue> FLandscapeHandlers::CreateLandscape(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	const int32 SubsectionSizeQuads = OptionalInt(Params, TEXT("subsectionSizeQuads"), 63);
	const int32 NumSubsections = OptionalInt(Params, TEXT("numSubsections"), 2);
	const int32 ComponentCountX = OptionalInt(Params, TEXT("componentCountX"), 8);
	const int32 ComponentCountY = OptionalInt(Params, TEXT("componentCountY"), 8);

	// Bounds checks: SubsectionSizeQuads must be one of the engine's supported
	// values (7, 15, 31, 63, 127, 255), NumSubsections is 1 or 2, and the
	// component grid has to be at least 1x1.
	auto IsPowOf2Minus1 = [](int32 v) {
		const int32 p = v + 1;
		return v >= 7 && v <= 255 && (p & (p - 1)) == 0;
	};
	if (!IsPowOf2Minus1(SubsectionSizeQuads))
	{
		return MCPError(FString::Printf(
			TEXT("subsectionSizeQuads must be one of 7, 15, 31, 63, 127, 255 (got %d)"),
			SubsectionSizeQuads));
	}
	if (NumSubsections != 1 && NumSubsections != 2)
	{
		return MCPError(FString::Printf(TEXT("numSubsections must be 1 or 2 (got %d)"), NumSubsections));
	}
	if (ComponentCountX < 1 || ComponentCountY < 1)
	{
		return MCPError(TEXT("componentCountX and componentCountY must be >= 1"));
	}

	const int32 ComponentSizeQuads = SubsectionSizeQuads * NumSubsections;
	const int32 SizeX = (ComponentCountX * ComponentSizeQuads) + 1;
	const int32 SizeY = (ComponentCountY * ComponentSizeQuads) + 1;

	const int32 HeightOffset = OptionalInt(Params, TEXT("heightOffset"), 32768);
	if (HeightOffset < 0 || HeightOffset > 65535)
	{
		return MCPError(TEXT("heightOffset must be in [0, 65535] (uint16 elevation)"));
	}

	const FVector Location = OptionalVec3(Params, TEXT("location"));
	const FVector Scale = OptionalVec3(Params, TEXT("scale"), FVector(100.0, 100.0, 100.0));

	const FString Label = OptionalString(Params, TEXT("label"));

	// Idempotency by label.
	if (auto Existing = MCPCheckActorLabelExists(World, Label, TEXT("skip"), TEXT("Landscape")))
	{
		return Existing;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ALandscape* Landscape = World->SpawnActor<ALandscape>(Location, FRotator::ZeroRotator, SpawnParams);
	if (!Landscape)
	{
		return MCPError(TEXT("Failed to spawn ALandscape actor"));
	}
	Landscape->SetActorScale3D(Scale);

	// Allocate a flat heightmap. Layer 0 (FGuid()) is the only edit layer for a
	// non-edit-layer landscape, which is what gets created by the Editor's
	// "create new landscape" defaults.
	TArray<uint16> HeightData;
	HeightData.SetNumUninitialized(SizeX * SizeY);
	for (int32 i = 0; i < HeightData.Num(); ++i)
	{
		HeightData[i] = static_cast<uint16>(HeightOffset);
	}

	TMap<FGuid, TArray<uint16>> ImportHeightData;
	ImportHeightData.Add(FGuid(), MoveTemp(HeightData));

	TMap<FGuid, TArray<FLandscapeImportLayerInfo>> ImportLayerInfo;
	ImportLayerInfo.Add(FGuid(), TArray<FLandscapeImportLayerInfo>());

	TArray<FLandscapeLayer> EmptyLayers;
	Landscape->Import(
		FGuid::NewGuid(),
		0, 0,
		SizeX - 1, SizeY - 1,
		NumSubsections,
		SubsectionSizeQuads,
		ImportHeightData,
		nullptr,
		ImportLayerInfo,
		ELandscapeImportAlphamapType::Additive,
#if UE_MCP_HAS_5_5_API
		MakeArrayView(EmptyLayers)
#else
		// 5.4: last arg is const TArray<FLandscapeLayer>* (TArrayView signature added in 5.5).
		&EmptyLayers
#endif
	);

	if (!Label.IsEmpty())
	{
		Landscape->SetActorLabel(Label);
	}

	// Register so subsequent get_landscape_info / sample_landscape calls find it.
	if (ULandscapeInfo* LI = Landscape->GetLandscapeInfo())
	{
		LI->FixupProxiesTransform();
		LI->RecreateCollisionComponents();
	}

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("actorLabel"), Landscape->GetActorLabel());
	Result->SetStringField(TEXT("actorPath"), Landscape->GetPathName());
	Result->SetNumberField(TEXT("componentCountX"), ComponentCountX);
	Result->SetNumberField(TEXT("componentCountY"), ComponentCountY);
	Result->SetNumberField(TEXT("componentSizeQuads"), ComponentSizeQuads);
	Result->SetNumberField(TEXT("subsectionSizeQuads"), SubsectionSizeQuads);
	Result->SetNumberField(TEXT("numSubsections"), NumSubsections);
	Result->SetNumberField(TEXT("sizeX"), SizeX);
	Result->SetNumberField(TEXT("sizeY"), SizeY);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("actorLabel"), Landscape->GetActorLabel());
	MCPSetRollback(Result, TEXT("delete_actor"), Payload);

	return MCPResult(Result);
}

// #251: standalone LayerInfo asset creation. Unlike add_landscape_layer_info
// (which requires a landscape in the world to register the layer against),
// this creates the ULandscapeLayerInfoObject asset in the content browser
// so paint workflows can pre-author layer assets before the landscape exists.
TSharedPtr<FJsonValue> FLandscapeHandlers::CreateLandscapeLayerInfo(const TSharedPtr<FJsonObject>& Params)
{
	FString LayerName;
	if (auto Err = RequireString(Params, TEXT("layerName"), LayerName)) return Err;

	const FString Name = OptionalString(Params, TEXT("name"), FString::Printf(TEXT("LI_%s"), *LayerName));
	const FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/Landscape/LayerInfos"));
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	TSharedPtr<FJsonValue> Existing = MCPCheckAssetExists(PackagePath, Name, OnConflict, TEXT("LandscapeLayerInfoObject"));
	if (Existing.IsValid()) return Existing;

	const FString PackageFullPath = PackagePath / Name;
	UPackage* Package = CreatePackage(*PackageFullPath);
	if (!Package)
	{
		return MCPError(FString::Printf(TEXT("Failed to create package: %s"), *PackageFullPath));
	}

	ULandscapeLayerInfoObject* LayerInfo = NewObject<ULandscapeLayerInfoObject>(
		Package, *Name, RF_Public | RF_Standalone);
	if (!LayerInfo)
	{
		return MCPError(TEXT("Failed to create LandscapeLayerInfoObject"));
	}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
	LayerInfo->LayerName = FName(*LayerName);
PRAGMA_ENABLE_DEPRECATION_WARNINGS

	// physMaterial was documented here and never applied - the caller had to
	// discover for themselves that it needed a second call. PhysicsCore is not
	// a hard dependency of this module, so the class is reached by path and the
	// property set through reflection rather than a link-time include.
	const FString PhysMaterialPath = OptionalString(Params, TEXT("physMaterial"));
	if (!PhysMaterialPath.IsEmpty())
	{
		UObject* PhysMat = LoadAssetByPath<UObject>(PhysMaterialPath);
		if (!PhysMat)
		{
			// Both failure paths here run after the package and object exist, so
			// bail out without leaving a half-made asset in memory.
			LayerInfo->MarkAsGarbage();
			return MCPError(FString::Printf(TEXT("physMaterial not found: %s"), *PhysMaterialPath));
		}
		FObjectProperty* Prop = CastField<FObjectProperty>(
			ULandscapeLayerInfoObject::StaticClass()->FindPropertyByName(TEXT("PhysMaterial")));
		if (!Prop || !Prop->PropertyClass || !PhysMat->IsA(Prop->PropertyClass))
		{
			LayerInfo->MarkAsGarbage();
			return MCPError(FString::Printf(
				TEXT("'%s' is a %s, not a PhysicalMaterial."),
				*PhysMaterialPath, *PhysMat->GetClass()->GetName()));
		}
		Prop->SetObjectPropertyValue_InContainer(LayerInfo, PhysMat);
	}

	double Hardness = 0.0;
	if (Params->TryGetNumberField(TEXT("hardness"), Hardness))
	{
		// Hardness is becoming private; the setter also handles Modify() and the
		// property-change notification the direct write skipped.
		LayerInfo->SetHardness(static_cast<float>(Hardness), /*bInModify=*/true, EPropertyChangeType::ValueSet);
	}

	FAssetRegistryModule::AssetCreated(LayerInfo);
	Package->MarkPackageDirty();
	SaveAssetPackage(LayerInfo);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("path"), LayerInfo->GetPathName());
	Result->SetStringField(TEXT("layerName"), LayerName);
	Result->SetStringField(TEXT("packagePath"), PackagePath);
	if (!PhysMaterialPath.IsEmpty()) Result->SetStringField(TEXT("physMaterial"), PhysMaterialPath);
	MCPSetDeleteAssetRollback(Result, LayerInfo->GetPathName());

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLandscapeHandlers::GetMaterialUsageSummary(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	TArray<TSharedPtr<FJsonValue>> ProxyArray;
	TSet<FString> UniqueMaterials;
	int32 TotalComponents = 0, TotalGrass = 0, TotalNanite = 0;

	for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
	{
		ALandscapeProxy* Proxy = *It;
		if (!Proxy) continue;

		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("label"), Proxy->GetActorLabel());
		Obj->SetStringField(TEXT("name"), Proxy->GetName());
		Obj->SetStringField(TEXT("class"), Proxy->GetClass()->GetName());
		Obj->SetStringField(TEXT("path"), Proxy->GetPathName());

		if (UMaterialInterface* Mat = Proxy->LandscapeMaterial)
		{
			Obj->SetStringField(TEXT("landscapeMaterial"), Mat->GetPathName());
			UniqueMaterials.Add(Mat->GetPathName());
		}
		if (UMaterialInterface* HoleMat = Proxy->LandscapeHoleMaterial)
		{
			Obj->SetStringField(TEXT("landscapeHoleMaterial"), HoleMat->GetPathName());
		}

		// Histogram components by class (grass / Nanite / regular landscape comps)
		int32 LandscapeComps = 0, GrassComps = 0, NaniteComps = 0;
		TArray<UActorComponent*> Comps;
		Proxy->GetComponents(Comps);
		for (UActorComponent* C : Comps)
		{
			if (!C) continue;
			const FString CName = C->GetClass()->GetName();
			if (CName == TEXT("LandscapeComponent")) LandscapeComps++;
			else if (CName == TEXT("GrassInstancedStaticMeshComponent")) GrassComps++;
			else if (CName == TEXT("LandscapeNaniteComponent")) NaniteComps++;
		}
		Obj->SetNumberField(TEXT("landscapeComponentCount"), LandscapeComps);
		Obj->SetNumberField(TEXT("grassComponentCount"), GrassComps);
		Obj->SetNumberField(TEXT("naniteComponentCount"), NaniteComps);
		TotalComponents += LandscapeComps;
		TotalGrass += GrassComps;
		TotalNanite += NaniteComps;

		const FVector Loc = Proxy->GetActorLocation();
		const FVector Scale = Proxy->GetActorScale3D();
		TSharedPtr<FJsonObject> LocObj = MakeShared<FJsonObject>();
		LocObj->SetNumberField(TEXT("x"), Loc.X);
		LocObj->SetNumberField(TEXT("y"), Loc.Y);
		LocObj->SetNumberField(TEXT("z"), Loc.Z);
		Obj->SetObjectField(TEXT("location"), LocObj);
		TSharedPtr<FJsonObject> ScaleObj = MakeShared<FJsonObject>();
		ScaleObj->SetNumberField(TEXT("x"), Scale.X);
		ScaleObj->SetNumberField(TEXT("y"), Scale.Y);
		ScaleObj->SetNumberField(TEXT("z"), Scale.Z);
		Obj->SetObjectField(TEXT("scale"), ScaleObj);

		ProxyArray.Add(MakeShared<FJsonValueObject>(Obj));
	}

	TArray<TSharedPtr<FJsonValue>> UniqueMatsArr;
	for (const FString& M : UniqueMaterials) UniqueMatsArr.Add(MakeShared<FJsonValueString>(M));

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("proxies"), ProxyArray);
	Result->SetNumberField(TEXT("proxyCount"), ProxyArray.Num());
	Result->SetArrayField(TEXT("uniqueLandscapeMaterials"), UniqueMatsArr);
	Result->SetNumberField(TEXT("totalLandscapeComponents"), TotalComponents);
	Result->SetNumberField(TEXT("totalGrassComponents"), TotalGrass);
	Result->SetNumberField(TEXT("totalNaniteComponents"), TotalNanite);
	return MCPResult(Result);
}

// #733: enumerate LandscapeStreamingProxy actors currently loaded in the world,
// with per-proxy world bounds and the parent Landscape count. On a World
// Partition map, an unloaded proxy silently reads layer weights as 0, so a
// measurement is only trustworthy once the covering proxy is confirmed loaded.
// Unloaded proxies are not spawned as actors, so the actor iterator only yields
// loaded ones - hence loaded is always true for enumerated entries; callers use
// the count + bounds to reason about coverage.
TSharedPtr<FJsonValue> FLandscapeHandlers::ListLandscapeProxies(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	int32 ParentLandscapes = 0;
	TArray<TSharedPtr<FJsonValue>> Proxies;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor) continue;
		if (Actor->IsA<ALandscape>())
		{
			ParentLandscapes++;
			continue;
		}
		ALandscapeStreamingProxy* Proxy = Cast<ALandscapeStreamingProxy>(Actor);
		if (!Proxy) continue;

		FVector Origin, Extent;
		Proxy->GetActorBounds(false, Origin, Extent);

		TSharedPtr<FJsonObject> ProxyObj = MakeShared<FJsonObject>();
		ProxyObj->SetStringField(TEXT("label"), Proxy->GetActorLabel());
		ProxyObj->SetBoolField(TEXT("loaded"), true);

		TSharedPtr<FJsonObject> Bounds = MakeShared<FJsonObject>();
		TSharedPtr<FJsonObject> OriginObj = MakeShared<FJsonObject>();
		OriginObj->SetNumberField(TEXT("x"), Origin.X);
		OriginObj->SetNumberField(TEXT("y"), Origin.Y);
		OriginObj->SetNumberField(TEXT("z"), Origin.Z);
		TSharedPtr<FJsonObject> ExtentObj = MakeShared<FJsonObject>();
		ExtentObj->SetNumberField(TEXT("x"), Extent.X);
		ExtentObj->SetNumberField(TEXT("y"), Extent.Y);
		ExtentObj->SetNumberField(TEXT("z"), Extent.Z);
		Bounds->SetObjectField(TEXT("origin"), OriginObj);
		Bounds->SetObjectField(TEXT("extent"), ExtentObj);
		ProxyObj->SetObjectField(TEXT("worldBounds"), Bounds);

		Proxies.Add(MakeShared<FJsonValueObject>(ProxyObj));
	}

	auto Result = MCPSuccess();
	Result->SetNumberField(TEXT("loadedProxies"), Proxies.Num());
	Result->SetNumberField(TEXT("parentLandscapes"), ParentLandscapes);
	Result->SetArrayField(TEXT("proxies"), Proxies);
	Result->SetStringField(TEXT("note"), TEXT("World Partition unloaded proxies are not spawned as actors, so only loaded proxies are listed."));
	return MCPResult(Result);
}

// #733: resolve which loaded LandscapeStreamingProxy's world bounds contain a
// world X/Y. Returns the covering proxy (loaded:true) or loaded:false when no
// loaded proxy covers the position - which usually means the covering proxy is
// streamed out, making any 0-weight readback there ambiguous rather than real.
TSharedPtr<FJsonValue> FLandscapeHandlers::FindLandscapeProxyAt(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	if (!Params->HasField(TEXT("worldX")) || !Params->HasField(TEXT("worldY")))
	{
		return MCPError(TEXT("Missing 'worldX'/'worldY' world position"));
	}
	const double TargetX = OptionalNumber(Params, TEXT("worldX"), 0.0);
	const double TargetY = OptionalNumber(Params, TEXT("worldY"), 0.0);

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		ALandscapeStreamingProxy* Proxy = Cast<ALandscapeStreamingProxy>(*It);
		if (!Proxy) continue;

		FVector Origin, Extent;
		Proxy->GetActorBounds(false, Origin, Extent);
		if (TargetX >= Origin.X - Extent.X && TargetX <= Origin.X + Extent.X &&
			TargetY >= Origin.Y - Extent.Y && TargetY <= Origin.Y + Extent.Y)
		{
			auto Result = MCPSuccess();
			Result->SetBoolField(TEXT("found"), true);
			Result->SetBoolField(TEXT("loaded"), true);
			Result->SetStringField(TEXT("label"), Proxy->GetActorLabel());
			return MCPResult(Result);
		}
	}

	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("found"), false);
	Result->SetBoolField(TEXT("loaded"), false);
	Result->SetStringField(TEXT("note"), TEXT("No loaded proxy covers this position; the covering proxy is likely streamed out, so weight/height readbacks here are ambiguous."));
	return MCPResult(Result);
}

// Refresh physical-material collision data after a LayerInfo PhysMaterial edit.
// LayerInfo PostEditChange requests a full edit-layer update as well as an
// immediate collision recreation. This batch path deliberately separates those
// operations: it refuses pending edit-layer work, updates layer/material data,
// waits for the material build, and recreates collision exactly once. Complete
// raw, complex-live, and simple-live heightfield signatures guard the mutation.
// Package saving is deliberately outside this action. Only loaded streaming
// proxies can be acted on; unloaded World Partition actors do not exist here.
TSharedPtr<FJsonValue> FLandscapeHandlers::RefreshPhysicalMaterialCollision(const TSharedPtr<FJsonObject>& Params)
{
#if !UE_MCP_HAS_5_8_API
	return MCPError(TEXT("Landscape physical-material collision refresh requires Unreal Engine 5.8 or newer"));
#else
	const double StartedAt = FPlatformTime::Seconds();
	if (!GEditor)
	{
		return MCPError(TEXT("Editor not available"));
	}
	if (GEditor->PlayWorld != nullptr || GEditor->bIsSimulatingInEditor)
	{
		return MCPError(TEXT("Stop PIE or SIE before refreshing landscape physical-material collision"));
	}

	REQUIRE_EDITOR_WORLD(World);
	if (World->WorldType != EWorldType::Editor)
	{
		return MCPError(TEXT("Physical-material collision refresh requires the current editor world"));
	}
	if (!World->IsPartitionedWorld())
	{
		return MCPError(TEXT("The current editor world is not a World Partition map"));
	}

	const int32 MaxActors = OptionalInt(Params, TEXT("maxActors"), 256);
	if (MaxActors < 1 || MaxActors > 1024)
	{
		return MCPError(TEXT("'maxActors' must be between 1 and 1024"));
	}
	if (OptionalBool(Params, TEXT("save"), false))
	{
		return MCPError(TEXT("save_not_supported: this action is in-memory only. Unreal Landscape PreSave can mutate pending edit-layer collision data, so persistence requires a separate validated workflow."));
	}

	TSet<FString> WantedLabels;
	const bool bHasLabelFilter = Params->HasField(TEXT("actorLabels"));
	if (bHasLabelFilter)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!Params->TryGetArrayField(TEXT("actorLabels"), Values) || !Values || Values->IsEmpty() || Values->Num() > 256)
		{
			return MCPError(TEXT("'actorLabels' must be a non-empty array of at most 256 strings"));
		}
		for (const TSharedPtr<FJsonValue>& Value : *Values)
		{
			FString Label;
			if (!Value.IsValid() || !Value->TryGetString(Label) || Label.IsEmpty())
			{
				return MCPError(TEXT("Every 'actorLabels' entry must be a non-empty string"));
			}
			WantedLabels.Add(Label.ToLower());
		}
	}

	TSet<FGuid> WantedGuids;
	const bool bHasGuidFilter = Params->HasField(TEXT("guids"));
	if (bHasGuidFilter)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!Params->TryGetArrayField(TEXT("guids"), Values) || !Values || Values->IsEmpty() || Values->Num() > 256)
		{
			return MCPError(TEXT("'guids' must be a non-empty array of at most 256 GUID strings"));
		}
		for (const TSharedPtr<FJsonValue>& Value : *Values)
		{
			FString GuidText;
			FGuid Guid;
			if (!Value.IsValid() || !Value->TryGetString(GuidText) || !FGuid::Parse(GuidText, Guid))
			{
				return MCPError(FString::Printf(TEXT("Invalid actor GUID: '%s'"), *GuidText));
			}
			WantedGuids.Add(Guid);
		}
	}

	FBox FilterBounds(ForceInit);
	const bool bHasBoundsFilter = Params->HasField(TEXT("bounds"));
	if (bHasBoundsFilter)
	{
		const TSharedPtr<FJsonObject>* BoundsObject = nullptr;
		const TSharedPtr<FJsonObject>* MinObject = nullptr;
		const TSharedPtr<FJsonObject>* MaxObject = nullptr;
		if (!Params->TryGetObjectField(TEXT("bounds"), BoundsObject) || !BoundsObject ||
			!(*BoundsObject)->TryGetObjectField(TEXT("min"), MinObject) || !MinObject ||
			!(*BoundsObject)->TryGetObjectField(TEXT("max"), MaxObject) || !MaxObject)
		{
			return MCPError(TEXT("'bounds' must be {min:{x,y,z}, max:{x,y,z}}"));
		}

		auto ReadVector = [](const TSharedPtr<FJsonObject>& Object, FVector& Out) -> bool
		{
			double X = 0.0, Y = 0.0, Z = 0.0;
			if (!Object->TryGetNumberField(TEXT("x"), X) ||
				!Object->TryGetNumberField(TEXT("y"), Y) ||
				!Object->TryGetNumberField(TEXT("z"), Z) ||
				!FMath::IsFinite(X) || !FMath::IsFinite(Y) || !FMath::IsFinite(Z))
			{
				return false;
			}
			Out = FVector(X, Y, Z);
			return true;
		};

		FVector Min, Max;
		if (!ReadVector(*MinObject, Min) || !ReadVector(*MaxObject, Max))
		{
			return MCPError(TEXT("Every bounds min/max coordinate must be a finite number"));
		}
		FilterBounds = FBox(FVector::Min(Min, Max), FVector::Max(Min, Max));
	}

	TArray<ALandscapeStreamingProxy*> LoadedProxies;
	TArray<ALandscapeStreamingProxy*> Matches;
	for (TActorIterator<ALandscapeStreamingProxy> It(World); It; ++It)
	{
		ALandscapeStreamingProxy* Proxy = *It;
		if (!Proxy || Proxy->GetWorld() != World) continue;
		LoadedProxies.Add(Proxy);

		if (bHasLabelFilter && !WantedLabels.Contains(Proxy->GetActorLabel().ToLower())) continue;
		if (bHasGuidFilter && !WantedGuids.Contains(Proxy->GetActorGuid())) continue;
		if (bHasBoundsFilter)
		{
			FVector Origin, Extent;
			Proxy->GetActorBounds(false, Origin, Extent);
			if (!FBox::BuildAABB(Origin, Extent).Intersect(FilterBounds)) continue;
		}
		Matches.Add(Proxy);
	}

	if (Matches.Num() > MaxActors)
	{
		return MCPError(FString::Printf(
			TEXT("%d loaded LandscapeStreamingProxy actors matched, above the maxActors limit of %d. Narrow actorLabels/guids/bounds or raise maxActors deliberately."),
			Matches.Num(), MaxActors));
	}
	for (ALandscapeStreamingProxy* Proxy : Matches)
	{
		if (UPackage* Package = Proxy ? Proxy->GetPackage() : nullptr)
		{
			// A later external save would fully load the package first. Establish the
			// same object residency before any safety baseline or layer-state test.
			Package->FullyLoad();
		}
	}

	struct FRefreshEntry
	{
		TSharedPtr<FJsonObject> Json;
		ALandscapeStreamingProxy* Proxy = nullptr;
		ALandscape* ParentLandscape = nullptr;
		FString PackagePath;
		FString Error;
		TArray<FLandscapeHeightCollisionSignature> HeightSignatures;
		int32 LandscapeComponents = 0;
		int32 CollisionComponents = 0;
		int32 IneligibleCollisionComponents = 0;
		int32 ComponentsWithPendingLayerUpdates = 0;
		int32 OutdatedBefore = 0;
		int32 OutdatedAfter = 0;
		bool bTextureResourcesReady = false;
		bool bLandscapeLayersUpToDate = false;
		bool bHeightCollisionVerified = false;
		bool bRefreshed = false;
	};

	TArray<FRefreshEntry> Entries;
	TArray<FString> PackagePaths;
	TMap<ALandscape*, bool> TextureResourcesReady;
	TMap<ALandscape*, bool> LandscapeLayersUpToDate;
	for (ALandscapeStreamingProxy* Proxy : Matches)
	{
		ALandscape* ParentLandscape = Proxy ? Proxy->GetLandscapeActor() : nullptr;
		if (ParentLandscape && !LandscapeLayersUpToDate.Contains(ParentLandscape))
		{
			// A LayerInfo PostEditChange requests Update_All. PreSave forcibly
			// flushes that work; on a stale edit-layer stack this can replace valid
			// collision heights while the package is already being serialized.
			// Refuse that state instead of discovering it after overwriting the OFPA
			// package.
			LandscapeLayersUpToDate.Add(ParentLandscape, ParentLandscape->IsUpToDate());
		}
		if (ParentLandscape && LandscapeLayersUpToDate.FindRef(ParentLandscape) &&
			!TextureResourcesReady.Contains(ParentLandscape))
		{
			// BuildPhysicalMaterial depends on resident weightmaps. Do this once per
			// parent landscape and wait, instead of letting each proxy make a
			// best-effort non-blocking request in the same frame.
			TextureResourcesReady.Add(ParentLandscape, ParentLandscape->PrepareTextureResources(true));
		}
	}
	for (TPair<ALandscape*, bool>& Pair : LandscapeLayersUpToDate)
	{
		// Texture preparation is allowed to service queued landscape work. Capture
		// the state that actually exists immediately before the height baseline.
		Pair.Value = Pair.Key && Pair.Key->IsUpToDate();
	}

	int32 Refreshed = 0;
	int32 CollisionComponentsRefreshed = 0;
	int32 OutdatedBefore = 0;

	for (ALandscapeStreamingProxy* Proxy : Matches)
	{
		FRefreshEntry& Entry = Entries.AddDefaulted_GetRef();
		Entry.Proxy = Proxy;
		Entry.ParentLandscape = Proxy->GetLandscapeActor();
		Entry.bLandscapeLayersUpToDate = Entry.ParentLandscape && LandscapeLayersUpToDate.FindRef(Entry.ParentLandscape);
		Entry.bTextureResourcesReady = Entry.ParentLandscape && TextureResourcesReady.FindRef(Entry.ParentLandscape);
		Entry.Json = MakeShared<FJsonObject>();
		Entry.Json->SetStringField(TEXT("label"), Proxy->GetActorLabel());
		Entry.Json->SetStringField(TEXT("guid"), Proxy->GetActorGuid().ToString());
		Entry.Json->SetBoolField(TEXT("landscapeLayersUpToDate"), Entry.bLandscapeLayersUpToDate);
		Entry.Json->SetBoolField(TEXT("textureResourcesReady"), Entry.bTextureResourcesReady);

		if (UPackage* Package = Proxy->GetPackage())
		{
			Entry.PackagePath = Package->GetName();
			Entry.Json->SetStringField(TEXT("package"), Entry.PackagePath);
			PackagePaths.AddUnique(Entry.PackagePath);
		}

		TArray<ULandscapeComponent*> Components;
		Proxy->GetComponents<ULandscapeComponent>(Components);
		for (ULandscapeComponent* Component : Components)
		{
			if (!Component) continue;
			++Entry.LandscapeComponents;
			if (Component && Component->GetLayerUpdateFlagPerMode() != 0)
			{
				++Entry.ComponentsWithPendingLayerUpdates;
			}
			if (Component && Component->IsRegistered() && Component->GetCollisionComponent())
			{
				++Entry.CollisionComponents;
				FLandscapeHeightCollisionSignature Signature;
				FString SignatureError;
				if (CaptureHeightCollisionSignature(Component->GetCollisionComponent(), Signature, SignatureError))
				{
					Entry.HeightSignatures.Add(MoveTemp(Signature));
				}
				else if (Entry.Error.IsEmpty())
				{
					Entry.Error = FString::Printf(TEXT("Height collision preflight failed: %s"), *SignatureError);
				}
			}
			else
			{
				++Entry.IneligibleCollisionComponents;
			}
		}
		Entry.Json->SetNumberField(TEXT("landscapeComponents"), Entry.LandscapeComponents);
		Entry.Json->SetNumberField(TEXT("collisionComponents"), Entry.CollisionComponents);
		Entry.Json->SetNumberField(TEXT("ineligibleCollisionComponents"), Entry.IneligibleCollisionComponents);
		Entry.Json->SetNumberField(TEXT("componentsWithPendingLayerUpdates"), Entry.ComponentsWithPendingLayerUpdates);
		Entry.Json->SetNumberField(TEXT("heightCollisionComponentsCaptured"), Entry.HeightSignatures.Num());
		if (Entry.Error.IsEmpty())
		{
			ValidateLandscapeCollisionPreflight(
				Entry.LandscapeComponents,
				Entry.CollisionComponents,
				Entry.HeightSignatures.Num(),
				Entry.ComponentsWithPendingLayerUpdates,
				Entry.bLandscapeLayersUpToDate,
				Entry.bTextureResourcesReady,
				Entry.Error);
		}
		if (!Entry.Error.IsEmpty())
		{
			Entry.Json->SetBoolField(TEXT("refreshed"), false);
			continue;
		}

	}

	const bool bBatchHeightPreflightPassed =
		Entries.Num() == Matches.Num() &&
		!Entries.ContainsByPredicate([](const FRefreshEntry& Entry)
		{
			return !Entry.Error.IsEmpty();
		});
	if (!bBatchHeightPreflightPassed)
	{
		// The selected proxy set is one safety unit. Updating the apparently-good
		// subset would leave the loaded landscape internally inconsistent and can
		// reproduce the stale-cache failure on the next save/reload.
		for (FRefreshEntry& Entry : Entries)
		{
			if (Entry.Error.IsEmpty())
			{
				Entry.Error = TEXT("Another matched proxy failed loaded/registered height-collision preflight; no matched proxy was mutated");
				Entry.Json->SetBoolField(TEXT("refreshed"), false);
			}
		}
	}

	FScopedLandscapePhysicalMaterialRecreateMode RecreateMode;
	if (!RecreateMode.IsValid())
	{
		return MCPError(TEXT("Required console variable landscape.ApplyPhysicalMaterialChangesImmediately was unavailable or could not be forced to zero; a higher-priority override may be active"));
	}

	for (FRefreshEntry& Entry : Entries)
	{
		if (!Entry.Error.IsEmpty()) continue;
		TArray<ULandscapeComponent*> Components;
		Entry.Proxy->GetComponents<ULandscapeComponent>(Components);
		for (ULandscapeComponent* Component : Components)
		{
			if (Component && Component->IsRegistered() && Component->GetCollisionComponent())
			{
				// UpdateCollisionLayerData invalidates the physical-material task but,
				// unlike ChangedPhysMaterial, intentionally does not tear down physics
				// before the asynchronous material output is ready.
				Component->UpdateCollisionLayerData();
			}
		}
		Entry.OutdatedBefore = Entry.Proxy->GetOudatedPhysicalMaterialComponentsCount();
		Entry.Json->SetNumberField(TEXT("outdatedBefore"), Entry.OutdatedBefore);
		// The return type changed between supported UE versions; ignoring it is
		// intentional. The exported outdated count below is the stable readback.
		Entry.Proxy->BuildPhysicalMaterial();
		Entry.bRefreshed = true;
		Entry.Json->SetBoolField(TEXT("refreshed"), true);
		++Refreshed;
		CollisionComponentsRefreshed += Entry.CollisionComponents;
		OutdatedBefore += Entry.OutdatedBefore;
	}

	// BuildPhysicalMaterial advances an asynchronous GPU readback. Re-enter the
	// exported build method after flushing render commands until it finalizes
	// every matched proxy, or until there is enough time left to return a useful
	// failure report before the handler's 600-second bridge timeout. Time spent
	// preparing texture resources counts too.
	const double BuildDeadline = StartedAt + 420.0;
	bool bBuildTimedOut = false;
	while (Refreshed > 0)
	{
		int32 Remaining = 0;
		for (const FRefreshEntry& Entry : Entries)
		{
			if (Entry.bRefreshed) Remaining += Entry.Proxy->GetOudatedPhysicalMaterialComponentsCount();
		}
		if (Remaining == 0) break;
		if (FPlatformTime::Seconds() >= BuildDeadline)
		{
			bBuildTimedOut = true;
			break;
		}

		FlushRenderingCommands();
		for (FRefreshEntry& Entry : Entries)
		{
			if (Entry.bRefreshed && Entry.Proxy->GetOudatedPhysicalMaterialComponentsCount() > 0)
			{
				Entry.Proxy->BuildPhysicalMaterial();
			}
		}
		FPlatformProcess::Sleep(0.001f);
	}
	if (Refreshed > 0) FlushRenderingCommands();

	int32 CollisionComponentsRecreateRequested = 0;
	int32 CollisionComponentsRecreatedAfterBuild = 0;
	int32 CollisionComponentsUnchangedAfterBuild = 0;
	int32 OutdatedAfterBuild = 0;
	auto VerifyEntryHeightCollision = [](
		FRefreshEntry& Entry,
		const TCHAR* Phase,
		bool bWriteJsonField) -> bool
	{
		for (const FLandscapeHeightCollisionSignature& Baseline : Entry.HeightSignatures)
		{
			if (!IsValid(Baseline.Collision))
			{
				Entry.Error = FString::Printf(
					TEXT("Height collision verification failed %s: %s is no longer valid"),
					Phase, *Baseline.ComponentPath);
				if (bWriteJsonField) Entry.Json->SetBoolField(TEXT("heightCollisionVerified"), false);
				return false;
			}

			FLandscapeHeightCollisionSignature Current;
			FString SignatureError;
			if (!CaptureHeightCollisionSignature(Baseline.Collision, Current, SignatureError) ||
				!MatchesHeightCollisionSignature(Baseline, Current, SignatureError))
			{
				Entry.Error = FString::Printf(
					TEXT("Height collision verification failed %s: %s"), Phase, *SignatureError);
				if (bWriteJsonField) Entry.Json->SetBoolField(TEXT("heightCollisionVerified"), false);
				return false;
			}
		}
		if (bWriteJsonField) Entry.Json->SetBoolField(TEXT("heightCollisionVerified"), true);
		return true;
	};

	for (FRefreshEntry& Entry : Entries)
	{
		if (!Entry.bRefreshed) continue;
		Entry.OutdatedAfter = Entry.Proxy->GetOudatedPhysicalMaterialComponentsCount();
		Entry.Json->SetNumberField(TEXT("outdatedAfterBuild"), Entry.OutdatedAfter);
		Entry.Json->SetBoolField(TEXT("physicalMaterialCurrentAfterBuild"), Entry.OutdatedAfter == 0);
		OutdatedAfterBuild += Entry.OutdatedAfter;
		if (Entry.OutdatedAfter == 0)
		{
			// Prove that the material build did not touch raw or live heights before
			// tearing down the valid heightfield. The scoped CVar above prevents the
			// engine finalizer from recreating it early.
			if (!VerifyEntryHeightCollision(Entry, TEXT("after the physical-material build"), false))
			{
				continue;
			}

			// Recreate exactly once, after both dominant-layer and rendered physical
			// material data are current. RecreateCollision's return value only says
			// whether a request was made; the full live height tile below is proof
			// that a valid, unchanged geometry was produced.
			int32 EntryRecreateRequested = 0;
			int32 EntryRecreated = 0;
			int32 EntryUnchanged = 0;
			for (const FLandscapeHeightCollisionSignature& Baseline : Entry.HeightSignatures)
			{
				if (IsValid(Baseline.Collision) && Baseline.Collision->IsRegistered())
				{
					++EntryRecreateRequested;
					if (Baseline.Collision->RecreateCollision()) ++EntryRecreated;
					else ++EntryUnchanged;
				}
			}
			Entry.Json->SetNumberField(TEXT("collisionRecreateRequestedAfterBuild"), EntryRecreateRequested);
			Entry.Json->SetNumberField(TEXT("collisionRecreatedAfterBuild"), EntryRecreated);
			Entry.Json->SetNumberField(TEXT("collisionUnchangedAfterBuild"), EntryUnchanged);
			CollisionComponentsRecreateRequested += EntryRecreateRequested;
			CollisionComponentsRecreatedAfterBuild += EntryRecreated;
			CollisionComponentsUnchangedAfterBuild += EntryUnchanged;
			Entry.bHeightCollisionVerified =
				EntryRecreateRequested == Entry.HeightSignatures.Num() &&
				EntryRecreated == Entry.HeightSignatures.Num() &&
				VerifyEntryHeightCollision(Entry, TEXT("after the final collision recreation"), true);
			if (!Entry.bHeightCollisionVerified && Entry.Error.IsEmpty())
			{
				Entry.Error = TEXT("Not every captured collision component was explicitly recreated for the new physical-material mapping");
				Entry.Json->SetBoolField(TEXT("heightCollisionVerified"), false);
			}
		}
		else
		{
			Entry.Error = FString::Printf(
				TEXT("Physical-material build did not flush %d outdated component(s)%s"),
				Entry.OutdatedAfter, bBuildTimedOut ? TEXT(" before the timeout") : TEXT(""));
		}
	}

	// Read back after the build/recreation so the response reflects the final
	// in-memory derived state. Persistence is deliberately unsupported here:
	// ALandscapeProxy::PreSave can mutate edit-layer collision data.
	int32 OutdatedAfter = 0;
	int32 PhysicalMaterialsCurrent = 0;
	for (FRefreshEntry& Entry : Entries)
	{
		if (!Entry.bRefreshed) continue;
		Entry.OutdatedAfter = Entry.Proxy->GetOudatedPhysicalMaterialComponentsCount();
		Entry.Json->SetNumberField(TEXT("outdatedAfter"), Entry.OutdatedAfter);
		const bool bPhysicalMaterialCurrent = Entry.OutdatedAfter == 0;
		Entry.Json->SetBoolField(TEXT("physicalMaterialCurrent"), bPhysicalMaterialCurrent);
		OutdatedAfter += Entry.OutdatedAfter;
		if (bPhysicalMaterialCurrent)
		{
			++PhysicalMaterialsCurrent;
		}
		else if (Entry.Error.IsEmpty())
		{
			Entry.Error = FString::Printf(
				TEXT("Physical-material state has %d outdated component(s) after the build"),
				Entry.OutdatedAfter);
		}
	}
	const bool bBatchAccepted = !Matches.IsEmpty() && Entries.Num() == Matches.Num() &&
		!Entries.ContainsByPredicate([](const FRefreshEntry& Entry)
		{
			return !Entry.Error.IsEmpty();
		});
	if (!bBatchAccepted)
	{
		for (FRefreshEntry& Entry : Entries)
		{
			if (Entry.Error.IsEmpty())
			{
				Entry.Error = TEXT("Another matched proxy failed the post-build safety check; the matched batch was not accepted and must not be saved");
			}
		}
	}

	TArray<TSharedPtr<FJsonValue>> ActorResults;
	TArray<FString> FailedPackagePaths;
	int32 Failed = 0;
	int32 HeightCollisionComponentsVerified = 0;
	for (FRefreshEntry& Entry : Entries)
	{
		Entry.Json->SetBoolField(TEXT("saved"), false);
		if (Entry.bHeightCollisionVerified)
		{
			HeightCollisionComponentsVerified += Entry.HeightSignatures.Num();
		}
		if (!Entry.Error.IsEmpty())
		{
			++Failed;
			Entry.Json->SetStringField(TEXT("error"), Entry.Error);
			if (!Entry.PackagePath.IsEmpty()) FailedPackagePaths.AddUnique(Entry.PackagePath);
		}
		ActorResults.Add(MakeShared<FJsonValueObject>(Entry.Json));
	}

	auto ToJsonStrings = [](const TArray<FString>& Strings)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		Values.Reserve(Strings.Num());
		for (const FString& String : Strings) Values.Add(MakeShared<FJsonValueString>(String));
		return Values;
	};

	auto Result = MCPSuccess();
	if (Failed > 0 || Matches.IsEmpty()) Result->SetBoolField(TEXT("success"), false);
	if (Refreshed > 0) MCPSetUpdated(Result);
	Result->SetStringField(TEXT("world"), World->GetPathName());
	Result->SetBoolField(TEXT("batchAccepted"), bBatchAccepted);
	Result->SetBoolField(TEXT("saveSupported"), false);
	Result->SetBoolField(TEXT("saveRequested"), false);
	Result->SetNumberField(TEXT("maxActors"), MaxActors);
	Result->SetNumberField(TEXT("loaded"), LoadedProxies.Num());
	Result->SetNumberField(TEXT("matched"), Matches.Num());
	Result->SetNumberField(TEXT("refreshed"), Refreshed);
	Result->SetNumberField(TEXT("collisionComponentsRefreshed"), CollisionComponentsRefreshed);
	Result->SetNumberField(TEXT("collisionComponentsRecreateRequested"), CollisionComponentsRecreateRequested);
	Result->SetNumberField(TEXT("collisionComponentsRecreatedAfterBuild"), CollisionComponentsRecreatedAfterBuild);
	Result->SetNumberField(TEXT("collisionComponentsUnchangedAfterBuild"), CollisionComponentsUnchangedAfterBuild);
	Result->SetNumberField(TEXT("heightCollisionComponentsVerified"), HeightCollisionComponentsVerified);
	Result->SetNumberField(TEXT("physicalMaterialsCurrent"), PhysicalMaterialsCurrent);
	Result->SetNumberField(TEXT("outdatedBefore"), OutdatedBefore);
	Result->SetNumberField(TEXT("outdatedAfterBuild"), OutdatedAfterBuild);
	Result->SetNumberField(TEXT("outdatedAfter"), OutdatedAfter);
	Result->SetBoolField(TEXT("buildTimedOut"), bBuildTimedOut);
	Result->SetNumberField(TEXT("textureResourceParents"), TextureResourcesReady.Num());
	int32 ReadyParents = 0;
	for (const TPair<ALandscape*, bool>& Pair : TextureResourcesReady) ReadyParents += Pair.Value ? 1 : 0;
	Result->SetNumberField(TEXT("textureResourceParentsReady"), ReadyParents);
	int32 UpToDateParents = 0;
	for (const TPair<ALandscape*, bool>& Pair : LandscapeLayersUpToDate) UpToDateParents += Pair.Value ? 1 : 0;
	Result->SetNumberField(TEXT("landscapeLayerParents"), LandscapeLayersUpToDate.Num());
	Result->SetNumberField(TEXT("landscapeLayerParentsUpToDate"), UpToDateParents);
	Result->SetNumberField(TEXT("saved"), 0);
	Result->SetNumberField(TEXT("failed"), Failed);
	Result->SetArrayField(TEXT("packagePaths"), ToJsonStrings(PackagePaths));
	Result->SetArrayField(TEXT("savedPackagePaths"), TArray<TSharedPtr<FJsonValue>>());
	Result->SetArrayField(TEXT("failedPackagePaths"), ToJsonStrings(FailedPackagePaths));
	Result->SetArrayField(TEXT("actors"), ActorResults);
	FString Note;
	if (Matches.IsEmpty())
	{
		Note = TEXT("No loaded LandscapeStreamingProxy actor matched. Unloaded proxies were not changed; pin them first with level(load_actor_descs), then rerun this action.");
	}
	else if (Failed > 0)
	{
		Note = FString::Printf(TEXT("%d of %d matched proxies failed; inspect the per-actor errors. Unloaded proxies were not changed."), Failed, Matches.Num());
	}
	else
	{
		Note = TEXT("Collision and physical-material data were rebuilt in memory and matched packages may now be dirty. No packages were saved. Unloaded proxies were not changed.");
	}
	Result->SetStringField(TEXT("note"), Note);
	return MCPResult(Result);
#endif // UE_MCP_HAS_5_8_API
}

#if WITH_DEV_AUTOMATION_TESTS && UE_MCP_HAS_5_8_API

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPLandscapePhysicalMaterialCollisionHeightSafetyTest,
	"UE.MCP.Landscape.PhysicalMaterialCollision.HeightSafety",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPLandscapePhysicalMaterialCollisionHeightSafetyTest::RunTest(const FString& Parameters)
{
	const TArray<uint16> TerrainRaw = {
		32768, 32896, 32640, 33024,
		32768, 32960, 32576, 33152
	};
	const TArray<float> TerrainLive = {
		100.0f, 200.0f, 0.0f, 300.0f,
		100.0f, 250.0f, -50.0f, 400.0f
	};
	const FTransform Transform(FRotator::ZeroRotator, FVector(0.0, 0.0, 100.0), FVector(100.0));

	FLandscapeHeightCollisionSignature Baseline;
	FString Error;
	TestTrue(TEXT("varied raw terrain agrees with its complex and simple live heightfields"),
		BuildHeightCollisionSignature(TerrainRaw, 4, 2, 4, 2, TerrainLive, Transform, Baseline, Error));
	TestTrue(TEXT("baseline reports a non-flat height range"),
		Baseline.LiveMaxWorldZ > Baseline.LiveMinWorldZ);

	const TArray<uint16> FlatRaw = {
		32768, 32768, 32768, 32768,
		32768, 32768, 32768, 32768
	};
	FLandscapeHeightCollisionSignature Invalid;
	Error.Reset();
	TestFalse(TEXT("flat raw data cannot masquerade as the original live terrain"),
		BuildHeightCollisionSignature(FlatRaw, 4, 2, 4, 2, TerrainLive, Transform, Invalid, Error));
	TestTrue(TEXT("raw/live mismatch names the unsafe sample"), Error.Contains(TEXT("mismatch at sample")));

	TArray<float> FlatSimpleLive = TerrainLive;
	for (int32 Index = 4; Index < FlatSimpleLive.Num(); ++Index) FlatSimpleLive[Index] = 100.0f;
	Error.Reset();
	TestFalse(TEXT("flattened simple live collision is rejected"),
		BuildHeightCollisionSignature(TerrainRaw, 4, 2, 4, 2, FlatSimpleLive, Transform, Invalid, Error));

	const TArray<float> FlatLive = {
		100.0f, 100.0f, 100.0f, 100.0f,
		100.0f, 100.0f, 100.0f, 100.0f
	};
	FLandscapeHeightCollisionSignature Flattened;
	Error.Reset();
	TestTrue(TEXT("internally consistent flat collision can be signed"),
		BuildHeightCollisionSignature(FlatRaw, 4, 2, 4, 2, FlatLive, Transform, Flattened, Error));
	TestFalse(TEXT("a flattened raw/live pair cannot match the terrain baseline"),
		MatchesHeightCollisionSignature(Baseline, Flattened, Error));

	FString PreflightError;
	TestFalse(TEXT("pending edit-layer work fails preflight"),
		ValidateLandscapeCollisionPreflight(1, 1, 1, 1, true, true, PreflightError));
	TestTrue(TEXT("pending-work failure explains PreSave risk"),
		PreflightError.Contains(TEXT("pending edit-layer updates")));
	PreflightError.Reset();
	TestTrue(TEXT("complete idle collision coverage passes preflight"),
		ValidateLandscapeCollisionPreflight(1, 1, 1, 0, true, true, PreflightError));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && UE_MCP_HAS_5_8_API
