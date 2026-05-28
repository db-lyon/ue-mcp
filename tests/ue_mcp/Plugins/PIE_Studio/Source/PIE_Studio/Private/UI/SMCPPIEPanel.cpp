#include "SMCPPIEPanel.h"
#include "PIE_StudioModule.h"
#include "PIE/PIEInputRecorder.h"
#include "PIE/PIEInputReplayer.h"
#include "PIE/PIEObserver.h"
#include "PIE/MCPObservationProfile.h"
#include "PIE/PIESequenceFormat.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Text/STextBlock.h"
#include "Framework/Docking/TabManager.h"
#include "ToolMenus.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"
#include "Styling/AppStyle.h"
#include "LevelEditor.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

const FName SMCPPIEPanel::TabId(TEXT("MCPPIEPanel"));

namespace
{
	FString RecorderStateStr(UEMCPPIE::ERecorderState S)
	{
		switch (S)
		{
		case UEMCPPIE::ERecorderState::Idle:           return TEXT("Idle");
		case UEMCPPIE::ERecorderState::Armed:          return TEXT("Armed");
		case UEMCPPIE::ERecorderState::WaitingForPawn: return TEXT("Waiting for Pawn");
		case UEMCPPIE::ERecorderState::Recording:      return TEXT("Recording");
		}
		return TEXT("?");
	}

	FString ReplayerStateStr(UEMCPPIE::EReplayerState S)
	{
		switch (S)
		{
		case UEMCPPIE::EReplayerState::Idle:           return TEXT("Idle");
		case UEMCPPIE::EReplayerState::Armed:          return TEXT("Armed");
		case UEMCPPIE::EReplayerState::WaitingForPawn: return TEXT("Waiting for Pawn");
		case UEMCPPIE::EReplayerState::Replaying:      return TEXT("Replaying");
		case UEMCPPIE::EReplayerState::Completed:      return TEXT("Completed");
		}
		return TEXT("?");
	}

	FString ObserverStateStr(UEMCPPIE::EObserverState S)
	{
		switch (S)
		{
		case UEMCPPIE::EObserverState::Idle:           return TEXT("Idle");
		case UEMCPPIE::EObserverState::Armed:          return TEXT("Armed");
		case UEMCPPIE::EObserverState::WaitingForPawn: return TEXT("Waiting for Pawn");
		case UEMCPPIE::EObserverState::Observing:      return TEXT("Observing");
		case UEMCPPIE::EObserverState::Completed:      return TEXT("Completed");
		}
		return TEXT("?");
	}

	FSlateColor StateColor(bool bActive)
	{
		return bActive ? FSlateColor(FLinearColor::Green) : FSlateColor(FSlateColor::UseForeground());
	}
}

void SMCPPIEPanel::RegisterTab()
{
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(TabId,
		FOnSpawnTab::CreateLambda([](const FSpawnTabArgs&) -> TSharedRef<SDockTab>
		{
			return SNew(SDockTab)
				.TabRole(NomadTab)
				.Label(FText::FromString(TEXT("PIE Studio")))
				[
					SNew(SMCPPIEPanel)
				];
		}))
		.SetDisplayName(FText::FromString(TEXT("PIE Studio")))
		.SetTooltipText(FText::FromString(TEXT("PIE Studio — Record / Replay / Observe")))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory());
}

void SMCPPIEPanel::UnregisterTab()
{
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TabId);
}

void SMCPPIEPanel::OpenTab()
{
	FGlobalTabmanager::Get()->TryInvokeTab(TabId);
}

TSharedPtr<FExtender> SMCPPIEPanel::ToolbarExtender;

