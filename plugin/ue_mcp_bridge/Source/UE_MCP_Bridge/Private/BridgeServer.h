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

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <winsock2.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

// One name for the platform socket handle so the connection code is written
// once instead of twice behind #if blocks that can silently drift apart.
#if PLATFORM_WINDOWS
typedef SOCKET FMCPSocketHandle;
#define MCP_INVALID_SOCKET INVALID_SOCKET
#else
typedef int32 FMCPSocketHandle;
#define MCP_INVALID_SOCKET (-1)
#endif

/** WebSocket opcodes the bridge understands (RFC 6455 section 5.2). */
enum class EMCPWebSocketOpcode : uint8
{
	Continuation = 0x0,
	Text         = 0x1,
	Binary       = 0x2,
	Close        = 0x8,
	Ping         = 0x9,
	Pong         = 0xA,
};

/** Outcome of trying to decode one frame off the front of a receive buffer. */
enum class EMCPFrameDecode : uint8
{
	/** The buffer holds a partial frame. Read more and try again. */
	NeedMoreData,
	/** OutFrame is filled in and the frame's bytes were consumed. */
	Decoded,
	/** The stream is no longer trustworthy. Close the connection. */
	ProtocolError,
};

/** One decoded WebSocket frame. A message may span several of these. */
struct FMCPWebSocketFrame
{
	EMCPWebSocketOpcode Opcode = EMCPWebSocketOpcode::Text;
	bool bFinal = true;
	TArray<uint8> Payload;
};

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
 * The port the bridge will try to bind, and where that number came from.
 *
 * The origin is carried alongside the number because the collision walk can
 * move it. A user who pinned a port needs the log to say the pin did not take
 * and what the bridge landed on, which is not something a bare int can say.
 */
struct FMCPBridgePortChoice
{
	/** The port to bind first. The walk in Run() starts here. */
	int32 Port = 0;

	/** Human-readable origin, for the one log line a user greps for. */
	FString Source = TEXT("unknown");

	/**
	 * True when a human asked for this exact number (command line, environment,
	 * or bridge.port in a config file). Losing a pinned port to a collision is
	 * a warning; losing a derived one is routine.
	 */
	bool bPinned = false;
};

/**
 * Releases the connection record the accept loop made before it spawned this
 * thread.
 *
 * The accept loop registers, so a shutdown racing an accept can never conclude
 * that nothing is running. This object is declared after the socket guard and
 * therefore destroyed before it, so the handle leaves the live set while it is
 * still open: half-closing a handle number the operating system has already
 * handed to someone else is worse than not waking it at all.
 */
class FMCPConnectionRelease
{
public:
	FMCPConnectionRelease(FMCPBridgeServer& InServer, FMCPSocketHandle InHandle);
	~FMCPConnectionRelease();

	FMCPConnectionRelease(const FMCPConnectionRelease&) = delete;
	FMCPConnectionRelease& operator=(const FMCPConnectionRelease&) = delete;

private:
	FMCPBridgeServer& Server;
	FMCPSocketHandle Handle;
};

class FMCPBridgeServer : public FRunnable
{
	friend class FMCPConnectionRelease;

public:
	FMCPBridgeServer(int32 Port = 9877, const FString& InPortSource = TEXT("default"), bool bInPortPinned = false);
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
	// #821: the record names the instance that wrote it, is published by
	// rename so a reader never sees a half-written file, and is only ever
	// removed by the instance whose id it carries.
	static FString GetPortLockfilePath();
	static FString GetBridgeErrorFilePath();
	void WritePortLockfile(int32 PortValue);
	void DeletePortLockfileIfOwned();

	/** Leave an on-disk trace for "editor alive, bridge dead". Written to its
	 *  own path so a failed start can never overwrite a live editor's record. */
	void WriteBindFailureRecord(int32 FirstPort, int32 LastPort, int32 ErrorCode);

	// #817: this process's own record under Saved/UE_MCP_Bridge/instances/.
	//
	// port.json names one bridge for a whole project directory, which is the
	// right answer for one editor and no answer at all for two. A per-pid file
	// has exactly one writer by construction, so a second editor of the same
	// project describes itself instead of overwriting the first, and a record
	// left by a crash is provably stale instead of merely suspicious.
	void WriteInstanceRecord(const FString& State, int32 PortValue);
	void DeleteOwnInstanceRecord();

	/** Remove records whose process is gone. Runs once, at startup, on the
	 *  server thread; the sweep never touches this instance's own record. */
	void ReapStaleInstanceRecords();

	// Deterministic per-worktree base port. Derived from a hash of the project
	// root path so every checkout gets a stable, launch-order-independent port
	// that the Node client computes identically (see src/port.ts). Keep the two
	// implementations in lockstep.
	static int32 DeriveProjectPort(const FString& ProjectRootDir);

