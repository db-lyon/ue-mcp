// The Pixel Streaming 2 streamer behind pie(stream_*).
// See UE_MCP_BridgeStream.Build.cs for why this is a module of its own, and
// Public/HandlerStreamBackend.h in UE_MCP_Bridge for the contract with the
// stream handlers.
//
// The streamer is created here rather than through Pixel Streaming 2's own
// editor commands, because those connect to a URL fixed at startup (the
// -PixelStreamingURL switch or ws://127.0.0.1:8888) and start PIE streaming
// only from PostPIEStarted. Creating the streamer directly lets the caller name
// the signalling server at run time and start while PIE is already running.

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "HandlerStreamBackend.h"

#if WITH_UEMCP_PIXELSTREAMING2
#include "Editor.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/IConsoleManager.h"
#include "Interfaces/IMainFrameModule.h"
#include "IPixelStreaming2InputHandler.h"
#include "IPixelStreaming2Module.h"
#include "IPixelStreaming2Streamer.h"
#include "LevelEditor.h"
#include "LevelEditorViewport.h"
#include "Misc/CommandLine.h"
#include "PixelStreaming2InputEnums.h"
#include "SLevelViewport.h"
#include "Slate/SceneViewport.h"
#include "VideoProducerBackBufferComposited.h"
#include "VideoProducerLevelEditor.h"
#include "VideoProducerPIEViewport.h"
#include "Widgets/SViewport.h"
#include "Widgets/SWindow.h"
#endif

#if WITH_UEMCP_PIXELSTREAMING2
namespace UEMCPStream
{
	TSharedPtr<FSceneViewport> ActiveLevelSceneViewport()
	{
		FLevelEditorModule* LevelEditor = FModuleManager::GetModulePtr<FLevelEditorModule>(TEXT("LevelEditor"));
		const TSharedPtr<SLevelViewport> Viewport = LevelEditor ? LevelEditor->GetFirstActiveLevelViewport() : nullptr;
		if (!Viewport.IsValid()) return nullptr;
		const TSharedPtr<SEditorViewport> Widget = Viewport->GetLevelViewportClient().GetEditorViewportWidget();
		return Widget.IsValid() ? Widget->GetSceneViewport() : nullptr;
	}

	/** The first PIE viewport that renders a client or standalone world, as Pixel Streaming 2's own PIE streamer picks it. */
	UGameViewportClient* FindPIEViewport()
	{
		if (!GEditor) return nullptr;
		for (const FWorldContext& Context : GEditor->GetWorldContexts())
		{
			if (Context.WorldType != EWorldType::PIE || !Context.World() || !Context.GameViewport) continue;
			const ENetMode NetMode = Context.World()->GetNetMode();
			if (NetMode == NM_Client || NetMode == NM_Standalone) return Context.GameViewport;
		}
		return nullptr;
	}

	bool IsOffscreenEditor()
	{
		return FParse::Param(FCommandLine::Get(), TEXT("RenderOffScreen"));
	}

	/** Resize the editor's root window, the way Pixel Streaming 2 applies -EditorPixelStreamingRes. */
	bool ResizeRootWindow(const FIntPoint& Size)
	{
		if (!FModuleManager::Get().IsModuleLoaded(TEXT("MainFrame"))) return false;
		const TSharedPtr<SWindow> Root = IMainFrameModule::Get().GetParentWindow();
		if (!Root.IsValid() || !Root->GetNativeWindow().IsValid()) return false;
		Root->Resize(FVector2D(Size.X, Size.Y));
		FSlateApplication::Get().OnSizeChanged(Root->GetNativeWindow().ToSharedRef(), Size.X, Size.Y);
		FSystemResolution::RequestResolutionChange(Size.X, Size.Y, GSystemResolution.WindowMode);
		IConsoleManager::Get().CallAllConsoleVariableSinks();
		return true;
	}

	class FBackend : public IMCPStreamBackend
	{
	public:
		FBackend()
		{
			PostPIEHandle = FEditorDelegates::PostPIEStarted.AddRaw(this, &FBackend::OnPostPIEStarted);
			EndPIEHandle = FEditorDelegates::EndPIE.AddRaw(this, &FBackend::OnEndPIE);
		}

