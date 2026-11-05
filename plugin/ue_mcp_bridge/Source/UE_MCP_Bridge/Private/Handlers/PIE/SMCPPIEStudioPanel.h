#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class FExtender;

class SMCPPIEStudioPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMCPPIEStudioPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	static void RegisterTab();
	static void UnregisterTab();
	static void OpenTab();
	static void RegisterToolbarButton();
	static void UnregisterToolbarButton();

	static const FName TabId;

private:
	static TSharedPtr<FExtender> ToolbarExtender;

private:
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	TSharedRef<SWidget> BuildRecorderSection();
	TSharedRef<SWidget> BuildTimeScaleSection();
	TSharedRef<SWidget> BuildRecordingsSection();
	TSharedRef<SWidget> BuildProfilesSection();
	TSharedRef<SWidget> BuildLiveDebugSection();

	void ApplyTimeScale(float Scale);

	void RefreshRecordings();
	void RefreshProfiles();
	void RefreshLiveDebug();

	TSharedPtr<STextBlock> RecorderStateText;

	// Recordings list
	TSharedPtr<SVerticalBox> RecordingsListBox;
	TArray<FString> CachedRecordingIds;
	TSet<FString> ExpandedRecordingIds;
	double LastRecordingsRefresh = 0.0;
	bool bCaptureGifOnReplay = false;

	// Profiles list
	TSharedPtr<SVerticalBox> ProfilesListBox;
	TArray<FString> CachedProfilePaths;
	TSet<FString> ActiveProfilePaths;
	double LastProfilesRefresh = 0.0;

	// Time scale
	TSharedPtr<STextBlock> TimeScaleText;
	float CurrentTimeScale = 1.0f;

	// Live debug
	TSharedPtr<SVerticalBox> LiveDebugContent;
	bool bLiveDebugExpanded = false;
	int32 LiveDebugTickCounter = 0;
};
