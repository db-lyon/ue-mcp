// UMG animation authoring, navigation rules, and the focus / accessibility
// audits. Split from WidgetHandlers.cpp as a translation-unit partition: every
// function here is still a member of FWidgetHandlers. Registration stays in
// WidgetHandlers.cpp::RegisterHandlers.
//
// Why these earn handlers at all, when the house rule is that asset(set_property)
// and editor(set_property) already reach any UPROPERTY at an objectPath:
//
//   * A UWidgetAnimation and its UMovieScene tracks and sections are OBJECTS.
//     They have to be constructed, outered to the right package, registered in
//     UWidgetBlueprint::Animations and paired with an FWidgetAnimationBinding
//     before any property on them exists to be written. set_property cannot
//     create the thing it would write to.
//   * UWidget::Navigation is an Instanced UPROPERTY that is NULL on every
//     widget until something makes one. The generic dotted-path setter reports
//     "object reference is null - cannot descend" and stops. So the subobject
//     creation is the gap; the per-direction rules themselves are ordinary
//     UPROPERTYs and stay reachable by set_property once the object exists,
//     which is exactly what set_widget_navigation's result tells the caller.
//   * The two audits evaluate rules ACROSS the whole widget tree - reachability
//     over a navigation graph, font sizes against a floor. Neither is a
//     property read, and neither can be assembled by a caller without pulling
//     the entire tree over the wire and reimplementing the rules.
//
// Deliberately NOT built here, because they already ship:
//   text scaling            editor(set_property) on
//                           /Script/Engine.Default__UserInterfaceSettings,
//                           propertyName "ApplicationScale" (config UPROPERTY).
//   colourblind mode        editor(invoke_static_function) on
//                           WidgetBlueprintLibrary::SetColorVisionDeficiencyType.
//   initial focus target    asset(set_property) on the Widget Blueprint,
//                           propertyName "DesiredFocusWidget.WidgetName"
//                           (FWidgetChild on the generated-class CDO).
//   navigation rule fields  widget(set_style) with propertyName
//                           "Navigation.Down" once the Navigation object exists.

#include "WidgetHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"

#include "WidgetBlueprint.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetNavigation.h"
#include "Components/Widget.h"
#include "Components/PanelWidget.h"
#include "Components/PanelSlot.h"
#include "Components/CanvasPanelSlot.h"

#include "Animation/WidgetAnimation.h"
#include "Animation/WidgetAnimationBinding.h"
#include "Animation/MovieSceneMarginTrack.h"
#include "Animation/MovieScene2DTransformTrack.h"

#include "MovieScene.h"
#include "MovieSceneSection.h"
#include "MovieSceneTrack.h"
#include "MovieScenePossessable.h"
#include "Channels/MovieSceneChannelData.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Channels/MovieSceneDoubleChannel.h"
#include "Channels/MovieSceneBoolChannel.h"
#include "Channels/MovieSceneIntegerChannel.h"
#include "Channels/MovieSceneEvent.h"
#include "Channels/MovieSceneEventChannel.h"
#include "Sections/MovieSceneEventTriggerSection.h"
#include "Tracks/MovieSceneEventTrack.h"
#include "Tracks/MovieScenePropertyTrack.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "Tracks/MovieSceneDoubleTrack.h"
#include "Tracks/MovieSceneBoolTrack.h"
#include "Tracks/MovieSceneIntegerTrack.h"
#include "Tracks/MovieSceneByteTrack.h"
#include "Tracks/MovieSceneEnumTrack.h"
#include "Tracks/MovieSceneColorTrack.h"

#include "K2Node_WidgetAnimationEvent.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "EditorAssetLibrary.h"

#include "Framework/Application/SlateApplication.h"
#include "Fonts/SlateFontInfo.h"
#include "Layout/Margin.h"
#include "Slate/WidgetTransform.h"
#include "Styling/SlateColor.h"
#include "Types/SlateEnums.h"
#include "Input/NavigationReply.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectIterator.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

// ─────────────────────────────────────────────────────────────────────────────
// File-local helpers.
//
// The module is a unity build, so two .cpp files in one blob share their
// anonymous namespace and a duplicated helper is a redefinition (C2084). Every
// name below is prefixed WAnim_ / MCPWidgetAnim and exists nowhere else in the
// tree; nothing was copied out of WidgetHandlers.cpp.
// ─────────────────────────────────────────────────────────────────────────────
namespace MCPWidgetAnim
{

/** Every widget in the tree, root first, parents before children. */
static void WAnim_CollectWidgets(UWidgetTree* Tree, TArray<UWidget*>& Out)
{
	if (!Tree) return;
	Tree->ForEachWidget([&Out](UWidget* W) { if (W) Out.Add(W); });
}

static UWidget* WAnim_FindWidget(UWidgetTree* Tree, const FString& Name)
{
	if (!Tree) return nullptr;
	UWidget* Found = nullptr;
	Tree->ForEachWidget([&](UWidget* W)
	{
		if (W && !Found && W->GetName() == Name) Found = W;
	});
	return Found;
}

/** The names in the tree, for an error that says what was actually searched. */
static FString WAnim_WidgetNameList(UWidgetTree* Tree, int32 Max = 24)
{
	TArray<UWidget*> All;
	WAnim_CollectWidgets(Tree, All);
	TArray<FString> Names;
	for (UWidget* W : All)
	{
		if (Names.Num() >= Max) { Names.Add(TEXT("...")); break; }
		Names.Add(W->GetName());
	}
	return FString::Join(Names, TEXT(", "));
}

static UWidgetAnimation* WAnim_FindAnimation(UWidgetBlueprint* BP, const FString& Name)
{
	if (!BP) return nullptr;
	for (UWidgetAnimation* Anim : BP->Animations)
	{
		if (!Anim) continue;
		if (Anim->GetName() == Name) return Anim;
#if WITH_EDITOR
		if (Anim->GetDisplayLabel() == Name) return Anim;
#endif
	}
	return nullptr;
}

static FString WAnim_AnimationNameList(UWidgetBlueprint* BP)
{
	TArray<FString> Names;
	if (BP)
	{
		for (UWidgetAnimation* Anim : BP->Animations)
		{
			if (Anim) Names.Add(Anim->GetName());
		}
	}
	return Names.Num() ? FString::Join(Names, TEXT(", ")) : FString(TEXT("(none)"));
}

/**
 * Compile + save, the way every other widget mutation in this category ends.
 *
 * #728: the compile is where a widget variable with no entry in
 * WidgetVariableNameToGuidMap surfaces, as an engine failure report naming the
 * widget. An animation is a generated variable too, so creating one without an
 * entry drifts the map the same way a widget does. CompileChecked writes the
 * missing entries first and refuses to compile when it cannot, so this returns
 * the error to hand back instead of null, and stamps the bookkeeping onto the
 * result when it does compile.
 */
static TSharedPtr<FJsonValue> WAnim_CommitBlueprint(
	UWidgetBlueprint* BP,
	const FString& AssetPath,
	const TSharedPtr<FJsonObject>& Result)
{
	BP->MarkPackageDirty();

	const MCPWidgetGuidMap::FSyncReport GuidSync = MCPWidgetGuidMap::CompileChecked(BP);
	if (!GuidSync.bCompiled)
	{
		return MCPWidgetGuidMap::BlockedError(AssetPath, GuidSync);
	}

	UEditorAssetLibrary::SaveAsset(AssetPath);
	MCPSetWidgetGuidOutcome(Result, GuidSync, AssetPath);
	return nullptr;
}

/** Seconds -> frames on this MovieScene's tick resolution. */
static FFrameNumber WAnim_Frame(const UMovieScene* MovieScene, double Seconds)
{
	// FFrameRate has no operator* taking seconds on the right. AsFrameTime is
	// the engine's own seconds -> tick conversion, and RoundToFrame lands on the
	// nearest tick rather than flooring it the way AsFrameNumber does.
	return MovieScene->GetTickResolution().AsFrameTime(Seconds).RoundToFrame();
}

static double WAnim_Seconds(const UMovieScene* MovieScene, FFrameNumber Frame)
{
	return MovieScene->GetTickResolution().AsSeconds(Frame);
}

/**
 * Track class for a widget property, decided by the property's own reflected
 * type rather than by a caller-supplied string. Returns null and fills OutWhy
 * when the type has no UMG track, which is the answer a caller needs: the
 * property exists, Sequencer just cannot key it.
 */
static UClass* WAnim_TrackClassForProperty(FProperty* Prop, FString& OutWhy)
{
	if (!Prop) { OutWhy = TEXT("property not found"); return nullptr; }

	if (Prop->IsA<FFloatProperty>())   return UMovieSceneFloatTrack::StaticClass();
	if (Prop->IsA<FDoubleProperty>())  return UMovieSceneDoubleTrack::StaticClass();
	if (Prop->IsA<FBoolProperty>())    return UMovieSceneBoolTrack::StaticClass();
	if (Prop->IsA<FIntProperty>())     return UMovieSceneIntegerTrack::StaticClass();
	if (Prop->IsA<FEnumProperty>())    return UMovieSceneEnumTrack::StaticClass();

	if (FByteProperty* ByteProp = CastField<FByteProperty>(Prop))
	{
		return ByteProp->Enum ? UMovieSceneEnumTrack::StaticClass()
		                      : UMovieSceneByteTrack::StaticClass();
	}

	if (FStructProperty* StructProp = CastField<FStructProperty>(Prop))
	{
		UScriptStruct* S = StructProp->Struct;
		if (S == TBaseStructure<FLinearColor>::Get()
			|| S == TBaseStructure<FColor>::Get()
			|| S == FSlateColor::StaticStruct())
		{
			return UMovieSceneColorTrack::StaticClass();
		}
		if (S == FMargin::StaticStruct())          return UMovieSceneMarginTrack::StaticClass();
		if (S == FWidgetTransform::StaticStruct()) return UMovieScene2DTransformTrack::StaticClass();

		OutWhy = FString::Printf(
			TEXT("property is a %s struct, which has no UMG animation track. Keyable structs are ")
			TEXT("LinearColor, Color, SlateColor, Margin and WidgetTransform."),
			*S->GetName());
		return nullptr;
	}

	OutWhy = FString::Printf(
		TEXT("property type %s has no UMG animation track. Keyable types are float, double, bool, ")
		TEXT("int32, byte, enum, LinearColor/Color/SlateColor, Margin and WidgetTransform."),
		*Prop->GetClass()->GetName());
	return nullptr;
}

/** The possessable GUID this animation uses for a widget, or an invalid GUID. */
static FGuid WAnim_FindBindingGuid(UWidgetAnimation* Anim, const FName WidgetName)
{
	for (const FWidgetAnimationBinding& Binding : Anim->GetBindings())
	{
		if (Binding.WidgetName == WidgetName && Binding.SlotWidgetName.IsNone())
		{
			return Binding.AnimationGuid;
		}
	}
	return FGuid();
}

/**
 * Bind a widget into the animation, creating the possessable and the
 * FWidgetAnimationBinding if they are not there yet. bOutCreated says which.
 */
static FGuid WAnim_EnsureBinding(UWidgetAnimation* Anim, UWidget* Widget, UWidget* RootWidget, bool& bOutCreated)
{
	bOutCreated = false;
	const FGuid Existing = WAnim_FindBindingGuid(Anim, Widget->GetFName());
	if (Existing.IsValid()) return Existing;

	UMovieScene* MovieScene = Anim->GetMovieScene();
	const FGuid NewGuid = MovieScene->AddPossessable(Widget->GetName(), Widget->GetClass());

	FWidgetAnimationBinding NewBinding;
	NewBinding.WidgetName = Widget->GetFName();
	NewBinding.AnimationGuid = NewGuid;
	NewBinding.bIsRootWidget = (Widget == RootWidget);
	Anim->AnimationBindings.Add(NewBinding);

	bOutCreated = true;
	return NewGuid;
}

/** Channel names for one section, index-aligned with GetChannels<T>(). */
template <typename ChannelType>
static TArray<FString> WAnim_ChannelNames(UMovieSceneSection* Section)
{
	TArray<FString> Names;
#if WITH_EDITOR
	for (const FMovieSceneChannelMetaData& Meta : Section->GetChannelProxy().GetMetaData<ChannelType>())
	{
		Names.Add(Meta.Name.ToString());
	}
#endif
	return Names;
}

/** Resolve a channel by index, or by name when the caller gave one. */
template <typename ChannelType>
static ChannelType* WAnim_ResolveChannel(
	UMovieSceneSection* Section, const FString& ChannelName, int32 ChannelIndex, FString& OutAvailable)
{
	TArrayView<ChannelType*> Channels = Section->GetChannelProxy().GetChannels<ChannelType>();
	const TArray<FString> Names = WAnim_ChannelNames<ChannelType>(Section);
	OutAvailable = Names.Num() ? FString::Join(Names, TEXT(", "))
	                           : FString::Printf(TEXT("%d unnamed channels"), Channels.Num());

	if (!ChannelName.IsEmpty())
	{
		for (int32 i = 0; i < Names.Num() && i < Channels.Num(); ++i)
		{
			if (Names[i].Equals(ChannelName, ESearchCase::IgnoreCase)) return Channels[i];
		}
		return nullptr;
	}
	return Channels.IsValidIndex(ChannelIndex) ? Channels[ChannelIndex] : nullptr;
}

/** Is this widget one Slate will ever hand focus to? */
static bool WAnim_IsFocusable(UWidget* Widget)
{
	UClass* Class = Widget->GetClass();
	for (const TCHAR* Name : { TEXT("bIsFocusable"), TEXT("IsFocusable") })
	{
		if (FBoolProperty* Prop = FindFProperty<FBoolProperty>(Class, Name))
		{
			return Prop->GetPropertyValue_InContainer(Widget);
		}
	}
	return false;
}

/** Visible enough to receive focus. Collapsed, Hidden and HitTestInvisible are not. */
static bool WAnim_IsFocusVisible(UWidget* Widget)
{
	const ESlateVisibility Vis = Widget->GetVisibility();
	return Vis == ESlateVisibility::Visible || Vis == ESlateVisibility::SelfHitTestInvisible;
}

static const TCHAR* WAnim_VisibilityName(ESlateVisibility Vis)
{
	switch (Vis)
	{
	case ESlateVisibility::Visible:               return TEXT("Visible");
	case ESlateVisibility::Collapsed:             return TEXT("Collapsed");
	case ESlateVisibility::Hidden:                return TEXT("Hidden");
	case ESlateVisibility::HitTestInvisible:      return TEXT("HitTestInvisible");
	case ESlateVisibility::SelfHitTestInvisible:  return TEXT("SelfHitTestInvisible");
	default:                                      return TEXT("Unknown");
	}
}

/** The six navigation directions, in the order the JSON uses. */
static const TCHAR* WAnim_DirectionNames[] = { TEXT("Up"), TEXT("Down"), TEXT("Left"), TEXT("Right"), TEXT("Next"), TEXT("Previous") };

static bool WAnim_IsDirection(const FString& Direction)
{
	for (const TCHAR* Dir : WAnim_DirectionNames)
	{
		if (Direction.Equals(Dir, ESearchCase::IgnoreCase)) return true;
	}
	return false;
}

static FWidgetNavigationData* WAnim_DirectionData(UWidgetNavigation* Nav, const FString& Direction)
{
	if (!Nav) return nullptr;
	if (Direction.Equals(TEXT("Up"),       ESearchCase::IgnoreCase)) return &Nav->Up;
	if (Direction.Equals(TEXT("Down"),     ESearchCase::IgnoreCase)) return &Nav->Down;
	if (Direction.Equals(TEXT("Left"),     ESearchCase::IgnoreCase)) return &Nav->Left;
	if (Direction.Equals(TEXT("Right"),    ESearchCase::IgnoreCase)) return &Nav->Right;
	if (Direction.Equals(TEXT("Next"),     ESearchCase::IgnoreCase)) return &Nav->Next;
	if (Direction.Equals(TEXT("Previous"), ESearchCase::IgnoreCase)) return &Nav->Previous;
	return nullptr;
}

static bool WAnim_ParseRule(const FString& In, EUINavigationRule& Out)
{
	if (In.Equals(TEXT("Escape"),         ESearchCase::IgnoreCase)) { Out = EUINavigationRule::Escape;         return true; }
	if (In.Equals(TEXT("Explicit"),       ESearchCase::IgnoreCase)) { Out = EUINavigationRule::Explicit;       return true; }
	if (In.Equals(TEXT("Wrap"),           ESearchCase::IgnoreCase)) { Out = EUINavigationRule::Wrap;           return true; }
	if (In.Equals(TEXT("Stop"),           ESearchCase::IgnoreCase)) { Out = EUINavigationRule::Stop;           return true; }
	if (In.Equals(TEXT("Custom"),         ESearchCase::IgnoreCase)) { Out = EUINavigationRule::Custom;         return true; }
	if (In.Equals(TEXT("CustomBoundary"), ESearchCase::IgnoreCase)) { Out = EUINavigationRule::CustomBoundary; return true; }
	return false;
}

/** The direction that should bring focus back. Empty when there is no pair. */
static FString WAnim_OppositeDirection(const FString& Direction)
{
	if (Direction.Equals(TEXT("Up"),       ESearchCase::IgnoreCase)) return TEXT("Down");
	if (Direction.Equals(TEXT("Down"),     ESearchCase::IgnoreCase)) return TEXT("Up");
	if (Direction.Equals(TEXT("Left"),     ESearchCase::IgnoreCase)) return TEXT("Right");
	if (Direction.Equals(TEXT("Right"),    ESearchCase::IgnoreCase)) return TEXT("Left");
	if (Direction.Equals(TEXT("Next"),     ESearchCase::IgnoreCase)) return TEXT("Previous");
	if (Direction.Equals(TEXT("Previous"), ESearchCase::IgnoreCase)) return TEXT("Next");
	return FString();
}

static const TCHAR* WAnim_RuleName(EUINavigationRule Rule)
{
	switch (Rule)
	{
	case EUINavigationRule::Escape:         return TEXT("Escape");
	case EUINavigationRule::Explicit:       return TEXT("Explicit");
	case EUINavigationRule::Wrap:           return TEXT("Wrap");
	case EUINavigationRule::Stop:           return TEXT("Stop");
	case EUINavigationRule::Custom:         return TEXT("Custom");
	case EUINavigationRule::CustomBoundary: return TEXT("CustomBoundary");
	default:                                return TEXT("Unknown");
	}
}

/** The current rules on a widget, as JSON, so a rollback can restore them exactly. */
static TSharedPtr<FJsonObject> WAnim_CaptureNavigation(UWidget* Widget)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	UWidgetNavigation* Nav = Widget->Navigation;
	Obj->SetBoolField(TEXT("hadNavigationObject"), Nav != nullptr);
	if (!Nav) return Obj;

