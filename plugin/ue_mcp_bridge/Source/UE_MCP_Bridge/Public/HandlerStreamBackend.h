#pragma once

// The seam between the pie(stream_*) handlers and the Pixel Streaming 2 plugin.
//
// Pixel Streaming 2 ships with the engine but disabled, and a project can
// leave it disabled, so this module never links it: a module that imported its
// DLLs would fail to load in every project without it. The streaming code
// lives in UE_MCP_BridgeStream, which the descriptor declares with
// LoadingPhase "None" so nothing loads it at startup. The stream handlers load
// it on first use, only after the plugin manager says PixelStreaming2 is
// enabled, and it registers itself here.
//
// The handlers stay in this module: they declare the specs, read and validate
// every parameter (the read tracking is per module), and shape the response.
// The backend only drives the streamer.

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

/** A validated stream request. */
struct FMCPStreamConfig
{
	/** ws:// or wss:// URL of the signalling server the streamer connects to. */
	FString SignallingUrl;
	FString StreamerId;
	/** pie | level_viewport | editor */
	FString Source;
	/** H264 | VP8 | VP9 | AV1 */
	FString Codec;
	int32 Fps = 30;
	int32 MaxBitrateKbps = 4000;
	/** Zero when the caller left the resolution alone. */
	FIntPoint Resolution = FIntPoint::ZeroValue;
};

/** The Pixel Streaming 2 streamer, implemented by the UE_MCP_BridgeStream module. */
class IMCPStreamBackend
{
public:
	virtual ~IMCPStreamBackend() = default;

	/** Start streaming with Config, replacing a stream already running. False with OutError when it cannot. */
	virtual bool Start(const FMCPStreamConfig& Config, FString& OutError) = 0;

	/** Stop the stream and disarm it. False when nothing was streaming or armed. */
	virtual bool Stop() = 0;

	/** The running stream's config, or null when nothing is streaming. */
	virtual const FMCPStreamConfig* GetActiveConfig() const = 0;

	/**
	 * Live fields of the running stream: streaming, armed, signallingConnected,
	 * connectedPeers, peerIds and resolutionApplied.
	 */
	virtual void WriteLiveState(const TSharedPtr<FJsonObject>& Out) const = 0;
};

namespace UEMCP
{
	/** Called by UE_MCP_BridgeStream on startup with its backend, and with null on shutdown. */
	UE_MCP_BRIDGE_API void SetStreamBackend(IMCPStreamBackend* Backend);

	/** The registered backend, or null when the module has not been loaded. */
	UE_MCP_BRIDGE_API IMCPStreamBackend* GetStreamBackend();
}
