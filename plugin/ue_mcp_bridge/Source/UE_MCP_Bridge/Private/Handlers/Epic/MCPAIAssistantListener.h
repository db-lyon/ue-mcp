#pragma once

// Receives UAIAssistantInstance::OnConversationUpdated (UE 5.9+) without linking
// the AIAssistant module. The update type arrives as its uint8 underlying value;
// the binding checks the delegate's parm layout matches before it subscribes.

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "MCPAIAssistantListener.generated.h"

UCLASS(Transient)
class UMCPAIAssistantListener : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION()
	void HandleConversationUpdated(const FString& ConversationId, uint8 UpdateType);
};
