#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"

/**
 * Movie Render Queue jobs, rendered in the editor through the Movie Render
 * Pipeline plugin. The plugin is reached at runtime through reflection, so the
 * bridge loads whether or not a project enables it, and every action answers
 * with a clear error when it is off. Registered under the editor category,
 * beside the sequencer actions.
 */
class FMovieRenderQueueHandlers
{
public:
	static void RegisterHandlers(class FMCPHandlerRegistry& Registry);

private:
	static TSharedPtr<FJsonValue> CreateJob(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListJobs(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> DeleteJob(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> Render(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetRenderStatus(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CancelRender(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListOutputs(const TSharedPtr<FJsonObject>& Params);
};