void SMCPPIEPanel::RegisterToolbarButton()
{
	ToolbarExtender = MakeShareable(new FExtender);
	ToolbarExtender->AddToolBarExtension(
		"Play",
		EExtensionHook::After,
		nullptr,
		FToolBarExtensionDelegate::CreateLambda([](FToolBarBuilder& Builder)
		{
			Builder.AddSeparator();

			Builder.AddToolBarButton(
				FUIAction(FExecuteAction::CreateLambda([]()
				{
					UEMCPPIE::FRecorderArmConfig Cfg;
					FString Err, Msg;
					UEMCPPIE::FPIEInputRecorder::Get().Arm(Cfg, Err, Msg);
					if (GEditor && !GEditor->PlayWorld)
					{
						FRequestPlaySessionParams P;
						GEditor->RequestPlaySession(P);
					}
				})),
				NAME_None,
				FText::FromString(TEXT("Rec+Play")),
				FText::FromString(TEXT("Arm MCP recorder and start PIE")),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Recording")
			);

			Builder.AddToolBarButton(
				FUIAction(FExecuteAction::CreateLambda([]()
				{
					UEMCPPIE::FRecorderArmConfig Cfg;
					FString Err, Msg;
					UEMCPPIE::FPIEInputRecorder::Get().Arm(Cfg, Err, Msg);
				})),
				NAME_None,
				FText::FromString(TEXT("Arm")),
				FText::FromString(TEXT("Arm MCP recorder (waits for PIE start)")),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Check")
			);

			Builder.AddToolBarButton(
				FUIAction(FExecuteAction::CreateLambda([]()
				{
					FString Err;
					UEMCPPIE::FPIEInputRecorder::Get().Disarm(Err);
				})),
				NAME_None,
				FText::FromString(TEXT("Disarm")),
				FText::FromString(TEXT("Disarm MCP recorder")),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.X")
			);

			Builder.AddToolBarButton(
				FUIAction(FExecuteAction::CreateLambda([]()
				{
					UEMCPPIE::FPIEInputRecorder::Get().ForceStop();
				})),
				NAME_None,
				FText::FromString(TEXT("Stop")),
				FText::FromString(TEXT("Force stop MCP recording")),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Delete")
			);
		})
	);

	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	LevelEditorModule.GetToolBarExtensibilityManager()->AddExtender(ToolbarExtender);
}

void SMCPPIEPanel::UnregisterToolbarButton()
{
	if (ToolbarExtender.IsValid())
	{
		FLevelEditorModule* LevelEditorModule = FModuleManager::GetModulePtr<FLevelEditorModule>("LevelEditor");
		if (LevelEditorModule)
		{
			LevelEditorModule->GetToolBarExtensibilityManager()->RemoveExtender(ToolbarExtender);
		}
		ToolbarExtender.Reset();
	}
}

void SMCPPIEPanel::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SScrollBox)
		+ SScrollBox::Slot().Padding(8)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
			[ BuildRecorderSection() ]

			+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
			[ SNew(SSeparator) ]

			+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 4)
			[ BuildReplayerSection() ]

			+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
			[ SNew(SSeparator) ]

			+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 4)
			[ BuildObserverSection() ]

			+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
			[ SNew(SSeparator) ]

			+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 4)
			[ BuildTimeScaleSection() ]

			+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
			[ SNew(SSeparator) ]

			+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 4)
			[ BuildRecordingsSection() ]

			+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
			[ SNew(SSeparator) ]

			+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 4)
			[ BuildProfilesSection() ]
		]
	];
}

TSharedRef<SWidget> SMCPPIEPanel::BuildRecorderSection()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Font(FAppStyle::GetFontStyle("BoldFont"))
				.Text(FText::FromString(TEXT("Recorder")))
			]
			+ SHorizontalBox::Slot().FillWidth(1.f)
			+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
			[
				SAssignNew(RecorderStateText, STextBlock)
				.Text(FText::FromString(TEXT("Idle")))
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("Record + Play")))
				.OnClicked_Lambda([]()
				{
					UEMCPPIE::FRecorderArmConfig Cfg;
					FString Err, Msg;
					UEMCPPIE::FPIEInputRecorder::Get().Arm(Cfg, Err, Msg);
					if (GEditor && !GEditor->PlayWorld)
					{
						FRequestPlaySessionParams P; GEditor->RequestPlaySession(P);
					}
					return FReply::Handled();
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("Arm")))
				.OnClicked_Lambda([]()
				{
					UEMCPPIE::FRecorderArmConfig Cfg;
					FString Err, Msg;
					UEMCPPIE::FPIEInputRecorder::Get().Arm(Cfg, Err, Msg);
					return FReply::Handled();
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("Disarm")))
				.OnClicked_Lambda([]()
				{
					FString Err;
					UEMCPPIE::FPIEInputRecorder::Get().Disarm(Err);
					return FReply::Handled();
				})
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("Stop")))
				.OnClicked_Lambda([]()
				{
					UEMCPPIE::FPIEInputRecorder::Get().ForceStop();
					return FReply::Handled();
				})
			]
		];
}

TSharedRef<SWidget> SMCPPIEPanel::BuildReplayerSection()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Font(FAppStyle::GetFontStyle("BoldFont"))
				.Text(FText::FromString(TEXT("Replayer")))
			]
			+ SHorizontalBox::Slot().FillWidth(1.f)
			+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
			[
				SAssignNew(ReplayerStateText, STextBlock)
				.Text(FText::FromString(TEXT("Idle")))
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("Disarm")))
				.OnClicked_Lambda([]()
				{
					FString Err;
					UEMCPPIE::FPIEInputReplayer::Get().Disarm(Err);
					return FReply::Handled();
				})
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("Stop")))
				.OnClicked_Lambda([]()
				{
					UEMCPPIE::FPIEInputReplayer::Get().ForceStop();
					return FReply::Handled();
				})
			]
		];
}

