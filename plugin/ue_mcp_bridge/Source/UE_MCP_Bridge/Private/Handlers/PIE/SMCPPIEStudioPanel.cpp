#include "Handlers/PIE/SMCPPIEStudioPanel.h"
#include "HandlerCheckpoint.h"
#include "Handlers/PIE/PIELog.h"
#include "Handlers/PIE/PIEInputRecorder.h"
#include "Handlers/PIE/PIEInputReplayer.h"
#include "Handlers/PIE/PIEObserver.h"
#include "Handlers/PIE/MCPPIEObservationProfile.h"
#include "Handlers/PIE/PIESequenceFormat.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Images/SImage.h"
#include "EditorAssetLibrary.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/SavePackage.h"
#include "Widgets/Text/STextBlock.h"
#include "Framework/Docking/TabManager.h"
#include "ToolMenus.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Misc/Paths.h"
#include "Interfaces/IPluginManager.h"
#include "LevelEditor.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

const FName SMCPPIEStudioPanel::TabId(TEXT("MCPPIEStudio"));

#define IMAGE_BRUSH(RelativePath, ...) FSlateImageBrush(PIEStudioStyleSet->RootToContentDir(RelativePath, TEXT(".png")), __VA_ARGS__)

static TSharedPtr<FSlateStyleSet> PIEStudioStyleSet;

static void RegisterPIEStudioStyle()
{
	if (PIEStudioStyleSet.IsValid()) return;

	const TSharedPtr<IPlugin> Bridge = IPluginManager::Get().FindPlugin(TEXT("UE_MCP_Bridge"));
	const FString ResourcesDir = Bridge.IsValid()
		? FPaths::Combine(Bridge->GetBaseDir(), TEXT("Resources"))
		: FPaths::Combine(FPaths::ProjectPluginsDir(), TEXT("UE_MCP_Bridge"), TEXT("Resources"));

	PIEStudioStyleSet = MakeShareable(new FSlateStyleSet("MCPPIEStudioStyle"));
	PIEStudioStyleSet->SetContentRoot(ResourcesDir);
	PIEStudioStyleSet->Set("MCPPIEStudio.Record", new IMAGE_BRUSH("Record_Icon40x", CoreStyleConstants::Icon16x16));
	PIEStudioStyleSet->Set("MCPPIEStudio.RecordPlay", new IMAGE_BRUSH("RecordPlay_Icon40x", CoreStyleConstants::Icon16x16));
	PIEStudioStyleSet->Set("MCPPIEStudio.Play", new IMAGE_BRUSH("Play_Icon40x", CoreStyleConstants::Icon16x16));
	FSlateStyleRegistry::RegisterSlateStyle(*PIEStudioStyleSet);
}

static void UnregisterPIEStudioStyle()
{
	if (PIEStudioStyleSet.IsValid())
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(*PIEStudioStyleSet);
		PIEStudioStyleSet.Reset();
	}
}

#undef IMAGE_BRUSH

namespace
{
	FString RecorderStateStr(MCPPIE::ERecorderState S)
	{
		switch (S)
		{
		case MCPPIE::ERecorderState::Idle:           return TEXT("Idle");
		case MCPPIE::ERecorderState::Armed:          return TEXT("Armed");
		case MCPPIE::ERecorderState::WaitingForPawn: return TEXT("Waiting for Pawn");
		case MCPPIE::ERecorderState::Recording:      return TEXT("Recording");
		}
		return TEXT("?");
	}

	FString ReplayerStateStr(MCPPIE::EReplayerState S)
	{
		switch (S)
		{
		case MCPPIE::EReplayerState::Idle:           return TEXT("Idle");
		case MCPPIE::EReplayerState::Armed:          return TEXT("Armed");
		case MCPPIE::EReplayerState::WaitingForPawn: return TEXT("Waiting for Pawn");
		case MCPPIE::EReplayerState::Replaying:      return TEXT("Replaying");
		case MCPPIE::EReplayerState::Completed:      return TEXT("Completed");
		}
		return TEXT("?");
	}

	FString ObserverStateStr(MCPPIE::EObserverState S)
	{
		switch (S)
		{
		case MCPPIE::EObserverState::Idle:           return TEXT("Idle");
		case MCPPIE::EObserverState::Armed:          return TEXT("Armed");
		case MCPPIE::EObserverState::WaitingForPawn: return TEXT("Waiting for Pawn");
		case MCPPIE::EObserverState::Observing:      return TEXT("Observing");
		case MCPPIE::EObserverState::Completed:      return TEXT("Completed");
		}
		return TEXT("?");
	}

