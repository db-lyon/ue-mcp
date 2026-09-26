#pragma once

// WebSocket framing types and the size and time limits the bridge enforces on a
// connection, shared by the frame codec and the connection loop.

#include "CoreMinimal.h"

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

namespace MCPWebSocketLimits
{
	// #821: a JSON-RPC message can span many TCP reads and many WebSocket
	// frames, so the reader accumulates. These are the bounds on how much it
	// will hold for one connection before it refuses and says why, instead of
	// growing without limit on a corrupt or hostile length field.
	constexpr int64 kMaxWebSocketMessageBytes = 64ll * 1024ll * 1024ll; // 64 MiB
	constexpr int32 kRecvChunkBytes = 65536;

	// The largest client frame header RFC 6455 permits: two fixed bytes, eight
	// for the 64-bit extended payload length, four for the mask key that a
	// client must always send.
	constexpr int64 kMaxWebSocketFrameHeaderBytes = 2 + 8 + 4;

	// What the unparsed receive buffer may hold. The frame decoder needs the
	// whole frame in the buffer before it will hand over a payload, so a bound
	// of exactly kMaxWebSocketMessageBytes made a message of that size
	// impossible to receive in a single frame: the buffer tripped on the header
	// bytes first and closed with a reason naming the buffer rather than the
	// message, while MaxMessageBytes() went on advertising a ceiling nothing
	// could reach. The header allowance is what makes the advertised limit the
	// one that is actually enforced, and leaves the receive-buffer bound to
	// mean only what it says: a peer accumulating bytes it never completes a
	// frame with.
	constexpr int64 kMaxPendingReceiveBytes = kMaxWebSocketMessageBytes + kMaxWebSocketFrameHeaderBytes;

	// The upgrade request is read to its terminator rather than in one recv, so
	// it needs its own bounds: how long the whole read may take, and how large
	// the headers may grow before the bridge stops waiting for a blank line.
	constexpr double kUpgradeReadTimeoutSeconds = 5.0;
	constexpr int32 kMaxUpgradeHeaderBytes = 16 * 1024;
}
