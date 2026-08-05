// Split from LevelHandlers.cpp to keep that file under 3k lines.
// All functions below are still members of FLevelHandlers - this file is a
// translation-unit partition, not a new class. Handler registration
// stays in LevelHandlers.cpp::RegisterHandlers.

#include "LevelHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerJsonProperty.h"
#include "JsonSerializer.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectIterator.h"
#include "Engine/PointLight.h"
#include "Engine/SpotLight.h"
#include "Engine/DirectionalLight.h"
#include "Engine/RectLight.h"
#include "Engine/SkyLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/LightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"


TSharedPtr<FJsonValue> FLevelHandlers::SpawnLight(const TSharedPtr<FJsonObject>& Params)
{
	FString LightType;
	if (auto Err = RequireString(Params, TEXT("lightType"), LightType)) return Err;

	REQUIRE_EDITOR_WORLD(World);

	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));
	const FString Label = OptionalString(Params, TEXT("label"));

	if (auto Existing = MCPCheckActorLabelExists(World, Label, OnConflict, TEXT("Light")))
	{
		return Existing;
	}

	const FVector Location = OptionalVec3(Params, TEXT("location"));

	double Intensity = OptionalNumber(Params, TEXT("intensity"), 5000.0);

	UClass* LightClass = nullptr;
	if (LightType.Equals(TEXT("point"), ESearchCase::IgnoreCase))
	{
		LightClass = APointLight::StaticClass();
	}
	else if (LightType.Equals(TEXT("spot"), ESearchCase::IgnoreCase))
	{
		LightClass = ASpotLight::StaticClass();
	}
	else if (LightType.Equals(TEXT("directional"), ESearchCase::IgnoreCase))
	{
		LightClass = ADirectionalLight::StaticClass();
	}
	else if (LightType.Equals(TEXT("rect"), ESearchCase::IgnoreCase))
	{
		LightClass = ARectLight::StaticClass();
	}
	else if (LightType.Equals(TEXT("sky"), ESearchCase::IgnoreCase) || LightType.Equals(TEXT("skylight"), ESearchCase::IgnoreCase))
	{
		LightClass = ASkyLight::StaticClass();
	}
	else
	{
		return MCPError(FString::Printf(TEXT("Unknown light type: %s. Use point, spot, directional, rect, or sky."), *LightType));
	}

	const FRotator Rotation = OptionalRotator(Params, TEXT("rotation"));

	FTransform LightTransform(Rotation, Location);
	AActor* NewLight = World->SpawnActor<AActor>(LightClass, LightTransform);
	if (!NewLight)
	{
		return MCPError(TEXT("Failed to spawn light actor"));
	}

	if (!Label.IsEmpty())
	{
		NewLight->SetActorLabel(Label);
	}

	// Parse optional color (RGB 0-255 each, matches set_light_properties shape).
	auto ParseLightColor = [&](FLinearColor& OutColor) -> bool
	{
		const TSharedPtr<FJsonObject>* ColorObj = nullptr;
		if (!Params->TryGetObjectField(TEXT("color"), ColorObj) || !ColorObj || !(*ColorObj).IsValid())
		{
			return false;
		}
		double R = 255.0, G = 255.0, B = 255.0;
		(*ColorObj)->TryGetNumberField(TEXT("r"), R);
		(*ColorObj)->TryGetNumberField(TEXT("g"), G);
		(*ColorObj)->TryGetNumberField(TEXT("b"), B);
		OutColor = FLinearColor(R / 255.0f, G / 255.0f, B / 255.0f);
		return true;
	};

	// Parse optional mobility (#310). Default to Movable so the light renders
	// immediately without a lighting build — that matches the "spawn this and
	// it just works" UX MCP callers expect. SkyLight ignores this.
	const FString MobilityStr = OptionalString(Params, TEXT("mobility"), TEXT("Movable"));
	EComponentMobility::Type Mobility = EComponentMobility::Movable;
	if (MobilityStr.Equals(TEXT("Static"), ESearchCase::IgnoreCase))
	{
		Mobility = EComponentMobility::Static;
	}
	else if (MobilityStr.Equals(TEXT("Stationary"), ESearchCase::IgnoreCase))
	{
		Mobility = EComponentMobility::Stationary;
	}

	if (ULightComponent* LightComponent = NewLight->FindComponentByClass<ULightComponent>())
	{
		LightComponent->SetMobility(Mobility);
		LightComponent->SetIntensity(Intensity);
		FLinearColor LightColor;
		if (ParseLightColor(LightColor))
		{
			LightComponent->SetLightColor(LightColor);
		}
		// #723: attenuationRadius was accepted by the schema but never applied.
		// It lives on the local-light components (point/spot/rect); directional
		// and sky lights have no attenuation radius, so they ignore it.
		double AttenuationRadius = 0.0;
		if (Params->TryGetNumberField(TEXT("attenuationRadius"), AttenuationRadius) && AttenuationRadius > 0.0)
		{
			if (UPointLightComponent* PointComp = Cast<UPointLightComponent>(LightComponent))
			{
				PointComp->SetAttenuationRadius(static_cast<float>(AttenuationRadius));
			}
			else if (URectLightComponent* RectComp = Cast<URectLightComponent>(LightComponent))
			{
				RectComp->SetAttenuationRadius(static_cast<float>(AttenuationRadius));
			}
		}
		LightComponent->SetVisibility(true);
		LightComponent->MarkRenderStateDirty();
	}
	else if (USkyLightComponent* SkyComp = NewLight->FindComponentByClass<USkyLightComponent>())
	{
		// SkyLight has no ULightComponent — set intensity on USkyLightComponent
		// directly and recapture so the change takes effect.
		SkyComp->SetIntensity(Intensity);
		FLinearColor LightColor;
		if (ParseLightColor(LightColor))
		{
			SkyComp->SetLightColor(LightColor);
		}
		SkyComp->SetVisibility(true);
		SkyComp->RecaptureSky();
	}

	const FString FinalLabel = NewLight->GetActorLabel();

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("actorLabel"), FinalLabel);
	Result->SetStringField(TEXT("actorName"), NewLight->GetName());
	Result->SetStringField(TEXT("lightType"), LightType);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("actorLabel"), FinalLabel);
	MCPSetRollback(Result, TEXT("delete_actor"), Payload);

	return MCPResult(Result);
}