TSharedRef<SWidget> SMCPPIEPanel::BuildObserverSection()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Font(FAppStyle::GetFontStyle("BoldFont"))
				.Text(FText::FromString(TEXT("Observer")))
			]
			+ SHorizontalBox::Slot().FillWidth(1.f)
			+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
			[
				SAssignNew(ObserverStateText, STextBlock)
				.Text(FText::FromString(TEXT("Idle")))
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("Disarm")))
				.OnClicked_Lambda([]()
				{
					FString Err;
					UEMCPPIE::FPIEObserver::Get().Disarm(Err);
					return FReply::Handled();
				})
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("Stop")))
				.OnClicked_Lambda([]()
				{
					UEMCPPIE::FPIEObserver::Get().ForceStop();
					return FReply::Handled();
				})
			]
		];
}

void SMCPPIEPanel::ApplyTimeScale(float Scale)
{
	CurrentTimeScale = Scale;
	UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
	if (World)
	{
		AWorldSettings* WS = World->GetWorldSettings();
		if (WS)
		{
			WS->MaxGlobalTimeDilation = FMath::Max(WS->MaxGlobalTimeDilation, 1000.f);
			WS->MinGlobalTimeDilation = FMath::Min(WS->MinGlobalTimeDilation, 0.0001f);
			UGameplayStatics::SetGlobalTimeDilation(World, Scale);
		}
	}
	if (TimeScaleText.IsValid())
	{
		TimeScaleText->SetText(FText::FromString(FString::Printf(TEXT("%.0f%%"), Scale * 100.f)));
	}
}

TSharedRef<SWidget> SMCPPIEPanel::BuildTimeScaleSection()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Font(FAppStyle::GetFontStyle("BoldFont"))
				.Text(FText::FromString(TEXT("Time Scale")))
			]
			+ SHorizontalBox::Slot().FillWidth(1.f)
			+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
			[
				SAssignNew(TimeScaleText, STextBlock)
				.Text(FText::FromString(TEXT("100%")))
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
		[
			SNew(SSlider)
			.MinValue(0.f)
			.MaxValue(1.f)
			.Value(0.5f)
			.OnValueChanged_Lambda([this](float Val)
			{
				float Scale;
				if (Val <= 0.5f)
				{
					Scale = FMath::Lerp(0.01f, 1.0f, Val * 2.0f);
				}
				else
				{
					Scale = FMath::Lerp(1.0f, 4.0f, (Val - 0.5f) * 2.0f);
				}
				ApplyTimeScale(Scale);
			})
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("1%")))
				.OnClicked_Lambda([this]() { ApplyTimeScale(0.01f); return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("10%")))
				.OnClicked_Lambda([this]() { ApplyTimeScale(0.1f); return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("25%")))
				.OnClicked_Lambda([this]() { ApplyTimeScale(0.25f); return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("50%")))
				.OnClicked_Lambda([this]() { ApplyTimeScale(0.5f); return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("100%")))
				.OnClicked_Lambda([this]() { ApplyTimeScale(1.0f); return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("200%")))
				.OnClicked_Lambda([this]() { ApplyTimeScale(2.0f); return FReply::Handled(); })
			]
		];
}

TSharedRef<SWidget> SMCPPIEPanel::BuildRecordingsSection()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Font(FAppStyle::GetFontStyle("BoldFont"))
				.Text(FText::FromString(TEXT("Recordings")))
			]
			+ SHorizontalBox::Slot().FillWidth(1.f)
			+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("Refresh")))
				.OnClicked_Lambda([this]()
				{
					RefreshRecordings();
					return FReply::Handled();
				})
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
		[
			SAssignNew(RecordingsListBox, SVerticalBox)
		];
}

TSharedRef<SWidget> SMCPPIEPanel::BuildProfilesSection()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Font(FAppStyle::GetFontStyle("BoldFont"))
				.Text(FText::FromString(TEXT("Observation Profiles")))
			]
			+ SHorizontalBox::Slot().FillWidth(1.f)
			+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("Refresh")))
				.OnClicked_Lambda([this]()
				{
					RefreshProfiles();
					return FReply::Handled();
				})
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
		[
			SAssignNew(ProfilesListBox, SVerticalBox)
		];
}

