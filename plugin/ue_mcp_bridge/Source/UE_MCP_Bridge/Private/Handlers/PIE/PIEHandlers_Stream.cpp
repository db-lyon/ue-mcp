// pie(stream_start | stream_stop | stream_status): stream PIE, the level viewport
// or the whole editor over Pixel Streaming 2 to a signalling server the caller
// names. See Public/HandlerStreamBackend.h for how the work is split with the
// UE_MCP_BridgeStream module.

#include "PIEHandlers.h"
#include "HandlerUtils.h"
#include "HandlerStreamBackend.h"
#include "Interfaces/IPluginManager.h"
#include "Modules/ModuleManager.h"

namespace UEMCP
{
	static IMCPStreamBackend*& MCPStreamBackendSlot()
	{
		static IMCPStreamBackend* Slot = nullptr;
		return Slot;
	}

	void SetStreamBackend(IMCPStreamBackend* Backend)
	{
		MCPStreamBackendSlot() = Backend;
	}

	IMCPStreamBackend* GetStreamBackend()
	{
		return MCPStreamBackendSlot();
	}
}

namespace
{
	const TCHAR* const MCPStreamPluginName = TEXT("PixelStreaming2");
	const TCHAR* const MCPStreamModuleName = TEXT("UE_MCP_BridgeStream");

	struct FMCPStreamPluginState
	{
		bool bInstalled = false;
		bool bEnabled = false;
	};

	FMCPStreamPluginState MCPStreamPluginState()
	{
		FMCPStreamPluginState State;
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(MCPStreamPluginName);
		State.bInstalled = Plugin.IsValid();
		State.bEnabled = Plugin.IsValid() && Plugin->IsEnabled();
		return State;
	}

	/** The backend, loading UE_MCP_BridgeStream on first use once the plugin is known to be enabled. Null with OutError otherwise. */
	IMCPStreamBackend* MCPStreamRequireBackend(FString& OutError)
	{
		if (IMCPStreamBackend* Backend = UEMCP::GetStreamBackend())
		{
			return Backend;
		}
		const FMCPStreamPluginState Plugin = MCPStreamPluginState();
		if (!Plugin.bInstalled)
		{
			OutError = TEXT("Pixel Streaming 2 (PixelStreaming2) is not installed in this engine, so the editor cannot stream. It ships with UE 5.5 and later; on Win64 it is part of every launcher install.");
			return nullptr;
		}
		if (!Plugin.bEnabled)
		{
			OutError = TEXT("Pixel Streaming 2 (PixelStreaming2) is not enabled in this project. ") + MCPEnablePluginStep(MCPStreamPluginName);
			return nullptr;
		}
		FModuleManager::Get().LoadModule(MCPStreamModuleName);
		if (IMCPStreamBackend* Backend = UEMCP::GetStreamBackend())
		{
			return Backend;
		}
		OutError = TEXT("Pixel Streaming 2 is enabled but the bridge's UE_MCP_BridgeStream module did not load or was built without it, ")
			TEXT("which happens on engines older than 5.7. On 5.7 or later, rebuild the UE_MCP_Bridge plugin; the editor log names the load failure.");
		return nullptr;
	}

	bool MCPStreamParseResolution(const FString& Text, FIntPoint& Out)
	{
		FString W, H;
		if (!Text.TrimStartAndEnd().Split(TEXT("x"), &W, &H, ESearchCase::IgnoreCase)) return false;
		W.TrimStartAndEndInline();
		H.TrimStartAndEndInline();
		if (!W.IsNumeric() || !H.IsNumeric() || W.Contains(TEXT(".")) || H.Contains(TEXT("."))) return false;
		Out = FIntPoint(FCString::Atoi(*W), FCString::Atoi(*H));
		return Out.X >= 64 && Out.Y >= 64 && Out.X <= 7680 && Out.Y <= 4320;
	}

	FString MCPStreamResolutionText(const FIntPoint& Resolution)
	{
		return Resolution == FIntPoint::ZeroValue ? FString() : FString::Printf(TEXT("%dx%d"), Resolution.X, Resolution.Y);
	}