	FSlateColor StateColor(bool bActive)
	{
		return bActive ? FSlateColor(FLinearColor::Green) : FSlateColor(FSlateColor::UseForeground());
	}
}

void SMCPPIEStudioPanel::RegisterTab()
{
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(TabId,
		FOnSpawnTab::CreateLambda([](const FSpawnTabArgs&) -> TSharedRef<SDockTab>
		{
			return SNew(SDockTab)
				.TabRole(NomadTab)
				.Label(FText::FromString(TEXT("PIE Studio")))
				[
					SNew(SMCPPIEStudioPanel)
				];
		}))
		.SetDisplayName(FText::FromString(TEXT("PIE Studio")))
		.SetTooltipText(FText::FromString(TEXT("PIE Studio: record, replay, observe")))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory());
}

void SMCPPIEStudioPanel::UnregisterTab()
{
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TabId);
}

void SMCPPIEStudioPanel::OpenTab()
{
	FGlobalTabmanager::Get()->TryInvokeTab(TabId);
}

TSharedPtr<FExtender> SMCPPIEStudioPanel::ToolbarExtender;

void SMCPPIEStudioPanel::RegisterToolbarButton()
{
	RegisterPIEStudioStyle();

	UToolMenu* ToolBar = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.PlayToolBar");
	FToolMenuSection& Section = ToolBar->FindOrAddSection("MCPPIEStudio");

	Section.AddDynamicEntry("MCPPIEStudioActions", FNewToolMenuSectionDelegate::CreateLambda([](FToolMenuSection& InSection)
	{
		{
			FToolMenuEntry Entry =
				FToolMenuEntry::InitToolBarButton(
					"Record",
					FExecuteAction::CreateLambda([]()
					{
						MCPPIE::FRecorderArmConfig Cfg;
						FString Err, Msg;
						MCPPIE::FPIEInputRecorder::Get().Arm(Cfg, Err, Msg);
					}),
					FText::GetEmpty(),
					FText::FromString(TEXT("Arm MCP recorder (waits for PIE start)")),
					FSlateIcon("MCPPIEStudioStyle", "MCPPIEStudio.Record"));
			Entry.StyleNameOverride = FName("Toolbar.BackplateLeft");
			InSection.AddEntry(Entry);
		}

		{
			FToolMenuEntry Entry =
				FToolMenuEntry::InitToolBarButton(
					"RecordPlay",
					FExecuteAction::CreateLambda([]()
					{
						MCPPIE::FRecorderArmConfig Cfg;
						FString Err, Msg;
						MCPPIE::FPIEInputRecorder::Get().Arm(Cfg, Err, Msg);
						if (GEditor && !GEditor->PlayWorld)
						{
							FRequestPlaySessionParams P;
							GEditor->RequestPlaySession(P);
						}
					}),
					FText::GetEmpty(),
					FText::FromString(TEXT("Arm MCP recorder and start PIE")),
					FSlateIcon("MCPPIEStudioStyle", "MCPPIEStudio.RecordPlay"));
			Entry.StyleNameOverride = FName("Toolbar.BackplateCenter");
			InSection.AddEntry(Entry);
		}

		{
			FToolMenuEntry ComboEntry =
				FToolMenuEntry::InitComboButton(
					"MCPPIEStudioMenu",
					FUIAction(),
					FNewMenuDelegate::CreateLambda([](FMenuBuilder& Menu)
					{
						Menu.BeginSection("Recording", FText::FromString(TEXT("Recording")));

						Menu.AddMenuEntry(
							FText::FromString(TEXT("Record + Play")),
							FText::FromString(TEXT("Arm MCP recorder and start PIE")),
							FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Recording"),
							FUIAction(FExecuteAction::CreateLambda([]()
							{
								MCPPIE::FRecorderArmConfig Cfg;
								FString Err, Msg;
								MCPPIE::FPIEInputRecorder::Get().Arm(Cfg, Err, Msg);
								if (GEditor && !GEditor->PlayWorld)
								{
									FRequestPlaySessionParams P;
									GEditor->RequestPlaySession(P);
								}
							}))
						);

						Menu.AddMenuEntry(
							FText::FromString(TEXT("Arm Recorder")),
							FText::FromString(TEXT("Arm MCP recorder (waits for PIE start)")),
							FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Recording"),
							FUIAction(FExecuteAction::CreateLambda([]()
							{
								MCPPIE::FRecorderArmConfig Cfg;
								FString Err, Msg;
								MCPPIE::FPIEInputRecorder::Get().Arm(Cfg, Err, Msg);
							}))
						);

						const auto RecState = MCPPIE::FPIEInputRecorder::Get().GetStatus().State;

						if (RecState == MCPPIE::ERecorderState::Armed || RecState == MCPPIE::ERecorderState::WaitingForPawn)
						{
							Menu.AddMenuEntry(
								FText::FromString(TEXT("Disarm Recorder")),
								FText::FromString(TEXT("Disarm MCP recorder")),
								FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.X"),
								FUIAction(FExecuteAction::CreateLambda([]()
								{
									FString Err;
									MCPPIE::FPIEInputRecorder::Get().Disarm(Err);
								}))
							);
						}

						if (RecState == MCPPIE::ERecorderState::Recording)
						{
							Menu.AddMenuEntry(
								FText::FromString(TEXT("Stop Recording")),
								FText::FromString(TEXT("Force stop MCP recording")),
								FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Delete"),
								FUIAction(FExecuteAction::CreateLambda([]()
								{
									MCPPIE::FPIEInputRecorder::Get().ForceStop();
								}))
							);
						}

						Menu.EndSection();

						Menu.BeginSection("Panel", FText::FromString(TEXT("Panel")));
						Menu.AddMenuEntry(
							FText::FromString(TEXT("Open PIE Studio Panel")),
							FText::FromString(TEXT("Open the full PIE Studio control panel")),
							FSlateIcon(),
							FUIAction(FExecuteAction::CreateLambda([]()
							{
								SMCPPIEStudioPanel::OpenTab();
							}))
						);
						Menu.EndSection();
					}),
					FText::GetEmpty(),
					FText::FromString(TEXT("PIE Studio Options")));
			ComboEntry.StyleNameOverride = FName("Toolbar.BackplateRightCombo");
			InSection.AddEntry(ComboEntry);
		}
	}));
}

