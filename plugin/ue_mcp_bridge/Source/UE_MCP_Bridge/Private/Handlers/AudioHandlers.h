#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"

class FAudioHandlers
{
public:
	static void RegisterHandlers(class FMCPHandlerRegistry& Registry);

private:
	static TSharedPtr<FJsonValue> ListSoundAssets(const TSharedPtr<FJsonObject>& Params);
	// #664: import a WAV/OGG file as a USoundWave asset.
	static TSharedPtr<FJsonValue> ImportAudio(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreateSoundCue(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreateMetaSoundSource(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PlaySoundAtLocation(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SpawnAmbientSound(const TSharedPtr<FJsonObject>& Params);
};
