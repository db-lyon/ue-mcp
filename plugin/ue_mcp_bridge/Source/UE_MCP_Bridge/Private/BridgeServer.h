#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"
#include "HandlerRegistry.h"
#include "GameThreadExecutor.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "HAL/ThreadSafeBool.h"
#include "Containers/Queue.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <winsock2.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

class FMCPBridgeServer : public FRunnable
{
public:
	FMCPBridgeServer(int32 Port = 9877);
	~FMCPBridgeServer();

	// Start the server
	bool Start();

	// FRunnable interface
	virtual bool Init() override;
	virtual uint32 Run() override;
	virtual void Stop() override;
	virtual void Exit() override;
	
	// Public stop method (calls FRunnable::Stop)
	void Shutdown();

	// #492: per-project port lockfile so multiple editors can coexist.
	static FString GetPortLockfilePath();
	static void WritePortLockfile(int32 PortValue);
	static void DeletePortLockfile();

	// Deterministic per-worktree base port. Derived from a hash of the project
	// root path so every checkout gets a stable, launch-order-independent port
	// that the Node client computes identically (see src/port.ts). Keep the two
	// implementations in lockstep.
	static int32 DeriveProjectPort(const FString& ProjectRootDir);

	// Resolve the base port to bind: -MCPPort= command line > UE_MCP_PORT env >
	// deterministic derived port. The probe loop in Run() walks upward from
	// here on collision, and the actual bound port is published to the lockfile.
	static int32 ResolveConfiguredPort();

	// Get handler registry
	FMCPHandlerRegistry& GetHandlerRegistry() { return HandlerRegistry; }

	// Get game thread executor (to set editor ready)
	FMCPGameThreadExecutor& GetGameThreadExecutor() { return GameThreadExecutor; }

	// Process a JSON-RPC message
	FString ProcessMessage(const FString& Message);

private:
	// Server port
	int32 ServerPort;

	// Thread management
	FRunnableThread* ServerThread;
	FThreadSafeBool bShouldStop;
	FThreadSafeBool bIsRunning;

	// Handler registry
	FMCPHandlerRegistry HandlerRegistry;

	// Game thread executor
	FMCPGameThreadExecutor GameThreadExecutor;

	// JSON-RPC processing
	TSharedPtr<FJsonObject> ParseJsonRpcRequest(const FString& Message);
	FString CreateJsonRpcResponse(const TSharedPtr<FJsonObject>& Request, const TSharedPtr<FJsonValue>& Result);
	FString CreateJsonRpcError(const TSharedPtr<FJsonObject>& Request, int32 ErrorCode, const FString& ErrorMessage);

	// WebSocket connection handling
#if PLATFORM_WINDOWS
	void HandleWebSocketConnection(SOCKET ClientSocketFD);
	FString PerformWebSocketHandshake(SOCKET ClientSocketFD);
	void ProcessWebSocketMessages(SOCKET ClientSocketFD);
	FString ReadHttpRequest(SOCKET SocketFD);
#else
	void HandleWebSocketConnection(int32 ClientSocketFD);
	FString PerformWebSocketHandshake(int32 ClientSocketFD);
	void ProcessWebSocketMessages(int32 ClientSocketFD);
	FString ReadHttpRequest(int32 SocketFD);
#endif
	FString CreateWebSocketAcceptKey(const FString& ClientKey);
	TArray<uint8> CreateWebSocketFrame(const FString& Message);
	FString ParseWebSocketFrame(const TArray<uint8>& Data);

	// Server socket (will use platform-specific implementation)
	void* ServerSocket;
};