void SMCPPIEStudioPanel::UnregisterToolbarButton()
{
	UnregisterPIEStudioStyle();
}

void SMCPPIEStudioPanel::Construct(const FArguments& InArgs)
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
			[ BuildTimeScaleSection() ]

			+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
			[ SNew(SSeparator) ]

			+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 4)
			[ BuildRecordingsSection() ]

			+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
			[ SNew(SSeparator) ]

			+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 4)
			[ BuildProfilesSection() ]

			+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
			[ SNew(SSeparator) ]

			+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 4)
			[ BuildLiveDebugSection() ]
		]
	];
}

TSharedRef<SWidget> SMCPPIEStudioPanel::BuildRecorderSection()
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
					MCPPIE::FRecorderArmConfig Cfg;
					FString Err, Msg;
					MCPPIE::FPIEInputRecorder::Get().Arm(Cfg, Err, Msg);
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
					MCPPIE::FRecorderArmConfig Cfg;
					FString Err, Msg;
					MCPPIE::FPIEInputRecorder::Get().Arm(Cfg, Err, Msg);
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
					MCPPIE::FPIEInputRecorder::Get().Disarm(Err);
					return FReply::Handled();
				})
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("Stop")))
				.OnClicked_Lambda([]()
				{
					MCPPIE::FPIEInputRecorder::Get().ForceStop();
					return FReply::Handled();
				})
			]
		];
}


void SMCPPIEStudioPanel::ApplyTimeScale(float Scale)
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

TSharedRef<SWidget> SMCPPIEStudioPanel::BuildTimeScaleSection()
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

TSharedRef<SWidget> SMCPPIEStudioPanel::BuildRecordingsSection()
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
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this]() { return bCaptureGifOnReplay ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState S) { bCaptureGifOnReplay = (S == ECheckBoxState::Checked); })
				.ToolTipText(FText::FromString(TEXT("Capture viewport frames during replay and assemble into a GIF on completion. Off = no frames written, no GIF.")))
				[
					SNew(STextBlock).Text(FText::FromString(TEXT("Capture GIF")))
				]
			]
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