void SMCPPIEPanel::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	// Update state labels
	{
		const auto RS = UEMCPPIE::FPIEInputRecorder::Get().GetStatus();
		const FString RecText = FString::Printf(TEXT("%s  %s  F:%d  %.1fs"),
			*RecorderStateStr(RS.State), *RS.Id, RS.CurrentFrame, RS.ElapsedSeconds);
		const bool bRecActive = RS.State == UEMCPPIE::ERecorderState::Recording;
		RecorderStateText->SetText(FText::FromString(RecText));
		RecorderStateText->SetColorAndOpacity(StateColor(bRecActive));
	}
	{
		const auto RS = UEMCPPIE::FPIEInputReplayer::Get().GetStatus();
		const FString RepText = FString::Printf(TEXT("%s  %s  %d/%d  %.1fs"),
			*ReplayerStateStr(RS.State), *RS.SourceRecordingId,
			RS.CurrentStep, RS.TotalSteps, RS.ElapsedSeconds);
		const bool bRepActive = RS.State == UEMCPPIE::EReplayerState::Replaying;
		ReplayerStateText->SetText(FText::FromString(RepText));
		ReplayerStateText->SetColorAndOpacity(StateColor(bRepActive));
	}
	{
		const auto OS = UEMCPPIE::FPIEObserver::Get().GetStatus();
		const FString ObsText = FString::Printf(TEXT("%s  %s  F:%d  %.1fs"),
			*ObserverStateStr(OS.State),
			*FPaths::GetBaseFilename(OS.ProfilePath),
			OS.FramesSampled, OS.ElapsedSeconds);
		const bool bObsActive = OS.State == UEMCPPIE::EObserverState::Observing;
		ObserverStateText->SetText(FText::FromString(ObsText));
		ObserverStateText->SetColorAndOpacity(StateColor(bObsActive));
	}

	// Auto-refresh lists every 5 seconds
	if (InCurrentTime - LastRecordingsRefresh > 5.0)
	{
		RefreshRecordings();
		LastRecordingsRefresh = InCurrentTime;
	}
	if (InCurrentTime - LastProfilesRefresh > 5.0)
	{
		RefreshProfiles();
		LastProfilesRefresh = InCurrentTime;
	}
}

void SMCPPIEPanel::RefreshRecordings()
{
	if (!RecordingsListBox.IsValid()) return;
	RecordingsListBox->ClearChildren();
	CachedRecordingIds.Reset();

	const FString Root = UEMCPPIE::DefaultRecordingsRoot();
	TArray<FString> Dirs;
	IFileManager::Get().FindFiles(Dirs, *(Root / TEXT("*")), false, true);
	Dirs.Sort([](const FString& A, const FString& B) { return A > B; });
	if (Dirs.Num() > 50) Dirs.SetNum(50);

	for (const FString& D : Dirs)
	{
		CachedRecordingIds.Add(D);
		const FString Id = D;

		RecordingsListBox->AddSlot().AutoHeight().Padding(0, 2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(FText::FromString(Id))
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("Replay")))
				.OnClicked_Lambda([Id]()
				{
					UEMCPPIE::FReplayerArmConfig Cfg;
					Cfg.SourceRecordingId = Id;
					FString Err, Msg;
					UEMCPPIE::FPIEInputReplayer::Get().Arm(Cfg, Err, Msg);
					return FReply::Handled();
				})
			]
		];
	}

	if (Dirs.Num() == 0)
	{
		RecordingsListBox->AddSlot().AutoHeight()
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("No recordings found")))
			.ColorAndOpacity(FSlateColor(FLinearColor::Gray))
		];
	}
}

void SMCPPIEPanel::RefreshProfiles()
{
	if (!ProfilesListBox.IsValid()) return;
	ProfilesListBox->ClearChildren();
	CachedProfilePaths.Reset();

	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	TArray<FAssetData> Assets;
	ARM.Get().GetAssetsByClass(UMCPObservationProfile::StaticClass()->GetClassPathName(), Assets, true);

	for (const FAssetData& A : Assets)
	{
		const FString Path = A.GetObjectPathString();
		CachedProfilePaths.Add(Path);

		ProfilesListBox->AddSlot().AutoHeight().Padding(0, 2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(FText::FromString(A.AssetName.ToString()))
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("Observe")))
				.OnClicked_Lambda([Path]()
				{
					UEMCPPIE::FObserverArmConfig Cfg;
					Cfg.ProfilePath = Path;
					FString Err, Msg;
					UEMCPPIE::FPIEObserver::Get().Arm(Cfg, Err, Msg);
					return FReply::Handled();
				})
			]
		];
	}

	if (Assets.Num() == 0)
	{
		ProfilesListBox->AddSlot().AutoHeight()
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("No profiles found")))
			.ColorAndOpacity(FSlateColor(FLinearColor::Gray))
		];
	}
}
