#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"

/**
 * Public API for plugins that extend the UE-MCP bridge with their own
 * native C++ handlers. Plugins should:
 *
 *   1. Add "UE_MCP_Bridge" to PrivateDependencyModuleNames in their .Build.cs.
 *   2. From IModuleInterface::StartupModule, call
 *      UEMCP::RegisterExternalHandler(TEXT("voxel.sample_density"),
 *          [](const TSharedPtr<FJsonObject>& Params) -> TSharedPtr<FJsonValue> {
 *              // ...do the work, return JSON
 *          });
 *
 * Registrations are global, thread-safe, and merged into every bridge
 * server instance on dispatch. They survive the bridge's lifetime, so
 * register exactly once at module startup. Unregister in ShutdownModule
 * to keep things clean across hot-reload (Live Coding) cycles.
 *
 * ABI: UEMCP_BRIDGE_API_VERSION declared below. Plugins should refuse to
 * load when their minBridgeApi exceeds the bridge's UEMCP_BRIDGE_API_VERSION.
 * The TS-side loader does this gate for plugins declared via
 * nativeModule: in ue-mcp.plugin.yml; native code that pokes here directly
 * is responsible for its own gating.
 */

/** Current bridge handler ABI version. Bump when the FHandlerFunction
 *  signature or registration contract changes in a breaking way. */
#define UEMCP_BRIDGE_API_VERSION 1

/**
 * Current bridge wire protocol version, reported by get_bridge_capabilities.
 *
 * Distinct from the handler ABI above: this describes what a client can expect
 * of the socket (framing, control frames, the handshake itself), not what a
 * native plugin can expect of the registration API.
 *
 * Version 1 is every bridge built before the handshake existed. Those answer
 * get_bridge_capabilities with "Unknown method", which is how a client
 * recognises them.
 *
 * Version 2 reassembles messages across reads and frames, answers close and
 * ping frames, validates the upgrade request, and serves this handshake.
 *
 * Bump this when a change would make an older client misread the wire. The
 * client compares its own expectation against what it is told and names both
 * numbers when they differ, so the value is only useful if it moves.
 */
#define UEMCP_BRIDGE_PROTOCOL_VERSION 2

namespace UEMCP
{
	using FExternalHandlerFn = TFunction<TSharedPtr<FJsonValue>(const TSharedPtr<FJsonObject>& Params)>;

	/** Register a handler under the given method name. Last writer wins
	 *  if the same name is registered twice. */
	UE_MCP_BRIDGE_API void RegisterExternalHandler(const FString& MethodName, FExternalHandlerFn Handler);

	/** Register a handler with a non-default game-thread execution timeout
	 *  (seconds). Use this for long-running operations like code-gen or
	 *  build invocations that legitimately need more than the default. */
	UE_MCP_BRIDGE_API void RegisterExternalHandlerWithTimeout(const FString& MethodName, FExternalHandlerFn Handler, float TimeoutSeconds);

	/** Remove a previously registered external handler. Safe to call for
	 *  unknown names. Call this from ShutdownModule for a clean reload. */
	UE_MCP_BRIDGE_API void UnregisterExternalHandler(const FString& MethodName);

	/** Lookup a registered external handler. Returns false if no handler
	 *  exists under MethodName. OutTimeoutSeconds is 0 when no per-handler
	 *  override was registered (caller should use its default). */
	UE_MCP_BRIDGE_API bool LookupExternalHandler(const FString& MethodName, FExternalHandlerFn& OutFn, float& OutTimeoutSeconds);

	/** Snapshot of registered external handler names. Diagnostic use only;
	 *  do not rely on the order. */
	UE_MCP_BRIDGE_API TArray<FString> GetExternalHandlerNames();
}