TSharedRef<SWidget> SMCPPIEStudioPanel::BuildProfilesSection()
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
				.Text(FText::FromString(TEXT("Create")))
				.OnClicked_Lambda([this]()
				{
					FString PackagePath = TEXT("/Game/PIEStudio");
					FString AssetName = FString::Printf(TEXT("OP_%s"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
					FString FullPath = PackagePath / AssetName;

					UPackage* Pkg = CreatePackage(*FullPath);
					UMCPPIEObservationProfile* NewProfile = NewObject<UMCPPIEObservationProfile>(Pkg, *AssetName, RF_Public | RF_Standalone);
					FAssetRegistryModule::AssetCreated(NewProfile);
					NewProfile->MarkPackageDirty();

					FString PackageFilename = FPackageName::LongPackageNameToFilename(FullPath, FPackageName::GetAssetPackageExtension());
					MCPCheckpointRecordBeforeWrite(PackageFilename);
					UPackage::SavePackage(Pkg, NewProfile, *PackageFilename, FSavePackageArgs());

					GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(NewProfile);
					RefreshProfiles();
					return FReply::Handled();
				})
			]
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

void SMCPPIEStudioPanel::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	// Update state labels
	{
		const auto RS = MCPPIE::FPIEInputRecorder::Get().GetStatus();
		const FString RecText = FString::Printf(TEXT("%s  %s  F:%d  %.1fs"),
			*RecorderStateStr(RS.State), *RS.Id, RS.CurrentFrame, RS.ElapsedSeconds);
		const bool bRecActive = RS.State == MCPPIE::ERecorderState::Recording;
		RecorderStateText->SetText(FText::FromString(RecText));
		RecorderStateText->SetColorAndOpacity(StateColor(bRecActive));
	}

	// Re-apply time scale if PIE is running and dilation doesn't match
	if (CurrentTimeScale != 1.0f)
	{
		UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
		if (World)
		{
			AWorldSettings* WS = World->GetWorldSettings();
			if (WS && !FMath::IsNearlyEqual(UGameplayStatics::GetGlobalTimeDilation(World), CurrentTimeScale, 0.001f))
			{
				ApplyTimeScale(CurrentTimeScale);
			}
		}
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

	// Live debug: refresh every 3rd tick when expanded and active
	if (bLiveDebugExpanded && LiveDebugContent.IsValid())
	{
		const bool bReplayActive = MCPPIE::FPIEInputReplayer::Get().IsActive();
		const bool bObserverActive = MCPPIE::FPIEObserver::Get().IsActive();
		if (bReplayActive || bObserverActive)
		{
			if (++LiveDebugTickCounter >= 3)
			{
				LiveDebugTickCounter = 0;
				RefreshLiveDebug();
			}
		}
		else if (LiveDebugContent->NumSlots() > 0)
		{
			LiveDebugContent->ClearChildren();
			LiveDebugContent->AddSlot().AutoHeight()
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("No active replay or observation")))
				.ColorAndOpacity(FSlateColor(FLinearColor::Gray))
			];
		}
	}
}

