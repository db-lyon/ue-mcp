#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"
#include "HandlerRegistry.h"
#include "GameThreadExecutor.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "HAL/ThreadSafeBool.h"
#include "HAL/ThreadSafeCounter.h"
#include "HAL/CriticalSection.h"
#include "Containers/Queue.h"
#include "Containers/Set.h"
#include "Misc/Guid.h"
#include "Misc/DateTime.h"

#include "MCPSocketPlatform.h"
#include "BridgeWebSocket.h"
#include "BridgePortConfig.h"
#include "BridgeStateFiles.h"

/**
 * Sole owner of one accepted client socket.
 *
 * The accept loop creates the handle and hands it to a connection thread; from
 * that moment this object is the only thing allowed to close it, and it does so
 * exactly once. The close call used to be copied down every early return in the
 * connection path, which is how a new return ends up leaking a socket and a
 * reconnect ends up closing one twice.
 */
class FMCPClientSocket
{
public:
	explicit FMCPClientSocket(FMCPSocketHandle InHandle);
	~FMCPClientSocket();

	FMCPClientSocket(const FMCPClientSocket&) = delete;
	FMCPClientSocket& operator=(const FMCPClientSocket&) = delete;

	FMCPSocketHandle Get() const { return Handle; }
	bool IsValid() const { return Handle != MCP_INVALID_SOCKET; }

	/** Close now. Idempotent, so the destructor is a no-op afterwards. */
	void Close();

private:
	FMCPSocketHandle Handle;
};

class FMCPBridgeServer;

/**
 * Takes the handle out of the live connection set.
 *
 * Declared LAST of the connection thread's guards and therefore destroyed
 * FIRST, so the handle leaves the set while it is still open: half-closing a
 * handle number the operating system has already handed to someone else is
 * worse than not waking it at all.
 */
class FMCPConnectionUnlist
{
public:
	FMCPConnectionUnlist(FMCPBridgeServer& InServer, FMCPSocketHandle InHandle);
	~FMCPConnectionUnlist();

	FMCPConnectionUnlist(const FMCPConnectionUnlist&) = delete;
	FMCPConnectionUnlist& operator=(const FMCPConnectionUnlist&) = delete;

private:
	FMCPBridgeServer& Server;
	FMCPSocketHandle Handle;
};

/**
 * Releases the connection record the accept loop made before it spawned this
 * thread.
 *
 * The accept loop registers, so a shutdown racing an accept can never conclude
 * that nothing is running. Dropping the count is what lets
 * WaitForConnectionsToFinish return, and once it returns the module is free to
 * finish ShutdownModule and let the DLL unload - so nothing this thread still
 * has to execute may live in the module's code pages after that point. This
 * object is therefore declared FIRST of the thread's guards and destroyed
 * LAST, after the handle has left the live set and after closesocket. Only the
 * lambda's own return sequence follows, which is why the count is dropped here
 * and not inside the unlist guard where a blocking closesocket still came
 * after it.
 */
class FMCPConnectionRelease
{
public:
	explicit FMCPConnectionRelease(FMCPBridgeServer& InServer);
	~FMCPConnectionRelease();

	FMCPConnectionRelease(const FMCPConnectionRelease&) = delete;
	FMCPConnectionRelease& operator=(const FMCPConnectionRelease&) = delete;

private:
	FMCPBridgeServer& Server;
};

/**
 * The one way to write to a client socket. A deferred answer is sent from a
 * thread of its own while the connection thread goes on answering, so every
 * frame is written whole under this lock, and nothing is written once the
 * connection has retired it.
 */
class FMCPConnectionWriter
{
public:
	explicit FMCPConnectionWriter(FMCPSocketHandle InSocket) : Socket(InSocket) {}

	bool Send(const TArray<uint8>& Frame);
	void Close(uint16 StatusCode, const FString& Reason);
	void Retire();

private:
	FCriticalSection Lock;
	FMCPSocketHandle Socket;
	bool bOpen = true;
};

class FMCPBridgeServer : public FRunnable
{
	friend class FMCPConnectionUnlist;
	friend class FMCPConnectionRelease;
	friend class FMCPConnectionWriter;

public:
	explicit FMCPBridgeServer(const FMCPBridgePortChoice& InPortChoice);
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

	/** Who this server is, as every record it publishes names it. */
	FMCPBridgeIdentity Identity() const;

	// Get handler registry
	FMCPHandlerRegistry& GetHandlerRegistry() { return HandlerRegistry; }

	// Get game thread executor (to set editor ready)
	FMCPGameThreadExecutor& GetGameThreadExecutor() { return GameThreadExecutor; }

	// Process a JSON-RPC message
	FString ProcessMessage(const FString& Message);

	// SendInterim writes a notification frame to the requesting client while a
	// deferred answer is awaited (progress).
	// OutDeferred, when given, receives the wait for a deferred answer instead of
	// it running here; the empty string is returned and the caller runs it.
	FString ProcessMessage(const FString& Message, const TFunction<void(const FString&)>& SendInterim, TFunction<FString()>* OutDeferred);

	/** The JSON-RPC response for a handler's result, with the engine state attached to a timeout. */
	FString FinishJsonRpcResponse(const TSharedPtr<FJsonObject>& Request, const FString& Method, const TSharedPtr<FJsonValue>& Result);

	/** Wait for a handler's deferred answer until the deadline, a cancel or shutdown, sending progress frames when asked. */
	TSharedPtr<FJsonValue> AwaitDeferredAnswer(
		const FJsonObject& Request, const FString& Method, class FMCPDeferredCall& Deferred, class FMCPRequestContext& Context,
		double DeadlineSeconds, bool bWantsProgress, const TFunction<void(const FString&)>& SendInterim);