	void MCPStreamWriteConfig(const TSharedPtr<FJsonObject>& Out, const FMCPStreamConfig& Config)
	{
		Out->SetStringField(TEXT("streamerId"), Config.StreamerId);
		Out->SetStringField(TEXT("signallingUrl"), Config.SignallingUrl);
		Out->SetStringField(TEXT("source"), Config.Source);
		Out->SetStringField(TEXT("codec"), Config.Codec);
		Out->SetNumberField(TEXT("fps"), Config.Fps);
		Out->SetNumberField(TEXT("maxBitrateKbps"), Config.MaxBitrateKbps);
		const FString Resolution = MCPStreamResolutionText(Config.Resolution);
		if (Resolution.IsEmpty()) Out->SetField(TEXT("resolution"), MakeShared<FJsonValueNull>());
		else Out->SetStringField(TEXT("resolution"), Resolution);
	}

	TSharedPtr<FJsonObject> MCPStreamStatusObject()
	{
		const FMCPStreamPluginState Plugin = MCPStreamPluginState();
		IMCPStreamBackend* Backend = UEMCP::GetStreamBackend();
		if (!Backend && Plugin.bEnabled)
		{
			// Loads only with the plugin enabled, so this reports whether the bridge was built with streaming.
			FModuleManager::Get().LoadModule(MCPStreamModuleName);
			Backend = UEMCP::GetStreamBackend();
		}
		auto Result = MCPSuccess();
		Result->SetBoolField(TEXT("available"), Plugin.bEnabled && Backend != nullptr);
		Result->SetBoolField(TEXT("pluginInstalled"), Plugin.bInstalled);
		Result->SetBoolField(TEXT("pluginEnabled"), Plugin.bEnabled);
		const FMCPStreamConfig* Config = Backend ? Backend->GetActiveConfig() : nullptr;
		if (Config)
		{
			MCPStreamWriteConfig(Result, *Config);
			Backend->WriteLiveState(Result);
		}
		else
		{
			Result->SetBoolField(TEXT("streaming"), false);
			Result->SetBoolField(TEXT("armed"), false);
			for (const TCHAR* Key : { TEXT("streamerId"), TEXT("signallingUrl"), TEXT("source"), TEXT("codec"), TEXT("fps"), TEXT("resolution") })
			{
				Result->SetField(Key, MakeShared<FJsonValueNull>());
			}
			Result->SetNumberField(TEXT("connectedPeers"), 0);
		}
		if (!Plugin.bEnabled)
		{
			Result->SetStringField(TEXT("wayForward"), Plugin.bInstalled
				? MCPEnablePluginStep(MCPStreamPluginName)
				: FString(TEXT("Pixel Streaming 2 is not installed in this engine.")));
		}
		return Result;
	}
}