void SMCPPIEStudioPanel::RefreshRecordings()
{
	if (!RecordingsListBox.IsValid()) return;
	RecordingsListBox->ClearChildren();
	CachedRecordingIds.Reset();

	const FString Root = MCPPIE::DefaultRecordingsRoot();
	TArray<FString> Dirs;
	IFileManager::Get().FindFiles(Dirs, *(Root / TEXT("*")), false, true);
	Dirs.Sort([](const FString& A, const FString& B) { return A > B; });
	if (Dirs.Num() > 50) Dirs.SetNum(50);

	for (const FString& D : Dirs)
	{
		CachedRecordingIds.Add(D);
		const FString Id = D;
		const FString RecDir = Root / Id;

		const FString CapturesDir = RecDir / TEXT("captures");
		TArray<FString> Gifs;
		IFileManager::Get().FindFiles(Gifs, *(CapturesDir / TEXT("*.gif")), true, false);
		Gifs.Sort([](const FString& A, const FString& B) { return A > B; });

		RecordingsListBox->AddSlot().AutoHeight().Padding(0, 2)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(FText::FromString(Id))
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
				[
					SNew(SButton)
					.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("SimpleButton"))
					.ContentPadding(FMargin(2))
					.ToolTipText_Lambda([this]() {
						return FText::FromString(bCaptureGifOnReplay ? TEXT("Replay + Capture GIF") : TEXT("Replay"));
					})
					.OnClicked_Lambda([this, Id]()
					{
						MCPPIE::FReplayerArmConfig Cfg;
						Cfg.SourceRecordingId = Id;
						Cfg.CaptureFrameEvery = bCaptureGifOnReplay ? 2 : 0;
						FString Err, Msg;
						MCPPIE::FPIEInputReplayer::Get().Arm(Cfg, Err, Msg);
						for (const FString& ProfilePath : ActiveProfilePaths)
						{
							MCPPIE::FObserverArmConfig OCfg;
							OCfg.ProfilePath = ProfilePath;
							FString OErr, OMsg;
							MCPPIE::FPIEObserver::Get().Arm(OCfg, OErr, OMsg);
						}
						if (GEditor && !GEditor->PlayWorld)
						{
							FRequestPlaySessionParams P;
							GEditor->RequestPlaySession(P);
						}
						return FReply::Handled();
					})
					[
						SNew(SImage)
						.Image(FSlateStyleRegistry::FindSlateStyle("MCPPIEStudioStyle")->GetBrush("MCPPIEStudio.Play"))
						.DesiredSizeOverride(FVector2D(16.f))
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(2, 0)
				[
					SNew(SButton)
					.Text(FText::FromString(TEXT("Delete")))
					.OnClicked_Lambda([this, Id]()
					{
						const FString RecPath = MCPPIE::DefaultRecordingsRoot() / Id;
						IFileManager::Get().DeleteDirectory(*RecPath, false, true);
						RefreshRecordings();
						return FReply::Handled();
					})
				]
			]
		];

		if (Gifs.Num() > 0)
		{
			TSharedRef<SVerticalBox> GifContent = SNew(SVerticalBox);
			for (const FString& GifName : Gifs)
			{
				FString GifFullPath = FPaths::ConvertRelativePathToFull(CapturesDir / GifName);
				FPaths::NormalizeFilename(GifFullPath);
				GifFullPath.ReplaceInline(TEXT("/"), TEXT("\\"));

				GifContent->AddSlot().AutoHeight().Padding(0, 1)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(FText::FromString(GifName))
						.ColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.7f, 0.7f)))
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
					[
						SNew(SButton)
						.Text(FText::FromString(TEXT("Open")))
						.OnClicked_Lambda([GifFullPath]()
						{
							FPlatformProcess::LaunchFileInDefaultExternalApplication(*GifFullPath);
							return FReply::Handled();
						})
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(2, 0)
					[
						SNew(SButton)
						.Text(FText::FromString(TEXT("Delete")))
						.OnClicked_Lambda([this, GifFullPath]()
						{
							IFileManager::Get().Delete(*GifFullPath);
							RefreshRecordings();
							return FReply::Handled();
						})
					]
				];
			}

			const bool bWasExpanded = ExpandedRecordingIds.Contains(Id);
			RecordingsListBox->AddSlot().AutoHeight().Padding(16, 0, 0, 0)
			[
				SNew(SExpandableArea)
				.InitiallyCollapsed(!bWasExpanded)
				.OnAreaExpansionChanged_Lambda([this, Id](bool bExpanded)
				{
					if (bExpanded)
						ExpandedRecordingIds.Add(Id);
					else
						ExpandedRecordingIds.Remove(Id);
				})
				.HeaderContent()
				[
					SNew(STextBlock)
					.Text(FText::FromString(FString::Printf(TEXT("%d replay GIFs"), Gifs.Num())))
					.ColorAndOpacity(FSlateColor(FLinearColor::Gray))
				]
				.BodyContent()
				[
					GifContent
				]
			];
		}
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