		virtual ~FBackend() override
		{
			FEditorDelegates::PostPIEStarted.Remove(PostPIEHandle);
			FEditorDelegates::EndPIE.Remove(EndPIEHandle);
			Stop();
		}

		virtual bool Start(const FMCPStreamConfig& InConfig, FString& OutError) override
		{
			IPixelStreaming2Module* Module = FModuleManager::GetModulePtr<IPixelStreaming2Module>(TEXT("PixelStreaming2"));
			if (!Module || !Module->IsReady())
			{
				OutError = TEXT("Pixel Streaming 2 is not ready. It starts only in an editor that renders, so a -nullrhi editor cannot stream: ")
					TEXT("launch one with editor(start_editor, window=\"offscreen\") instead. In a rendering editor it is ready once the renderer is up, so retry in a few seconds.");
				return false;
			}

			Stop();

			if (Module->FindStreamer(InConfig.StreamerId).IsValid())
			{
				OutError = FString::Printf(TEXT("streamerId '%s' is already used by another Pixel Streaming 2 streamer in this editor. Pass a different streamerId."), *InConfig.StreamerId);
				return false;
			}

			if (InConfig.Source == TEXT("level_viewport") && !ActiveLevelSceneViewport().IsValid())
			{
				OutError = TEXT("No level viewport is open to stream. Open one, or stream source=\"editor\" or source=\"pie\".");
				return false;
			}

			ApplySettings(InConfig);
			Config = InConfig;
			bActive = true;
			bResolutionApplied = ApplyResolution();

			// A PIE stream is connected only while PIE runs: it disconnects from signalling when PIE ends and
			// connects again when the next session starts, which is how a viewer tells waiting from live.
			const bool bConnectNow = Config.Source != TEXT("pie") || (GEditor && GEditor->PlayWorld);
			if (bConnectNow && !ConnectStreamer(OutError))
			{
				Stop();
				return false;
			}
			return true;
		}

		virtual bool Stop() override
		{
			if (!bActive) return false;
			bActive = false;
			RetireStreamer();
			if (bFixedLevelViewport)
			{
				if (const TSharedPtr<FSceneViewport> Viewport = ActiveLevelSceneViewport())
				{
					Viewport->SetFixedViewportSize(0, 0);
				}
				bFixedLevelViewport = false;
			}
			RestoreSettings();
			return true;
		}

		virtual const FMCPStreamConfig* GetActiveConfig() const override
		{
			return bActive ? &Config : nullptr;
		}

		virtual void WriteLiveState(const TSharedPtr<FJsonObject>& Out) const override
		{
			const bool bStreaming = bActive && Streamer.IsValid() && Streamer->IsStreaming();
			const bool bPlaying = GEditor && GEditor->PlayWorld;
			Out->SetBoolField(TEXT("streaming"), bStreaming);
			Out->SetBoolField(TEXT("armed"), bActive && Config.Source == TEXT("pie") && !bPlaying);
			Out->SetBoolField(TEXT("signallingConnected"), bActive && Streamer.IsValid() && Streamer->IsConnected());
			TArray<TSharedPtr<FJsonValue>> PeerIds;
			if (bActive && Streamer.IsValid())
			{
				for (const FString& Id : Streamer->GetConnectedPlayers())
				{
					PeerIds.Add(MakeShared<FJsonValueString>(Id));
				}
			}
			Out->SetNumberField(TEXT("connectedPeers"), PeerIds.Num());
			Out->SetArrayField(TEXT("peerIds"), PeerIds);
			if (Config.Resolution != FIntPoint::ZeroValue)
			{
				Out->SetBoolField(TEXT("resolutionApplied"), bActive && bResolutionApplied);
			}
		}

