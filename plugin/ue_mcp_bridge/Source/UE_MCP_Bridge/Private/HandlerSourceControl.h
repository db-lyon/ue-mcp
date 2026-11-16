#pragma once

// Checkout on write: whether a bridge write checks a read-only package out of
// the editor's source control provider first. Off by default; turned on with
// source_control(set_checkout_mode, mode=auto) or UE_MCP_CHECKOUT_MODE=auto.

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"

namespace MCPSourceControl
{
	/** True when bridge writes check out a read-only file before writing it. */
	UE_MCP_BRIDGE_API bool IsAutoCheckoutEnabled();

	UE_MCP_BRIDGE_API void SetAutoCheckoutEnabled(bool bEnabled);

	/** In auto mode, check FileName out through the provider. True when it is
	 *  writable afterwards. False with OutNote saying why when a checkout was
	 *  attempted or refused; false with OutNote empty in manual mode. */
	UE_MCP_BRIDGE_API bool TryAutoCheckout(const FString& FileName, FString& OutNote);

	/** How many auto checkouts have been recorded; a handler dispatch takes this before it runs. */
	UE_MCP_BRIDGE_API int32 AutoCheckoutMark();

	/** Report the checkouts recorded since Mark on Result as autoCheckedOut, and forget them. */
	UE_MCP_BRIDGE_API void AttachAutoCheckouts(const TSharedPtr<FJsonValue>& Result, int32 Mark);
}