TSharedPtr<FJsonValue> FPIEHandlers::PieStreamStart(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	FMCPStreamConfig Config;
	if (auto Err = RequireString(Params, TEXT("signallingUrl"), Config.SignallingUrl)) return Err;
	Config.StreamerId = OptionalString(Params, TEXT("streamerId"), TEXT("ue-mcp"));
	Config.Source = OptionalString(Params, TEXT("source"), TEXT("pie")).ToLower();
	Config.Codec = OptionalString(Params, TEXT("codec"), TEXT("H264")).ToUpper();
	const FString ResolutionText = OptionalString(Params, TEXT("resolution"));
	const double Fps = OptionalNumber(Params, TEXT("fps"), 30);
	const double Bitrate = OptionalNumber(Params, TEXT("maxBitrateKbps"), 4000);

	// The URL reaches the streamer byte for byte: a signalling server may carry a secret in its path.
	if (!Config.SignallingUrl.StartsWith(TEXT("ws://"), ESearchCase::IgnoreCase) && !Config.SignallingUrl.StartsWith(TEXT("wss://"), ESearchCase::IgnoreCase))
		return MCPError(FString::Printf(TEXT("signallingUrl must be a ws:// or wss:// URL, got '%s'"), *Config.SignallingUrl));
	Config.StreamerId.TrimStartAndEndInline();
	if (Config.StreamerId.IsEmpty()) return MCPError(TEXT("streamerId must not be empty"));
	if (Config.Source != TEXT("pie") && Config.Source != TEXT("level_viewport") && Config.Source != TEXT("editor"))
		return MCPError(FString::Printf(TEXT("source must be pie, level_viewport or editor, got '%s'"), *Config.Source));
	if (Config.Codec != TEXT("H264") && Config.Codec != TEXT("VP8") && Config.Codec != TEXT("VP9") && Config.Codec != TEXT("AV1"))
		return MCPError(FString::Printf(TEXT("codec must be H264, VP8, VP9 or AV1, got '%s'"), *Config.Codec));
	if (!FMath::IsFinite(Fps) || Fps < 1 || Fps > 240 || Fps != FMath::FloorToDouble(Fps))
		return MCPError(TEXT("fps must be a whole number from 1 to 240"));
	if (!FMath::IsFinite(Bitrate) || Bitrate < 100 || Bitrate > 100000 || Bitrate != FMath::FloorToDouble(Bitrate))
		return MCPError(TEXT("maxBitrateKbps must be a whole number from 100 to 100000"));
	if (!ResolutionText.IsEmpty() && !MCPStreamParseResolution(ResolutionText, Config.Resolution))
		return MCPError(FString::Printf(TEXT("resolution must be WIDTHxHEIGHT between 64x64 and 7680x4320, such as 1280x720; got '%s'"), *ResolutionText));
	Config.Fps = static_cast<int32>(Fps);
	Config.MaxBitrateKbps = static_cast<int32>(Bitrate);

	FString Error;
	IMCPStreamBackend* Backend = MCPStreamRequireBackend(Error);
	if (!Backend) return MCPError(Error);

	const FMCPStreamConfig* Previous = Backend->GetActiveConfig();
	const bool bReplaced = Previous != nullptr;
	if (!Backend->Start(Config, Error)) return MCPError(Error);

	auto Result = MCPSuccess();
	MCPStreamWriteConfig(Result, Config);
	Backend->WriteLiveState(Result);
	Result->SetBoolField(TEXT("replaced"), bReplaced);
	if (bReplaced) MCPSetUpdated(Result);
	else MCPSetCreated(Result);
	if (Result->GetBoolField(TEXT("armed")))
	{
		Result->SetStringField(TEXT("note"), TEXT("No PIE session is running, so the stream is armed: the streamer connects to signalling when PIE starts and disconnects when it ends, for every PIE session until stream_stop."));
	}
	MCPSetRollback(Result, TEXT("pie_stream_stop"), MakeShared<FJsonObject>());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPIEHandlers::PieStreamStop(const TSharedPtr<FJsonObject>& /*Params*/)
{
	MCP_CHECK_GAME_THREAD();
	IMCPStreamBackend* Backend = UEMCP::GetStreamBackend();
	const FMCPStreamConfig* Active = Backend ? Backend->GetActiveConfig() : nullptr;
	const TOptional<FMCPStreamConfig> Stopping = Active ? TOptional<FMCPStreamConfig>(*Active) : TOptional<FMCPStreamConfig>();
	const bool bStopped = Backend && Backend->Stop();
	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("streaming"), false);
	Result->SetBoolField(TEXT("armed"), false);
	Result->SetBoolField(TEXT("stopped"), bStopped);
	Result->SetBoolField(TEXT("alreadyStopped"), !bStopped);
	if (bStopped && Stopping.IsSet())
	{
		// Starting the same stream again is the inverse.
		auto Payload = MakeShared<FJsonObject>();
		MCPStreamWriteConfig(Payload, Stopping.GetValue());
		Payload->RemoveField(TEXT("resolution"));
		if (Stopping->Resolution != FIntPoint::ZeroValue)
		{
			Payload->SetStringField(TEXT("resolution"), MCPStreamResolutionText(Stopping->Resolution));
		}
		MCPSetRollback(Result, TEXT("pie_stream_start"), Payload);
	}
	else
	{
		MCPSetNoRollback(Result, TEXT("Nothing was streaming, so nothing changed"));
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPIEHandlers::PieStreamStatus(const TSharedPtr<FJsonObject>& /*Params*/)
{
	MCP_CHECK_GAME_THREAD();
	return MCPResult(MCPStreamStatusObject());
}