	private:
		/** Create the streamer for Config, attach its source and connect it to the signalling URL. */
		bool ConnectStreamer(FString& OutError)
		{
			IPixelStreaming2Module* Module = FModuleManager::GetModulePtr<IPixelStreaming2Module>(TEXT("PixelStreaming2"));
			if (!Module)
			{
				OutError = TEXT("The PixelStreaming2 module is not loaded.");
				return false;
			}
			TSharedPtr<IPixelStreaming2Streamer> NewStreamer = Module->CreateStreamer(Config.StreamerId);
			if (!NewStreamer.IsValid())
			{
				OutError = TEXT("Pixel Streaming 2 could not create a streamer; the editor log under LogPixelStreaming2 says why.");
				return false;
			}
			// Passed exactly as given: a signalling server may carry a secret in the path.
			NewStreamer->SetConnectionURL(Config.SignallingUrl);
			NewStreamer->SetStreamFPS(Config.Fps);
			if (!AttachSource(Config, NewStreamer, OutError))
			{
				Module->DeleteStreamer(NewStreamer);
				return false;
			}
			Streamer = NewStreamer;
			if (Config.Source == TEXT("pie"))
			{
				TargetPIEInput();
			}
			Streamer->StartStreaming();
			return true;
		}

		/** Disconnect the streamer from signalling and drop it. */
		void RetireStreamer()
		{
			if (!Streamer.IsValid()) return;
			Streamer->StopStreaming();
			if (IPixelStreaming2Module* Module = FModuleManager::GetModulePtr<IPixelStreaming2Module>(TEXT("PixelStreaming2")))
			{
				Module->DeleteStreamer(Streamer);
			}
			// The disconnect finishes on later ticks, so the streamer is kept alive until the next one retires.
			Retired = Streamer;
			Streamer.Reset();
		}

		bool AttachSource(const FMCPStreamConfig& InConfig, const TSharedPtr<IPixelStreaming2Streamer>& Target, FString& OutError)
		{
			const TSharedPtr<IPixelStreaming2InputHandler> Input = Target->GetInputHandler().Pin();
			if (InConfig.Source == TEXT("pie"))
			{
				// Captures whichever PIE viewport is running and nothing while none is, so it can be attached before PIE starts.
				Target->SetVideoProducer(UE::PixelStreaming2::FVideoProducerPIEViewport::Create());
				return true;
			}
			if (InConfig.Source == TEXT("level_viewport"))
			{
				const TSharedPtr<FSceneViewport> Viewport = ActiveLevelSceneViewport();
				if (!Viewport.IsValid())
				{
					OutError = TEXT("No level viewport is open to stream. Open one, or stream source=\"editor\" or source=\"pie\".");
					return false;
				}
				if (Input.IsValid())
				{
					Input->SetTargetViewport(Viewport->GetViewportWidget());
					Input->SetTargetWindow(Viewport->FindWindow());
					Input->SetInputType(EPixelStreaming2InputType::RouteToWindow);
				}
				Target->SetVideoProducer(UE::EditorPixelStreaming2::FVideoProducerLevelEditor::Create());
				return true;
			}
			const TSharedPtr<UE::EditorPixelStreaming2::FVideoProducerBackBufferComposited> Producer =
				UE::EditorPixelStreaming2::FVideoProducerBackBufferComposited::Create();
			if (Input.IsValid())
			{
				Input->SetTargetViewport(nullptr);
				Input->SetTargetWindow(nullptr);
				Input->SetInputType(EPixelStreaming2InputType::RouteToWindow);
				Producer->OnFrameSizeChanged.AddSP(Input.ToSharedRef(), &IPixelStreaming2InputHandler::SetTargetScreenRect);
			}
			Target->SetVideoProducer(Producer);
			return true;
		}

		bool ApplyResolution()
		{
			if (Config.Resolution == FIntPoint::ZeroValue) return false;
			bool bApplied = false;
			// Only an offscreen editor's window is resized: a visible one belongs to the person at the screen.
			if (IsOffscreenEditor())
			{
				bApplied = ResizeRootWindow(Config.Resolution);
			}
			if (Config.Source == TEXT("level_viewport"))
			{
				if (const TSharedPtr<FSceneViewport> Viewport = ActiveLevelSceneViewport())
				{
					Viewport->SetFixedViewportSize(Config.Resolution.X, Config.Resolution.Y);
					bFixedLevelViewport = true;
					bApplied = true;
				}
			}
			return bApplied;
		}