	// Resolve the base port to bind: -MCPPort= command line > UE_MCP_PORT env >
	// the port the client published in Saved/UE_MCP_Bridge/requested.json >
	// `ue-mcp.bridge.port` from the project's config files > deterministic
	// derived port. That order is the client's (see src/bridge.ts); both sides
	// have to walk it identically or a pinned project ends up with a client
	// aimed at one port and an editor listening on another (#819). The probe
	// loop in Run() walks upward from here on collision, and the actual bound
	// port is published to the lockfile.
	//
	// requested.json (#817) sits above the config read because it is the same
	// answer computed from more inputs: the client merges four config layers
	// and the environment, none of which an editor launched from Explorer can
	// see. It exists only while a pin exists, so an unpinned install resolves
	// exactly as it did before that channel was added.
	static FMCPBridgePortChoice ResolveConfiguredPort();

	// Get handler registry
	FMCPHandlerRegistry& GetHandlerRegistry() { return HandlerRegistry; }

	// Get game thread executor (to set editor ready)
	FMCPGameThreadExecutor& GetGameThreadExecutor() { return GameThreadExecutor; }

	// Process a JSON-RPC message
	FString ProcessMessage(const FString& Message);

	/**
	 * #821: what this bridge is and what it can do, answered without the game
	 * thread. Protocol version, handler ABI version, the binary's compile
	 * timestamp, this instance's identity, and the action list the running
	 * binary actually registered.
	 */
	TSharedPtr<FJsonObject> BuildCapabilitiesPayload();

	// ── Framing ──────────────────────────────────────────────────────────────
	//
	// Public because they are pure functions over a byte buffer with no server
	// state behind them, and because the automation spec in
	// Private/Tests/BridgeProtocolTests.cpp is the only thing that can prove
	// them. The alternative was befriending the test class, which encodes a
	// test's name into the header and breaks whenever the test is renamed.

	/**
	 * Decode at most one frame from the front of Buffer.
	 *
	 * On Decoded the frame's bytes (header, mask and payload) are consumed from
	 * Buffer and whatever follows is left in place, so a read that delivered two
	 * pipelined requests yields both instead of dropping the second. On
	 * NeedMoreData nothing is consumed and the caller reads again.
	 *
	 * On ProtocolError OutCloseCode carries the status the peer should be closed
	 * with: 1002 for framing the bridge cannot follow, 1009 when the frame is
	 * merely too large. The caller sends that code, so the client can tell a
	 * size refusal from a broken stream.
	 */
	static EMCPFrameDecode DecodeWebSocketFrame(TArray<uint8>& Buffer, FMCPWebSocketFrame& OutFrame, FString& OutError, uint16& OutCloseCode);

	/** Frame a text message for the wire. Server to client, so never masked. */
	static TArray<uint8> CreateWebSocketFrame(const FString& Message);

	/** Frame a control opcode (close, ping, pong) with its payload. */
	static TArray<uint8> CreateControlFrame(EMCPWebSocketOpcode Opcode, const TArray<uint8>& Payload);

	/** The largest assembled message the bridge will hold for one connection. */
	static int64 MaxMessageBytes();

private:
	// Server port
	int32 ServerPort;

	// Where ServerPort came from before the collision walk touched it, and
	// whether it was pinned by a human. Only the log lines read these.
	FString PortSource;
	bool bPortPinned;

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

	/** Refuse an upgrade with an HTTP status the caller can read. */
	static void SendHttpError(FMCPSocketHandle SocketFD, int32 StatusCode, const FString& StatusText, const FString& Detail);

	FString CreateWebSocketAcceptKey(const FString& ClientKey);

	/** Send a close frame carrying a status code and a human-readable reason. */
	static void SendCloseFrame(FMCPSocketHandle SocketFD, uint16 StatusCode, const FString& Reason);

	/** Write every byte or report failure. Partial sends are not success. */
	static bool SendAll(FMCPSocketHandle SocketFD, const uint8* Data, int32 NumBytes);

	// #821: connection threads capture `this` and outlive the accept loop, so
	// shutdown has to be able to find them, wake them, and wait for them. The
	// module frees this object the moment Shutdown returns.
	void RegisterConnection(FMCPSocketHandle Handle);
	void UnregisterConnection(FMCPSocketHandle Handle);

	/** Half-close every live client socket so a blocked recv returns now
	 *  rather than at the end of its next one-second select. */
	void WakeAllConnections();

	/** Block until no connection thread is running. False on timeout. */
	bool WaitForConnectionsToFinish(double TimeoutSeconds);

	FCriticalSection ConnectionsMutex;
	TSet<FMCPSocketHandle> LiveConnections;
	FThreadSafeCounter ActiveConnectionCount;

	// #821: identity for this server object, so a record on disk can say which
	// process wrote it and only that process can take it away. A pid alone is
	// not enough; pids are recycled.
	FGuid InstanceId;
	FDateTime StartedAtUtc;
};
