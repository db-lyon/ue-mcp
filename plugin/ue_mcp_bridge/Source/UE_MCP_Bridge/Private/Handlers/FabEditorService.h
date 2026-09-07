#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "UObject/Object.h"
#include "FabEditorService.generated.h"

// Only a nonce-scoped JSON response is accepted. No credentials or arbitrary
// editor operations are exposed to the embedded page.
UCLASS(Transient)
class UMCPFabBrowserReply : public UObject
{
	GENERATED_BODY()
public:
	FString OperationId;
	UFUNCTION()
	void Complete(const FString& Nonce, const FString& Json);
};

namespace MCPFabEditor
{
	void Register();
	void Shutdown();
	TSharedPtr<FJsonValue> Execute(const TSharedPtr<FJsonObject>& Params);
	void CompleteBrowser(const FString& OperationId, const FString& Nonce, const FString& Json);

	// Pure helpers also exercised by the native automation tests.
	bool AccountFromToken(const FString& Token, FString& OutAccount);
	bool IsFabUrl(const FString& Url);
	bool ParseLibraryPage(const TSharedPtr<FJsonObject>& Page,
		TArray<TSharedPtr<FJsonObject>>& OutItems, FString& OutCursor, FString& OutError);
}