	for (const TCHAR* Dir : WAnim_DirectionNames)
	{
		const FWidgetNavigationData* Data = WAnim_DirectionData(Nav, Dir);
		if (!Data) continue;
		TSharedPtr<FJsonObject> D = MakeShared<FJsonObject>();
		D->SetStringField(TEXT("rule"), WAnim_RuleName(Data->Rule));
		D->SetStringField(TEXT("widgetToFocus"), Data->WidgetToFocus.ToString());
		Obj->SetObjectField(Dir, D);
	}
	return Obj;
}

/** Every FSlateFontInfo reachable from this widget, with the path that found it. */
static void WAnim_CollectFonts(
	const void* Container, UStruct* Struct, const FString& Prefix, int32 Depth,
	TArray<TPair<FString, const FSlateFontInfo*>>& Out)
{
	if (Depth > 3 || !Struct || !Container) return;
	for (TFieldIterator<FProperty> It(Struct); It; ++It)
	{
		FStructProperty* StructProp = CastField<FStructProperty>(*It);
		if (!StructProp) continue;
		const void* Value = StructProp->ContainerPtrToValuePtr<void>(Container);
		const FString Path = Prefix.IsEmpty() ? StructProp->GetName() : Prefix + TEXT(".") + StructProp->GetName();

		if (StructProp->Struct == FSlateFontInfo::StaticStruct())
		{
			Out.Add(TPair<FString, const FSlateFontInfo*>(Path, static_cast<const FSlateFontInfo*>(Value)));
			continue;
		}
		WAnim_CollectFonts(Value, StructProp->Struct, Path, Depth + 1, Out);
	}
}

/** The PIE / game world, or null. The audits below only read it. */
static UWorld* WAnim_RuntimeWorld()
{
	if (!GEngine) return nullptr;
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		if ((Context.WorldType == EWorldType::PIE || Context.WorldType == EWorldType::Game) && Context.World())
		{
			return Context.World();
		}
	}
	return nullptr;
}

} // namespace MCPWidgetAnim

using namespace MCPWidgetAnim;

// ═════════════════════════════════════════════════════════════════════════════
// Ticket A - UMG animation authoring
// ═════════════════════════════════════════════════════════════════════════════