void SMCPPIEStudioPanel::RefreshProfiles()
{
	if (!ProfilesListBox.IsValid()) return;
	ProfilesListBox->ClearChildren();
	CachedProfilePaths.Reset();

	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	TArray<FAssetData> Assets;
	ARM.Get().GetAssetsByClass(UMCPPIEObservationProfile::StaticClass()->GetClassPathName(), Assets, true);

	for (const FAssetData& A : Assets)
	{
		const FString Path = A.GetObjectPathString();
		CachedProfilePaths.Add(Path);
		const FString Name = A.AssetName.ToString();
		const bool bActive = ActiveProfilePaths.Contains(Path);

		ProfilesListBox->AddSlot().AutoHeight().Padding(0, 2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
			[
				SNew(SCheckBox)
				.IsChecked(bActive ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
				.OnCheckStateChanged_Lambda([this, Path](ECheckBoxState NewState)
				{
					if (NewState == ECheckBoxState::Checked)
						ActiveProfilePaths.Add(Path);
					else
						ActiveProfilePaths.Remove(Path);
				})
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(FText::FromString(Name))
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2, 0)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("Edit")))
				.OnClicked_Lambda([Path]()
				{
					UObject* Asset = LoadObject<UMCPPIEObservationProfile>(nullptr, *Path);
					if (Asset)
					{
						GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Asset);
					}
					return FReply::Handled();
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2, 0)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("Delete")))
				.OnClicked_Lambda([this, Path]()
				{
					UEditorAssetLibrary::DeleteAsset(Path);
					ActiveProfilePaths.Remove(Path);
					RefreshProfiles();
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

TSharedRef<SWidget> SMCPPIEStudioPanel::BuildLiveDebugSection()
{
	return SNew(SExpandableArea)
		.InitiallyCollapsed(true)
		.OnAreaExpansionChanged_Lambda([this](bool bExpanded) { bLiveDebugExpanded = bExpanded; })
		.HeaderContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Font(FAppStyle::GetFontStyle("BoldFont"))
				.Text(FText::FromString(TEXT("Live Debug")))
			]
			+ SHorizontalBox::Slot().FillWidth(1.f)
			+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
			[
				SNew(STextBlock)
				.Text_Lambda([]() -> FText
				{
					const bool bRep = MCPPIE::FPIEInputReplayer::Get().IsActive();
					const bool bObs = MCPPIE::FPIEObserver::Get().IsActive();
					return FText::FromString((bRep || bObs) ? TEXT("Active") : TEXT("Idle"));
				})
				.ColorAndOpacity_Lambda([]() -> FSlateColor
				{
					const bool bRep = MCPPIE::FPIEInputReplayer::Get().IsActive();
					const bool bObs = MCPPIE::FPIEObserver::Get().IsActive();
					return (bRep || bObs) ? FSlateColor(FLinearColor::Green) : FSlateColor(FSlateColor::UseForeground());
				})
			]
		]
		.BodyContent()
		[
			SAssignNew(LiveDebugContent, SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("No active replay or observation")))
				.ColorAndOpacity(FSlateColor(FLinearColor::Gray))
			]
		];
}

namespace
{
	FSlateColor DeltaColor(bool bChanged)
	{
		return bChanged
			? FSlateColor(FLinearColor(1.0f, 0.8f, 0.0f))
			: FSlateColor(FSlateColor::UseForeground());
	}

	void AddPropertyRow(SVerticalBox& Box, const FString& Label, const FString& Value, bool bChanged)
	{
		Box.AddSlot().AutoHeight().Padding(0, 1)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Label))
				.MinDesiredWidth(140.f)
				.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
			]
			+ SHorizontalBox::Slot().FillWidth(1.f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Value))
				.ColorAndOpacity(DeltaColor(bChanged))
			]
		];
	}

	FString VecStr(const FVector& V)
	{
		return FString::Printf(TEXT("%.1f, %.1f, %.1f"), V.X, V.Y, V.Z);
	}

	FString RotStr(const FRotator& R)
	{
		return FString::Printf(TEXT("Y:%.1f P:%.1f R:%.1f"), R.Yaw, R.Pitch, R.Roll);
	}
}

