#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"

class FPCGHandlers
{
public:
	static void RegisterHandlers(class FMCPHandlerRegistry& Registry);

private:
	static TSharedPtr<FJsonValue> ListPCGGraphs(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetPCGComponents(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreatePCGGraph(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ReadPCGGraph(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddPCGNode(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ConnectPCGNodes(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RemovePCGNode(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetPCGNodeSettings(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ExecutePCGGraph(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SpawnPCGVolume(const TSharedPtr<FJsonObject>& Params);
};
