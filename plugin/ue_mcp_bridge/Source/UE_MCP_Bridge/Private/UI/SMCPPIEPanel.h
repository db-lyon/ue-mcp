#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class SMCPPIEPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMCPPIEPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	static void RegisterTab();
	static void UnregisterTab();
	static void OpenTab();

	static const FName TabId;

private:
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	TSharedRef<SWidget> BuildRecorderSection();
	TSharedRef<SWidget> BuildReplayerSection();
	TSharedRef<SWidget> BuildObserverSection();
	TSharedRef<SWidget> BuildRecordingsSection();
	TSharedRef<SWidget> BuildProfilesSection();

	void RefreshRecordings();
	void RefreshProfiles();

	// State text blocks updated per tick
	TSharedPtr<STextBlock> RecorderStateText;
	TSharedPtr<STextBlock> ReplayerStateText;
	TSharedPtr<STextBlock> ObserverStateText;

	// Recordings list
	TSharedPtr<SVerticalBox> RecordingsListBox;
	TArray<FString> CachedRecordingIds;
	double LastRecordingsRefresh = 0.0;

	// Profiles list
	TSharedPtr<SVerticalBox> ProfilesListBox;
	TArray<FString> CachedProfilePaths;
	double LastProfilesRefresh = 0.0;
};