void SMCPPIEStudioPanel::RefreshLiveDebug()
{
	if (!LiveDebugContent.IsValid()) return;
	LiveDebugContent->ClearChildren();

	// Replay status
	const auto Rep = MCPPIE::FPIEInputReplayer::Get().GetLiveSnapshot();
	if (Rep.State == MCPPIE::EReplayerState::Replaying)
	{
		LiveDebugContent->AddSlot().AutoHeight().Padding(0, 0, 0, 4)
		[
			SNew(STextBlock)
			.Font(FAppStyle::GetFontStyle("BoldFont"))
			.Text(FText::FromString(FString::Printf(TEXT("Replay: %s  Step %d/%d  %.1fs"),
				*Rep.SourceRecordingId, Rep.CurrentStep, Rep.TotalSteps, Rep.ElapsedSeconds)))
		];

		// Drift summary
		LiveDebugContent->AddSlot().AutoHeight().Padding(8, 0, 0, 2)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(
					TEXT("Drift - Pos: %.1f cm  Vel: %.1f cm/s  Rot: %.1f°  Montage: %d  Frames: %d"),
					Rep.MaxPositionDriftCm, Rep.MaxVelocityDriftCms, Rep.MaxRotationDriftDeg,
					Rep.MontageMismatches, Rep.FramesCompared)))
				.ColorAndOpacity(FSlateColor(Rep.MaxPositionDriftCm > 50.f
					? FLinearColor(1.f, 0.3f, 0.3f)
					: FLinearColor(0.7f, 0.7f, 0.7f)))
			]
		];

		if (Rep.MaxTrackedDeltas.Num() > 0)
		{
			TSharedRef<SVerticalBox> TrackedDriftBox = SNew(SVerticalBox);
			for (const auto& KV : Rep.MaxTrackedDeltas)
			{
				AddPropertyRow(*TrackedDriftBox,
					FString::Printf(TEXT("  %s"), *KV.Key),
					FString::Printf(TEXT("Δ %.4f"), KV.Value),
					KV.Value > 0.01f);
			}
			LiveDebugContent->AddSlot().AutoHeight().Padding(8, 0, 0, 4)
			[
				TrackedDriftBox
			];
		}
	}

	// Observation profiles
	TArray<MCPPIE::FLiveObservationSnapshot> Snaps = MCPPIE::FPIEObserver::Get().GetLiveSnapshots();
	for (const auto& Snap : Snaps)
	{
		LiveDebugContent->AddSlot().AutoHeight().Padding(0, 4, 0, 0)
		[
			SNew(STextBlock)
			.Font(FAppStyle::GetFontStyle("BoldFont"))
			.Text(FText::FromString(FString::Printf(TEXT("Profile: %s  F:%d  %.1fs"),
				*Snap.ProfileName, Snap.FramesSampled, Snap.ElapsedSeconds)))
		];

		TSharedRef<SVerticalBox> PropsBox = SNew(SVerticalBox);

		// Pawn state
		const auto& Cur = Snap.LastRow;
		const auto& Prev = Snap.PrevRow;

		AddPropertyRow(*PropsBox, TEXT("Position"), VecStr(Cur.PawnLocation),
			!(Cur.PawnLocation - Prev.PawnLocation).IsNearlyZero(0.1));
		AddPropertyRow(*PropsBox, TEXT("Rotation"), RotStr(Cur.PawnRotation),
			!(Cur.PawnRotation - Prev.PawnRotation).IsNearlyZero(0.1));
		AddPropertyRow(*PropsBox, TEXT("Velocity"), VecStr(Cur.PawnVelocity),
			!(Cur.PawnVelocity - Prev.PawnVelocity).IsNearlyZero(0.1));
		AddPropertyRow(*PropsBox, TEXT("Speed2D"),
			FString::Printf(TEXT("%.1f"), Cur.Speed2D),
			!FMath::IsNearlyEqual(Cur.Speed2D, Prev.Speed2D, 0.1f));

		if (!Cur.MontageSection.IsEmpty())
		{
			AddPropertyRow(*PropsBox, TEXT("Montage"), Cur.MontageSection,
				Cur.MontageSection != Prev.MontageSection);
		}

		// Tracked values
		for (const auto& KV : Cur.TrackedValues)
		{
			const double* PrevVal = Prev.TrackedValues.Find(KV.Key);
			const bool bChanged = !PrevVal || !FMath::IsNearlyEqual(KV.Value, *PrevVal, 0.0001);
			AddPropertyRow(*PropsBox, KV.Key,
				FString::Printf(TEXT("%.4f"), KV.Value), bChanged);
		}

		// Tracked actors
		for (const auto& KV : Snap.LastActorRow.Actors)
		{
			if (!KV.Value.bResolved) continue;
			AddPropertyRow(*PropsBox,
				FString::Printf(TEXT("[%s] Pos"), *KV.Key),
				VecStr(KV.Value.Location), true);
			AddPropertyRow(*PropsBox,
				FString::Printf(TEXT("[%s] Rot"), *KV.Key),
				RotStr(KV.Value.Rotation), true);
		}

		LiveDebugContent->AddSlot().AutoHeight().Padding(8, 0, 0, 0)
		[
			PropsBox
		];
	}

	if (Rep.State != MCPPIE::EReplayerState::Replaying && Snaps.Num() == 0)
	{
		LiveDebugContent->AddSlot().AutoHeight()
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("Waiting for data...")))
			.ColorAndOpacity(FSlateColor(FLinearColor::Gray))
		];
	}
}
