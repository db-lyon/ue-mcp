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

	// Capture previous values before mutation for self-inverse rollback.
	const double PreviousIntensity = LightComponent ? (double)LightComponent->Intensity : (SkyForProps ? (double)SkyForProps->Intensity : 0.0);
	const FLinearColor PreviousColor = LightComponent ? LightComponent->GetLightColor() : FLinearColor::White;
	const FRotator PreviousRotation = Actor->GetActorRotation();

	bool bAnyChange = false;

	double Intensity = 0.0;
	if (Params->TryGetNumberField(TEXT("intensity"), Intensity))
	{
		if (LightComponent) { LightComponent->SetIntensity(Intensity); }
		else if (SkyForProps) { SkyForProps->Intensity = (float)Intensity; SkyForProps->MarkRenderStateDirty(); }
		bAnyChange = true;
	}

	const TSharedPtr<FJsonObject>* ColorObj = nullptr;
	if (Params->TryGetObjectField(TEXT("color"), ColorObj))
	{
		double R = 255.0, G = 255.0, B = 255.0;
		(*ColorObj)->TryGetNumberField(TEXT("r"), R);
		(*ColorObj)->TryGetNumberField(TEXT("g"), G);
		(*ColorObj)->TryGetNumberField(TEXT("b"), B);
		if (LightComponent) { LightComponent->SetLightColor(FLinearColor(R / 255.0f, G / 255.0f, B / 255.0f)); }
		else if (SkyForProps) { SkyForProps->LightColor = FColor((uint8)R, (uint8)G, (uint8)B); SkyForProps->MarkRenderStateDirty(); }
		bAnyChange = true;
	}

	// #608: volumetric scattering, source radius, and spot cone angles.
	if (LightComponent)
	{
		double VolScatter = 0.0;
		if (Params->TryGetNumberField(TEXT("volumetricScatteringIntensity"), VolScatter))
		{
			LightComponent->SetVolumetricScatteringIntensity((float)VolScatter);
			bAnyChange = true;
		}
		double SourceRadius = 0.0;
		if (Params->TryGetNumberField(TEXT("sourceRadius"), SourceRadius))
		{
			if (UPointLightComponent* PLC = Cast<UPointLightComponent>(LightComponent))
			{
				PLC->SetSourceRadius((float)SourceRadius);
				bAnyChange = true;
			}
		}
		if (USpotLightComponent* SLC = Cast<USpotLightComponent>(LightComponent))
		{
			double Inner = 0.0, Outer = 0.0;
			if (Params->TryGetNumberField(TEXT("innerConeAngle"), Inner)) { SLC->SetInnerConeAngle((float)Inner); bAnyChange = true; }
			if (Params->TryGetNumberField(TEXT("outerConeAngle"), Outer)) { SLC->SetOuterConeAngle((float)Outer); bAnyChange = true; }
		}
	}
	else if (SkyForProps)
	{
		double VolScatter = 0.0;
		if (Params->TryGetNumberField(TEXT("volumetricScatteringIntensity"), VolScatter))
		{
			SkyForProps->SetVolumetricScatteringIntensity((float)VolScatter);
			bAnyChange = true;
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
		Actor->SetActorRotation(FRotator((float)Pitch, (float)Yaw, (float)Roll));
		bAnyChange = true;
	}

	// #310: mobility setter — static/stationary/movable.
	FString MobilityStr;
	if (Params->TryGetStringField(TEXT("mobility"), MobilityStr) && !MobilityStr.IsEmpty())
	{
		EComponentMobility::Type NewMobility = EComponentMobility::Movable;
		if (MobilityStr.Equals(TEXT("Static"), ESearchCase::IgnoreCase))
		{
			NewMobility = EComponentMobility::Static;
		}
		else if (MobilityStr.Equals(TEXT("Stationary"), ESearchCase::IgnoreCase))
		{
			NewMobility = EComponentMobility::Stationary;
		}
		LightComponent->SetMobility(NewMobility);
		LightComponent->MarkRenderStateDirty();
		bAnyChange = true;
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

	FLinearColor CurrentColor = LightComponent ? LightComponent->GetLightColor()
		: (SkyForProps ? FLinearColor(SkyForProps->LightColor) : FLinearColor::White);
	TSharedPtr<FJsonObject> ColorResult = MakeShared<FJsonObject>();
	ColorResult->SetNumberField(TEXT("r"), CurrentColor.R * 255.0f);
	ColorResult->SetNumberField(TEXT("g"), CurrentColor.G * 255.0f);
	ColorResult->SetNumberField(TEXT("b"), CurrentColor.B * 255.0f);
	Result->SetObjectField(TEXT("color"), ColorResult);

	if (bAnyChange)
	{
		TSharedPtr<FJsonObject> PrevColor = MakeShared<FJsonObject>();
		PrevColor->SetNumberField(TEXT("r"), PreviousColor.R * 255.0f);
		PrevColor->SetNumberField(TEXT("g"), PreviousColor.G * 255.0f);
		PrevColor->SetNumberField(TEXT("b"), PreviousColor.B * 255.0f);
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("actorLabel"), ActorLabel);
		Payload->SetNumberField(TEXT("intensity"), PreviousIntensity);
		Payload->SetObjectField(TEXT("color"), PrevColor);
		MCPSetRollback(Result, TEXT("set_light_properties"), Payload);
	}

	return MCPResult(Result);
}


// #94: ExponentialHeightFog tuning
TSharedPtr<FJsonValue> FLevelHandlers::SetFogProperties(const TSharedPtr<FJsonObject>& Params)
{
	FString WorldScope = OptionalString(Params, TEXT("world"), TEXT("editor"));
	UWorld* World = ResolveWorldScope(WorldScope);
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
