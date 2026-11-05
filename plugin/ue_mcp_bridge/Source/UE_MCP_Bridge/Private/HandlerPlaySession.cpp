#include "HandlerPlaySession.h"

#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Framework/Application/SlateApplication.h"
#include "IAssetViewport.h"
#include "LevelEditor.h"
#include "Modules/ModuleManager.h"
#include "PlayInEditorDataTypes.h"
#include "Settings/LevelEditorPlaySettings.h"

namespace MCPPlaySession
{
	const TCHAR* PlayModeName(EMCPPlayMode Mode)
	{
		switch (Mode)
		{
		case EMCPPlayMode::NewWindow:  return TEXT("new_window");
		case EMCPPlayMode::Standalone: return TEXT("standalone");
		default:                       return TEXT("viewport");
		}
	}

	bool ParsePlayMode(const FString& Name, EMCPPlayMode& OutMode)
	{
		const FString N = Name.TrimStartAndEnd().ToLower();
		if (N == TEXT("viewport"))   { OutMode = EMCPPlayMode::Viewport;   return true; }
		if (N == TEXT("new_window")) { OutMode = EMCPPlayMode::NewWindow;  return true; }
		if (N == TEXT("standalone")) { OutMode = EMCPPlayMode::Standalone; return true; }
		return false;
	}

	EMCPPlayMode EditorPlayMode()
	{
		const ULevelEditorPlaySettings* Settings = GetDefault<ULevelEditorPlaySettings>();
		if (!Settings) return EMCPPlayMode::Viewport;
		// Compared as integers so the deprecated preview enumerators are never named.
		const int32 Type = static_cast<int32>(Settings->LastExecutedPlayModeType.GetValue());
		if (Type == static_cast<int32>(PlayMode_InEditorFloating)) return EMCPPlayMode::NewWindow;
		if (Type == static_cast<int32>(PlayMode_InNewProcess)
			|| Type == static_cast<int32>(PlayMode_InMobilePreview)
			|| Type == static_cast<int32>(PlayMode_InTargetedMobilePreview))
		{
			return EMCPPlayMode::Standalone;
		}
		// In viewport, simulate, VR and quick launch all start from the level viewport.
		return EMCPPlayMode::Viewport;
	}

	void SetEditorPlayMode(EMCPPlayMode Mode)
	{
		ULevelEditorPlaySettings* Settings = GetMutableDefault<ULevelEditorPlaySettings>();
		if (!Settings) return;
		EPlayModeType Type = PlayMode_InViewPort;
		if (Mode == EMCPPlayMode::NewWindow) Type = PlayMode_InEditorFloating;
		else if (Mode == EMCPPlayMode::Standalone) Type = PlayMode_InNewProcess;
		Settings->LastExecutedPlayModeType = Type;
		// The toolbar's Play button reads this through the change notification.
		if (FProperty* Prop = ULevelEditorPlaySettings::StaticClass()->FindPropertyByName(
				GET_MEMBER_NAME_CHECKED(ULevelEditorPlaySettings, LastExecutedPlayModeType)))
		{
			FPropertyChangedEvent Event(Prop);
			Settings->PostEditChangeProperty(Event);
		}
	}

	static TSharedPtr<IAssetViewport> VisibleLevelViewport()
	{
		if (!FSlateApplication::IsInitialized()) return nullptr;
		FLevelEditorModule* LevelEditor = FModuleManager::GetModulePtr<FLevelEditorModule>(TEXT("LevelEditor"));
		if (!LevelEditor) return nullptr;
		TSharedPtr<IAssetViewport> Viewport = LevelEditor->GetFirstActiveViewport();
		// A viewport in a background tab has no window; PIE cannot start in it.
		if (!Viewport.IsValid() || !FSlateApplication::Get().FindWidgetWindow(Viewport->AsWidget()).IsValid())
		{
			return nullptr;
		}
		return Viewport;
	}

	FLaunch Request(bool bNeedsEditorWorld)
	{
		FLaunch Launch;
		Launch.Requested = EditorPlayMode();
		Launch.Used = Launch.Requested;
		if (!GEditor) return Launch;

		if (Launch.Used == EMCPPlayMode::Standalone && bNeedsEditorWorld)
		{
			Launch.Used = EMCPPlayMode::Viewport;
			Launch.Note = TEXT("The editor plays standalone, which runs no PIE world in this editor, and this action drives the PIE world, so it played in the level viewport.");
		}

		FRequestPlaySessionParams Params;
		if (Launch.Used == EMCPPlayMode::Standalone)
		{
			Params.SessionDestination = EPlaySessionDestinationType::NewProcess;
		}
		else if (Launch.Used == EMCPPlayMode::Viewport)
		{
			if (TSharedPtr<IAssetViewport> Viewport = VisibleLevelViewport())
			{
				Params.DestinationSlateViewport = Viewport;
			}
			else
			{
				Launch.Used = EMCPPlayMode::NewWindow;
				Launch.Note = TEXT("No level viewport is visible, so the engine opened the session in a new window.");
			}
		}
		GEditor->RequestPlaySession(Params);
		return Launch;
	}

	void WriteLaunch(const FLaunch& Launch, const TSharedPtr<FJsonObject>& Out)
	{
		if (!Out.IsValid()) return;
		Out->SetStringField(TEXT("playMode"), PlayModeName(Launch.Used));
		Out->SetStringField(TEXT("playModeRequested"), PlayModeName(Launch.Requested));
		if (!Launch.Note.IsEmpty()) Out->SetStringField(TEXT("playModeNote"), Launch.Note);
	}
}
