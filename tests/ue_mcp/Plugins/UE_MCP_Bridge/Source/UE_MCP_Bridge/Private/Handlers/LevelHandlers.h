#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"

class FLevelHandlers
{
public:
	// Register all level handlers
	static void RegisterHandlers(class FMCPHandlerRegistry& Registry);

private:
	// Handler implementations
	static TSharedPtr<FJsonValue> GetOutliner(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PlaceActor(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> DeleteActor(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetActorDetails(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetCurrentLevel(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListLevels(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetSelectedActors(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListVolumes(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> MoveActor(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SelectActors(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SpawnLight(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetLightProperties(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SpawnVolume(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddComponentToActor(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> LoadLevel(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SaveLevel(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListSublevels(const TSharedPtr<FJsonObject>& Params);
};
