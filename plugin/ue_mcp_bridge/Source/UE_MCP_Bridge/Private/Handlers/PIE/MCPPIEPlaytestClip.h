#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MCPPIEPlaytestClip.generated.h"

/**
 * A playtest clip (#2097): input tracks, events, check notes and regions on a timeline of game time. The clip is
 * stored as its JSON, the one format people and agents both edit; pie(clip_read) returns it, and its compact text
 * form, without loading anything else.
 */
UCLASS(BlueprintType)
class UMCPPIEPlaytestClip : public UDataAsset
{
	GENERATED_BODY()
public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Clip",
		meta=(MultiLine="true", ToolTip="What this clip tests."))
	FString Description;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, AssetRegistrySearchable, Category="Clip",
		meta=(ToolTip="Level the clip plays in. Empty plays whatever level is open."))
	FString Map;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Clip",
		meta=(MultiLine="true", ToolTip="The clip as JSON: rate, length, regions, input tracks (notes and curve keys), events and checks. Edit it through pie(clip_edit) or pie(clip_update)."))
	FString ClipJson;
};
