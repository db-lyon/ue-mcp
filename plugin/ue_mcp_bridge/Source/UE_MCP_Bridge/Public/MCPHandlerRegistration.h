#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"
#include "MCPHandlerSpec.h"
#include "MCPWireContract.h"

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

// UEMCP_BRIDGE_API_VERSION (the handler ABI) and UEMCP_BRIDGE_PROTOCOL_VERSION
// (the socket) come from MCPWireContract.h, generated from the contract the
// client is generated from too.

namespace UEMCP
{
	using FExternalHandlerFn = TFunction<TSharedPtr<FJsonValue>(const TSharedPtr<FJsonObject>& Params)>;

	/** Register a handler under the given method name. Last writer wins
	 *  if the same name is registered twice, and a timeout from an earlier
	 *  registration is dropped. */
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

	/** Whether an external handler is registered under MethodName, without
	 *  copying the handler. */
	UE_MCP_BRIDGE_API bool HasExternalHandler(const FString& MethodName);

	/** The registered timeout for MethodName in seconds, or 0 when none was
	 *  registered (or no such handler exists). */
	UE_MCP_BRIDGE_API float GetExternalHandlerTimeout(const FString& MethodName);

	/** Snapshot of registered external handler names. Diagnostic use only;
	 *  do not rely on the order. */
	UE_MCP_BRIDGE_API TArray<FString> GetExternalHandlerNames();

	/** Register a handler together with its parameter contract (#1282), the same
	 *  contract a core handler declares (MCPHandlerSpec.h). The bridge publishes it in
	 *  get_bridge_capabilities.pluginHandlerSpecs and renames declared aliases before the
	 *  handler runs. The handler is registered either way; a spec that fails
	 *  validation is logged and dropped, and the call returns false.
	 *  TimeoutSeconds 0 keeps the default game-thread timeout. */
	UE_MCP_BRIDGE_API bool RegisterExternalHandler(const FString& MethodName, FExternalHandlerFn Handler,
		const TArray<FMCPParamSpec>& Params, const FMCPSpecRules& Rules = FMCPSpecRules(), float TimeoutSeconds = 0.0f);

	/** The contract registered with MethodName, if it was registered with one. */
	UE_MCP_BRIDGE_API bool LookupExternalHandlerSpec(const FString& MethodName, FMCPHandlerSpec& OutSpec);

	/** Snapshot of every external handler's contract, keyed by method name. */
	UE_MCP_BRIDGE_API TMap<FString, FMCPHandlerSpec> GetExternalHandlerSpecs();
}