TSharedPtr<FJsonValue> FLevelHandlers::SetLightProperties(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorLabel;
	if (auto Err = RequireString(Params, TEXT("actorLabel"), ActorLabel)) return Err;

	REQUIRE_EDITOR_WORLD(World);

	AActor* Actor = FindActorByLabel(World, ActorLabel);
	if (!Actor)
	{
		return MCPError(FString::Printf(TEXT("Actor not found: %s"), *ActorLabel));
	}

	ULightComponent* LightComponent = Actor->FindComponentByClass<ULightComponent>();
	// #608: USkyLightComponent is not a ULightComponent, so it was previously
	// rejected here. Handle its intensity/color/volumetric-scattering directly.
	USkyLightComponent* SkyForProps = Actor->FindComponentByClass<USkyLightComponent>();
	if (!LightComponent && !SkyForProps)
	{
		return MCPError(FString::Printf(TEXT("Actor '%s' does not have a light or sky-light component"), *ActorLabel));
	}

	USceneComponent* PropertyComponent = LightComponent
		? static_cast<USceneComponent*>(LightComponent)
		: static_cast<USceneComponent*>(SkyForProps);

	// Capture previous values before mutation for a field-exact self-inverse
	// rollback. SkyLight stores color on ULightComponentBase even though it is
	// not a ULightComponent, so do not substitute white for its actual color.
	const double PreviousIntensity = LightComponent ? (double)LightComponent->Intensity : (double)SkyForProps->Intensity;
	const FLinearColor PreviousColor = LightComponent
		? LightComponent->GetLightColor()
		: FLinearColor(SkyForProps->LightColor);
	const FRotator PreviousRotation = Actor->GetActorRotation();
	const EComponentMobility::Type PreviousMobility = PropertyComponent->Mobility;
	const double PreviousVolumetricScatteringIntensity = LightComponent
		? (double)LightComponent->VolumetricScatteringIntensity
		: (double)SkyForProps->VolumetricScatteringIntensity;
	UPointLightComponent* PointComponent = Cast<UPointLightComponent>(LightComponent);
	USpotLightComponent* SpotComponent = Cast<USpotLightComponent>(LightComponent);
	const double PreviousSourceRadius = PointComponent ? (double)PointComponent->SourceRadius : 0.0;
	const double PreviousInnerConeAngle = SpotComponent ? (double)SpotComponent->InnerConeAngle : 0.0;
	const double PreviousOuterConeAngle = SpotComponent ? (double)SpotComponent->OuterConeAngle : 0.0;

	auto MobilityToString = [](EComponentMobility::Type Mobility) -> FString
	{
		switch (Mobility)
		{
		case EComponentMobility::Static: return TEXT("static");
		case EComponentMobility::Stationary: return TEXT("stationary");
		default: return TEXT("movable");
		}
	};

	bool bAnyChange = false;
	bool bChangedIntensity = false;
	bool bChangedColor = false;
	bool bChangedVolumetricScattering = false;
	bool bChangedSourceRadius = false;
	bool bChangedInnerConeAngle = false;
	bool bChangedOuterConeAngle = false;
	bool bChangedRotation = false;
	bool bChangedMobility = false;
	bool bActorModified = false;
	bool bComponentModified = false;

	auto PrepareActorMutation = [&]()
	{
		if (!bActorModified)
		{
			Actor->Modify();
			bActorModified = true;
		}
	};
	auto PrepareComponentMutation = [&]()
	{
		PrepareActorMutation();
		if (!bComponentModified)
		{
			PropertyComponent->Modify();
			bComponentModified = true;
		}
	};
	auto ApplyMobility = [&](EComponentMobility::Type NewMobility)
	{
		PrepareComponentMutation();
		PropertyComponent->SetMobility(NewMobility);
		PropertyComponent->MarkRenderStateDirty();
	};

	// Parse mobility before the light properties so setters never run while a
	// registered component is Static. UE deliberately ignores intensity, color,
	// scattering, radius, and cone updates in that state. Promote first when the
	// requested final mobility is dynamic; otherwise promote temporarily and
	// restore Static after applying the properties.
	FString MobilityStr;
	const bool bHasMobilityRequest =
		Params->TryGetStringField(TEXT("mobility"), MobilityStr) && !MobilityStr.IsEmpty();
	EComponentMobility::Type RequestedMobility = PreviousMobility;
	if (bHasMobilityRequest)
	{
		RequestedMobility = EComponentMobility::Movable;
		if (MobilityStr.Equals(TEXT("Static"), ESearchCase::IgnoreCase))
		{
			RequestedMobility = EComponentMobility::Static;
		}
		else if (MobilityStr.Equals(TEXT("Stationary"), ESearchCase::IgnoreCase))
		{
			RequestedMobility = EComponentMobility::Stationary;
		}
	}

	double DynamicPropertyProbe = 0.0;
	const TSharedPtr<FJsonObject>* DynamicColorProbe = nullptr;
	const bool bHasDynamicLightPropertyRequest =
		Params->TryGetNumberField(TEXT("intensity"), DynamicPropertyProbe)
		|| Params->TryGetObjectField(TEXT("color"), DynamicColorProbe)
		|| Params->TryGetNumberField(TEXT("volumetricScatteringIntensity"), DynamicPropertyProbe)
		|| (PointComponent && Params->TryGetNumberField(TEXT("sourceRadius"), DynamicPropertyProbe))
		|| (SpotComponent && Params->TryGetNumberField(TEXT("innerConeAngle"), DynamicPropertyProbe))
		|| (SpotComponent && Params->TryGetNumberField(TEXT("outerConeAngle"), DynamicPropertyProbe));
	bool bMobilityAppliedBeforeProperties = false;
	bool bTemporarilyPromotedStatic = false;
	if (PreviousMobility == EComponentMobility::Static && bHasDynamicLightPropertyRequest)
	{
		if (bHasMobilityRequest && RequestedMobility != EComponentMobility::Static)
		{
			ApplyMobility(RequestedMobility);
			bAnyChange = true;
			bChangedMobility = true;
			bMobilityAppliedBeforeProperties = true;
		}
		else
		{
			ApplyMobility(EComponentMobility::Stationary);
			bTemporarilyPromotedStatic = true;
		}
	}

	double Intensity = 0.0;
	if (Params->TryGetNumberField(TEXT("intensity"), Intensity))
	{
		PrepareComponentMutation();
		if (LightComponent) { LightComponent->SetIntensity(Intensity); }
		else { SkyForProps->SetIntensity((float)Intensity); }
		bAnyChange = true;
		bChangedIntensity = true;
	}

	const TSharedPtr<FJsonObject>* ColorObj = nullptr;
	if (Params->TryGetObjectField(TEXT("color"), ColorObj))
	{
		double R = 255.0, G = 255.0, B = 255.0;
		(*ColorObj)->TryGetNumberField(TEXT("r"), R);
		(*ColorObj)->TryGetNumberField(TEXT("g"), G);
		(*ColorObj)->TryGetNumberField(TEXT("b"), B);
		PrepareComponentMutation();
		if (LightComponent) { LightComponent->SetLightColor(FLinearColor(R / 255.0f, G / 255.0f, B / 255.0f)); }
		else { SkyForProps->SetLightColor(FLinearColor(R / 255.0f, G / 255.0f, B / 255.0f)); }
		bAnyChange = true;
		bChangedColor = true;
	}

	// #608: volumetric scattering, source radius, and spot cone angles.
	if (LightComponent)
	{
		double VolScatter = 0.0;
		if (Params->TryGetNumberField(TEXT("volumetricScatteringIntensity"), VolScatter))
		{
			PrepareComponentMutation();
			LightComponent->SetVolumetricScatteringIntensity((float)VolScatter);
			bAnyChange = true;
			bChangedVolumetricScattering = true;
		}
		double SourceRadius = 0.0;
		if (Params->TryGetNumberField(TEXT("sourceRadius"), SourceRadius))
		{
			if (PointComponent)
			{
				PrepareComponentMutation();
				PointComponent->SetSourceRadius((float)SourceRadius);
				bAnyChange = true;
				bChangedSourceRadius = true;
			}
		}
		if (SpotComponent)
		{
			double Inner = 0.0, Outer = 0.0;
			if (Params->TryGetNumberField(TEXT("innerConeAngle"), Inner))
			{
				PrepareComponentMutation();
				SpotComponent->SetInnerConeAngle((float)Inner);
				bAnyChange = true;
				bChangedInnerConeAngle = true;
			}
			if (Params->TryGetNumberField(TEXT("outerConeAngle"), Outer))
			{
				PrepareComponentMutation();
				SpotComponent->SetOuterConeAngle((float)Outer);
				bAnyChange = true;
				bChangedOuterConeAngle = true;
			}
		}
	}
	else if (SkyForProps)
	{
		double VolScatter = 0.0;
		if (Params->TryGetNumberField(TEXT("volumetricScatteringIntensity"), VolScatter))
		{
			PrepareComponentMutation();
			SkyForProps->SetVolumetricScatteringIntensity((float)VolScatter);
			bAnyChange = true;
			bChangedVolumetricScattering = true;
		}
	}

	// #94: DirectionalLight rotation support (sun angle for time-of-day)
	const TSharedPtr<FJsonObject>* RotObj = nullptr;
	if (Params->TryGetObjectField(TEXT("rotation"), RotObj))
	{
		double Pitch = 0.0, Yaw = 0.0, Roll = 0.0;
		(*RotObj)->TryGetNumberField(TEXT("pitch"), Pitch);
		(*RotObj)->TryGetNumberField(TEXT("yaw"), Yaw);
		(*RotObj)->TryGetNumberField(TEXT("roll"), Roll);
		PrepareActorMutation();
		Actor->SetActorRotation(FRotator((float)Pitch, (float)Yaw, (float)Roll));
		bAnyChange = true;
		bChangedRotation = true;
	}

	// #310: mobility setter - static/stationary/movable. Demotions happen
	// after property setters; promotions required by those setters happened above.
	if (bHasMobilityRequest && !bMobilityAppliedBeforeProperties)
	{
		ApplyMobility(RequestedMobility);
		bAnyChange = true;
		bChangedMobility = true;
	}
	else if (bTemporarilyPromotedStatic)
	{
		ApplyMobility(PreviousMobility);
	}

	if (bAnyChange)
	{
		// Modify() above makes the actor/component transaction-aware. Explicitly
		// dirty their shared package so the new mobility and light settings are
		// offered to save instead of existing only in the live editor object.
		Actor->MarkPackageDirty();
		PropertyComponent->MarkPackageDirty();
	}

	// #94: SkyLight recapture after intensity/color change.
	// USkyLightComponent does not inherit from ULightComponent, so look up
	// directly on the actor instead of casting from LightComponent (#207).
	if (USkyLightComponent* Sky = Actor->FindComponentByClass<USkyLightComponent>())
	{
		bool bRecapture = false;
		Params->TryGetBoolField(TEXT("recaptureSky"), bRecapture);
		if (bRecapture || bAnyChange)
		{
			Sky->RecaptureSky();
		}
	}

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetNumberField(TEXT("intensity"), LightComponent ? (double)LightComponent->Intensity : (SkyForProps ? (double)SkyForProps->Intensity : 0.0));
	Result->SetBoolField(TEXT("isSkyLight"), LightComponent == nullptr && SkyForProps != nullptr);
	Result->SetStringField(TEXT("mobility"), MobilityToString(PropertyComponent->Mobility));

	FLinearColor CurrentColor = LightComponent ? LightComponent->GetLightColor()
		: (SkyForProps ? FLinearColor(SkyForProps->LightColor) : FLinearColor::White);
	TSharedPtr<FJsonObject> ColorResult = MakeShared<FJsonObject>();
	ColorResult->SetNumberField(TEXT("r"), CurrentColor.R * 255.0f);
	ColorResult->SetNumberField(TEXT("g"), CurrentColor.G * 255.0f);
	ColorResult->SetNumberField(TEXT("b"), CurrentColor.B * 255.0f);
	Result->SetObjectField(TEXT("color"), ColorResult);

	if (bAnyChange)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("actorLabel"), ActorLabel);
		if (bChangedIntensity)
		{
			Payload->SetNumberField(TEXT("intensity"), PreviousIntensity);
		}
		if (bChangedColor)
		{
			TSharedPtr<FJsonObject> PrevColor = MakeShared<FJsonObject>();
			PrevColor->SetNumberField(TEXT("r"), PreviousColor.R * 255.0f);
			PrevColor->SetNumberField(TEXT("g"), PreviousColor.G * 255.0f);
			PrevColor->SetNumberField(TEXT("b"), PreviousColor.B * 255.0f);
			Payload->SetObjectField(TEXT("color"), PrevColor);
		}
		if (bChangedVolumetricScattering)
		{
			Payload->SetNumberField(TEXT("volumetricScatteringIntensity"), PreviousVolumetricScatteringIntensity);
		}
		if (bChangedSourceRadius)
		{
			Payload->SetNumberField(TEXT("sourceRadius"), PreviousSourceRadius);
		}
		if (bChangedInnerConeAngle)
		{
			Payload->SetNumberField(TEXT("innerConeAngle"), PreviousInnerConeAngle);
		}
		if (bChangedOuterConeAngle)
		{
			Payload->SetNumberField(TEXT("outerConeAngle"), PreviousOuterConeAngle);
		}
		if (bChangedRotation)
		{
			TSharedPtr<FJsonObject> PrevRotation = MakeShared<FJsonObject>();
			PrevRotation->SetNumberField(TEXT("pitch"), PreviousRotation.Pitch);
			PrevRotation->SetNumberField(TEXT("yaw"), PreviousRotation.Yaw);
			PrevRotation->SetNumberField(TEXT("roll"), PreviousRotation.Roll);
			Payload->SetObjectField(TEXT("rotation"), PrevRotation);
		}
		if (bChangedMobility)
		{
			Payload->SetStringField(TEXT("mobility"), MobilityToString(PreviousMobility));
		}
		MCPSetRollback(Result, TEXT("set_light_properties"), Payload);
	}

	return MCPResult(Result);
}


