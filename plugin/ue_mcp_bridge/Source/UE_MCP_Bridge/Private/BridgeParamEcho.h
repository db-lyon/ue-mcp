#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "HAL/CriticalSection.h"
#include "HAL/ThreadSafeBool.h"
#include "Misc/DateTime.h"

/**
 * What the bridge was actually handed, so a test can assert it.
 *
 * Handlers read the fields they know about through the helpers in
 * HandlerUtils.h and ignore everything else. That is the right behaviour on the
 * wire (an older bridge must not choke on a newer client's extra field) and it
 * means a parameter the client leaked into a call is completely invisible: the
 * call succeeds, the result is correct, and nothing anywhere reports that a
 * routing key travelled to the editor.
 *
 * Multi-editor addressing turns that from untidy into load-bearing. The
 * `editor` parameter selects which session a call goes to and must be consumed
 * by the client; forwarding it to a handler is a bug that no assertion on the
 * response can see. This records the parameter names each dispatch arrived
 * with, which gives that assertion something to read.
 *
 * Names only, never values. The point is which keys arrived, and a bridge that
 * remembered values would be a bridge that could be asked to read back asset
 * paths, prompts and file contents from calls it has already answered.
 *
 * Off unless the process was started with it asked for. It is a test facility,
 * so it is enabled from the command line or the environment at construction and
 * there is deliberately no way to turn it on over the socket.
 */
struct FMCPParamEchoEntry
{
	FString Method;
	/** Sorted, so an assertion does not depend on JSON field order. */
	TArray<FString> ParamNames;
	FDateTime ReceivedAtUtc;
};

class FMCPParamEcho
{
public:
	/** How many dispatches to remember. Oldest are dropped first. */
	static constexpr int32 MaxEntries = 256;

	static FMCPParamEcho& Get();

	bool IsEnabled() const { return bEnabled; }
	void SetEnabled(bool bInEnabled);

	/** True when -MCPParamEcho or UE_MCP_PARAM_ECHO asked for the echo. */
	static bool ResolveEnabledFromEnvironment();

	/** Note one dispatch. A no-op, and lock-free, while disabled. */
	void Record(const FString& Method, const TSharedPtr<FJsonObject>& Params);

	TArray<FMCPParamEchoEntry> Snapshot() const;
	void Clear();

	/** The answer to get_param_echo, built without touching the game thread. */
	TSharedPtr<FJsonObject> BuildPayload() const;

private:
	mutable FCriticalSection Mutex;
	TArray<FMCPParamEchoEntry> Entries;
	FThreadSafeBool bEnabled{false};
};