		/** Route the stream's input to the running PIE viewport. */
		void TargetPIEInput()
		{
			if (!Streamer.IsValid()) return;
			const TSharedPtr<IPixelStreaming2InputHandler> Input = Streamer->GetInputHandler().Pin();
			UGameViewportClient* Viewport = FindPIEViewport();
			if (!Input.IsValid() || !Viewport) return;
			Input->SetTargetViewport(Viewport->GetGameViewportWidget());
			Input->SetTargetWindow(Viewport->GetWindow());
			Input->SetInputType(EPixelStreaming2InputType::RouteToWindow);
			Streamer->ForceKeyFrame();
		}

		void OnPostPIEStarted(bool /*bIsSimulating*/)
		{
			if (!bActive || Config.Source != TEXT("pie")) return;
			if (Streamer.IsValid())
			{
				TargetPIEInput();
				return;
			}
			FString Error;
			if (!ConnectStreamer(Error))
			{
				UE_LOG(LogTemp, Warning, TEXT("[UE-MCP] pie(stream_start) could not connect the PIE stream: %s"), *Error);
			}
		}

		void OnEndPIE(bool /*bIsSimulating*/)
		{
			if (bActive && Config.Source == TEXT("pie"))
			{
				RetireStreamer();
			}
		}

		void SetCVar(const TCHAR* Name, const FString& Value)
		{
			IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Name);
			if (!Var) return;
			if (!SavedCVars.Contains(Name))
			{
				SavedCVars.Add(Name, Var->GetString());
			}
			Var->Set(*Value, ECVF_SetByCode);
		}

		static int32 ReadIntCVar(const TCHAR* Name, int32 Default)
		{
			const IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Name);
			return Var ? Var->GetInt() : Default;
		}

		void ApplySettings(const FMCPStreamConfig& InConfig)
		{
			const int32 MaxBps = InConfig.MaxBitrateKbps * 1000;
			SetCVar(TEXT("PixelStreaming2.Encoder.Codec"), InConfig.Codec);
			SetCVar(TEXT("PixelStreaming2.WebRTC.Fps"), FString::FromInt(InConfig.Fps));
			SetCVar(TEXT("PixelStreaming2.WebRTC.MinBitrate"), FString::FromInt(FMath::Min(ReadIntCVar(TEXT("PixelStreaming2.WebRTC.MinBitrate"), 100000), MaxBps)));
			SetCVar(TEXT("PixelStreaming2.WebRTC.StartBitrate"), FString::FromInt(FMath::Min(ReadIntCVar(TEXT("PixelStreaming2.WebRTC.StartBitrate"), 1000000), MaxBps)));
			SetCVar(TEXT("PixelStreaming2.WebRTC.MaxBitrate"), FString::FromInt(MaxBps));
			// Pixel Streaming 2's own PIE streamer would start beside this one on every PIE start, with a local signalling server.
			SetCVar(TEXT("PixelStreaming2.Editor.AutoStreamPIE"), TEXT("0"));
		}

		void RestoreSettings()
		{
			for (const TPair<FString, FString>& Saved : SavedCVars)
			{
				if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(*Saved.Key))
				{
					Var->Set(*Saved.Value, ECVF_SetByCode);
				}
			}
			SavedCVars.Reset();
		}

		FMCPStreamConfig Config;
		TSharedPtr<IPixelStreaming2Streamer> Streamer;
		TSharedPtr<IPixelStreaming2Streamer> Retired;
		TMap<FString, FString> SavedCVars;
		FDelegateHandle PostPIEHandle;
		FDelegateHandle EndPIEHandle;
		bool bActive = false;
		bool bResolutionApplied = false;
		bool bFixedLevelViewport = false;
	};
}
#endif // WITH_UEMCP_PIXELSTREAMING2

class FUE_MCP_BridgeStreamModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
#if WITH_UEMCP_PIXELSTREAMING2
		Backend = MakeUnique<UEMCPStream::FBackend>();
		UEMCP::SetStreamBackend(Backend.Get());
#endif
	}

	virtual void ShutdownModule() override
	{
		if (Backend.IsValid() && UEMCP::GetStreamBackend() == Backend.Get())
		{
			UEMCP::SetStreamBackend(nullptr);
		}
		Backend.Reset();
	}

private:
	TUniquePtr<IMCPStreamBackend> Backend;
};

IMPLEMENT_MODULE(FUE_MCP_BridgeStreamModule, UE_MCP_BridgeStream)
