#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"

/**
 * Neural Network Engine runtimes and models, and Learning Agents observation
 * schemas. Both are engine-bundled: NNE is an engine module, linked when the
 * engine has it; Learning Agents is an optional plugin reached through
 * reflection and the Blueprint graph of an interactor. Registered under the
 * gameplay category.
 */
class FMachineLearningHandlers
{
public:
	static void RegisterHandlers(class FMCPHandlerRegistry& Registry);

private:
	// MachineLearningHandlers_NNE.cpp
	static TSharedPtr<FJsonValue> NNEListRuntimes(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> NNEImportModel(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> NNERunInference(const TSharedPtr<FJsonObject>& Params);

	// MachineLearningHandlers_LearningAgents.cpp
	static TSharedPtr<FJsonValue> LearningAgentsListObservationTypes(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> LearningAgentsReadObservations(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> LearningAgentsAddObservation(const TSharedPtr<FJsonObject>& Params);
};
