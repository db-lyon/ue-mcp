#pragma once

// The platform socket calls the bridge makes, spelled once, so the Windows and
// POSIX paths of the server and the state-file port probe cannot drift apart.

#include "CoreMinimal.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include "Windows/HideWindowsPlatformTypes.h"
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/select.h>
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

namespace MCPSocket
{
	/** Start the socket layer. Refcounted per process on Windows, so every caller
	 *  pairs it with Cleanup; nothing to do elsewhere. */
	inline bool Startup()
	{
#if PLATFORM_WINDOWS
		WSADATA WsaData;
		return WSAStartup(MAKEWORD(2, 2), &WsaData) == 0;
#else
		return true;
#endif
	}

	inline void Cleanup()
	{
#if PLATFORM_WINDOWS
		WSACleanup();
#endif
	}

	inline void Close(FMCPSocketHandle Handle)
	{
#if PLATFORM_WINDOWS
		closesocket(Handle);
#else
		close(Handle);
#endif
	}

	/** Half-close both directions, so a blocked recv on the handle returns. */
	inline void ShutdownBoth(FMCPSocketHandle Handle)
	{
#if PLATFORM_WINDOWS
		shutdown(Handle, SD_BOTH);
#else
		shutdown(Handle, SHUT_RDWR);
#endif
	}

	inline void SetNonBlocking(FMCPSocketHandle Handle)
	{
#if PLATFORM_WINDOWS
		u_long NonBlocking = 1;
		ioctlsocket(Handle, FIONBIO, &NonBlocking);
#else
		const int32 Flags = fcntl(Handle, F_GETFL, 0);
		fcntl(Handle, F_SETFL, Flags | O_NONBLOCK);
#endif
	}

	/** True when a non-blocking connect is still under way rather than failed. */
	inline bool ConnectInProgress()
	{
#if PLATFORM_WINDOWS
		return WSAGetLastError() == WSAEWOULDBLOCK;
#else
		return errno == EINPROGRESS;
#endif
	}
}
