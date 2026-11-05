#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"

// Source control through the editor's own provider (ISourceControlModule), so
// the same calls work for Perforce, Git, Unity Version Control and Subversion.
// Every action except get_provider refuses with one clear result when no
// provider is enabled.
class FSourceControlHandlers
{
public:
	static void RegisterHandlers(class FMCPHandlerRegistry& Registry);

private:
	static TSharedPtr<FJsonValue> GetProvider(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetStatus(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListChanges(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> Checkout(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> MarkForAdd(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> Revert(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> Diff(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> Submit(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetCheckoutMode(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> MarkForDelete(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> Sync(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> History(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> Connect(const TSharedPtr<FJsonObject>& Params);
};