TSharedPtr<FJsonValue> FWidgetHandlers::CreateWidgetAnimation(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	FString AnimationName;
	if (auto Err = RequireString(Params, TEXT("animationName"), AnimationName)) return Err;

	TSharedPtr<FJsonValue> ResolveError;
	UWidgetBlueprint* WidgetBP = MCPWidget::ResolveWidgetBlueprintOrError(AssetPath, ResolveError);
	if (!WidgetBP) return ResolveError;

	const double DurationSeconds = OptionalNumber(Params, TEXT("durationSeconds"), 1.0);
	const double DisplayRateFps  = OptionalNumber(Params, TEXT("displayRate"), 60.0);
	const FString DisplayLabel   = OptionalString(Params, TEXT("displayLabel"), AnimationName);

	if (DurationSeconds <= 0.0)
	{
		return MCPError(FString::Printf(
			TEXT("durationSeconds must be greater than 0 (got %f). A zero-length animation has no ")
			TEXT("playback range to key into."), DurationSeconds));
	}

	// ── Idempotency: an animation of this name already on the asset is a replay.
	if (UWidgetAnimation* Existing = WAnim_FindAnimation(WidgetBP, AnimationName))
	{
		auto Res = MCPSuccess();
		MCPSetExisted(Res);
		Res->SetStringField(TEXT("animationName"), Existing->GetName());
		Res->SetStringField(TEXT("assetPath"), AssetPath);
		Res->SetStringField(TEXT("hint"), TEXT("Read it back with widget(get_animation)."));
		return MCPResult(Res);
	}

	// ── Create. Outered to the Widget Blueprint, transactional, like the designer's own.
	const FName UniqueName = MakeUniqueObjectName(WidgetBP, UWidgetAnimation::StaticClass(), FName(*AnimationName));
	UWidgetAnimation* NewAnim = NewObject<UWidgetAnimation>(WidgetBP, UniqueName, RF_Transactional);
	if (!NewAnim)
	{
		return MCPError(FString::Printf(
			TEXT("NewObject<UWidgetAnimation> returned null for '%s' on %s."), *AnimationName, *AssetPath));
	}
#if WITH_EDITOR
	NewAnim->SetDisplayLabel(DisplayLabel);
#endif

	UMovieScene* MovieScene = NewObject<UMovieScene>(NewAnim, UniqueName, RF_Transactional);
	NewAnim->MovieScene = MovieScene;

	// Tick resolution comes from UMovieScene::PostInitProperties (project
	// settings). Only the display rate and the playback range are ours to set,
	// and the range is computed on whatever tick resolution the engine chose.
	MovieScene->SetDisplayRate(FFrameRate(FMath::Max(1, FMath::RoundToInt(DisplayRateFps)), 1));
	MovieScene->SetPlaybackRange(FFrameNumber(0), (WAnim_Frame(MovieScene, DurationSeconds)).Value);

	WidgetBP->Animations.Add(NewAnim);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);

	// Rollback: the inverse is deleting it. Captured before the commit so the
	// payload names the object that actually reached disk.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	Payload->SetStringField(TEXT("animationName"), NewAnim->GetName());
	MCPSetRollback(Result, TEXT("delete_widget_animation"), Payload);

	if (auto Err = WAnim_CommitBlueprint(WidgetBP, AssetPath, Result)) return Err;

	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("animationName"), NewAnim->GetName());
	Result->SetStringField(TEXT("requestedName"), AnimationName);
	Result->SetStringField(TEXT("displayLabel"), DisplayLabel);
	Result->SetNumberField(TEXT("durationSeconds"), DurationSeconds);
	Result->SetNumberField(TEXT("displayRate"), MovieScene->GetDisplayRate().AsDecimal());
	Result->SetNumberField(TEXT("tickResolution"), MovieScene->GetTickResolution().AsDecimal());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FWidgetHandlers::DeleteWidgetAnimation(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	FString AnimationName;
	if (auto Err = RequireString(Params, TEXT("animationName"), AnimationName)) return Err;

	TSharedPtr<FJsonValue> ResolveError;
	UWidgetBlueprint* WidgetBP = MCPWidget::ResolveWidgetBlueprintOrError(AssetPath, ResolveError);
	if (!WidgetBP) return ResolveError;

	UWidgetAnimation* Anim = WAnim_FindAnimation(WidgetBP, AnimationName);
	if (!Anim)
	{
		auto Res = MCPSuccess();
		Res->SetBoolField(TEXT("alreadyDeleted"), true);
		Res->SetStringField(TEXT("animationName"), AnimationName);
		Res->SetStringField(TEXT("searchedAnimations"), WAnim_AnimationNameList(WidgetBP));
		return MCPResult(Res);
	}

	// Capture what the inverse can restore BEFORE mutating.
	double DurationSeconds = 0.0;
	double DisplayRate = 60.0;
	int32 TrackCount = 0;
	if (UMovieScene* MovieScene = Anim->GetMovieScene())
	{
		const TRange<FFrameNumber> Range = MovieScene->GetPlaybackRange();
		if (Range.HasLowerBound() && Range.HasUpperBound())
		{
			DurationSeconds = WAnim_Seconds(MovieScene, Range.GetUpperBoundValue())
				- WAnim_Seconds(MovieScene, Range.GetLowerBoundValue());
		}
		DisplayRate = MovieScene->GetDisplayRate().AsDecimal();
		TrackCount = MovieScene->GetTracks().Num();
		for (const FMovieSceneBinding& Binding : const_cast<const UMovieScene*>(MovieScene)->GetBindings())
		{
			TrackCount += Binding.GetTracks().Num();
		}
	}
	const FString RemovedName = Anim->GetName();

	WidgetBP->Animations.Remove(Anim);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetBoolField(TEXT("alreadyDeleted"), false);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	Payload->SetStringField(TEXT("animationName"), RemovedName);
	Payload->SetNumberField(TEXT("durationSeconds"), DurationSeconds);
	Payload->SetNumberField(TEXT("displayRate"), DisplayRate);
	MCPSetRollback(Result, TEXT("create_widget_animation"), Payload);

	// Said plainly rather than implied: the inverse rebuilds an EMPTY animation
	// of the same name, duration and rate. Tracks, sections and keys are gone.
	Result->SetBoolField(TEXT("rollbackIsLossy"), true);
	Result->SetStringField(TEXT("rollbackRestores"),
		FString::Printf(TEXT("An empty animation of the same name, duration and display rate. The %d ")
			TEXT("track(s), their sections and every key are NOT restored - re-author them with ")
			TEXT("widget(add_animation_track) and widget(add_animation_key)."), TrackCount));

	if (auto Err = WAnim_CommitBlueprint(WidgetBP, AssetPath, Result)) return Err;

	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("animationName"), RemovedName);
	Result->SetNumberField(TEXT("removedTrackCount"), TrackCount);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FWidgetHandlers::GetWidgetAnimation(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	FString AnimationName;
	if (auto Err = RequireString(Params, TEXT("animationName"), AnimationName)) return Err;

	TSharedPtr<FJsonValue> ResolveError;
	UWidgetBlueprint* WidgetBP = MCPWidget::ResolveWidgetBlueprintOrError(AssetPath, ResolveError);
	if (!WidgetBP) return ResolveError;

	UWidgetAnimation* Anim = WAnim_FindAnimation(WidgetBP, AnimationName);
	if (!Anim)
	{
		return MCPError(FString::Printf(
			TEXT("Animation '%s' not found on %s. Searched Animations by object name and display label; ")
			TEXT("the asset has: %s. List them with widget(read_animations), create one with ")
			TEXT("widget(create_animation)."),
			*AnimationName, *AssetPath, *WAnim_AnimationNameList(WidgetBP)));
	}

	UMovieScene* MovieScene = Anim->GetMovieScene();
	if (!MovieScene)
	{
		return MCPError(FString::Printf(
			TEXT("Animation '%s' on %s has no MovieScene. The asset is malformed; delete the animation ")
			TEXT("with widget(delete_animation) and recreate it."), *AnimationName, *AssetPath));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("animationName"), Anim->GetName());
#if WITH_EDITOR
	Result->SetStringField(TEXT("displayLabel"), Anim->GetDisplayLabel());
#endif
	Result->SetNumberField(TEXT("displayRate"), MovieScene->GetDisplayRate().AsDecimal());
	Result->SetNumberField(TEXT("tickResolution"), MovieScene->GetTickResolution().AsDecimal());

	const TRange<FFrameNumber> Range = MovieScene->GetPlaybackRange();
	if (Range.HasLowerBound() && Range.HasUpperBound())
	{
		Result->SetNumberField(TEXT("startTime"), WAnim_Seconds(MovieScene, Range.GetLowerBoundValue()));
		Result->SetNumberField(TEXT("endTime"), WAnim_Seconds(MovieScene, Range.GetUpperBoundValue()));
	}

	// One entry per bound widget, each with its tracks, sections, channels and
	// key times in SECONDS. read_animations reports the shape; this reports the
	// values, which is what you need to verify a key actually landed.
	TArray<TSharedPtr<FJsonValue>> BindingsJson;
	for (const FWidgetAnimationBinding& Binding : Anim->GetBindings())
	{
		TSharedPtr<FJsonObject> BindingObj = MakeShared<FJsonObject>();
		BindingObj->SetStringField(TEXT("widgetName"), Binding.WidgetName.ToString());
		BindingObj->SetStringField(TEXT("slotWidgetName"), Binding.SlotWidgetName.ToString());
		BindingObj->SetStringField(TEXT("bindingId"), Binding.AnimationGuid.ToString());
		BindingObj->SetBoolField(TEXT("isRootWidget"), Binding.bIsRootWidget);

		TArray<TSharedPtr<FJsonValue>> TracksJson;
		for (const FMovieSceneBinding& SceneBinding : const_cast<const UMovieScene*>(MovieScene)->GetBindings())
		{
			if (SceneBinding.GetObjectGuid() != Binding.AnimationGuid) continue;
			for (UMovieSceneTrack* Track : SceneBinding.GetTracks())
			{
				if (!Track) continue;
				TSharedPtr<FJsonObject> TrackObj = MakeShared<FJsonObject>();
				TrackObj->SetStringField(TEXT("trackClass"), Track->GetClass()->GetName());
				if (UMovieScenePropertyTrack* PropTrack = Cast<UMovieScenePropertyTrack>(Track))
				{
					TrackObj->SetStringField(TEXT("propertyName"), PropTrack->GetPropertyName().ToString());
					TrackObj->SetStringField(TEXT("propertyPath"), PropTrack->GetPropertyPath().ToString());
				}

				TArray<TSharedPtr<FJsonValue>> SectionsJson;
				for (UMovieSceneSection* Section : Track->GetAllSections())
				{
					if (!Section) continue;
					TSharedPtr<FJsonObject> SectionObj = MakeShared<FJsonObject>();
					const TRange<FFrameNumber> SR = Section->GetRange();
					if (SR.HasLowerBound()) SectionObj->SetNumberField(TEXT("startTime"), WAnim_Seconds(MovieScene, SR.GetLowerBoundValue()));
					if (SR.HasUpperBound()) SectionObj->SetNumberField(TEXT("endTime"), WAnim_Seconds(MovieScene, SR.GetUpperBoundValue()));
					SectionObj->SetNumberField(TEXT("rowIndex"), Section->GetRowIndex());

					TArray<TSharedPtr<FJsonValue>> ChannelsJson;
					const TArray<FString> FloatNames = WAnim_ChannelNames<FMovieSceneFloatChannel>(Section);
					TArrayView<FMovieSceneFloatChannel*> FloatChannels =
						Section->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>();
					for (int32 i = 0; i < FloatChannels.Num(); ++i)
					{
						TSharedPtr<FJsonObject> ChannelObj = MakeShared<FJsonObject>();
						ChannelObj->SetNumberField(TEXT("index"), i);
						ChannelObj->SetStringField(TEXT("name"), FloatNames.IsValidIndex(i) ? FloatNames[i] : FString());
						ChannelObj->SetStringField(TEXT("type"), TEXT("float"));
						TArray<TSharedPtr<FJsonValue>> KeysJson;
						TMovieSceneChannelData<FMovieSceneFloatValue> Data = FloatChannels[i]->GetData();
						TArrayView<const FFrameNumber> Times = Data.GetTimes();
						TArrayView<const FMovieSceneFloatValue> Values = Data.GetValues();
						for (int32 k = 0; k < Times.Num(); ++k)
						{
							TSharedPtr<FJsonObject> KeyObj = MakeShared<FJsonObject>();
							KeyObj->SetNumberField(TEXT("time"), WAnim_Seconds(MovieScene, Times[k]));
							KeyObj->SetNumberField(TEXT("value"), Values.IsValidIndex(k) ? Values[k].Value : 0.f);
							KeysJson.Add(MakeShared<FJsonValueObject>(KeyObj));
						}
						ChannelObj->SetArrayField(TEXT("keys"), KeysJson);
						ChannelsJson.Add(MakeShared<FJsonValueObject>(ChannelObj));
					}
					SectionObj->SetArrayField(TEXT("channels"), ChannelsJson);
					SectionsJson.Add(MakeShared<FJsonValueObject>(SectionObj));
				}
				TrackObj->SetArrayField(TEXT("sections"), SectionsJson);
				TracksJson.Add(MakeShared<FJsonValueObject>(TrackObj));
			}
		}
		BindingObj->SetArrayField(TEXT("tracks"), TracksJson);
		BindingsJson.Add(MakeShared<FJsonValueObject>(BindingObj));
	}
	Result->SetArrayField(TEXT("bindings"), BindingsJson);

	// Event tracks are not bound to a widget, so they live on the MovieScene.
	TArray<TSharedPtr<FJsonValue>> EventTracksJson;
	for (UMovieSceneTrack* Track : MovieScene->GetTracks())
	{
		UMovieSceneEventTrack* EventTrack = Cast<UMovieSceneEventTrack>(Track);
		if (!EventTrack) continue;
		TSharedPtr<FJsonObject> TrackObj = MakeShared<FJsonObject>();
		TrackObj->SetStringField(TEXT("trackName"), EventTrack->GetDisplayName().ToString());
		TArray<TSharedPtr<FJsonValue>> EventsJson;
		for (UMovieSceneSection* Section : EventTrack->GetAllSections())
		{
			UMovieSceneEventTriggerSection* Trigger = Cast<UMovieSceneEventTriggerSection>(Section);
			if (!Trigger) continue;
			TMovieSceneChannelData<FMovieSceneEvent> Data = Trigger->EventChannel.GetData();
			TArrayView<const FFrameNumber> Times = Data.GetTimes();
			TArrayView<const FMovieSceneEvent> Values = Data.GetValues();
			for (int32 k = 0; k < Times.Num(); ++k)
			{
				TSharedPtr<FJsonObject> EventObj = MakeShared<FJsonObject>();
				EventObj->SetNumberField(TEXT("time"), WAnim_Seconds(MovieScene, Times[k]));
				const UFunction* Fn = Values.IsValidIndex(k) ? Values[k].Ptrs.Function.Get() : nullptr;
				EventObj->SetStringField(TEXT("functionName"), Fn ? Fn->GetName() : FString());
				EventObj->SetBoolField(TEXT("bound"), Fn != nullptr);
				EventsJson.Add(MakeShared<FJsonValueObject>(EventObj));
			}
		}
		TrackObj->SetArrayField(TEXT("events"), EventsJson);
		EventTracksJson.Add(MakeShared<FJsonValueObject>(TrackObj));
	}
	Result->SetArrayField(TEXT("eventTracks"), EventTracksJson);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FWidgetHandlers::AddWidgetAnimationTrack(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	FString AnimationName;
	if (auto Err = RequireString(Params, TEXT("animationName"), AnimationName)) return Err;

	FString WidgetName;
	if (auto Err = RequireString(Params, TEXT("widgetName"), WidgetName)) return Err;

	FString PropertyName;
	if (auto Err = RequireString(Params, TEXT("propertyName"), PropertyName)) return Err;

	TSharedPtr<FJsonValue> ResolveError;
	UWidgetBlueprint* WidgetBP = MCPWidget::ResolveWidgetBlueprintOrError(AssetPath, ResolveError);
	if (!WidgetBP) return ResolveError;
	if (!WidgetBP->WidgetTree) return MCPWidget::MissingWidgetTreeError(AssetPath);

	UWidgetAnimation* Anim = WAnim_FindAnimation(WidgetBP, AnimationName);
	if (!Anim)
	{
		return MCPError(FString::Printf(
			TEXT("Animation '%s' not found on %s. The asset has: %s. Create it with ")
			TEXT("widget(create_animation) first."),
			*AnimationName, *AssetPath, *WAnim_AnimationNameList(WidgetBP)));
	}

	UWidget* Widget = WAnim_FindWidget(WidgetBP->WidgetTree, WidgetName);
	if (!Widget)
	{
		return MCPError(FString::Printf(
			TEXT("Widget '%s' not found in the tree of %s. Searched every widget by object name; the ")
			TEXT("tree has: %s. List it with widget(read_tree)."),
			*WidgetName, *AssetPath, *WAnim_WidgetNameList(WidgetBP->WidgetTree)));
	}

	FProperty* Prop = FindFProperty<FProperty>(Widget->GetClass(), *PropertyName);
	if (!Prop)
	{
		return MCPError(FString::Printf(
			TEXT("Property '%s' not found on %s ('%s'). Searched the reflected properties of that class ")
			TEXT("and its supers. Dump them with widget(get_properties)."),
			*PropertyName, *Widget->GetClass()->GetName(), *WidgetName));
	}

	FString Why;
	UClass* TrackClass = WAnim_TrackClassForProperty(Prop, Why);
	if (!TrackClass)
	{
		return MCPError(FString::Printf(
			TEXT("Cannot animate '%s' on '%s': %s"), *PropertyName, *WidgetName, *Why));
	}

	UMovieScene* MovieScene = Anim->GetMovieScene();
	if (!MovieScene)
	{
		return MCPError(FString::Printf(
			TEXT("Animation '%s' on %s has no MovieScene."), *AnimationName, *AssetPath));
	}

	// ── Idempotency: a track of this class already keying this property on this
	// binding is a replay, not a second track.
	const FGuid ExistingGuid = WAnim_FindBindingGuid(Anim, Widget->GetFName());
	if (ExistingGuid.IsValid())
	{
		for (UMovieSceneTrack* Track : MovieScene->FindTracks(TrackClass, ExistingGuid))
		{
			UMovieScenePropertyTrack* PropTrack = Cast<UMovieScenePropertyTrack>(Track);
			if (PropTrack && PropTrack->GetPropertyName() == FName(*PropertyName))
			{
				auto Res = MCPSuccess();
				MCPSetExisted(Res);
				Res->SetStringField(TEXT("assetPath"), AssetPath);
				Res->SetStringField(TEXT("animationName"), Anim->GetName());
				Res->SetStringField(TEXT("widgetName"), WidgetName);
				Res->SetStringField(TEXT("propertyName"), PropertyName);
				Res->SetStringField(TEXT("trackClass"), TrackClass->GetName());
				Res->SetStringField(TEXT("bindingId"), ExistingGuid.ToString());
				Res->SetNumberField(TEXT("sectionCount"), PropTrack->GetAllSections().Num());
				return MCPResult(Res);
			}
		}
	}

	UWidget* RootWidget = WidgetBP->WidgetTree->RootWidget;
	bool bBindingCreated = false;
	const FGuid BindingGuid = WAnim_EnsureBinding(Anim, Widget, RootWidget, bBindingCreated);
	if (!BindingGuid.IsValid())
	{
		return MCPError(FString::Printf(
			TEXT("UMovieScene::AddPossessable returned an invalid GUID for widget '%s' in animation '%s'."),
			*WidgetName, *AnimationName));
	}

	UMovieSceneTrack* NewTrack = MovieScene->AddTrack(TrackClass, BindingGuid);
	if (!NewTrack)
	{
		return MCPError(FString::Printf(
			TEXT("UMovieScene::AddTrack refused %s on animation '%s'. UWidgetAnimation rejects track ")
			TEXT("classes it does not support; check widget(get_animation) for what it holds."),
			*TrackClass->GetName(), *AnimationName));
	}
	if (UMovieScenePropertyTrack* PropTrack = Cast<UMovieScenePropertyTrack>(NewTrack))
	{
		PropTrack->SetPropertyNameAndPath(FName(*PropertyName), PropertyName);
	}

	// One section spanning the playback range, so the first key has somewhere to land.
	UMovieSceneSection* Section = NewTrack->CreateNewSection();
	if (!Section)
	{
		return MCPError(FString::Printf(
			TEXT("%s::CreateNewSection returned null on animation '%s'."), *TrackClass->GetName(), *AnimationName));
	}
	Section->SetRange(MovieScene->GetPlaybackRange());
	Section->SetRowIndex(0);
	NewTrack->AddSection(*Section);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	Payload->SetStringField(TEXT("animationName"), Anim->GetName());
	Payload->SetStringField(TEXT("widgetName"), WidgetName);
	Payload->SetStringField(TEXT("propertyName"), PropertyName);
	MCPSetRollback(Result, TEXT("remove_widget_animation_track"), Payload);

	if (auto Err = WAnim_CommitBlueprint(WidgetBP, AssetPath, Result)) return Err;

	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("animationName"), Anim->GetName());
	Result->SetStringField(TEXT("widgetName"), WidgetName);
	Result->SetStringField(TEXT("propertyName"), PropertyName);
	Result->SetStringField(TEXT("trackClass"), TrackClass->GetName());
	Result->SetStringField(TEXT("bindingId"), BindingGuid.ToString());
	Result->SetBoolField(TEXT("bindingCreated"), bBindingCreated);

	// Channel names are how the caller addresses a key next; report them here so
	// add_animation_key never has to be guessed at.
	const TArray<FString> FloatNames = WAnim_ChannelNames<FMovieSceneFloatChannel>(Section);
	TArray<TSharedPtr<FJsonValue>> ChannelJson;
	for (const FString& Name : FloatNames) ChannelJson.Add(MakeShared<FJsonValueString>(Name));
	Result->SetArrayField(TEXT("channels"), ChannelJson);
	Result->SetNumberField(TEXT("floatChannelCount"),
		Section->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>().Num());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FWidgetHandlers::RemoveWidgetAnimationTrack(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	FString AnimationName;
	if (auto Err = RequireString(Params, TEXT("animationName"), AnimationName)) return Err;

	FString WidgetName;
	if (auto Err = RequireString(Params, TEXT("widgetName"), WidgetName)) return Err;

	FString PropertyName;
	if (auto Err = RequireString(Params, TEXT("propertyName"), PropertyName)) return Err;

	TSharedPtr<FJsonValue> ResolveError;
	UWidgetBlueprint* WidgetBP = MCPWidget::ResolveWidgetBlueprintOrError(AssetPath, ResolveError);
	if (!WidgetBP) return ResolveError;

	UWidgetAnimation* Anim = WAnim_FindAnimation(WidgetBP, AnimationName);
	if (!Anim || !Anim->GetMovieScene())
	{
		return MCPError(FString::Printf(
			TEXT("Animation '%s' not found on %s (or it has no MovieScene). The asset has: %s."),
			*AnimationName, *AssetPath, *WAnim_AnimationNameList(WidgetBP)));
	}

	UMovieScene* MovieScene = Anim->GetMovieScene();
	const FGuid BindingGuid = WAnim_FindBindingGuid(Anim, FName(*WidgetName));

	UMovieSceneTrack* Target = nullptr;
	if (BindingGuid.IsValid())
	{
		for (const FMovieSceneBinding& SceneBinding : const_cast<const UMovieScene*>(MovieScene)->GetBindings())
		{
			if (SceneBinding.GetObjectGuid() != BindingGuid) continue;
			for (UMovieSceneTrack* Track : SceneBinding.GetTracks())
			{
				UMovieScenePropertyTrack* PropTrack = Cast<UMovieScenePropertyTrack>(Track);
				if (PropTrack && PropTrack->GetPropertyName() == FName(*PropertyName)) { Target = Track; break; }
			}
			if (Target) break;
		}
	}

	if (!Target)
	{
		auto Res = MCPSuccess();
		Res->SetBoolField(TEXT("alreadyRemoved"), true);
		Res->SetStringField(TEXT("animationName"), AnimationName);
		Res->SetStringField(TEXT("widgetName"), WidgetName);
		Res->SetStringField(TEXT("propertyName"), PropertyName);
		Res->SetStringField(TEXT("searched"),
			BindingGuid.IsValid()
				? TEXT("the widget is bound but has no property track of that name")
				: TEXT("the widget is not bound into this animation at all"));
		return MCPResult(Res);
	}

	// Capture before mutating.
	int32 KeyCount = 0;
	for (UMovieSceneSection* Section : Target->GetAllSections())
	{
		if (!Section) continue;
		for (FMovieSceneFloatChannel* Channel : Section->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>())
		{
			KeyCount += Channel->GetNumKeys();
		}
	}
	const FString TrackClassName = Target->GetClass()->GetName();

	MovieScene->RemoveTrack(*Target);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetBoolField(TEXT("alreadyRemoved"), false);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	Payload->SetStringField(TEXT("animationName"), Anim->GetName());
	Payload->SetStringField(TEXT("widgetName"), WidgetName);
	Payload->SetStringField(TEXT("propertyName"), PropertyName);
	MCPSetRollback(Result, TEXT("add_widget_animation_track"), Payload);
	Result->SetBoolField(TEXT("rollbackIsLossy"), true);
	Result->SetStringField(TEXT("rollbackRestores"),
		FString::Printf(TEXT("An empty track and section for the same property. The %d key(s) on it are ")
			TEXT("NOT restored - re-add them with widget(add_animation_key)."), KeyCount));

	if (auto Err = WAnim_CommitBlueprint(WidgetBP, AssetPath, Result)) return Err;

	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("trackClass"), TrackClassName);
	Result->SetNumberField(TEXT("removedKeyCount"), KeyCount);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FWidgetHandlers::AddWidgetAnimationKey(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	FString AnimationName;
	if (auto Err = RequireString(Params, TEXT("animationName"), AnimationName)) return Err;

	FString WidgetName;
	if (auto Err = RequireString(Params, TEXT("widgetName"), WidgetName)) return Err;

	FString PropertyName;
	if (auto Err = RequireString(Params, TEXT("propertyName"), PropertyName)) return Err;

	if (!Params->HasField(TEXT("time")))
	{
		return MCPError(TEXT("Missing required parameter 'time' (seconds from the start of the animation)."));
	}
	if (!Params->HasField(TEXT("value")))
	{
		return MCPError(TEXT("Missing required parameter 'value' (the keyed number)."));
	}

	const double Time = OptionalNumber(Params, TEXT("time"), 0.0);
	const double Value = OptionalNumber(Params, TEXT("value"), 0.0);
	const FString ChannelName = OptionalString(Params, TEXT("channel"));
	const int32 ChannelIndex = OptionalInt(Params, TEXT("channelIndex"), 0);
	const FString Interp = OptionalString(Params, TEXT("interpolation"), TEXT("cubic"));

	TSharedPtr<FJsonValue> ResolveError;
	UWidgetBlueprint* WidgetBP = MCPWidget::ResolveWidgetBlueprintOrError(AssetPath, ResolveError);
	if (!WidgetBP) return ResolveError;

	UWidgetAnimation* Anim = WAnim_FindAnimation(WidgetBP, AnimationName);
	if (!Anim || !Anim->GetMovieScene())
	{
		return MCPError(FString::Printf(
			TEXT("Animation '%s' not found on %s (or it has no MovieScene). The asset has: %s."),
			*AnimationName, *AssetPath, *WAnim_AnimationNameList(WidgetBP)));
	}
	UMovieScene* MovieScene = Anim->GetMovieScene();

	const FGuid BindingGuid = WAnim_FindBindingGuid(Anim, FName(*WidgetName));
	UMovieSceneSection* Section = nullptr;
	if (BindingGuid.IsValid())
	{
		for (const FMovieSceneBinding& SceneBinding : const_cast<const UMovieScene*>(MovieScene)->GetBindings())
		{
			if (SceneBinding.GetObjectGuid() != BindingGuid) continue;
			for (UMovieSceneTrack* Track : SceneBinding.GetTracks())
			{
				UMovieScenePropertyTrack* PropTrack = Cast<UMovieScenePropertyTrack>(Track);
				if (!PropTrack || PropTrack->GetPropertyName() != FName(*PropertyName)) continue;
				for (UMovieSceneSection* Candidate : PropTrack->GetAllSections())
				{
					if (Candidate) { Section = Candidate; break; }
				}
				if (Section) break;
			}
			if (Section) break;
		}
	}

	if (!Section)
	{
		return MCPError(FString::Printf(
			TEXT("No section for property '%s' on widget '%s' in animation '%s'. Searched the tracks of ")
			TEXT("that widget's binding%s. Call widget(add_animation_track) with the same assetPath, ")
			TEXT("animationName, widgetName and propertyName first."),
			*PropertyName, *WidgetName, *AnimationName,
			BindingGuid.IsValid() ? TEXT("") : TEXT(" - the widget is not bound into this animation")));
	}

	FString Available;
	FMovieSceneFloatChannel* Channel =
		WAnim_ResolveChannel<FMovieSceneFloatChannel>(Section, ChannelName, ChannelIndex, Available);
	if (!Channel)
	{
		return MCPError(FString::Printf(
			TEXT("No float channel %s on the '%s' section. Available channels: %s. Pass `channel` by name ")
			TEXT("or `channelIndex`; widget(get_animation) lists them."),
			ChannelName.IsEmpty() ? *FString::Printf(TEXT("at index %d"), ChannelIndex) : *FString::Printf(TEXT("named '%s'"), *ChannelName),
			*PropertyName, *Available));
	}

	const FFrameNumber Frame = WAnim_Frame(MovieScene, Time);

	// Idempotency: a key already at this frame is updated in place, not duplicated.
	TMovieSceneChannelData<FMovieSceneFloatValue> Data = Channel->GetData();
	const int32 ExistingIndex = Data.FindKey(Frame);
	const bool bExisted = ExistingIndex != INDEX_NONE;
	const float PreviousValue = bExisted ? Data.GetValues()[ExistingIndex].Value : 0.f;

	if (bExisted)
	{
		Data.RemoveKey(ExistingIndex);
	}
	if (Interp.Equals(TEXT("linear"), ESearchCase::IgnoreCase))        Channel->AddLinearKey(Frame, static_cast<float>(Value));
	else if (Interp.Equals(TEXT("constant"), ESearchCase::IgnoreCase)) Channel->AddConstantKey(Frame, static_cast<float>(Value));
	else                                                               Channel->AddCubicKey(Frame, static_cast<float>(Value));
	Channel->AutoSetTangents();

	auto Result = MCPSuccess();
	if (bExisted) MCPSetUpdated(Result); else MCPSetCreated(Result);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	Payload->SetStringField(TEXT("animationName"), Anim->GetName());
	Payload->SetStringField(TEXT("widgetName"), WidgetName);
	Payload->SetStringField(TEXT("propertyName"), PropertyName);
	Payload->SetNumberField(TEXT("time"), Time);
	if (!ChannelName.IsEmpty()) Payload->SetStringField(TEXT("channel"), ChannelName);
	Payload->SetNumberField(TEXT("channelIndex"), ChannelIndex);
	if (bExisted)
	{
		// Overwrote a key: the inverse is putting the old value back, not deleting.
		Payload->SetNumberField(TEXT("value"), PreviousValue);
		MCPSetRollback(Result, TEXT("add_widget_animation_key"), Payload);
		Result->SetNumberField(TEXT("previousValue"), PreviousValue);
	}
	else
	{
		MCPSetRollback(Result, TEXT("remove_widget_animation_key"), Payload);
	}

	if (auto Err = WAnim_CommitBlueprint(WidgetBP, AssetPath, Result)) return Err;

	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("animationName"), Anim->GetName());
	Result->SetStringField(TEXT("widgetName"), WidgetName);
	Result->SetStringField(TEXT("propertyName"), PropertyName);
	Result->SetNumberField(TEXT("time"), Time);
	Result->SetNumberField(TEXT("frame"), Frame.Value);
	Result->SetNumberField(TEXT("value"), Value);
	Result->SetStringField(TEXT("interpolation"), Interp);
	Result->SetNumberField(TEXT("keyCount"), Channel->GetNumKeys());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FWidgetHandlers::RemoveWidgetAnimationKey(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	FString AnimationName;
	if (auto Err = RequireString(Params, TEXT("animationName"), AnimationName)) return Err;

	FString WidgetName;
	if (auto Err = RequireString(Params, TEXT("widgetName"), WidgetName)) return Err;

	FString PropertyName;
	if (auto Err = RequireString(Params, TEXT("propertyName"), PropertyName)) return Err;

	const double Time = OptionalNumber(Params, TEXT("time"), 0.0);
	const FString ChannelName = OptionalString(Params, TEXT("channel"));
	const int32 ChannelIndex = OptionalInt(Params, TEXT("channelIndex"), 0);

	TSharedPtr<FJsonValue> ResolveError;
	UWidgetBlueprint* WidgetBP = MCPWidget::ResolveWidgetBlueprintOrError(AssetPath, ResolveError);
	if (!WidgetBP) return ResolveError;

	UWidgetAnimation* Anim = WAnim_FindAnimation(WidgetBP, AnimationName);
	if (!Anim || !Anim->GetMovieScene())
	{
		return MCPError(FString::Printf(
			TEXT("Animation '%s' not found on %s (or it has no MovieScene). The asset has: %s."),
			*AnimationName, *AssetPath, *WAnim_AnimationNameList(WidgetBP)));
	}
	UMovieScene* MovieScene = Anim->GetMovieScene();

	const FGuid BindingGuid = WAnim_FindBindingGuid(Anim, FName(*WidgetName));
	UMovieSceneSection* Section = nullptr;
	if (BindingGuid.IsValid())
	{
		for (const FMovieSceneBinding& SceneBinding : const_cast<const UMovieScene*>(MovieScene)->GetBindings())
		{
			if (SceneBinding.GetObjectGuid() != BindingGuid) continue;
			for (UMovieSceneTrack* Track : SceneBinding.GetTracks())
			{
				UMovieScenePropertyTrack* PropTrack = Cast<UMovieScenePropertyTrack>(Track);
				if (!PropTrack || PropTrack->GetPropertyName() != FName(*PropertyName)) continue;
				for (UMovieSceneSection* Candidate : PropTrack->GetAllSections())
				{
					if (Candidate) { Section = Candidate; break; }
				}
				if (Section) break;
			}
			if (Section) break;
		}
	}

	if (!Section)
	{
		auto Res = MCPSuccess();
		Res->SetBoolField(TEXT("alreadyRemoved"), true);
		Res->SetStringField(TEXT("searched"),
			FString::Printf(TEXT("no section for property '%s' on widget '%s' in animation '%s'"),
				*PropertyName, *WidgetName, *AnimationName));
		return MCPResult(Res);
	}

	FString Available;
	FMovieSceneFloatChannel* Channel =
		WAnim_ResolveChannel<FMovieSceneFloatChannel>(Section, ChannelName, ChannelIndex, Available);
	if (!Channel)
	{
		return MCPError(FString::Printf(
			TEXT("No float channel %s on the '%s' section. Available channels: %s."),
			ChannelName.IsEmpty() ? *FString::Printf(TEXT("at index %d"), ChannelIndex) : *FString::Printf(TEXT("named '%s'"), *ChannelName),
			*PropertyName, *Available));
	}

	const FFrameNumber Frame = WAnim_Frame(MovieScene, Time);
	TMovieSceneChannelData<FMovieSceneFloatValue> Data = Channel->GetData();
	const int32 Index = Data.FindKey(Frame);
	if (Index == INDEX_NONE)
	{
		auto Res = MCPSuccess();
		Res->SetBoolField(TEXT("alreadyRemoved"), true);
		Res->SetNumberField(TEXT("time"), Time);
		Res->SetNumberField(TEXT("frame"), Frame.Value);
		Res->SetNumberField(TEXT("keyCount"), Channel->GetNumKeys());
		Res->SetStringField(TEXT("searched"),
			TEXT("the channel has no key at that frame; widget(get_animation) lists the key times"));
		return MCPResult(Res);
	}

	// Capture the value BEFORE removing so the inverse can put it back.
	const float PreviousValue = Data.GetValues()[Index].Value;
	Data.RemoveKey(Index);
	Channel->AutoSetTangents();

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetBoolField(TEXT("alreadyRemoved"), false);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	Payload->SetStringField(TEXT("animationName"), Anim->GetName());
	Payload->SetStringField(TEXT("widgetName"), WidgetName);
	Payload->SetStringField(TEXT("propertyName"), PropertyName);
	Payload->SetNumberField(TEXT("time"), Time);
	Payload->SetNumberField(TEXT("value"), PreviousValue);
	if (!ChannelName.IsEmpty()) Payload->SetStringField(TEXT("channel"), ChannelName);
	Payload->SetNumberField(TEXT("channelIndex"), ChannelIndex);
	MCPSetRollback(Result, TEXT("add_widget_animation_key"), Payload);
	// The value comes back; the tangents are recomputed rather than restored.
	Result->SetBoolField(TEXT("rollbackIsLossy"), true);
	Result->SetStringField(TEXT("rollbackRestores"),
		TEXT("The key time and value. Hand-edited tangents are recomputed by AutoSetTangents, not restored."));

	if (auto Err = WAnim_CommitBlueprint(WidgetBP, AssetPath, Result)) return Err;

	Result->SetNumberField(TEXT("time"), Time);
	Result->SetNumberField(TEXT("frame"), Frame.Value);
	Result->SetNumberField(TEXT("removedValue"), PreviousValue);
	Result->SetNumberField(TEXT("keyCount"), Channel->GetNumKeys());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FWidgetHandlers::AddWidgetAnimationEventKey(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	FString AnimationName;
	if (auto Err = RequireString(Params, TEXT("animationName"), AnimationName)) return Err;

	FString FunctionName;
	if (auto Err = RequireString(Params, TEXT("functionName"), FunctionName)) return Err;

	const double Time = OptionalNumber(Params, TEXT("time"), 0.0);
	const FString TrackName = OptionalString(Params, TEXT("trackName"), TEXT("Events"));

	TSharedPtr<FJsonValue> ResolveError;
	UWidgetBlueprint* WidgetBP = MCPWidget::ResolveWidgetBlueprintOrError(AssetPath, ResolveError);
	if (!WidgetBP) return ResolveError;

	UWidgetAnimation* Anim = WAnim_FindAnimation(WidgetBP, AnimationName);
	if (!Anim || !Anim->GetMovieScene())
	{
		return MCPError(FString::Printf(
			TEXT("Animation '%s' not found on %s (or it has no MovieScene). The asset has: %s."),
			*AnimationName, *AssetPath, *WAnim_AnimationNameList(WidgetBP)));
	}
	UMovieScene* MovieScene = Anim->GetMovieScene();

	// The event calls a UFUNCTION on the widget's own generated class. Resolve it
	// now rather than storing an unbound event that silently never fires.
	UClass* GeneratedClass = WidgetBP->GeneratedClass ? WidgetBP->GeneratedClass.Get() : nullptr;
	if (!GeneratedClass)
	{
		return MCPError(FString::Printf(
			TEXT("%s has no GeneratedClass yet. Compile it (widget(add_widget) or any widget mutation ")
			TEXT("compiles) before adding an event key."), *AssetPath));
	}
	UFunction* Function = GeneratedClass->FindFunctionByName(FName(*FunctionName));
	if (!Function)
	{
		TArray<FString> Candidates;
		for (TFieldIterator<UFunction> It(GeneratedClass); It && Candidates.Num() < 30; ++It)
		{
			if (It->NumParms == 0) Candidates.Add(It->GetName());
		}
		return MCPError(FString::Printf(
			TEXT("Function '%s' not found on %s. Searched every UFUNCTION on the generated class and its ")
			TEXT("supers. Parameterless candidates: %s. Create one with blueprint(create_function) on the ")
			TEXT("same asset, then retry."),
			*FunctionName, *GeneratedClass->GetName(),
			Candidates.Num() ? *FString::Join(Candidates, TEXT(", ")) : TEXT("(none)")));
	}
	if (Function->NumParms != 0)
	{
		return MCPError(FString::Printf(
			TEXT("Function '%s' takes %d parameter(s). A Sequencer event endpoint must have no parameters ")
			TEXT("and no return value. Wrap it in a parameterless function and bind that instead."),
			*FunctionName, static_cast<int32>(Function->NumParms)));
	}

	// Event tracks are not bound to a widget, so they live on the MovieScene itself.
	UMovieSceneEventTrack* EventTrack = nullptr;
	bool bTrackCreated = false;
	for (UMovieSceneTrack* Track : MovieScene->GetTracks())
	{
		UMovieSceneEventTrack* Candidate = Cast<UMovieSceneEventTrack>(Track);
		if (Candidate && Candidate->GetDisplayName().ToString() == TrackName) { EventTrack = Candidate; break; }
	}
	if (!EventTrack)
	{
		EventTrack = Cast<UMovieSceneEventTrack>(MovieScene->AddTrack(UMovieSceneEventTrack::StaticClass()));
		if (!EventTrack)
		{
			return MCPError(FString::Printf(
				TEXT("UMovieScene::AddTrack refused UMovieSceneEventTrack on animation '%s'. ")
				TEXT("UWidgetAnimation gates which track classes it supports."), *AnimationName));
		}
		EventTrack->SetDisplayName(FText::FromString(TrackName));
		bTrackCreated = true;
	}

	UMovieSceneEventTriggerSection* Trigger = nullptr;
	for (UMovieSceneSection* Section : EventTrack->GetAllSections())
	{
		if (UMovieSceneEventTriggerSection* Candidate = Cast<UMovieSceneEventTriggerSection>(Section))
		{
			Trigger = Candidate;
			break;
		}
	}
	if (!Trigger)
	{
		Trigger = Cast<UMovieSceneEventTriggerSection>(EventTrack->CreateNewSection());
		if (!Trigger)
		{
			return MCPError(TEXT("UMovieSceneEventTrack::CreateNewSection did not return a trigger section."));
		}
		Trigger->SetRange(MovieScene->GetPlaybackRange());
		Trigger->SetRowIndex(0);
		EventTrack->AddSection(*Trigger);
	}

	const FFrameNumber Frame = WAnim_Frame(MovieScene, Time);
	TMovieSceneChannelData<FMovieSceneEvent> Data = Trigger->EventChannel.GetData();
	const int32 ExistingIndex = Data.FindKey(Frame);
	const bool bExisted = ExistingIndex != INDEX_NONE;
	FString PreviousFunction;
	if (bExisted)
	{
		const UFunction* Old = Data.GetValues()[ExistingIndex].Ptrs.Function.Get();
		PreviousFunction = Old ? Old->GetName() : FString();
		Data.RemoveKey(ExistingIndex);
	}

	FMovieSceneEvent Event;
	Event.Ptrs.Function = Function;
	Data.AddKey(Frame, Event);

	auto Result = MCPSuccess();
	if (bExisted) MCPSetUpdated(Result); else MCPSetCreated(Result);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	Payload->SetStringField(TEXT("animationName"), Anim->GetName());
	Payload->SetStringField(TEXT("trackName"), TrackName);
	Payload->SetNumberField(TEXT("time"), Time);
	if (bExisted && !PreviousFunction.IsEmpty())
	{
		Payload->SetStringField(TEXT("functionName"), PreviousFunction);
		MCPSetRollback(Result, TEXT("add_widget_animation_event_key"), Payload);
		Result->SetStringField(TEXT("previousFunctionName"), PreviousFunction);
	}
	else
	{
		MCPSetRollback(Result, TEXT("remove_widget_animation_event_key"), Payload);
	}

	if (auto Err = WAnim_CommitBlueprint(WidgetBP, AssetPath, Result)) return Err;

	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("animationName"), Anim->GetName());
	Result->SetStringField(TEXT("trackName"), TrackName);
	Result->SetStringField(TEXT("functionName"), FunctionName);
	Result->SetNumberField(TEXT("time"), Time);
	Result->SetNumberField(TEXT("frame"), Frame.Value);
	Result->SetBoolField(TEXT("trackCreated"), bTrackCreated);
	// Honest about the half that is not covered: the key stores the compiled
	// UFunction pointer, not a graph endpoint, so Sequencer's event UI shows it
	// as an unbound entry until someone re-picks the endpoint there.
	Result->SetStringField(TEXT("note"),
		TEXT("The key binds the compiled UFunction directly. It fires at runtime, but the Sequencer event ")
		TEXT("panel has no graph endpoint to display for it, so opening and re-saving the animation in the ")
		TEXT("UMG designer can clear the binding."));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FWidgetHandlers::RemoveWidgetAnimationEventKey(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	FString AnimationName;
	if (auto Err = RequireString(Params, TEXT("animationName"), AnimationName)) return Err;

	const double Time = OptionalNumber(Params, TEXT("time"), 0.0);
	const FString TrackName = OptionalString(Params, TEXT("trackName"), TEXT("Events"));

	TSharedPtr<FJsonValue> ResolveError;
	UWidgetBlueprint* WidgetBP = MCPWidget::ResolveWidgetBlueprintOrError(AssetPath, ResolveError);
	if (!WidgetBP) return ResolveError;

	UWidgetAnimation* Anim = WAnim_FindAnimation(WidgetBP, AnimationName);
	if (!Anim || !Anim->GetMovieScene())
	{
		return MCPError(FString::Printf(
			TEXT("Animation '%s' not found on %s (or it has no MovieScene). The asset has: %s."),
			*AnimationName, *AssetPath, *WAnim_AnimationNameList(WidgetBP)));
	}
	UMovieScene* MovieScene = Anim->GetMovieScene();

	UMovieSceneEventTriggerSection* Trigger = nullptr;
	for (UMovieSceneTrack* Track : MovieScene->GetTracks())
	{
		UMovieSceneEventTrack* EventTrack = Cast<UMovieSceneEventTrack>(Track);
		if (!EventTrack || EventTrack->GetDisplayName().ToString() != TrackName) continue;
		for (UMovieSceneSection* Section : EventTrack->GetAllSections())
		{
			if (UMovieSceneEventTriggerSection* Candidate = Cast<UMovieSceneEventTriggerSection>(Section))
			{
				Trigger = Candidate;
				break;
			}
		}
		if (Trigger) break;
	}

	if (!Trigger)
	{
		auto Res = MCPSuccess();
		Res->SetBoolField(TEXT("alreadyRemoved"), true);
		Res->SetStringField(TEXT("searched"),
			FString::Printf(TEXT("animation '%s' has no event track named '%s'"), *AnimationName, *TrackName));
		return MCPResult(Res);
	}

	const FFrameNumber Frame = WAnim_Frame(MovieScene, Time);
	TMovieSceneChannelData<FMovieSceneEvent> Data = Trigger->EventChannel.GetData();
	const int32 Index = Data.FindKey(Frame);
	if (Index == INDEX_NONE)
	{
		auto Res = MCPSuccess();
		Res->SetBoolField(TEXT("alreadyRemoved"), true);
		Res->SetNumberField(TEXT("time"), Time);
		Res->SetStringField(TEXT("searched"),
			TEXT("the event track has no key at that frame; widget(get_animation) lists the event times"));
		return MCPResult(Res);
	}

	const UFunction* Old = Data.GetValues()[Index].Ptrs.Function.Get();
	const FString PreviousFunction = Old ? Old->GetName() : FString();
	Data.RemoveKey(Index);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetBoolField(TEXT("alreadyRemoved"), false);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	Payload->SetStringField(TEXT("animationName"), Anim->GetName());
	Payload->SetStringField(TEXT("trackName"), TrackName);
	Payload->SetNumberField(TEXT("time"), Time);
	Payload->SetStringField(TEXT("functionName"), PreviousFunction);
	MCPSetRollback(Result, TEXT("add_widget_animation_event_key"), Payload);
	if (PreviousFunction.IsEmpty())
	{
		Result->SetBoolField(TEXT("rollbackIsLossy"), true);
		Result->SetStringField(TEXT("rollbackRestores"),
			TEXT("Nothing: the removed key had no bound function, so there is no endpoint to re-add."));
	}

	if (auto Err = WAnim_CommitBlueprint(WidgetBP, AssetPath, Result)) return Err;

	Result->SetNumberField(TEXT("time"), Time);
	Result->SetStringField(TEXT("removedFunctionName"), PreviousFunction);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FWidgetHandlers::BindWidgetAnimationEvent(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	FString AnimationName;
	if (auto Err = RequireString(Params, TEXT("animationName"), AnimationName)) return Err;

	const FString EventName = OptionalString(Params, TEXT("event"), TEXT("Finished"));
	const FString UserTag = OptionalString(Params, TEXT("userTag"));

	if (!EventName.Equals(TEXT("Started"), ESearchCase::IgnoreCase)
		&& !EventName.Equals(TEXT("Finished"), ESearchCase::IgnoreCase))
	{
		return MCPError(FString::Printf(
			TEXT("event '%s' is not a widget animation event. EWidgetAnimationEvent has exactly two ")
			TEXT("values: Started, Finished."), *EventName));
	}
	const EWidgetAnimationEvent Action = EventName.Equals(TEXT("Started"), ESearchCase::IgnoreCase)
		? EWidgetAnimationEvent::Started : EWidgetAnimationEvent::Finished;

	TSharedPtr<FJsonValue> ResolveError;
	UWidgetBlueprint* WidgetBP = MCPWidget::ResolveWidgetBlueprintOrError(AssetPath, ResolveError);
	if (!WidgetBP) return ResolveError;

	UWidgetAnimation* Anim = WAnim_FindAnimation(WidgetBP, AnimationName);
	if (!Anim)
	{
		return MCPError(FString::Printf(
			TEXT("Animation '%s' not found on %s. The asset has: %s."),
			*AnimationName, *AssetPath, *WAnim_AnimationNameList(WidgetBP)));
	}

	if (WidgetBP->UbergraphPages.Num() == 0 || !WidgetBP->UbergraphPages[0])
	{
		return MCPError(FString::Printf(
			TEXT("%s has no event graph to place the node in. Widget Blueprints normally carry one; ")
			TEXT("open the asset and add an Event Graph, or recreate it with widget(create)."), *AssetPath));
	}
	UEdGraph* EventGraph = WidgetBP->UbergraphPages[0];

	const FName AnimationProperty = Anim->GetFName();

	// ── Idempotency: one node per (animation, event, userTag).
	for (UEdGraphNode* Node : EventGraph->Nodes)
	{
		UK2Node_WidgetAnimationEvent* Existing = Cast<UK2Node_WidgetAnimationEvent>(Node);
		if (!Existing) continue;
		if (Existing->AnimationPropertyName == AnimationProperty
			&& Existing->Action == Action
			&& Existing->UserTag == FName(*UserTag))
		{
			auto Res = MCPSuccess();
			MCPSetExisted(Res);
			Res->SetStringField(TEXT("assetPath"), AssetPath);
			Res->SetStringField(TEXT("animationName"), Anim->GetName());
			Res->SetStringField(TEXT("event"), EventName);
			Res->SetStringField(TEXT("nodeName"), Existing->GetName());
			Res->SetStringField(TEXT("functionName"), Existing->CustomFunctionName.ToString());
			return MCPResult(Res);
		}
	}

	UK2Node_WidgetAnimationEvent* NewNode =
		NewObject<UK2Node_WidgetAnimationEvent>(EventGraph, NAME_None, RF_Transactional);
	if (!NewNode)
	{
		return MCPError(TEXT("NewObject<UK2Node_WidgetAnimationEvent> returned null."));
	}

	NewNode->Action = Action;
	NewNode->AnimationPropertyName = AnimationProperty;
	NewNode->UserTag = FName(*UserTag);
	NewNode->SourceWidgetBlueprint = WidgetBP;
	NewNode->bOverrideFunction = false;
	NewNode->bInternalEvent = true;
	NewNode->CustomFunctionName = FName(*FString::Printf(
		TEXT("BndEvt__%s_%s_%s"), *WidgetBP->GetName(), *AnimationProperty.ToString(), *EventName));

	// Placed below whatever is already there so the graph stays readable.
	int32 LowestY = 0;
	for (UEdGraphNode* Node : EventGraph->Nodes) LowestY = FMath::Max(LowestY, Node->NodePosY + 120);
	NewNode->NodePosX = 0;
	NewNode->NodePosY = LowestY;

	NewNode->CreateNewGuid();
	NewNode->PostPlacedNewNode();
	NewNode->AllocateDefaultPins();
	EventGraph->AddNode(NewNode, false, false);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	Payload->SetStringField(TEXT("animationName"), Anim->GetName());
	Payload->SetStringField(TEXT("event"), EventName);
	if (!UserTag.IsEmpty()) Payload->SetStringField(TEXT("userTag"), UserTag);
	MCPSetRollback(Result, TEXT("unbind_widget_animation_event"), Payload);

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBP);
	if (auto Err = WAnim_CommitBlueprint(WidgetBP, AssetPath, Result)) return Err;

	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("animationName"), Anim->GetName());
	Result->SetStringField(TEXT("event"), EventName);
	Result->SetStringField(TEXT("userTag"), UserTag);
	Result->SetStringField(TEXT("nodeName"), NewNode->GetName());
	Result->SetStringField(TEXT("functionName"), NewNode->CustomFunctionName.ToString());
	Result->SetStringField(TEXT("hint"),
		TEXT("The node is an entry point with an unconnected exec pin. Wire logic onto it with ")
		TEXT("blueprint(add_node) / blueprint(connect_pins) using this nodeName."));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FWidgetHandlers::UnbindWidgetAnimationEvent(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	FString AnimationName;
	if (auto Err = RequireString(Params, TEXT("animationName"), AnimationName)) return Err;

	const FString EventName = OptionalString(Params, TEXT("event"), TEXT("Finished"));
	const FString UserTag = OptionalString(Params, TEXT("userTag"));

	TSharedPtr<FJsonValue> ResolveError;
	UWidgetBlueprint* WidgetBP = MCPWidget::ResolveWidgetBlueprintOrError(AssetPath, ResolveError);
	if (!WidgetBP) return ResolveError;

	const EWidgetAnimationEvent Action = EventName.Equals(TEXT("Started"), ESearchCase::IgnoreCase)
		? EWidgetAnimationEvent::Started : EWidgetAnimationEvent::Finished;

	UWidgetAnimation* Anim = WAnim_FindAnimation(WidgetBP, AnimationName);
	const FName AnimationProperty = Anim ? Anim->GetFName() : FName(*AnimationName);

	UEdGraph* EventGraph = WidgetBP->UbergraphPages.Num() ? WidgetBP->UbergraphPages[0] : nullptr;
	UK2Node_WidgetAnimationEvent* Target = nullptr;
	if (EventGraph)
	{
		for (UEdGraphNode* Node : EventGraph->Nodes)
		{
			UK2Node_WidgetAnimationEvent* Candidate = Cast<UK2Node_WidgetAnimationEvent>(Node);
			if (!Candidate) continue;
			if (Candidate->AnimationPropertyName == AnimationProperty
				&& Candidate->Action == Action
				&& Candidate->UserTag == FName(*UserTag))
			{
				Target = Candidate;
				break;
			}
		}
	}

	if (!Target)
	{
		auto Res = MCPSuccess();
		Res->SetBoolField(TEXT("alreadyRemoved"), true);
		Res->SetStringField(TEXT("animationName"), AnimationName);
		Res->SetStringField(TEXT("event"), EventName);
		Res->SetStringField(TEXT("searched"),
			EventGraph ? TEXT("every UK2Node_WidgetAnimationEvent in the event graph")
			           : TEXT("the asset has no event graph"));
		return MCPResult(Res);
	}

	// Capture before mutating. Connected logic downstream of the node is NOT
	// captured, which is what makes the rollback lossy.
	int32 LinkedPins = 0;
	for (UEdGraphPin* Pin : Target->Pins)
	{
		if (Pin) LinkedPins += Pin->LinkedTo.Num();
	}
	const FString NodeName = Target->GetName();

	EventGraph->RemoveNode(Target);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetBoolField(TEXT("alreadyRemoved"), false);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	Payload->SetStringField(TEXT("animationName"), AnimationName);
	Payload->SetStringField(TEXT("event"), EventName);
	if (!UserTag.IsEmpty()) Payload->SetStringField(TEXT("userTag"), UserTag);
	MCPSetRollback(Result, TEXT("bind_widget_animation_event"), Payload);
	if (LinkedPins > 0)
	{
		Result->SetBoolField(TEXT("rollbackIsLossy"), true);
		Result->SetStringField(TEXT("rollbackRestores"),
			FString::Printf(TEXT("The event node itself. Its %d pin connection(s) are NOT restored - ")
				TEXT("rewire them with blueprint(connect_pins)."), LinkedPins));
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBP);
	if (auto Err = WAnim_CommitBlueprint(WidgetBP, AssetPath, Result)) return Err;

	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("removedNodeName"), NodeName);
	Result->SetNumberField(TEXT("severedPinLinks"), LinkedPins);
	return MCPResult(Result);
}

// ═════════════════════════════════════════════════════════════════════════════
// Ticket B - navigation, focus and accessibility
// ═════════════════════════════════════════════════════════════════════════════

TSharedPtr<FJsonValue> FWidgetHandlers::SetWidgetNavigation(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	TSharedPtr<FJsonValue> ResolveError;
	UWidgetBlueprint* WidgetBP = MCPWidget::ResolveWidgetBlueprintOrError(AssetPath, ResolveError);
	if (!WidgetBP) return ResolveError;
	if (!WidgetBP->WidgetTree) return MCPWidget::MissingWidgetTreeError(AssetPath);

	// One shape for both the single and the bulk case: `rules` is an array of
	// {widgetName, direction, rule, widgetToFocus?}. A single write can also be
	// spelled with the three top-level params, which is folded into the array.
	TArray<TSharedPtr<FJsonValue>> RuleEntries;
	const TArray<TSharedPtr<FJsonValue>>* GivenRules = nullptr;
	if (Params->TryGetArrayField(TEXT("rules"), GivenRules) && GivenRules)
	{
		RuleEntries = *GivenRules;
	}
	else
	{
		FString WidgetName;
		if (auto Err = RequireString(Params, TEXT("widgetName"), WidgetName))
		{
			return MCPError(
				TEXT("Pass either widgetName + direction + rule for one write, or rules[] as an array of ")
				TEXT("{widgetName, direction, rule, widgetToFocus?} for a bulk write. Neither was given."));
		}
		TSharedPtr<FJsonObject> One = MakeShared<FJsonObject>();
		One->SetStringField(TEXT("widgetName"), WidgetName);
		One->SetStringField(TEXT("direction"), OptionalString(Params, TEXT("direction")));
		One->SetStringField(TEXT("rule"), OptionalString(Params, TEXT("rule"), TEXT("Explicit")));
		One->SetStringField(TEXT("widgetToFocus"), OptionalString(Params, TEXT("widgetToFocus")));
		RuleEntries.Add(MakeShared<FJsonValueObject>(One));
	}

	if (RuleEntries.Num() == 0)
	{
		return MCPError(TEXT("rules[] is empty. Nothing to write."));
	}

	// ── Validate every entry before touching anything, so a bad entry in the
	// middle of a bulk write leaves the asset untouched instead of half-applied.
	struct FPlannedRule
	{
		UWidget* Widget = nullptr;
		FString WidgetName;
		FString Direction;
		EUINavigationRule Rule = EUINavigationRule::Escape;
		FName WidgetToFocus;
	};
	TArray<FPlannedRule> Planned;
	for (int32 i = 0; i < RuleEntries.Num(); ++i)
	{
		const TSharedPtr<FJsonObject>* Entry = nullptr;
		if (!RuleEntries[i]->TryGetObject(Entry) || !Entry)
		{
			return MCPError(FString::Printf(
				TEXT("rules[%d] is not an object. Each entry is {widgetName, direction, rule, widgetToFocus?}."), i));
		}

		FPlannedRule P;
		(*Entry)->TryGetStringField(TEXT("widgetName"), P.WidgetName);
		(*Entry)->TryGetStringField(TEXT("direction"), P.Direction);
		FString RuleText(TEXT("Explicit"));
		(*Entry)->TryGetStringField(TEXT("rule"), RuleText);
		FString FocusText;
		(*Entry)->TryGetStringField(TEXT("widgetToFocus"), FocusText);

		P.Widget = WAnim_FindWidget(WidgetBP->WidgetTree, P.WidgetName);
		if (!P.Widget)
		{
			return MCPError(FString::Printf(
				TEXT("rules[%d]: widget '%s' not found in %s. The tree has: %s. Nothing was written. ")
				TEXT("List it with widget(read_tree)."),
				i, *P.WidgetName, *AssetPath, *WAnim_WidgetNameList(WidgetBP->WidgetTree)));
		}
		if (!WAnim_IsDirection(P.Direction))
		{
			return MCPError(FString::Printf(
				TEXT("rules[%d]: direction '%s' is not one of Up, Down, Left, Right, Next, Previous. ")
				TEXT("Nothing was written."), i, *P.Direction));
		}
		if (!WAnim_ParseRule(RuleText, P.Rule))
		{
			return MCPError(FString::Printf(
				TEXT("rules[%d]: rule '%s' is not one of Escape, Explicit, Wrap, Stop, Custom, ")
				TEXT("CustomBoundary. Nothing was written."), i, *RuleText));
		}
		if (P.Rule == EUINavigationRule::Explicit)
		{
			if (FocusText.IsEmpty())
			{
				return MCPError(FString::Printf(
					TEXT("rules[%d]: rule Explicit needs widgetToFocus. Nothing was written."), i));
			}
			if (!WAnim_FindWidget(WidgetBP->WidgetTree, FocusText))
			{
				return MCPError(FString::Printf(
					TEXT("rules[%d]: widgetToFocus '%s' is not a widget in %s. The tree has: %s. ")
					TEXT("Nothing was written."),
					i, *FocusText, *AssetPath, *WAnim_WidgetNameList(WidgetBP->WidgetTree)));
			}
		}
		P.WidgetToFocus = FName(*FocusText);
		Planned.Add(P);
	}

	// ── Capture the previous state of every touched widget for the rollback.
	TArray<TSharedPtr<FJsonValue>> PreviousJson;
	TSet<FString> Captured;
	for (const FPlannedRule& P : Planned)
	{
		if (Captured.Contains(P.WidgetName)) continue;
		Captured.Add(P.WidgetName);
		TSharedPtr<FJsonObject> Snapshot = WAnim_CaptureNavigation(P.Widget);
		Snapshot->SetStringField(TEXT("widgetName"), P.WidgetName);
		PreviousJson.Add(MakeShared<FJsonValueObject>(Snapshot));
	}

	// ── Apply.
	int32 Changed = 0;
	int32 NavigationObjectsCreated = 0;
	TArray<TSharedPtr<FJsonValue>> AppliedJson;
	for (const FPlannedRule& P : Planned)
	{
		if (!P.Widget->Navigation)
		{
			// The one thing set_property cannot do: make the Instanced subobject.
			P.Widget->Navigation = NewObject<UWidgetNavigation>(P.Widget, NAME_None, RF_Transactional);
			++NavigationObjectsCreated;
		}
		FWidgetNavigationData* Data = WAnim_DirectionData(P.Widget->Navigation, P.Direction);
		if (!Data) continue;

		const bool bSame = (Data->Rule == P.Rule) && (Data->WidgetToFocus == P.WidgetToFocus);
		if (!bSame)
		{
			Data->Rule = P.Rule;
			Data->WidgetToFocus = P.WidgetToFocus;
			Data->Widget.Reset();
			++Changed;
		}

		TSharedPtr<FJsonObject> Applied = MakeShared<FJsonObject>();
		Applied->SetStringField(TEXT("widgetName"), P.WidgetName);
		Applied->SetStringField(TEXT("direction"), P.Direction);
		Applied->SetStringField(TEXT("rule"), WAnim_RuleName(P.Rule));
		Applied->SetStringField(TEXT("widgetToFocus"), P.WidgetToFocus.ToString());
		Applied->SetBoolField(TEXT("changed"), !bSame);
		AppliedJson.Add(MakeShared<FJsonValueObject>(Applied));
	}

	auto Result = MCPSuccess();
	if (Changed > 0) MCPSetUpdated(Result); else MCPSetExisted(Result);
	Result->SetBoolField(TEXT("unchanged"), Changed == 0);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	Payload->SetArrayField(TEXT("previous"), PreviousJson);
	MCPSetRollback(Result, TEXT("restore_widget_navigation"), Payload);

	if (Changed > 0)
	{
		if (auto Err = WAnim_CommitBlueprint(WidgetBP, AssetPath, Result)) return Err;
	}

	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetArrayField(TEXT("applied"), AppliedJson);
	Result->SetNumberField(TEXT("changedCount"), Changed);
	Result->SetNumberField(TEXT("navigationObjectsCreated"), NavigationObjectsCreated);
	Result->SetStringField(TEXT("note"),
		TEXT("UWidget::Navigation is an Instanced UPROPERTY that is null until something creates it, which ")
		TEXT("is the only part of this that set_property cannot do. Now that it exists on these widgets, ")
		TEXT("every field is reachable directly: widget(set_style) with propertyName 'Navigation.Down' and ")
		TEXT("a JSON value, or 'Navigation.Down.Rule' for one field."));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FWidgetHandlers::ClearWidgetNavigation(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	FString WidgetName;
	if (auto Err = RequireString(Params, TEXT("widgetName"), WidgetName)) return Err;

	const FString Direction = OptionalString(Params, TEXT("direction"));

	TSharedPtr<FJsonValue> ResolveError;
	UWidgetBlueprint* WidgetBP = MCPWidget::ResolveWidgetBlueprintOrError(AssetPath, ResolveError);
	if (!WidgetBP) return ResolveError;
	if (!WidgetBP->WidgetTree) return MCPWidget::MissingWidgetTreeError(AssetPath);

	UWidget* Widget = WAnim_FindWidget(WidgetBP->WidgetTree, WidgetName);
	if (!Widget)
	{
		return MCPError(FString::Printf(
			TEXT("Widget '%s' not found in %s. The tree has: %s."),
			*WidgetName, *AssetPath, *WAnim_WidgetNameList(WidgetBP->WidgetTree)));
	}

	if (!Widget->Navigation)
	{
		auto Res = MCPSuccess();
		Res->SetBoolField(TEXT("alreadyRemoved"), true);
		Res->SetStringField(TEXT("widgetName"), WidgetName);
		Res->SetStringField(TEXT("searched"), TEXT("the widget has no Navigation object, so no rules to clear"));
		return MCPResult(Res);
	}

	// Capture before mutating.
	TSharedPtr<FJsonObject> Snapshot = WAnim_CaptureNavigation(Widget);
	Snapshot->SetStringField(TEXT("widgetName"), WidgetName);

	int32 Cleared = 0;
	if (Direction.IsEmpty())
	{
		Widget->Navigation = nullptr;
		Cleared = 6;
	}
	else
	{
		FWidgetNavigationData* Data = WAnim_DirectionData(Widget->Navigation, Direction);
		if (!Data)
		{
			return MCPError(FString::Printf(
				TEXT("direction '%s' is not one of Up, Down, Left, Right, Next, Previous."), *Direction));
		}
		if (Data->Rule != EUINavigationRule::Escape || !Data->WidgetToFocus.IsNone())
		{
			Data->Rule = EUINavigationRule::Escape;
			Data->WidgetToFocus = NAME_None;
			Data->Widget.Reset();
			Cleared = 1;
		}
	}

	auto Result = MCPSuccess();
	if (Cleared > 0) MCPSetUpdated(Result); else MCPSetExisted(Result);
	Result->SetBoolField(TEXT("alreadyRemoved"), Cleared == 0);

	TArray<TSharedPtr<FJsonValue>> PreviousJson;
	PreviousJson.Add(MakeShared<FJsonValueObject>(Snapshot));
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	Payload->SetArrayField(TEXT("previous"), PreviousJson);
	MCPSetRollback(Result, TEXT("restore_widget_navigation"), Payload);

	if (Cleared > 0)
	{
		if (auto Err = WAnim_CommitBlueprint(WidgetBP, AssetPath, Result)) return Err;
	}

	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("widgetName"), WidgetName);
	Result->SetStringField(TEXT("direction"), Direction.IsEmpty() ? TEXT("(all)") : *Direction);
	Result->SetNumberField(TEXT("clearedDirections"), Cleared);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FWidgetHandlers::RestoreWidgetNavigation(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	const TArray<TSharedPtr<FJsonValue>>* Previous = nullptr;
	if (!Params->TryGetArrayField(TEXT("previous"), Previous) || !Previous)
	{
		return MCPError(
			TEXT("Missing required parameter 'previous' (the array set_widget_navigation / ")
			TEXT("clear_widget_navigation put in their rollback payload)."));
	}

	TSharedPtr<FJsonValue> ResolveError;
	UWidgetBlueprint* WidgetBP = MCPWidget::ResolveWidgetBlueprintOrError(AssetPath, ResolveError);
	if (!WidgetBP) return ResolveError;
	if (!WidgetBP->WidgetTree) return MCPWidget::MissingWidgetTreeError(AssetPath);

	int32 Restored = 0;
	int32 Missing = 0;
	for (const TSharedPtr<FJsonValue>& Entry : *Previous)
	{
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (!Entry->TryGetObject(Obj) || !Obj) continue;
		FString WidgetName;
		(*Obj)->TryGetStringField(TEXT("widgetName"), WidgetName);
		UWidget* Widget = WAnim_FindWidget(WidgetBP->WidgetTree, WidgetName);
		if (!Widget) { ++Missing; continue; }

		bool bHadNav = false;
		(*Obj)->TryGetBoolField(TEXT("hadNavigationObject"), bHadNav);
		if (!bHadNav)
		{
			Widget->Navigation = nullptr;
			++Restored;
			continue;
		}
		if (!Widget->Navigation)
		{
			Widget->Navigation = NewObject<UWidgetNavigation>(Widget, NAME_None, RF_Transactional);
		}
		for (const TCHAR* Dir : WAnim_DirectionNames)
		{
			const TSharedPtr<FJsonObject>* D = nullptr;
			if (!(*Obj)->TryGetObjectField(Dir, D) || !D) continue;
			FWidgetNavigationData* Data = WAnim_DirectionData(Widget->Navigation, Dir);
			if (!Data) continue;
			FString RuleText, FocusText;
			(*D)->TryGetStringField(TEXT("rule"), RuleText);
			(*D)->TryGetStringField(TEXT("widgetToFocus"), FocusText);
			EUINavigationRule Rule = EUINavigationRule::Escape;
			WAnim_ParseRule(RuleText, Rule);
			Data->Rule = Rule;
			Data->WidgetToFocus = FocusText.IsEmpty() ? NAME_None : FName(*FocusText);
			Data->Widget.Reset();
		}
		++Restored;
	}

	auto Result = MCPSuccess();
	if (Restored > 0) MCPSetUpdated(Result); else MCPSetExisted(Result);
	Result->SetBoolField(TEXT("unchanged"), Restored == 0);

	// The inverse of a restore is another restore: hand back what is there now.
	TArray<TSharedPtr<FJsonValue>> NowJson;
	for (const TSharedPtr<FJsonValue>& Entry : *Previous)
	{
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (!Entry->TryGetObject(Obj) || !Obj) continue;
		FString SnapshotName;
		(*Obj)->TryGetStringField(TEXT("widgetName"), SnapshotName);
		UWidget* Widget = WAnim_FindWidget(WidgetBP->WidgetTree, SnapshotName);
		if (!Widget) continue;
		TSharedPtr<FJsonObject> Snapshot = WAnim_CaptureNavigation(Widget);
		Snapshot->SetStringField(TEXT("widgetName"), Widget->GetName());
		NowJson.Add(MakeShared<FJsonValueObject>(Snapshot));
	}
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	Payload->SetArrayField(TEXT("previous"), NowJson);
	MCPSetRollback(Result, TEXT("restore_widget_navigation"), Payload);

	if (Restored > 0)
	{
		if (auto Err = WAnim_CommitBlueprint(WidgetBP, AssetPath, Result)) return Err;
	}

	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetNumberField(TEXT("restoredCount"), Restored);
	Result->SetNumberField(TEXT("missingWidgetCount"), Missing);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FWidgetHandlers::AuditWidgetFocusChain(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	TSharedPtr<FJsonValue> ResolveError;
	UWidgetBlueprint* WidgetBP = MCPWidget::ResolveWidgetBlueprintOrError(AssetPath, ResolveError);
	if (!WidgetBP) return ResolveError;
	if (!WidgetBP->WidgetTree) return MCPWidget::MissingWidgetTreeError(AssetPath);

	TArray<UWidget*> All;
	WAnim_CollectWidgets(WidgetBP->WidgetTree, All);

	// ── Pass 1: classify every widget, and record its explicit outbound edges.
	struct FNode
	{
		UWidget* Widget = nullptr;
		bool bFocusable = false;
		bool bVisible = false;
		bool bEnabled = false;
		TMap<FString, FString> Explicit;   // direction -> target name
		TMap<FString, FString> Rules;      // direction -> rule name
	};
	TMap<FString, FNode> Nodes;
	TArray<FString> Order;
	int32 ExplicitRuleCount = 0;

	for (UWidget* W : All)
	{
		FNode Node;
		Node.Widget = W;
		Node.bFocusable = WAnim_IsFocusable(W);
		Node.bVisible = WAnim_IsFocusVisible(W);
		Node.bEnabled = W->GetIsEnabled();

		if (UWidgetNavigation* Nav = W->Navigation)
		{
			for (const TCHAR* Dir : WAnim_DirectionNames)
			{
				FWidgetNavigationData* Data = WAnim_DirectionData(Nav, Dir);
				if (!Data) continue;
				Node.Rules.Add(Dir, WAnim_RuleName(Data->Rule));
				if (Data->Rule == EUINavigationRule::Explicit)
				{
					Node.Explicit.Add(Dir, Data->WidgetToFocus.ToString());
					++ExplicitRuleCount;
				}
			}
		}
		Order.Add(W->GetName());
		Nodes.Add(W->GetName(), Node);
	}

	// ── The declared entry point. A screen with focusable widgets and no initial
	// focus is the single most common way gamepad navigation dies silently: the
	// first press goes nowhere because nothing holds focus to navigate FROM.
	FString InitialFocus;
	bool bInitialFocusResolves = false;
	bool bInitialFocusUsable = false;
	if (UClass* Generated = WidgetBP->GeneratedClass ? WidgetBP->GeneratedClass.Get() : nullptr)
	{
		if (UUserWidget* CDO = Cast<UUserWidget>(Generated->GetDefaultObject(false)))
		{
			InitialFocus = CDO->GetDesiredFocusWidgetName().ToString();
		}
	}
	if (!InitialFocus.IsEmpty() && InitialFocus != TEXT("None"))
	{
		if (const FNode* Node = Nodes.Find(InitialFocus))
		{
			bInitialFocusResolves = true;
			bInitialFocusUsable = Node->bFocusable && Node->bVisible && Node->bEnabled;
		}
	}
	else
	{
		InitialFocus.Reset();
	}

	// ── Pass 2: the findings.
	TArray<TSharedPtr<FJsonValue>> Dangling;     // rule points at nothing usable
	TArray<TSharedPtr<FJsonValue>> Unreachable;  // focusable, but nothing navigates to it
	TArray<TSharedPtr<FJsonValue>> OneWay;       // A -> B without B -> A
	TArray<TSharedPtr<FJsonValue>> Focusables;

	TSet<FString> HasInboundEdge;
	for (const FString& Name : Order)
	{
		const FNode& Node = Nodes[Name];
		for (const TPair<FString, FString>& Edge : Node.Explicit)
		{
			HasInboundEdge.Add(Edge.Value);
		}
	}

	for (const FString& Name : Order)
	{
		const FNode& Node = Nodes[Name];

		if (Node.bFocusable)
		{
			TSharedPtr<FJsonObject> F = MakeShared<FJsonObject>();
			F->SetStringField(TEXT("widgetName"), Name);
			F->SetStringField(TEXT("class"), Node.Widget->GetClass()->GetName());
			F->SetStringField(TEXT("visibility"), WAnim_VisibilityName(Node.Widget->GetVisibility()));
			F->SetBoolField(TEXT("enabled"), Node.bEnabled);
			F->SetBoolField(TEXT("hasExplicitRules"), Node.Explicit.Num() > 0);
			Focusables.Add(MakeShared<FJsonValueObject>(F));
		}

		for (const TPair<FString, FString>& Edge : Node.Explicit)
		{
			const FNode* Target = Nodes.Find(Edge.Value);
			FString Problem;
			if (Edge.Value.IsEmpty() || Edge.Value == TEXT("None"))
			{
				Problem = TEXT("rule is Explicit but WidgetToFocus is empty, so the direction does nothing");
			}
			else if (!Target)
			{
				Problem = TEXT("WidgetToFocus names a widget that is not in this tree");
			}
			else if (!Target->bFocusable)
			{
				Problem = TEXT("target is not focusable, so Slate will refuse the move");
			}
			else if (!Target->bVisible)
			{
				Problem = TEXT("target is Collapsed, Hidden or HitTestInvisible and cannot take focus");
			}
			else if (!Target->bEnabled)
			{
				Problem = TEXT("target is disabled and cannot take focus");
			}

			if (!Problem.IsEmpty())
			{
				TSharedPtr<FJsonObject> D = MakeShared<FJsonObject>();
				D->SetStringField(TEXT("widgetName"), Name);
				D->SetStringField(TEXT("direction"), Edge.Key);
				D->SetStringField(TEXT("widgetToFocus"), Edge.Value);
				D->SetStringField(TEXT("problem"), Problem);
				Dangling.Add(MakeShared<FJsonValueObject>(D));
				continue;
			}

			// A one-way link is a gamepad trap: you can get there, not back.
			const FString Back = WAnim_OppositeDirection(Edge.Key);
			if (!Back.IsEmpty())
			{
				const FString* Return = Target->Explicit.Find(Back);
				if (!Return || *Return != Name)
				{
					TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
					O->SetStringField(TEXT("widgetName"), Name);
					O->SetStringField(TEXT("direction"), Edge.Key);
					O->SetStringField(TEXT("widgetToFocus"), Edge.Value);
					O->SetStringField(TEXT("expectedReturnDirection"), Back);
					O->SetStringField(TEXT("actualReturnTarget"), Return ? *Return : FString(TEXT("(no explicit rule)")));
					OneWay.Add(MakeShared<FJsonValueObject>(O));
				}
			}
		}

		const bool bUsable = Node.bFocusable && Node.bVisible && Node.bEnabled;
		if (bUsable && ExplicitRuleCount > 0 && !HasInboundEdge.Contains(Name) && Name != InitialFocus)
		{
			TSharedPtr<FJsonObject> U = MakeShared<FJsonObject>();
			U->SetStringField(TEXT("widgetName"), Name);
			U->SetStringField(TEXT("class"), Node.Widget->GetClass()->GetName());
			U->SetStringField(TEXT("reason"),
				TEXT("focusable and visible, but no other widget's explicit navigation rule points at it and ")
				TEXT("it is not the initial focus target"));
			Unreachable.Add(MakeShared<FJsonValueObject>(U));
		}
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetNumberField(TEXT("widgetCount"), All.Num());
	Result->SetNumberField(TEXT("focusableCount"), Focusables.Num());
	Result->SetNumberField(TEXT("explicitRuleCount"), ExplicitRuleCount);
	Result->SetArrayField(TEXT("focusable"), Focusables);
	Result->SetArrayField(TEXT("danglingRules"), Dangling);
	Result->SetArrayField(TEXT("oneWayLinks"), OneWay);
	Result->SetArrayField(TEXT("unreachable"), Unreachable);

	TSharedPtr<FJsonObject> InitialObj = MakeShared<FJsonObject>();
	InitialObj->SetStringField(TEXT("widgetName"), InitialFocus);
	InitialObj->SetBoolField(TEXT("declared"), !InitialFocus.IsEmpty());
	InitialObj->SetBoolField(TEXT("resolves"), bInitialFocusResolves);
	InitialObj->SetBoolField(TEXT("usable"), bInitialFocusUsable);
	Result->SetObjectField(TEXT("initialFocus"), InitialObj);

	// The audit's verdict, stated once, in the terms someone fixing it needs.
	TArray<FString> Verdict;
	if (Focusables.Num() > 0 && InitialFocus.IsEmpty())
	{
		Verdict.Add(FString::Printf(
			TEXT("%d focusable widget(s) and no initial focus target: gamepad and keyboard start with focus ")
			TEXT("nowhere, so the first input does nothing. Set it with asset(set_property) on %s, ")
			TEXT("propertyName 'DesiredFocusWidget.WidgetName'."), Focusables.Num(), *AssetPath));
	}
	if (!InitialFocus.IsEmpty() && !bInitialFocusUsable)
	{
		Verdict.Add(FString::Printf(
			TEXT("Initial focus '%s' %s."), *InitialFocus,
			bInitialFocusResolves ? TEXT("is not focusable, visible and enabled")
			                      : TEXT("does not name a widget in this tree")));
	}
	if (Dangling.Num() > 0)
	{
		Verdict.Add(FString::Printf(TEXT("%d explicit rule(s) point somewhere focus cannot go."), Dangling.Num()));
	}
	if (OneWay.Num() > 0)
	{
		Verdict.Add(FString::Printf(
			TEXT("%d one-way link(s): focus can reach the target but the opposite direction does not come back."),
			OneWay.Num()));
	}
	if (ExplicitRuleCount == 0 && Focusables.Num() > 1)
	{
		Verdict.Add(
			TEXT("No explicit navigation rules at all. Slate falls back to analog geometry-based navigation, ")
			TEXT("which is correct for a simple grid and unpredictable for overlapping or nested panels. ")
			TEXT("Reachability could not be checked; only the geometry decides it."));
	}
	TArray<TSharedPtr<FJsonValue>> FindingsJson;
	for (const FString& V : Verdict) FindingsJson.Add(MakeShared<FJsonValueString>(V));
	Result->SetArrayField(TEXT("findings"), FindingsJson);
	Result->SetBoolField(TEXT("clean"), Verdict.Num() == 0);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FWidgetHandlers::AuditWidgetAccessibility(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	const int32 MinFontSize = OptionalInt(Params, TEXT("minFontSize"), 12);
	const double MinHitSize = OptionalNumber(Params, TEXT("minHitSize"), 40.0);

	TSharedPtr<FJsonValue> ResolveError;
	UWidgetBlueprint* WidgetBP = MCPWidget::ResolveWidgetBlueprintOrError(AssetPath, ResolveError);
	if (!WidgetBP) return ResolveError;
	if (!WidgetBP->WidgetTree) return MCPWidget::MissingWidgetTreeError(AssetPath);

	TArray<UWidget*> All;
	WAnim_CollectWidgets(WidgetBP->WidgetTree, All);

	TArray<TSharedPtr<FJsonValue>> SmallFonts;
	TArray<TSharedPtr<FJsonValue>> MissingLabels;
	TArray<TSharedPtr<FJsonValue>> SmallTargets;
	TArray<TSharedPtr<FJsonValue>> NotAccessible;

	for (UWidget* W : All)
	{
		const FString Name = W->GetName();
		const FString ClassName = W->GetClass()->GetName();

		// ── Font sizes, found by reflection so this covers TextBlock, every
		// editable text variant, and the font nested inside a style struct.
		TArray<TPair<FString, const FSlateFontInfo*>> Fonts;
		WAnim_CollectFonts(W, W->GetClass(), FString(), 0, Fonts);
		for (const TPair<FString, const FSlateFontInfo*>& Entry : Fonts)
		{
			if (!Entry.Value) continue;
			if (Entry.Value->Size >= MinFontSize) continue;
			TSharedPtr<FJsonObject> F = MakeShared<FJsonObject>();
			F->SetStringField(TEXT("widgetName"), Name);
			F->SetStringField(TEXT("class"), ClassName);
			F->SetStringField(TEXT("propertyPath"), Entry.Key);
			F->SetNumberField(TEXT("fontSize"), Entry.Value->Size);
			F->SetNumberField(TEXT("minFontSize"), MinFontSize);
			F->SetStringField(TEXT("fix"), FString::Printf(
				TEXT("widget(set_property) assetPath=%s widgetName=%s propertyName=%s.Size value=%d"),
				*AssetPath, *Name, *Entry.Key, MinFontSize));
			SmallFonts.Add(MakeShared<FJsonValueObject>(F));
		}

		const bool bFocusable = WAnim_IsFocusable(W);

		// ── An interactive widget with no tooltip and no accessible text has
		// nothing for a screen reader, and nothing for a tooltip-driven UI test.
		if (bFocusable)
		{
			const bool bHasToolTip = !W->GetToolTipText().IsEmpty();
			bool bHasAccessibleText = false;
			if (FTextProperty* TextProp = FindFProperty<FTextProperty>(W->GetClass(), TEXT("AccessibleText")))
			{
				bHasAccessibleText = !TextProp->GetPropertyValue_InContainer(W).IsEmpty();
			}
			if (!bHasToolTip && !bHasAccessibleText)
			{
				TSharedPtr<FJsonObject> M = MakeShared<FJsonObject>();
				M->SetStringField(TEXT("widgetName"), Name);
				M->SetStringField(TEXT("class"), ClassName);
				M->SetStringField(TEXT("problem"),
					TEXT("focusable with neither ToolTipText nor AccessibleText, so it announces as nothing"));
				M->SetStringField(TEXT("fix"), FString::Printf(
					TEXT("widget(set_property) assetPath=%s widgetName=%s propertyName=ToolTipText value=..."),
					*AssetPath, *Name));
				MissingLabels.Add(MakeShared<FJsonValueObject>(M));
			}

			// AccessibleBehavior is an enum on UWidget; NotAccessible on something
			// focusable hides it from assistive tech entirely.
			if (FProperty* Behavior = FindFProperty<FProperty>(W->GetClass(), TEXT("AccessibleBehavior")))
			{
				FString BehaviorText;
				Behavior->ExportTextItem_InContainer(BehaviorText, W, nullptr, W, PPF_None);
				if (BehaviorText.Contains(TEXT("NotAccessible")))
				{
					TSharedPtr<FJsonObject> N = MakeShared<FJsonObject>();
					N->SetStringField(TEXT("widgetName"), Name);
					N->SetStringField(TEXT("class"), ClassName);
					N->SetStringField(TEXT("accessibleBehavior"), BehaviorText);
					N->SetStringField(TEXT("problem"),
						TEXT("focusable but AccessibleBehavior is NotAccessible, so assistive tech never sees it"));
					NotAccessible.Add(MakeShared<FJsonValueObject>(N));
				}
			}

			// ── Hit target size, only where the slot states one. An auto-sized
			// widget has no authored size to judge, so it is not reported.
			if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(W->Slot))
			{
				// Only an authored, non-stretched, non-auto size can be judged
				// here. A stretched or content-sized slot has no number to check
				// until it is laid out, so it is left to the runtime path.
				const FAnchors Anchors = CanvasSlot->GetAnchors();
				const bool bFixedSize = !CanvasSlot->GetAutoSize()
					&& Anchors.Minimum.Equals(Anchors.Maximum);
				if (bFixedSize)
				{
					const FVector2D Size = CanvasSlot->GetSize();
					const double Width = Size.X;
					const double Height = Size.Y;
					if (Width > 0.0 && Height > 0.0 && (Width < MinHitSize || Height < MinHitSize))
					{
						TSharedPtr<FJsonObject> S = MakeShared<FJsonObject>();
						S->SetStringField(TEXT("widgetName"), Name);
						S->SetStringField(TEXT("class"), ClassName);
						S->SetNumberField(TEXT("width"), Width);
						S->SetNumberField(TEXT("height"), Height);
						S->SetNumberField(TEXT("minHitSize"), MinHitSize);
						S->SetStringField(TEXT("problem"),
							TEXT("authored canvas size is below the touch/gamepad target floor"));
						SmallTargets.Add(MakeShared<FJsonValueObject>(S));
					}
				}
			}
		}
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetNumberField(TEXT("widgetCount"), All.Num());
	Result->SetNumberField(TEXT("minFontSize"), MinFontSize);
	Result->SetNumberField(TEXT("minHitSize"), MinHitSize);
	Result->SetArrayField(TEXT("smallFonts"), SmallFonts);
	Result->SetArrayField(TEXT("missingLabels"), MissingLabels);
	Result->SetArrayField(TEXT("smallHitTargets"), SmallTargets);
	Result->SetArrayField(TEXT("markedNotAccessible"), NotAccessible);
	Result->SetBoolField(TEXT("clean"),
		SmallFonts.Num() == 0 && MissingLabels.Num() == 0
		&& SmallTargets.Num() == 0 && NotAccessible.Num() == 0);

	// What this audit deliberately does not claim to have checked.
	TArray<TSharedPtr<FJsonValue>> NotChecked;
	NotChecked.Add(MakeShared<FJsonValueString>(
		TEXT("Contrast: a brush or text colour can come from a style asset, a binding, or a material, so a ")
		TEXT("static ratio would be wrong as often as right.")));
	NotChecked.Add(MakeShared<FJsonValueString>(
		TEXT("Reduced motion: Unreal has no engine-level reduced-motion flag. Gate your own animations on a ")
		TEXT("game setting and read it with editor(get_property).")));
	NotChecked.Add(MakeShared<FJsonValueString>(
		TEXT("Rendered size: this reads authored slot values only. For the real geometry run PIE and call ")
		TEXT("widget(get_runtime) with includeLayout=true.")));
	Result->SetArrayField(TEXT("notChecked"), NotChecked);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FWidgetHandlers::GetRuntimeFocusPath(const TSharedPtr<FJsonObject>& Params)
{
	const int32 UserIndex = OptionalInt(Params, TEXT("userIndex"), 0);

	if (!FSlateApplication::IsInitialized())
	{
		return MCPError(TEXT("Slate is not initialised in this process, so there is no focus to report."));
	}
	if (!WAnim_RuntimeWorld())
	{
		return MCPError(
			TEXT("No PIE or Game world is running. Focus is only meaningful while the game is playing - ")
			TEXT("start PIE with editor(play_in_editor) first. The editor's own focus is not reported ")
			TEXT("because it is the editor UI, not the game's."));
	}

	FSlateApplication& Slate = FSlateApplication::Get();
	TSharedPtr<SWidget> Focused = Slate.GetUserFocusedWidget(static_cast<uint32>(UserIndex));

	auto Result = MCPSuccess();
	Result->SetNumberField(TEXT("userIndex"), UserIndex);
	Result->SetBoolField(TEXT("hasFocus"), Focused.IsValid());

	if (!Focused.IsValid())
	{
		Result->SetStringField(TEXT("diagnosis"),
			FString::Printf(TEXT("User %d holds no focus at all. Gamepad and keyboard navigation cannot ")
				TEXT("start from nowhere: give the screen an initial focus target, or call ")
				TEXT("widget(set_runtime_focus)."), UserIndex));
		Result->SetArrayField(TEXT("path"), TArray<TSharedPtr<FJsonValue>>());
		return MCPResult(Result);
	}

	// The chain from the focused widget up to the window, which is what tells you
	// WHICH panel actually swallowed the focus.
	TArray<TSharedPtr<FJsonValue>> PathJson;
	TSharedPtr<SWidget> Current = Focused;
	int32 Depth = 0;
	while (Current.IsValid() && Depth < 64)
	{
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetNumberField(TEXT("depth"), Depth);
		Entry->SetStringField(TEXT("type"), Current->GetTypeAsString());
		Entry->SetStringField(TEXT("tag"), Current->GetTag().ToString());
		Entry->SetBoolField(TEXT("supportsKeyboardFocus"), Current->SupportsKeyboardFocus());
		Entry->SetBoolField(TEXT("enabled"), Current->IsEnabled());
		const FGeometry& Geometry = Current->GetTickSpaceGeometry();
		Entry->SetNumberField(TEXT("localWidth"), Geometry.GetLocalSize().X);
		Entry->SetNumberField(TEXT("localHeight"), Geometry.GetLocalSize().Y);
		PathJson.Add(MakeShared<FJsonValueObject>(Entry));
		Current = Current->GetParentWidget();
		++Depth;
	}

	Result->SetStringField(TEXT("focusedWidgetType"), Focused->GetTypeAsString());
	Result->SetArrayField(TEXT("path"), PathJson);
	Result->SetNumberField(TEXT("pathDepth"), PathJson.Num());
	Result->SetStringField(TEXT("note"),
		TEXT("These are Slate widget types, not UMG widget names: a UMG UButton is an SButton here. Match ")
		TEXT("them to UMG names with widget(get_runtime) on the same live instance."));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FWidgetHandlers::SetRuntimeFocus(const TSharedPtr<FJsonObject>& Params)
{
	FString WidgetName;
	if (auto Err = RequireString(Params, TEXT("widgetName"), WidgetName)) return Err;

	const int32 UserIndex = OptionalInt(Params, TEXT("userIndex"), 0);
	const FString ClassFilter = OptionalString(Params, TEXT("className"));

	if (!FSlateApplication::IsInitialized())
	{
		return MCPError(TEXT("Slate is not initialised in this process."));
	}
	UWorld* World = WAnim_RuntimeWorld();
	if (!World)
	{
		return MCPError(
			TEXT("No PIE or Game world is running. Focus can only be set on a live widget - start PIE with ")
			TEXT("editor(play_in_editor), then add the widget with widget(add_to_viewport)."));
	}

	// Find the live UMG widget. Named child first (the common case), then a
	// UUserWidget of that name.
	UWidget* Target = nullptr;
	TArray<FString> Considered;
	for (TObjectIterator<UUserWidget> It; It; ++It)
	{
		UUserWidget* UserWidget = *It;
		if (!UserWidget || UserWidget->GetWorld() != World) continue;
		if (!ClassFilter.IsEmpty() && !UserWidget->GetClass()->GetName().Contains(ClassFilter)) continue;
		if (Considered.Num() < 20) Considered.Add(UserWidget->GetClass()->GetName());

		if (UserWidget->GetName() == WidgetName) { Target = UserWidget; break; }
		if (UserWidget->WidgetTree)
		{
			if (UWidget* Child = WAnim_FindWidget(UserWidget->WidgetTree, WidgetName)) { Target = Child; break; }
		}
	}

	if (!Target)
	{
		return MCPError(FString::Printf(
			TEXT("No live widget named '%s' in the running world. Searched every UUserWidget%s and each of ")
			TEXT("their widget trees; the classes present are: %s. List them with widget(list_runtime)."),
			*WidgetName,
			ClassFilter.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" matching className '%s'"), *ClassFilter),
			Considered.Num() ? *FString::Join(Considered, TEXT(", ")) : TEXT("(none)")));
	}

	TSharedPtr<SWidget> Slate = Target->GetCachedWidget();
	if (!Slate.IsValid())
	{
		return MCPError(FString::Printf(
			TEXT("Widget '%s' exists but has no constructed Slate widget, so it is not on screen and cannot ")
			TEXT("take focus. Add its owning widget to the viewport with widget(add_to_viewport) first."),
			*WidgetName));
	}
	if (!Slate->SupportsKeyboardFocus())
	{
		return MCPError(FString::Printf(
			TEXT("Widget '%s' (%s -> %s) does not support keyboard focus. Slate will refuse it. Set its ")
			TEXT("IsFocusable property, or target a widget that is focusable; widget(audit_focus_chain) ")
			TEXT("lists which ones are."),
			*WidgetName, *Target->GetClass()->GetName(), *Slate->GetTypeAsString()));
	}

	// Capture the current holder BEFORE moving focus, so the inverse can name it.
	FSlateApplication& App = FSlateApplication::Get();
	TSharedPtr<SWidget> Previous = App.GetUserFocusedWidget(static_cast<uint32>(UserIndex));
	const FString PreviousType = Previous.IsValid() ? Previous->GetTypeAsString() : FString();
	const bool bAlreadyFocused = Previous.IsValid() && Previous == Slate;

	auto Result = MCPSuccess();
	if (bAlreadyFocused)
	{
		MCPSetExisted(Result);
		Result->SetBoolField(TEXT("alreadySet"), true);
	}
	else
	{
		const bool bMoved = App.SetUserFocus(static_cast<uint32>(UserIndex), Slate, EFocusCause::SetDirectly);
		if (!bMoved)
		{
			return MCPError(FString::Printf(
				TEXT("FSlateApplication::SetUserFocus refused to move focus to '%s' for user %d. The widget ")
				TEXT("is focusable, so the usual cause is that it is not inside the focused window - check ")
				TEXT("widget(get_runtime_focus_path) for which window currently holds focus."),
				*WidgetName, UserIndex));
		}
		MCPSetUpdated(Result);
		Result->SetBoolField(TEXT("alreadySet"), false);
	}

	// The inverse is putting focus back where it was. It is expressed in UMG
	// terms, which is lossy: the previous holder may have been a raw Slate widget
	// with no UMG name at all.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetNumberField(TEXT("userIndex"), UserIndex);
	FString PreviousUmgName;
	if (Previous.IsValid())
	{
		for (TObjectIterator<UWidget> It; It; ++It)
		{
			UWidget* Candidate = *It;
			if (Candidate && Candidate->GetWorld() == World && Candidate->GetCachedWidget() == Previous)
			{
				PreviousUmgName = Candidate->GetName();
				break;
			}
		}
	}
	if (!PreviousUmgName.IsEmpty())
	{
		Payload->SetStringField(TEXT("widgetName"), PreviousUmgName);
		MCPSetRollback(Result, TEXT("set_runtime_focus"), Payload);
	}
	else
	{
		Payload->SetStringField(TEXT("widgetName"), WidgetName);
		MCPSetRollback(Result, TEXT("set_runtime_focus"), Payload);
		Result->SetBoolField(TEXT("rollbackIsLossy"), true);
		Result->SetStringField(TEXT("rollbackRestores"),
			Previous.IsValid()
				? FString::Printf(TEXT("Nothing useful: focus was held by a Slate widget (%s) with no UMG ")
					TEXT("name, so the inverse re-focuses this same widget rather than the previous holder."),
					*PreviousType)
				: FString(TEXT("Nothing: no widget held focus before this call, and there is no call that ")
					TEXT("returns focus to nowhere.")));
	}

	Result->SetStringField(TEXT("widgetName"), WidgetName);
	Result->SetStringField(TEXT("widgetClass"), Target->GetClass()->GetName());
	Result->SetStringField(TEXT("slateType"), Slate->GetTypeAsString());
	Result->SetNumberField(TEXT("userIndex"), UserIndex);
	Result->SetStringField(TEXT("previousFocusSlateType"), PreviousType);
	Result->SetStringField(TEXT("previousFocusWidgetName"), PreviousUmgName);
	return MCPResult(Result);
}