	/**
	 * #821: what this bridge is and what it can do, answered without the game
	 * thread. Protocol version, handler ABI version, the binary's compile
	 * timestamp, this instance's identity, and the action list the running
	 * binary actually registered.
	 */
	/** The refusal every non-modal-safe method gets while a modal is up. */
	TSharedPtr<FJsonObject> BuildDialogGateRefusal(
		const FString& Method, const FString& Title, const FString& Message, const TArray<FString>& Buttons);

	TSharedPtr<FJsonObject> BuildCapabilitiesPayload();

	/** The socket-thread methods (get_engine_state, get_bridge_capabilities, the
	 *  parameter echo pair, cancel_request), registered like any handler. */
	void RegisterBuiltInMethods();

	/**
	 * Write an unrequested JSON-RPC notification (a frame with a method and no
	 * id) to every connected client, for news the editor has that nobody asked
	 * for, such as a Play session the user started by hand. Callable from any
	 * thread. A client that has gone away is skipped, not waited for.
	 */
	void BroadcastNotification(const FString& Method, const TSharedRef<FJsonObject>& Params);

private:
	// The bound port once Run() has bound one; until then the port to try first.
	int32 ServerPort;

	// The port policy's answer, resolved on the game thread before the server
	// thread starts: where the first port came from, whether a human pinned it,
	// and the ports the collision walk steps over.
	FMCPBridgePortChoice PortChoice;

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

	/** "Unknown method: x" with the registered names that look like it. */
	FString DescribeUnknownMethod(const FString& Method) const;

	// WebSocket connection handling
	void HandleWebSocketConnection(FMCPSocketHandle ClientSocketFD);
	void ProcessWebSocketMessages(FMCPSocketHandle ClientSocketFD, TArray<uint8>& InitialBytes);

	/**
	 * Validate the upgrade request and build the 101 response, or return an
	 * empty string having already told the client why it was refused.
	 * OutPipelinedBytes receives anything the client sent behind the request.
	 */
	FString PerformWebSocketHandshake(FMCPSocketHandle ClientSocketFD, TArray<uint8>& OutPipelinedBytes);

	/** Read the upgrade request through its blank line, not just one recv. */
	static bool ReadHttpRequest(FMCPSocketHandle SocketFD, FString& OutRequest, TArray<uint8>& OutPipelinedBytes);

	/** Case-insensitive, line-scoped header lookup. */
	static bool FindHeaderValue(const FString& Request, const FString& HeaderName, FString& OutValue);

	/** Refuse an upgrade with an HTTP status the caller can read. ExtraHeaders
	 *  is zero or more complete header lines, each ending in CRLF. */
	static void SendHttpError(FMCPSocketHandle SocketFD, int32 StatusCode, const FString& StatusText, const FString& Detail, const FString& ExtraHeaders = FString());

	/**
	 * Generate this session's token and publish it to the per-user token file
	 * for ServerPort. Runs once on the server thread before the first accept.
	 * On failure SessionToken stays empty and every upgrade is refused.
	 */
	void PublishSessionToken();

	/** Remove the token file if it still holds this session's token. */
	void RetractSessionToken();

	FString CreateWebSocketAcceptKey(const FString& ClientKey);

	/** Send a close frame carrying a status code and a human-readable reason. */
	static void SendCloseFrame(FMCPSocketHandle SocketFD, uint16 StatusCode, const FString& Reason);

	/** Write every byte or report failure. Partial sends are not success. */
	static bool SendAll(FMCPSocketHandle SocketFD, const uint8* Data, int32 NumBytes);

	// #821: connection threads capture `this` and outlive the accept loop, so
	// shutdown has to be able to find them, wake them, and wait for them. The
	// module frees this object the moment Shutdown returns.
	void RegisterConnection(FMCPSocketHandle Handle);

	/** Take the handle out of the live set. Does not drop the count. */
	void UnlistConnection(FMCPSocketHandle Handle);

	/** Drop the count. The last thing a connection thread does that touches
	 *  this object, and the point after which shutdown may unload the module. */
	void ReleaseConnectionSlot();

	/** Half-close every live client socket so a blocked recv returns now
	 *  rather than at the end of its next one-second select. */
	void WakeAllConnections();

	/** Block until no connection thread is running. False on timeout. */
	bool WaitForConnectionsToFinish(double TimeoutSeconds);

	FCriticalSection ConnectionsMutex;
	TSet<FMCPSocketHandle> LiveConnections;

	// The writer of every connection that finished its upgrade, which is who a
	// broadcast reaches. Guarded by ConnectionsMutex; a broadcast copies it and
	// sends outside the lock, and a retired writer refuses the send.
	TArray<TSharedRef<FMCPConnectionWriter, ESPMode::ThreadSafe>> LiveWriters;
	FThreadSafeCounter ActiveConnectionCount;

	// #821: identity for this server object, so a record on disk can say which
	// process wrote it and only that process can take it away. A pid alone is
	// not enough; pids are recycled. Held in its wire form, which is the only
	// form any record or payload uses.
	const FString InstanceId;
	FDateTime StartedAtUtc;

	// Required as "Authorization: Bearer <token>" on every upgrade. Written on
	// the server thread before the accept loop starts and read-only after, so
	// connection threads read it without a lock. Never logged, and never
	// written into the project's port lockfile or any other record.
	FString SessionToken;
	FString SessionTokenDir;
};