// #94: ExponentialHeightFog tuning
TSharedPtr<FJsonValue> FLevelHandlers::SetFogProperties(const TSharedPtr<FJsonObject>& Params)
{
	FString WorldScope = OptionalString(Params, TEXT("world"), TEXT("editor"));
	UWorld* World = ResolveWorldFromParams(Params, *WorldScope);
	if (!World) return MCPError(TEXT("World not available"));

	FString ActorLabel = OptionalString(Params, TEXT("actorLabel"));

	AExponentialHeightFog* Fog = nullptr;
	for (TActorIterator<AExponentialHeightFog> It(World); It; ++It)
	{
		if (ActorLabel.IsEmpty() || It->GetActorLabel() == ActorLabel)
		{
			Fog = *It;
			break;
		}
	}
	if (!Fog) return MCPError(TEXT("No ExponentialHeightFog actor found"));

	UExponentialHeightFogComponent* FC = Fog->GetComponent();
	if (!FC) return MCPError(TEXT("Fog component missing"));

	double Density = 0.0;
	if (Params->TryGetNumberField(TEXT("fogDensity"), Density))
	{
		FC->FogDensity = (float)Density;
	}
	double HeightFalloff = 0.0;
	if (Params->TryGetNumberField(TEXT("fogHeightFalloff"), HeightFalloff))
	{
		FC->FogHeightFalloff = (float)HeightFalloff;
	}
	double StartDistance = 0.0;
	if (Params->TryGetNumberField(TEXT("startDistance"), StartDistance))
	{
		FC->StartDistance = (float)StartDistance;
	}
	const TSharedPtr<FJsonObject>* ColorObj = nullptr;
	if (Params->TryGetObjectField(TEXT("fogInscatteringColor"), ColorObj) ||
	    Params->TryGetObjectField(TEXT("color"), ColorObj))
	{
		double R = 255, G = 255, B = 255;
		(*ColorObj)->TryGetNumberField(TEXT("r"), R);
		(*ColorObj)->TryGetNumberField(TEXT("g"), G);
		(*ColorObj)->TryGetNumberField(TEXT("b"), B);
		FC->FogInscatteringLuminance = FLinearColor(R / 255.0f, G / 255.0f, B / 255.0f);
	}

	// #608: volumetric fog controls.
	if (Params->HasField(TEXT("enableVolumetricFog")))
	{
		FC->SetVolumetricFog(OptionalBool(Params, TEXT("enableVolumetricFog"), true));
	}
	double VolScatterDist = 0.0;
	if (Params->TryGetNumberField(TEXT("volumetricFogScatteringDistribution"), VolScatterDist))
	{
		FC->SetVolumetricFogScatteringDistribution((float)VolScatterDist);
	}
	double VolExtinction = 0.0;
	if (Params->TryGetNumberField(TEXT("volumetricFogExtinctionScale"), VolExtinction))
	{
		FC->SetVolumetricFogExtinctionScale((float)VolExtinction);
	}
	double VolDistance = 0.0;
	if (Params->TryGetNumberField(TEXT("volumetricFogDistance"), VolDistance))
	{
		FC->SetVolumetricFogDistance((float)VolDistance);
	}
	const TSharedPtr<FJsonObject>* AlbedoObj = nullptr;
	if (Params->TryGetObjectField(TEXT("volumetricFogAlbedo"), AlbedoObj) && AlbedoObj)
	{
		double R = 255, G = 255, B = 255;
		(*AlbedoObj)->TryGetNumberField(TEXT("r"), R);
		(*AlbedoObj)->TryGetNumberField(TEXT("g"), G);
		(*AlbedoObj)->TryGetNumberField(TEXT("b"), B);
		FC->SetVolumetricFogAlbedo(FColor((uint8)R, (uint8)G, (uint8)B));
	}

	FC->MarkRenderStateDirty();

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("actorLabel"), Fog->GetActorLabel());
	Result->SetNumberField(TEXT("fogDensity"), FC->FogDensity);
	Result->SetNumberField(TEXT("fogHeightFalloff"), FC->FogHeightFalloff);
	return MCPResult(Result);
}

// #94: Bulk actor lookup helper
