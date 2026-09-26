#include "WidgetHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerPagination.h"
#include "HandlerAssetCreate.h"
#include <type_traits>
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "EditorScriptingUtilities/Public/EditorAssetLibrary.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintFactory.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "Components/PanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/Button.h"
#include "Components/ProgressBar.h"
#include "Components/CheckBox.h"
#include "Components/Slider.h"
#include "Components/EditableTextBox.h"
#include "Components/ComboBoxString.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/VerticalBox.h"
#include "Components/Overlay.h"
#include "Components/GridPanel.h"
#include "Components/UniformGridPanel.h"
#include "Components/WidgetSwitcher.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/ScaleBox.h"
#include "Components/Border.h"
#include "Components/Spacer.h"
#include "Components/RichTextBlock.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/OverlaySlot.h"
#include "Animation/WidgetAnimation.h"
#include "MovieScene.h"
#include "MovieScenePossessable.h"
#include "MovieSceneSpawnable.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Layout/SlateRect.h"
#include "Misc/App.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectIterator.h"
#include "Editor.h"
#include "EditorUtilitySubsystem.h"
#include "EditorUtilityWidget.h"
#include "EditorUtilityWidgetBlueprint.h"
#include "EditorUtilityBlueprint.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Engine/Texture2D.h"
#include "Engine/GameViewportClient.h"
#include "Materials/MaterialInterface.h"
#include "EngineUtils.h"
#include "Widgets/SViewport.h"
#include "UObject/SoftObjectPath.h"

// ── WidgetBlueprint resolution (#972) ────────────────────────────────────────
// See the contract and the mechanism note on MCPWidget in WidgetHandlers.h.
// Everything here is defined exactly once, in this translation unit, and
// declared in the shared header, because the module is a unity build and a
// second file-local copy would be a redefinition on some grouping.
namespace MCPWidget
{

/**
 * "WidgetBlueprint'/Game/UI/WBP_Foo.WBP_Foo'", "/Game/UI/WBP_Foo",
 * "/Game/UI/WBP_Foo.WBP_Foo" and "/Game/UI/WBP_Foo.WBP_Foo_C" all normalise to
 * "/Game/UI/WBP_Foo.WBP_Foo". A path with no object part gets one inferred from
 * the package name, which is the convention every asset in the content browser
 * follows.
 */
static FString NormalizeWidgetBlueprintObjectPath(const FString& InAssetPath)
{
	FString Path = InAssetPath;
	Path.TrimStartAndEndInline();
	if (Path.IsEmpty()) return Path;

	// "Class'/Game/...'" and "Class /Game/..." export forms.
	int32 QuoteIndex = INDEX_NONE;
	if (Path.FindChar(TCHAR('\''), QuoteIndex))
	{
		Path = Path.RightChop(QuoteIndex + 1);
		Path.RemoveFromEnd(TEXT("'"));
	}
	else
	{
		int32 SpaceIndex = INDEX_NONE;
		if (Path.FindChar(TCHAR(' '), SpaceIndex))
		{
			Path = Path.RightChop(SpaceIndex + 1);
		}
	}
	Path.TrimStartAndEndInline();

	// Subobject part ("Package.Asset:Inner") is not ours to resolve.
	int32 ColonIndex = INDEX_NONE;
	if (Path.FindChar(TCHAR(':'), ColonIndex))
	{
		Path = Path.Left(ColonIndex);
	}

	const int32 LastSlash = Path.Find(TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
	const int32 LastDot = Path.Find(TEXT("."), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
	if (LastDot <= LastSlash)
	{
		// No object part. Infer it from the package name.
		const FString AssetName = FPackageName::GetLongPackageAssetName(Path);
		if (AssetName.IsEmpty()) return FString();
		return Path + TEXT(".") + AssetName;
	}

	// "/Game/UI/WBP_Foo.WBP_Foo_C" names the generated class. Keep the trailing
	// _C off the object path; the class is resolved from the blueprint anyway.
	FString ObjectName = Path.RightChop(LastDot + 1);
	if (ObjectName.EndsWith(TEXT("_C"), ESearchCase::CaseSensitive))
	{
		ObjectName.LeftChopInline(2);
		Path = Path.Left(LastDot + 1) + ObjectName;
	}
	return Path;
}

/** A WidgetBlueprint the editor still consults. Liveness is
 *  MCPIsLiveAssetObject's answer; what is widget-specific is the cast and
 *  mapping a generated-class path back to its blueprint. */
static UWidgetBlueprint* AsLiveWidgetBlueprint(UObject* Candidate, FString& OutFoundClass)
{
	if (!MCPIsLiveAssetObject(Candidate)) return nullptr;

	if (UWidgetBlueprint* AsBlueprint = Cast<UWidgetBlueprint>(Candidate))
	{
		return AsBlueprint;
	}
	// A UClass reaches here only from a caller that resolved one directly; the
	// _C spelling is normalized away before the lookup.
	if (UClass* AsClass = Cast<UClass>(Candidate))
	{
		if (UWidgetBlueprint* Generated = Cast<UWidgetBlueprint>(AsClass->ClassGeneratedBy))
		{
			return Generated;
		}
	}
	OutFoundClass = Candidate->GetClass()->GetName();
	return nullptr;
}

FWidgetBlueprintResolve ResolveWidgetBlueprint(const FString& AssetPath)
{
	FWidgetBlueprintResolve Out;
	Out.ObjectPath = NormalizeWidgetBlueprintObjectPath(AssetPath);
	if (Out.ObjectPath.IsEmpty())
	{
		Out.Failure = EWidgetBlueprintResolveFailure::NotFound;
		return Out;
	}

	Out.bAssetExists = MCPAssetExistsWithoutLoading(MCPAssetPathForms(Out.ObjectPath));

	// One ladder, the one every asset action uses. Each of its steps resolves
	// the same path, so a step that finds the wrong type is the answer and
	// continuing past it would only find the same object again.
	if (UWidgetBlueprint* Live =
		AsLiveWidgetBlueprint(MCPLoadAssetObject(Out.ObjectPath), Out.FoundClass))
	{
		Out.Blueprint = Live;
		return Out;
	}

	if (!Out.FoundClass.IsEmpty())
	{
		Out.Failure = EWidgetBlueprintResolveFailure::WrongType;
	}
	else if (Out.bAssetExists)
	{
		Out.Failure = EWidgetBlueprintResolveFailure::Unresolvable;
	}
	else
	{
		Out.Failure = EWidgetBlueprintResolveFailure::NotFound;
	}
	return Out;
}

TSharedPtr<FJsonValue> WidgetBlueprintResolveError(
	const FString& AssetPath,
	const FWidgetBlueprintResolve& Resolved)
{
	switch (Resolved.Failure)
	{
	case EWidgetBlueprintResolveFailure::WrongType:
		return MCPError(FString::Printf(
			TEXT("'%s' is a %s, not a WidgetBlueprint."),
			*AssetPath, *Resolved.FoundClass));

	case EWidgetBlueprintResolveFailure::Unresolvable:
		// The distinction the caller needs: the asset is there, so retrying or
		// reloading the bridge is the move. Renaming or re-creating it is not.
		//
		// #1065: and if play is running, say so first. A WidgetBlueprint that
		// was present and valid failed here for no reason but PIE, and the
		// message named a stale handle, which sent the debugging somewhere
		// else entirely. The engine logs the real reason where a bridge caller
		// never sees it.
		return MCPError(FString::Printf(
			TEXT("'%s' exists but could not be resolved to a live WidgetBlueprint on this call. ")
			TEXT("The object handle went stale (a package reload or a GC pass replaced it), the asset is not missing. ")
			TEXT("Retry the call; if it keeps failing, editor(action=\"reload_bridge\") clears it.%s"),
			*AssetPath, *MCPPlayInEditorLoadNote()));

	case EWidgetBlueprintResolveFailure::NotFound:
	default:
		return MCPError(FString::Printf(
			TEXT("No asset exists at '%s'. Nothing of that name is in the AssetRegistry and no package of that name is on disk. ")
			TEXT("Check the path with widget(action=\"list\") or asset(action=\"search\").%s"),
			*AssetPath, *MCPPlayInEditorLoadNote()));
	}
}

UWidgetBlueprint* ResolveWidgetBlueprintOrError(
	const FString& AssetPath,
	TSharedPtr<FJsonValue>& OutError)
{
	const FWidgetBlueprintResolve Resolved = ResolveWidgetBlueprint(AssetPath);
	if (!Resolved.Blueprint)
	{
		OutError = WidgetBlueprintResolveError(AssetPath, Resolved);
	}
	return Resolved.Blueprint;
}

TSharedPtr<FJsonValue> MissingWidgetTreeError(const FString& AssetPath)
{
	return MCPError(FString::Printf(
		TEXT("WidgetBlueprint '%s' resolved but has no WidgetTree. The asset is loaded and broken, not missing; ")
		TEXT("open it in the editor or re-create it."),
		*AssetPath));
}

}

void FWidgetHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	// Reports parameters its handlers never read (#1057).
	FMCPHandlerRegistry::FCategoryScope CategoryScope(Registry, TEXT("widget"));

	// #1057: a handler registered with a spec declares its parameters here and
	// nowhere else; the TS surface is generated from a recording of these. The
	// TS normalizer still folds the legacy spellings into assetPath, widgetName
	// and parentWidgetName first, and mirrors assetPath into path, so the
	// aliases below matter to direct bridge callers. The three create actions
	// are contract-exempt: their values would create an asset before anything
	// failed.
	using EType = EMCPParamType;
	auto AssetPath = []()
	{
		return MCPParam::Required(TEXT("assetPath"), EType::String, TEXT("Widget Blueprint or Editor Utility asset path, e.g. /Game/UI/WBP_Example (#798)")).Alias(TEXT("path"));
	};
	auto WidgetName = []()
	{
		return MCPParam::Required(TEXT("widgetName"), EType::String, TEXT("Name of a widget inside the tree (#798)"));
	};
	auto AnimationName = []()
	{
		return MCPParam::Required(TEXT("animationName"), EType::String, TEXT("The UWidgetAnimation's object name or display label"));
	};
	auto PropertyName = []()
	{
		return MCPParam::Required(TEXT("propertyName"), EType::String, TEXT("Reflected property name on the widget"));
	};
	auto Cursor = []()
	{
		return MCPParam::Optional(TEXT("cursor"), EType::String, TEXT("Resume a paged read: pass back the 'nextCursor' from the previous page, unmodified"));
	};
	auto Limit = [](const TCHAR* Description)
	{
		return MCPParam::Optional(TEXT("limit"), EType::Integer, Description);
	};
	auto EventTime = []()
	{
		return MCPParam::Optional(TEXT("time"), EType::Number, TEXT("Key time in SECONDS, converted to frames on the animation's tick resolution"));
	};
	auto TrackName = []()
	{
		return MCPParam::Optional(TEXT("trackName"), EType::String, TEXT("Event track display name (default Events)"));
	};
	auto AnimEvent = []()
	{
		return MCPParam::Optional(TEXT("event"), EType::String, TEXT("Animation lifecycle event: Finished (default) or Started"));
	};
	auto UserTag = []()
	{
		return MCPParam::Optional(TEXT("userTag"), EType::String, TEXT("User tag scoping a Started binding"));
	};
	auto Channel = []()
	{
		return MCPParam::Optional(TEXT("channel"), EType::String, TEXT("Channel by name (R/G/B/A, Left/Top/Right/Bottom, Translation.X); a miss lists the section's real channels"));
	};
	auto ChannelIndex = []()
	{
		return MCPParam::Optional(TEXT("channelIndex"), EType::Integer, TEXT("Channel by index, used when channel is not given (default 0)"));
	};
	auto UserIndex = []()
	{
		return MCPParam::Optional(TEXT("userIndex"), EType::Integer, TEXT("Local player user index (default 0)"));
	};
	auto ClassName = []()
	{
		return MCPParam::Optional(TEXT("className"), EType::String, TEXT("Widget class name that locates the live host widget"));
	};
	// Where a create action puts its asset: assetPath, or name in packagePath.
	auto CreateAssetPath = []()
	{
		return MCPParam::Optional(TEXT("assetPath"), EType::String, TEXT("Full destination, e.g. /Game/UI/WBP_Example; wins over name + packagePath")).Alias(TEXT("path"));
	};
	auto CreateName = []()
	{
		return MCPParam::Optional(TEXT("name"), EType::String, TEXT("Bare asset name, placed in packagePath"));
	};
	auto CreatePackagePath = [](const TCHAR* Description)
	{
		return MCPParam::Optional(TEXT("packagePath"), EType::String, Description);
	};
	auto CreateOnConflict = []()
	{
		return MCPParam::Optional(TEXT("onConflict"), EType::String, TEXT("When the asset exists: skip (default, report it) | error"));
	};

	Registry.RegisterHandler(TEXT("list_widget_blueprints"), &ListWidgetBlueprints, {
		MCPParam::Optional(TEXT("recursive"), EType::Boolean, TEXT("Include sub-paths (default true)")),
		Cursor(),
		Limit(TEXT("Rows to return on this page (default 200, max 2000)")),
	});
	Registry.RegisterHandler(TEXT("create_widget_blueprint"), &CreateWidgetBlueprint, {
		CreateAssetPath(),
		CreateName(),
		CreatePackagePath(TEXT("Folder for name (default /Game/UI/Widgets)")),
		MCPParam::Optional(TEXT("parentClass"), EType::String, TEXT("UUserWidget subclass: a short name or a class path (default UserWidget)")),
		CreateOnConflict(),
	}, MCPSpec::AtLeastOne({ { TEXT("assetPath") }, { TEXT("name") } }).ContractExempt(TEXT("Creates and saves a Widget Blueprint under the contract values; nothing it reads fails first")));
	Registry.RegisterHandler(TEXT("read_widget_tree"), &ReadWidgetTree, {
		AssetPath(),
	});
	Registry.RegisterHandler(TEXT("extract_widget_subtree"), &ExtractWidgetSubtree, {
		MCPParam::Required(TEXT("sourceAssetPath"), EType::String, TEXT("WidgetBlueprint the subtree is read from")).Alias(TEXT("sourcePath")),
		MCPParam::Required(TEXT("sourceWidgetName"), EType::String, TEXT("Widget in the source that becomes the extracted root")).Alias(TEXT("widgetName")),
		MCPParam::Required(TEXT("destinationAssetPath"), EType::String, TEXT("Destination package path, including the new asset name")).Alias(TEXT("destinationPath")),
		MCPParam::Optional(TEXT("destinationParentClass"), EType::String, TEXT("UUserWidget subclass for the destination (default UserWidget)")),
		MCPParam::Optional(TEXT("destinationRootName"), EType::String, TEXT("Name override for the extracted root; descendants keep their names")),
		MCPParam::Optional(TEXT("dryRun"), EType::Boolean, TEXT("Plan only, no asset is created or saved (default true)")),
	});
	Registry.RegisterHandler(TEXT("create_editor_utility_widget"), &CreateEditorUtilityWidget, {
		CreateAssetPath(),
		CreateName(),
		CreatePackagePath(TEXT("Folder for name (default /Game/EditorUtilities)")),
		CreateOnConflict(),
	}, MCPSpec::AtLeastOne({ { TEXT("assetPath") }, { TEXT("name") } }).ContractExempt(TEXT("Creates and saves an Editor Utility Widget under the contract values; nothing it reads fails first")));
	Registry.RegisterHandler(TEXT("create_editor_utility_blueprint"), &CreateEditorUtilityBlueprint, {
		CreateAssetPath(),
		CreateName(),
		CreatePackagePath(TEXT("Folder for name (default /Game/EditorUtilities)")),
		CreateOnConflict(),
	}, MCPSpec::AtLeastOne({ { TEXT("assetPath") }, { TEXT("name") } }).ContractExempt(TEXT("Creates and saves an Editor Utility Blueprint under the contract values; nothing it reads fails first")));
	Registry.RegisterHandler(TEXT("get_widget_details"), &GetWidgetProperties, {
		AssetPath(),
		WidgetName(),
	});
	Registry.RegisterHandler(TEXT("get_widget_properties"), &GetWidgetFullProperties, {
		AssetPath(),
		WidgetName(),
		MCPParam::Optional(TEXT("includeSubtree"), EType::Boolean, TEXT("Also dump descendant widgets (#547)")),
	});
	Registry.RegisterHandler(TEXT("list_widget_bindings"), &ListWidgetBindings, {
		AssetPath(),
		MCPParam::Optional(TEXT("filterWidgetName"), EType::String, TEXT("Only bindings on this widget (#530)")),
		MCPParam::Optional(TEXT("filterProperty"), EType::String, TEXT("Only bindings of this property (#530)")),
	});
	Registry.RegisterHandler(TEXT("clear_widget_binding"), &ClearWidgetBinding, {
		AssetPath(),
		WidgetName(),
		MCPParam::Optional(TEXT("propertyName"), EType::String, TEXT("Reflected property name on the widget")),
	});
	Registry.RegisterHandler(TEXT("set_widget_property"), &SetWidgetProperty, {
		AssetPath(),
		WidgetName(),
		PropertyName(),
		MCPParam::Required(TEXT("value"), EType::Any, TEXT("New value as UE export text, e.g. (Left=8,Top=8,Right=8,Bottom=8)")).Alias(TEXT("propertyValue")),
	});
	Registry.RegisterHandler(TEXT("set_widget_style"), &SetWidgetStyle, {
		AssetPath(),
		WidgetName(),
		PropertyName(),
		MCPParam::Required(TEXT("value"), EType::Any, TEXT("JSON object mirroring the style struct, or a scalar")),
	});
	Registry.RegisterHandler(TEXT("bulk_set_widget_properties"), &BulkSetWidgetProperties, {
		AssetPath(),
		MCPParam::Required(TEXT("properties"), EType::Array, TEXT("[{widgetName, propertyName, value}] (#563)")).Items(EType::Object),
	});
	Registry.RegisterHandler(TEXT("reorder_child"), &ReorderChild, {
		AssetPath(),
		WidgetName(),
		MCPParam::Required(TEXT("index"), EType::Number, TEXT("Target sibling index within the parent panel (#635)")),
	});
	Registry.RegisterHandler(TEXT("read_widget_animations"), &ReadWidgetAnimations, {
		AssetPath(),
	});
	Registry.RegisterHandler(TEXT("run_editor_utility_widget"), &RunEditorUtilityWidget, {
		AssetPath(),
	});
	Registry.RegisterHandler(TEXT("run_editor_utility_blueprint"), &RunEditorUtilityBlueprint, {
		AssetPath(),
	});
	Registry.RegisterHandler(TEXT("add_widget"), &AddWidget, {
		AssetPath(),
		MCPParam::Required(TEXT("widgetClass"), EType::String, TEXT("Widget class: a short name (TextBlock, CanvasPanel), a full path, or a Widget Blueprint path")).Alias(TEXT("typeName")),
		MCPParam::Optional(TEXT("widgetName"), EType::String, TEXT("Name of a widget inside the tree (#798)")).Alias(TEXT("name")),
		MCPParam::Optional(TEXT("parentWidgetName"), EType::String, TEXT("Name of the parent panel widget (#798)")),
	});
	Registry.RegisterHandler(TEXT("remove_widget"), &RemoveWidget, {
		AssetPath(),
		WidgetName(),
	});
	Registry.RegisterHandler(TEXT("move_widget"), &MoveWidget, {
		AssetPath(),
		WidgetName(),
		MCPParam::Required(TEXT("newParentWidgetName"), EType::String, TEXT("Panel widget to reparent into")).Alias(TEXT("parentWidgetName")),
	});
	Registry.RegisterHandler(TEXT("set_root_widget"), &SetRoot, {
		AssetPath(),
		WidgetName(),
	});
	Registry.RegisterHandler(TEXT("wrap_root_widget"), &WrapRoot, {
		AssetPath(),
		MCPParam::Required(TEXT("wrapperClass"), EType::String, TEXT("Panel widget class (CanvasPanel, VerticalBox, Overlay, etc.); must be a UPanelWidget subclass")).Alias(TEXT("widgetClass")),
		MCPParam::Optional(TEXT("wrapperName"), EType::String, TEXT("Name for the new wrapper widget")),
	});
	Registry.RegisterHandler(TEXT("list_widget_classes"), &ListWidgetClasses, {
		MCPParam::Optional(TEXT("filter"), EType::String, TEXT("Case-insensitive substring of the class name")),
		MCPParam::Optional(TEXT("module"), EType::String, TEXT("Case-insensitive substring of the defining module, e.g. UMG or CommonUI")),
		MCPParam::Optional(TEXT("includeAbstract"), EType::Boolean, TEXT("Include abstract base classes, which cannot be added to a tree (default false)")),
		MCPParam::Optional(TEXT("includeBlueprint"), EType::Boolean, TEXT("Include loaded Widget Blueprint generated classes as well as native ones (default false)")),
		Cursor(),
		Limit(TEXT("Rows to return on this page (default 300, max 5000)")),
	});
	Registry.RegisterHandler(TEXT("list_runtime_widgets"), &ListRuntimeWidgets, {
		MCPParam::Optional(TEXT("classFilter"), EType::String, TEXT("Class name substring filter")),
		MCPParam::Optional(TEXT("namePrefix"), EType::String, TEXT("Instance name prefix filter")),
		MCPParam::Optional(TEXT("viewportOnly"), EType::Boolean, TEXT("Only widgets currently added to the viewport")),
		Cursor(),
		Limit(TEXT("Rows to return on this page (default 200, max 2000)")),
	});
	// Both selectors filter together: a given widgetName and className must both match.
	Registry.RegisterHandler(TEXT("get_runtime_widget"), &GetRuntimeWidget, {
		MCPParam::Optional(TEXT("widgetName"), EType::String, TEXT("Exact live instance name")),
		ClassName(),
		MCPParam::Optional(TEXT("childName"), EType::String, TEXT("Named child inside the UserWidget (#559)")),
		MCPParam::Optional(TEXT("maxDepth"), EType::Integer, TEXT("Max widget-tree depth to walk (default 6)")),
		MCPParam::Optional(TEXT("includeLayout"), EType::Boolean, TEXT("Add read-only layout diagnostics (geometry, slot, clipping, viewport, per-node deltas) to every node and report the host UserWidget under host (#775)")),
	}, MCPSpec::AtLeastOne({ { TEXT("widgetName") }, { TEXT("className") } }));
	Registry.RegisterHandler(TEXT("inspect_runtime_instances"), &InspectRuntimeInstances, {
		MCPParam::Optional(TEXT("widgetName"), EType::String, TEXT("Exact live instance name. Provide this or classFilter")),
		MCPParam::Optional(TEXT("classFilter"), EType::String, TEXT("Class name substring filter")),
		MCPParam::Optional(TEXT("propertyNames"), EType::Array, TEXT("Exact reflected property names to serialize")).Items(EType::String),
		MCPParam::Optional(TEXT("includeSubtree"), EType::Boolean, TEXT("Also dump descendant widgets (#547)")),
		MCPParam::Optional(TEXT("childName"), EType::String, TEXT("Named child inside the UserWidget (#559)")),
		MCPParam::Optional(TEXT("childClassFilter"), EType::String, TEXT("Class substring filter for subtree nodes (implies includeSubtree)")),
		MCPParam::Optional(TEXT("viewportOnly"), EType::Boolean, TEXT("Only widgets currently added to the viewport")),
		MCPParam::Optional(TEXT("world"), EType::String, TEXT("Runtime world scope: pie (default) | game | auto. The editor world is never a valid target")),
		MCPParam::Optional(TEXT("pieInstance"), EType::Integer, TEXT("PIE instance id for multi-client sessions")),
		MCPParam::Optional(TEXT("maxInstances"), EType::Integer, TEXT("Maximum matching widget instances returned (1 to 500, default 100)")),
		MCPParam::Optional(TEXT("maxNodesPerInstance"), EType::Integer, TEXT("Maximum root/subtree nodes per instance (1 to 2000, default 250)")),
	});
	// #161: Runtime delegate inspection
	Registry.RegisterHandler(TEXT("get_runtime_delegates"), &GetRuntimeDelegates, {
		MCPParam::Optional(TEXT("widgetName"), EType::String, TEXT("Exact live instance name. Provide this or className")),
		ClassName(),
	});
	Registry.RegisterHandler(TEXT("add_to_viewport"), &AddWidgetToViewport, {
		MCPParam::Required(TEXT("assetPath"), EType::String, TEXT("Widget Blueprint or Editor Utility asset path, e.g. /Game/UI/WBP_Example (#798)")).Alias(TEXT("path")).Alias(TEXT("widgetBlueprintPath")),
		MCPParam::Optional(TEXT("zOrder"), EType::Number, TEXT("Viewport Z-order (#602)")),
	});
	// Needs a PIE world, and a widget the contract values name nothing of.
	Registry.RegisterHandler(TEXT("invoke_runtime_function"), &InvokeRuntimeWidgetFunction, {
		MCPParam::Optional(TEXT("widgetName"), EType::String, TEXT("Exact live instance name")),
		ClassName(),
		MCPParam::Optional(TEXT("functionName"), EType::String, TEXT("Parameterless UFUNCTION to call on the live widget (#559), or with childName the child delegate to fire (#812)")),
		MCPParam::Optional(TEXT("childName"), EType::String, TEXT("Named child inside the UserWidget (#559)")),
		MCPParam::Optional(TEXT("value"), EType::Any, TEXT("Value for the child interaction: true, false or toggle for a CheckBox, a number for a Slider or SpinBox, text for a text box, an option or index for a ComboBoxString")),
		MCPParam::Optional(TEXT("commitMethod"), EType::String, TEXT("Text and spin box commit type: OnEnter (default), OnUserMovedFocus, OnCleared, Default (#812)")),
	}, MCPSpec::AtLeastOne({ { TEXT("widgetName") }, { TEXT("className") } }));

	// UMG animation authoring, navigation rules, focus and accessibility.
	// Bodies live in WidgetHandlers_Animation.cpp.
	Registry.RegisterHandler(TEXT("create_widget_animation"), &CreateWidgetAnimation, {
		AssetPath(),
		AnimationName(),
		MCPParam::Optional(TEXT("durationSeconds"), EType::Number, TEXT("Playback range length in seconds (default 1)")),
		MCPParam::Optional(TEXT("displayRate"), EType::Number, TEXT("Timeline display rate in fps (default 60)")),
		MCPParam::Optional(TEXT("displayLabel"), EType::String, TEXT("Designer-facing label (defaults to animationName)")),
	});
	Registry.RegisterHandler(TEXT("delete_widget_animation"), &DeleteWidgetAnimation, {
		AssetPath(),
		AnimationName(),
	});
	Registry.RegisterHandler(TEXT("get_widget_animation"), &GetWidgetAnimation, {
		AssetPath(),
		AnimationName(),
	});
	Registry.RegisterHandler(TEXT("add_widget_animation_track"), &AddWidgetAnimationTrack, {
		AssetPath(),
		AnimationName(),
		WidgetName(),
		PropertyName(),
	});
	Registry.RegisterHandler(TEXT("remove_widget_animation_track"), &RemoveWidgetAnimationTrack, {
		AssetPath(),
		AnimationName(),
		WidgetName(),
		PropertyName(),
	});
	Registry.RegisterHandler(TEXT("add_widget_animation_key"), &AddWidgetAnimationKey, {
		AssetPath(),
		AnimationName(),
		WidgetName(),
		PropertyName(),
		MCPParam::Required(TEXT("time"), EType::Number, TEXT("Key time in SECONDS, converted to frames on the animation's tick resolution")),
		MCPParam::Required(TEXT("value"), EType::Any, TEXT("The keyed number")),
		Channel(),
		ChannelIndex(),
		MCPParam::Optional(TEXT("interpolation"), EType::String, TEXT("cubic (default), linear or constant")),
	});
	Registry.RegisterHandler(TEXT("remove_widget_animation_key"), &RemoveWidgetAnimationKey, {
		AssetPath(),
		AnimationName(),
		WidgetName(),
		PropertyName(),
		MCPParam::Required(TEXT("time"), EType::Number, TEXT("Key time in SECONDS, converted to frames on the animation's tick resolution")),
		Channel(),
		ChannelIndex(),
	});
	Registry.RegisterHandler(TEXT("add_widget_animation_event_key"), &AddWidgetAnimationEventKey, {
		AssetPath(),
		AnimationName(),
		MCPParam::Required(TEXT("functionName"), EType::String, TEXT("Widget Blueprint function the event key calls")),
		EventTime(),
		TrackName(),
	});
	Registry.RegisterHandler(TEXT("remove_widget_animation_event_key"), &RemoveWidgetAnimationEventKey, {
		AssetPath(),
		AnimationName(),
		EventTime(),
		TrackName(),
	});
	Registry.RegisterHandler(TEXT("bind_widget_animation_event"), &BindWidgetAnimationEvent, {
		AssetPath(),
		AnimationName(),
		AnimEvent(),
		UserTag(),
	});
	Registry.RegisterHandler(TEXT("unbind_widget_animation_event"), &UnbindWidgetAnimationEvent, {
		AssetPath(),
		AnimationName(),
		AnimEvent(),
		UserTag(),
	});
	// Called once per branch by the contract test: the rules array, then the
	// single write. Either way the asset load fails first.
	Registry.RegisterHandler(TEXT("set_widget_navigation"), &SetWidgetNavigation, {
		AssetPath(),
		MCPParam::Optional(TEXT("rules"), EType::Array, TEXT("Navigation writes applied as one validated batch")).Items(EType::Object).WithFields({
			MCPParam::RequiredField(TEXT("widgetName"), EType::String, TEXT("Widget whose navigation is written")),
			MCPParam::RequiredField(TEXT("direction"), EType::String, TEXT("Up, Down, Left, Right, Next or Previous")),
			MCPParam::OptionalField(TEXT("rule"), EType::String, TEXT("Escape, Explicit (default), Wrap, Stop, Custom or CustomBoundary")),
			MCPParam::OptionalField(TEXT("widgetToFocus"), EType::String, TEXT("Target widget name; required for Explicit")),
		}),
		MCPParam::Optional(TEXT("widgetName"), EType::String, TEXT("Widget whose navigation a single write sets")),
		MCPParam::Optional(TEXT("direction"), EType::String, TEXT("Direction of a single write, which it needs: Up, Down, Left, Right, Next or Previous")),
		MCPParam::Optional(TEXT("rule"), EType::String, TEXT("Rule of a single write: Escape, Explicit (default), Wrap, Stop, Custom or CustomBoundary")),
		MCPParam::Optional(TEXT("widgetToFocus"), EType::String, TEXT("Target widget of a single Explicit write")),
	}, MCPSpec::ExactlyOne({ { TEXT("rules") }, { TEXT("widgetName") } }));
	Registry.RegisterHandler(TEXT("clear_widget_navigation"), &ClearWidgetNavigation, {
		AssetPath(),
		WidgetName(),
		MCPParam::Optional(TEXT("direction"), EType::String, TEXT("Up, Down, Left, Right, Next or Previous; omit to clear all six")),
	});
	Registry.RegisterHandler(TEXT("restore_widget_navigation"), &RestoreWidgetNavigation, {
		AssetPath(),
		MCPParam::Required(TEXT("previous"), EType::Array, TEXT("The captured navigation snapshot set_navigation / clear_navigation return in their rollback payload")).Items(EType::Object),
	});
	Registry.RegisterHandler(TEXT("audit_widget_focus_chain"), &AuditWidgetFocusChain, {
		AssetPath(),
	});
	Registry.RegisterHandler(TEXT("audit_widget_accessibility"), &AuditWidgetAccessibility, {
		AssetPath(),
		MCPParam::Optional(TEXT("minFontSize"), EType::Number, TEXT("Smallest acceptable font size in points (default 12)")),
		MCPParam::Optional(TEXT("minHitSize"), EType::Number, TEXT("Smallest acceptable interactive hit area in slate units (default 40)")),
	});
	Registry.RegisterHandler(TEXT("get_runtime_focus_path"), &GetRuntimeFocusPath, {
		UserIndex(),
	});
	Registry.RegisterHandler(TEXT("set_runtime_focus"), &SetRuntimeFocus, {
		MCPParam::Required(TEXT("widgetName"), EType::String, TEXT("Named child of a live PIE widget, or the live UserWidget's own name")),
		UserIndex(),
		ClassName(),
	});

	// CommonUI. Bodies live in WidgetHandlers_CommonUI.cpp.
	// className names the contract; assetPath also checks that blueprint's tree against it.
	Registry.RegisterHandler(TEXT("get_bind_widget_contract"), &GetBindWidgetContract, {
		MCPParam::Optional(TEXT("className"), EType::String, TEXT("Native UserWidget parent whose contract to read: a short name, a class path, or a Widget Blueprint path")),
		MCPParam::Optional(TEXT("assetPath"), EType::String, TEXT("Widget Blueprint whose parent's contract to read and whose tree to check against it")).Alias(TEXT("path")),
	}, MCPSpec::AtLeastOne({ { TEXT("className") }, { TEXT("assetPath") } }));
	Registry.RegisterHandler(TEXT("audit_commonui"), &AuditCommonUI, {
		MCPParam::Optional(TEXT("assetPath"), EType::String, TEXT("Widget Blueprint whose CommonUI wiring to check as well")).Alias(TEXT("path")),
	});
}

UWidget* FWidgetHandlers::FindWidgetByNameRecursive(UWidget* Root, const FString& WidgetName)
{
	if (!Root) return nullptr;

	if (Root->GetName() == WidgetName)
	{
		return Root;
	}

	UPanelWidget* PanelWidget = Cast<UPanelWidget>(Root);
	if (PanelWidget)
	{
		for (int32 i = 0; i < PanelWidget->GetChildrenCount(); ++i)
		{
			UWidget* Child = PanelWidget->GetChildAt(i);
			UWidget* Found = FindWidgetByNameRecursive(Child, WidgetName);
			if (Found)
			{
				return Found;
			}
		}
	}

	return nullptr;
}

TSharedPtr<FJsonValue> FWidgetHandlers::ListWidgetBlueprints(const TSharedPtr<FJsonObject>& Params)
{
	bool bRecursive = OptionalBool(Params, TEXT("recursive"), true);

	// T3: paged.
	MCPPagination::FPageRequest Page;
	if (auto Err = MCPPagination::ReadPageRequest(
			Params,
			FString::Printf(TEXT("list_widget_blueprints|recursive=%d"), bRecursive ? 1 : 0),
			/*DefaultLimit*/ 200, /*MaxLimit*/ 2000, Page))
	{
		return Err;
	}

	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

	TArray<FAssetData> AssetDataList;
	AssetRegistry.GetAssetsByClass(FTopLevelAssetPath(TEXT("/Script/UMGEditor"), TEXT("WidgetBlueprint")), AssetDataList, bRecursive);

	TArray<MCPPagination::FPageRow> Rows;
	Rows.Reserve(AssetDataList.Num());
	for (const FAssetData& AssetData : AssetDataList)
	{
		TSharedPtr<FJsonObject> AssetObj = MakeShared<FJsonObject>();
		AssetObj->SetStringField(TEXT("name"), AssetData.AssetName.ToString());
		AssetObj->SetStringField(TEXT("path"), AssetData.GetObjectPathString());
		AssetObj->SetStringField(TEXT("packagePath"), AssetData.PackagePath.ToString());
		// The asset's object path is the page anchor: two folders can each hold
		// a WBP_HUD, and a page boundary has to name exactly one of them.
		Rows.Add({ AssetData.GetObjectPathString(), MakeShared<FJsonValueObject>(AssetObj) });
	}
	// The registry returns assets in scan order, which is not a contract.
	Rows.Sort([](const MCPPagination::FPageRow& A, const MCPPagination::FPageRow& B)
		{ return A.Id < B.Id; });

	auto Result = MCPSuccess();
	MCPPagination::EmitPage(Page, Rows, TEXT("assets"), Result);

	return MCPResult(Result);
}

/**
 * Where a widget create action puts its asset: the full assetPath, or name in
 * packagePath (DefaultPackage when omitted). assetPath wins when both arrive,
 * which is what the TS normalizer sends after composing one from the other.
 */
static TSharedPtr<FJsonValue> WidgetCreateTargetFromParams(
	const TSharedPtr<FJsonObject>& Params, const TCHAR* DefaultPackage, FString& OutName, FString& OutPackagePath)
{
	const FString AssetPath = OptionalString(Params, TEXT("assetPath"));
	const FString Name = OptionalString(Params, TEXT("name"));
	const FString PackagePath = OptionalString(Params, TEXT("packagePath"));
	if (!AssetPath.IsEmpty())
	{
		AssetPath.Split(TEXT("/"), &OutPackagePath, &OutName, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		if (OutName.IsEmpty() || OutPackagePath.IsEmpty())
		{
			return MCPError(FString::Printf(TEXT("Invalid assetPath '%s'. Expected '/Game/.../AssetName'"), *AssetPath));
		}
		return nullptr;
	}
	if (!Name.IsEmpty())
	{
		OutName = Name;
		OutPackagePath = PackagePath.IsEmpty() ? FString(DefaultPackage) : PackagePath;
		return nullptr;
	}
	return MCPError(TEXT("Missing required parameter 'assetPath' (or name, with packagePath)"));
}

TSharedPtr<FJsonValue> FWidgetHandlers::CreateWidgetBlueprint(const TSharedPtr<FJsonObject>& Params)
{
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));
	FString ParentClassName = OptionalString(Params, TEXT("parentClass"), TEXT("UserWidget"));
	FString Name;
	FString PackagePath;
	if (auto Err = WidgetCreateTargetFromParams(Params, TEXT("/Game/UI/Widgets"), Name, PackagePath)) return Err;

	// (#134) Resolve parentClass string - accept short names ("UserWidget"),
	// short names with U prefix, and full class paths. Default to UUserWidget
	// only when the caller didn't pass a parentClass.
	UClass* ParentClass = MCPResolveClassOfType(ParentClassName, UUserWidget::StaticClass());
	if (!ParentClass)
	{
		UClass* Other = MCPResolveClass(ParentClassName);
		if (!Other) return MCPClassNotFoundError(ParentClassName, TEXT("parentClass"));
		ParentClass = Other;
	}
	if (!ParentClass->IsChildOf(UUserWidget::StaticClass()))
	{
		return MCPError(FString::Printf(TEXT("parentClass '%s' is not a UUserWidget subclass"), *ParentClassName));
	}

	UWidgetBlueprintFactory* WidgetFactory = NewObject<UWidgetBlueprintFactory>();
	WidgetFactory->ParentClass = ParentClass;

	auto Created = MCPCreateAssetIdempotent<UWidgetBlueprint>(Name, PackagePath, OnConflict, TEXT("WidgetBlueprint"), WidgetFactory);
	if (Created.EarlyReturn) return Created.EarlyReturn;

	// #728: a project can name a default root widget class, so the factory can
	// hand back a blueprint that already owns a widget. Give it its entry in
	// WidgetVariableNameToGuidMap before the asset reaches disk, rather than
	// leaving the first compile to report the missing one.
	const MCPWidgetGuidMap::FSyncReport GuidSync = MCPWidgetGuidMap::Sync(Created.Asset);

	FString SaveError;
	const bool bSaved = SaveAssetPackageChecked(Created.Asset, SaveError);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	MCPNoteSaveOutcome(Result, Created.Asset->GetPathName(), bSaved, SaveError);
	Result->SetStringField(TEXT("path"), Created.Asset->GetPathName());
	Result->SetStringField(TEXT("name"), Name);
	Result->SetStringField(TEXT("parentClass"), ParentClass->GetPathName());
	MCPSetWidgetGuidOutcome(Result, GuidSync, Created.Asset->GetPathName());
	MCPSetDeleteAssetRollback(Result, Created.Asset->GetPathName());

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FWidgetHandlers::ReadWidgetTree(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;

	TSharedPtr<FJsonValue> ResolveError;
	UWidgetBlueprint* WidgetBP = MCPWidget::ResolveWidgetBlueprintOrError(AssetPath, ResolveError);
	if (!WidgetBP) return ResolveError;

	auto Result = MCPSuccess();

	// Recursive lambda to build widget hierarchy
	TFunction<TSharedPtr<FJsonObject>(UWidget*)> BuildWidgetJson = [&](UWidget* Widget) -> TSharedPtr<FJsonObject>
	{
		if (!Widget) return nullptr;

		TSharedPtr<FJsonObject> WidgetObj = MakeShared<FJsonObject>();
		WidgetObj->SetStringField(TEXT("name"), Widget->GetName());
		WidgetObj->SetStringField(TEXT("class"), Widget->GetClass()->GetName());
		WidgetObj->SetBoolField(TEXT("isVisible"), Widget->IsVisible());

		// If it's a panel widget, recurse into children
		UPanelWidget* PanelWidget = Cast<UPanelWidget>(Widget);
		if (PanelWidget)
		{
			TArray<TSharedPtr<FJsonValue>> ChildrenArray;
			for (int32 i = 0; i < PanelWidget->GetChildrenCount(); ++i)
			{
				UWidget* Child = PanelWidget->GetChildAt(i);
				TSharedPtr<FJsonObject> ChildObj = BuildWidgetJson(Child);
				if (ChildObj.IsValid())
				{
					ChildrenArray.Add(MakeShared<FJsonValueObject>(ChildObj));
				}
			}
			WidgetObj->SetArrayField(TEXT("children"), ChildrenArray);
		}

		return WidgetObj;
	};

	// Get the root widget from the WidgetTree
	UWidget* RootWidget = WidgetBP->WidgetTree ? WidgetBP->WidgetTree->RootWidget : nullptr;
	if (RootWidget)
	{
		TSharedPtr<FJsonObject> TreeObj = BuildWidgetJson(RootWidget);
		Result->SetObjectField(TEXT("widgetTree"), TreeObj);
	}
	else
	{
		Result->SetStringField(TEXT("widgetTree"), TEXT("empty"));
	}

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FWidgetHandlers::CreateEditorUtilityWidget(const TSharedPtr<FJsonObject>& Params)
{
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));
	FString PackagePath;
	FString AssetName;
	if (auto Err = WidgetCreateTargetFromParams(Params, TEXT("/Game/EditorUtilities"), AssetName, PackagePath)) return Err;

	UClass* EUWBClass = FindObject<UClass>(nullptr, TEXT("/Script/Blutility.EditorUtilityWidgetBlueprint"));
	if (!EUWBClass)
	{
		return MCPError(TEXT("EditorUtilityWidgetBlueprint class not found. Enable Blutility plugin."));
	}

	UWidgetBlueprintFactory* WidgetFactory = NewObject<UWidgetBlueprintFactory>();
	WidgetFactory->ParentClass = UUserWidget::StaticClass();
	WidgetFactory->BlueprintType = BPTYPE_Normal;

	auto Created = MCPCreateAssetIdempotent<UObject>(AssetName, PackagePath, OnConflict, TEXT("EditorUtilityWidgetBlueprint"), EUWBClass, WidgetFactory);
	if (Created.EarlyReturn) return Created.EarlyReturn;

	// #728: an editor utility widget is a WidgetBlueprint too, and a project
	// that names a default root widget class has the factory build one here.
	MCPWidgetGuidMap::FSyncReport GuidSync;
	if (UWidgetBlueprint* CreatedWidgetBP = Cast<UWidgetBlueprint>(Created.Asset))
	{
		GuidSync = MCPWidgetGuidMap::Sync(CreatedWidgetBP);
	}

	FString SaveError;
	const bool bSaved = SaveAssetPackageChecked(Created.Asset, SaveError);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	MCPNoteSaveOutcome(Result, Created.Asset->GetPathName(), bSaved, SaveError);
	Result->SetStringField(TEXT("path"), Created.Asset->GetPathName());
	Result->SetStringField(TEXT("name"), AssetName);
	MCPSetWidgetGuidOutcome(Result, GuidSync, Created.Asset->GetPathName());
	MCPSetDeleteAssetRollback(Result, Created.Asset->GetPathName());

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FWidgetHandlers::CreateEditorUtilityBlueprint(const TSharedPtr<FJsonObject>& Params)
{
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));
	FString PackagePath;
	FString AssetName;
	if (auto Err = WidgetCreateTargetFromParams(Params, TEXT("/Game/EditorUtilities"), AssetName, PackagePath)) return Err;

	UClass* EUBClass = FindObject<UClass>(nullptr, TEXT("/Script/Blutility.EditorUtilityBlueprint"));
	if (!EUBClass)
	{
		return MCPError(TEXT("EditorUtilityBlueprint class not found. Enable Blutility plugin."));
	}

	auto Created = MCPCreateAssetIdempotent<UObject>(AssetName, PackagePath, OnConflict, TEXT("EditorUtilityBlueprint"), EUBClass, nullptr);
	if (Created.EarlyReturn) return Created.EarlyReturn;

	FString SaveError;
	const bool bSaved = SaveAssetPackageChecked(Created.Asset, SaveError);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	MCPNoteSaveOutcome(Result, Created.Asset->GetPathName(), bSaved, SaveError);
	Result->SetStringField(TEXT("path"), Created.Asset->GetPathName());
	Result->SetStringField(TEXT("name"), AssetName);
	MCPSetDeleteAssetRollback(Result, Created.Asset->GetPathName());

	return MCPResult(Result);
}
TSharedPtr<FJsonValue> FWidgetHandlers::RunEditorUtilityWidget(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;

	// UEditorUtilityWidgetBlueprint derives from UWidgetBlueprint, so it goes
	// through the same revalidating resolver and gets the same stale-handle
	// recovery every other widget action gets (#972).
	TSharedPtr<FJsonValue> ResolveError;
	UWidgetBlueprint* ResolvedBP = MCPWidget::ResolveWidgetBlueprintOrError(AssetPath, ResolveError);
	if (!ResolvedBP) return ResolveError;
	UEditorUtilityWidgetBlueprint* EUWidget = Cast<UEditorUtilityWidgetBlueprint>(ResolvedBP);
	if (!EUWidget)
	{
		return MCPError(FString::Printf(
			TEXT("'%s' is a %s, not an EditorUtilityWidgetBlueprint."),
			*AssetPath, *ResolvedBP->GetClass()->GetName()));
	}

	UEditorUtilitySubsystem* Subsystem = GEditor->GetEditorSubsystem<UEditorUtilitySubsystem>();
	if (!Subsystem)
	{
		return MCPError(TEXT("EditorUtilitySubsystem not available"));
	}

	// No rollback: destructive/external - opens a dockable tab in the editor.
	Subsystem->SpawnAndRegisterTab(EUWidget);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("name"), EUWidget->GetName());
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("This spawns and registers an editor tab, and the widget's own construction script runs inside it. No action closes that tab, ")
		TEXT("and nothing here knows what the widget did once it was open, so there is nothing to undo and no action that would undo it."));

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FWidgetHandlers::RunEditorUtilityBlueprint(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;

	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	UEditorUtilityBlueprint* EUBlueprint = Cast<UEditorUtilityBlueprint>(LoadedAsset);
	if (!EUBlueprint)
	{
		return MCPError(FString::Printf(TEXT("Failed to load EditorUtilityBlueprint at '%s'"), *AssetPath));
	}

	UEditorUtilitySubsystem* Subsystem = GEditor->GetEditorSubsystem<UEditorUtilitySubsystem>();
	if (!Subsystem)
	{
		return MCPError(TEXT("EditorUtilitySubsystem not available"));
	}

	// No rollback: destructive/external - runs an editor utility script.
	Subsystem->TryRun(LoadedAsset);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("name"), EUBlueprint->GetName());
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("This runs a user-authored editor script. What it changed is decided by the blueprint, not by this action, so nothing here ")
		TEXT("can name an inverse. Undo it the way the script's own author would."));

	return MCPResult(Result);
}

// ── Widget class lookup ───────────────────────────────────────────────
/** Friendly lower-case aliases first, then the shared class resolver restricted
 *  to UWidget, then a Widget Blueprint asset path, whose class is generated. */
UClass* MCPWidget::ResolveWidgetClass(const FString& ClassName)
{
	if (ClassName.IsEmpty()) return nullptr;

	static const TMap<FString, FString> ShortNames = {
		// Panels / containers
		{ TEXT("canvaspanel"),       TEXT("/Script/UMG.CanvasPanel") },
		{ TEXT("horizontalbox"),     TEXT("/Script/UMG.HorizontalBox") },
		{ TEXT("verticalbox"),       TEXT("/Script/UMG.VerticalBox") },
		{ TEXT("overlay"),           TEXT("/Script/UMG.Overlay") },
		{ TEXT("gridpanel"),         TEXT("/Script/UMG.GridPanel") },
		{ TEXT("uniformgridpanel"),  TEXT("/Script/UMG.UniformGridPanel") },
		{ TEXT("widgetswitcher"),    TEXT("/Script/UMG.WidgetSwitcher") },
		{ TEXT("scrollbox"),         TEXT("/Script/UMG.ScrollBox") },
		{ TEXT("sizebox"),           TEXT("/Script/UMG.SizeBox") },
		{ TEXT("scalebox"),          TEXT("/Script/UMG.ScaleBox") },
		{ TEXT("border"),            TEXT("/Script/UMG.Border") },
		// Common widgets
		{ TEXT("textblock"),         TEXT("/Script/UMG.TextBlock") },
		{ TEXT("image"),             TEXT("/Script/UMG.Image") },
		{ TEXT("button"),            TEXT("/Script/UMG.Button") },
		{ TEXT("progressbar"),       TEXT("/Script/UMG.ProgressBar") },
		{ TEXT("checkbox"),          TEXT("/Script/UMG.CheckBox") },
		{ TEXT("slider"),            TEXT("/Script/UMG.Slider") },
		{ TEXT("editabletextbox"),   TEXT("/Script/UMG.EditableTextBox") },
		{ TEXT("comboboxstring"),    TEXT("/Script/UMG.ComboBoxString") },
		{ TEXT("spacer"),            TEXT("/Script/UMG.Spacer") },
		{ TEXT("richtextblock"),     TEXT("/Script/UMG.RichTextBlock") },
	};

	if (const FString* AliasPath = ShortNames.Find(ClassName.ToLower()))
	{
		if (UClass* Aliased = MCPResolveClassOfType(*AliasPath, UWidget::StaticClass())) return Aliased;
	}
	if (UClass* Resolved = MCPResolveClassOfType(ClassName, UWidget::StaticClass())) return Resolved;
	if (UBlueprint* BP = LoadAssetByPath<UBlueprint>(ClassName))
	{
		if (BP->GeneratedClass && BP->GeneratedClass->IsChildOf(UWidget::StaticClass())) return BP->GeneratedClass;
	}
	return nullptr;
}

/**
 * Compile state of a Widget Blueprint as a stable string (#799). A caller that
 * gets `created: true` still needs to know whether the asset it just changed
 * compiles, so the mutation handlers report this alongside the outcome.
 */
static FString WidgetCompileStatusString(const UWidgetBlueprint* WidgetBP)
{
	if (!WidgetBP) return TEXT("unknown");
	switch (WidgetBP->Status.GetValue())
	{
	case BS_UpToDate:             return TEXT("upToDate");
	case BS_UpToDateWithWarnings: return TEXT("upToDateWithWarnings");
	case BS_Dirty:                return TEXT("dirty");
	case BS_Error:                return TEXT("error");
	case BS_BeingCreated:         return TEXT("beingCreated");
	default:                      return TEXT("unknown");
	}
}

/** Stamp compile state onto a mutation result, and withdraw the success claim
 *  when the blueprint no longer compiles (#799). */
static void MCPSetWidgetCompileOutcome(
	TSharedPtr<FJsonObject> Result,
	const UWidgetBlueprint* WidgetBP,
	const FString& AssetPath,
	const FString& WhatHappened)
{
	const FString CompileStatus = WidgetCompileStatusString(WidgetBP);
	Result->SetStringField(TEXT("compileStatus"), CompileStatus);
	if (CompileStatus == TEXT("error"))
	{
		// The mutation is already on disk, so keep reporting what landed, but a
		// blueprint that no longer compiles is not a success.
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), FString::Printf(
			TEXT("%s and saved, but '%s' no longer compiles - open the blueprint's compiler results for the cause."),
			*WhatHappened, *AssetPath));
	}
}

TSharedPtr<FJsonValue> FWidgetHandlers::AddWidget(const TSharedPtr<FJsonObject>& Params)
{
	// ── Required: assetPath ──
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;

	// ── Required: widgetClass (e.g. "TextBlock", "CanvasPanel") ──
	FString WidgetClassName;
	if (auto Err = RequireString(Params, TEXT("widgetClass"), WidgetClassName)) return Err;

	// ── Optional: widgetName, parentWidgetName ──
	// `name` and `typeName` are spec aliases, resolved by the registry (#1057).
	FString WidgetName = OptionalString(Params, TEXT("widgetName"));

	FString ParentWidgetName = OptionalString(Params, TEXT("parentWidgetName"));

	// ── Load the WidgetBlueprint ──
	TSharedPtr<FJsonValue> ResolveError;
	UWidgetBlueprint* WidgetBP = MCPWidget::ResolveWidgetBlueprintOrError(AssetPath, ResolveError);
	if (!WidgetBP) return ResolveError;

	if (!WidgetBP->WidgetTree) return MCPWidget::MissingWidgetTreeError(AssetPath);

	// ── Resolve the UClass ──
	UClass* WClass = MCPWidget::ResolveWidgetClass(WidgetClassName);
	if (!WClass)
	{
		return MCPError(FString::Printf(
			TEXT("Unknown widget class '%s'. Short names of loaded UWidget subclasses resolve (TextBlock, ")
			TEXT("CanvasPanel, Image, Button, and every widget a loaded plugin adds), as does a full path ")
			TEXT("(/Script/UMG.TextBlock, /Script/CommonUI.CommonButtonBase) or a Widget Blueprint path. ")
			TEXT("List what this editor actually has with widget(list_classes), optionally filtered by ")
			TEXT("`module` or `filter`. A class from a plugin that is off does not exist until the plugin ")
			TEXT("is enabled with project(enable_plugin) and the editor restarts."), *WidgetClassName));
	}

	// Idempotency by assetPath + widgetName: a caller that retries after an
	// ambiguous result (a client-side timeout on a call the editor actually
	// completed) gets the same answer instead of a duplicate widget (#799).
	// The class is compared too, so a name that already belongs to something
	// else is reported rather than passed off as the requested widget.
	if (!WidgetName.IsEmpty())
	{
		UWidget* Existing = nullptr;
		WidgetBP->WidgetTree->ForEachWidget([&](UWidget* Widget)
		{
			if (Widget && Widget->GetName() == WidgetName) Existing = Widget;
		});
		if (Existing)
		{
			if (Existing->GetClass() != WClass)
			{
				return MCPError(FString::Printf(
					TEXT("Widget '%s' already exists in '%s' as a %s, not a %s. Pick another widgetName or remove the existing widget first."),
					*WidgetName, *AssetPath, *Existing->GetClass()->GetName(), *WClass->GetName()));
			}

			auto ExistingResult = MCPSuccess();
			MCPSetExisted(ExistingResult);
			ExistingResult->SetStringField(TEXT("widgetName"), WidgetName);
			ExistingResult->SetStringField(TEXT("requestedWidgetName"), WidgetName);
			ExistingResult->SetStringField(TEXT("persistedWidgetName"), WidgetName);
			ExistingResult->SetBoolField(TEXT("renamed"), false);
			ExistingResult->SetStringField(TEXT("widgetClass"), Existing->GetClass()->GetName());
			ExistingResult->SetStringField(TEXT("assetPath"), AssetPath);
			if (UPanelWidget* ExistingParent = Existing->GetParent())
			{
				ExistingResult->SetStringField(TEXT("parentWidgetName"), ExistingParent->GetName());
			}
			ExistingResult->SetBoolField(TEXT("isRoot"), WidgetBP->WidgetTree->RootWidget == Existing);
			return MCPResult(ExistingResult);
		}
	}

	// ── Construct the widget ──
	UWidget* NewWidget = WidgetBP->WidgetTree->ConstructWidget<UWidget>(WClass, WidgetName.IsEmpty() ? NAME_None : FName(*WidgetName));
	if (!NewWidget)
	{
		return MCPError(FString::Printf(TEXT("Failed to construct widget of class '%s'"), *WidgetClassName));
	}

	// ── Place in hierarchy ──
	bool bIsRoot = false;
	if (!ParentWidgetName.IsEmpty())
	{
		// Find specified parent
		UWidget* ParentRaw = nullptr;
		WidgetBP->WidgetTree->ForEachWidget([&](UWidget* Widget)
		{
			if (Widget && Widget->GetName() == ParentWidgetName)
			{
				ParentRaw = Widget;
			}
		});

		if (!ParentRaw)
		{
			return MCPError(FString::Printf(TEXT("Parent widget '%s' not found"), *ParentWidgetName));
		}

		UPanelWidget* ParentPanel = Cast<UPanelWidget>(ParentRaw);
		if (!ParentPanel)
		{
			return MCPError(FString::Printf(TEXT("Parent widget '%s' (%s) is not a panel widget and cannot have children"), *ParentWidgetName, *ParentRaw->GetClass()->GetName()));
		}

		UPanelSlot* Slot = ParentPanel->AddChild(NewWidget);
		if (!Slot)
		{
			return MCPError(FString::Printf(TEXT("Failed to add '%s' as child of '%s'"), *NewWidget->GetName(), *ParentWidgetName));
		}
	}
	else if (WidgetBP->WidgetTree->RootWidget == nullptr)
	{
		// No root yet - make this the root widget
		WidgetBP->WidgetTree->RootWidget = NewWidget;
		bIsRoot = true;
	}
	else
	{
		// Root exists, try to add as child of root if it's a panel
		UPanelWidget* RootPanel = Cast<UPanelWidget>(WidgetBP->WidgetTree->RootWidget);
		if (RootPanel)
		{
			RootPanel->AddChild(NewWidget);
		}
		else
		{
			return MCPError(TEXT("Root widget is not a panel. Specify parentWidgetName or set a panel as root first."));
		}
	}

	// ── Save ──
	// Read the name back off the widget after the compile, not before: the
	// compile is what settles the name the asset is saved with (#799).
	TWeakObjectPtr<UWidget> AddedWidget(NewWidget);
	FString PersistedName = NewWidget->GetName();

	WidgetBP->MarkPackageDirty();

	// #728: the WidgetBlueprintCompiler ensures every widget it generates a
	// variable for owns an entry in WidgetVariableNameToGuidMap ("Widget [X]
	// was added but did not get a GUID"). CompileChecked writes the entries
	// first, compiles, then writes them again because the compile can rename a
	// widget whose requested name collided, and refuses to compile at all when
	// an entry could not be written (#799).
	const MCPWidgetGuidMap::FSyncReport GuidSync = MCPWidgetGuidMap::CompileChecked(WidgetBP);
	if (!GuidSync.bCompiled)
	{
		return MCPWidgetGuidMap::BlockedError(AssetPath, GuidSync);
	}

	if (AddedWidget.IsValid())
	{
		PersistedName = AddedWidget->GetName();
	}

	FString SaveError;
	const bool bSaved = SaveAssetPackageChecked(WidgetBP, SaveError);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	MCPNoteSaveOutcome(Result, AssetPath, bSaved, SaveError);
	Result->SetStringField(TEXT("widgetName"), PersistedName);
	// Both names, always: the requested one is what a caller retries with, the
	// persisted one is what the asset actually holds (#799).
	Result->SetStringField(TEXT("persistedWidgetName"), PersistedName);
	if (!WidgetName.IsEmpty())
	{
		Result->SetStringField(TEXT("requestedWidgetName"), WidgetName);
		Result->SetBoolField(TEXT("renamed"), !WidgetName.Equals(PersistedName, ESearchCase::CaseSensitive));
	}
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("widgetClass"), WClass->GetName());
	Result->SetBoolField(TEXT("isRoot"), bIsRoot);
	if (!ParentWidgetName.IsEmpty())
	{
		Result->SetStringField(TEXT("parentWidgetName"), ParentWidgetName);
	}
	MCPSetWidgetGuidOutcome(Result, GuidSync, AssetPath);
	MCPSetWidgetCompileOutcome(Result, WidgetBP, AssetPath,
		FString::Printf(TEXT("Widget '%s' was added"), *PersistedName));

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	Payload->SetStringField(TEXT("widgetName"), PersistedName);
	MCPSetRollback(Result, TEXT("remove_widget"), Payload);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FWidgetHandlers::RemoveWidget(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;

	FString WidgetName;
	if (auto Err = RequireString(Params, TEXT("widgetName"), WidgetName)) return Err;

	TSharedPtr<FJsonValue> ResolveError;
	UWidgetBlueprint* WidgetBP = MCPWidget::ResolveWidgetBlueprintOrError(AssetPath, ResolveError);
	if (!WidgetBP) return ResolveError;

	if (!WidgetBP->WidgetTree) return MCPWidget::MissingWidgetTreeError(AssetPath);

	// Find the widget
	UWidget* FoundWidget = nullptr;
	WidgetBP->WidgetTree->ForEachWidget([&](UWidget* Widget)
	{
		if (Widget && Widget->GetName() == WidgetName)
		{
			FoundWidget = Widget;
		}
	});

	if (!FoundWidget)
	{
		// A widget detached from its parent by an older build is still owned by
		// the WidgetTree, so the compiler still generates a variable for it and
		// still names it in the designer. The walk above cannot see one, which
		// is exactly the asset an agent is trying to clean up here, so look
		// through the tree's own contents before answering "already deleted".
		TArray<UObject*> Owned;
		MCPGetDirectSubobjects(WidgetBP->WidgetTree, Owned);
		for (UObject* Object : Owned)
		{
			UWidget* Orphan = Cast<UWidget>(Object);
			if (Orphan && Orphan->GetName() == WidgetName)
			{
				FoundWidget = Orphan;
				break;
			}
		}
	}

	if (!FoundWidget)
	{
		// Idempotent: nothing to delete. An asset last touched by an older build
		// can still carry the GUID entry of a widget that is already gone, and
		// this is the call an agent makes after the compiler complains about
		// that name, so clear the dead metadata here too (#799).
		const MCPWidgetGuidMap::FSyncReport PruneOnly = MCPWidgetGuidMap::Sync(WidgetBP);
		bool bPruneSaved = true;
		FString PruneSaveError;
		if (PruneOnly.Pruned > 0 || PruneOnly.Added > 0)
		{
			bPruneSaved = SaveAssetPackageChecked(WidgetBP, PruneSaveError);
		}

		auto AlreadyResult = MCPSuccess();
		MCPNoteSaveOutcome(AlreadyResult, AssetPath, bPruneSaved, PruneSaveError);
		AlreadyResult->SetBoolField(TEXT("alreadyDeleted"), true);
		AlreadyResult->SetStringField(TEXT("widgetName"), WidgetName);
		AlreadyResult->SetStringField(TEXT("assetPath"), AssetPath);
		MCPSetWidgetGuidOutcome(AlreadyResult, PruneOnly, AssetPath);
		return MCPResult(AlreadyResult);
	}

	FString RemovedClass = FoundWidget->GetClass()->GetName();
	// Captured for the rollback, before the removal takes the links apart. The
	// class travels as its full path rather than its short name, because
	// add_widget resolves a path exactly while a short name has to be unique
	// among every loaded UWidget subclass - and a widget's own generated class
	// (WBP_Foo_C) is exactly the case a short name can miss.
	const FString RemovedClassPath = FoundWidget->GetClass()->GetPathName();
	const UPanelWidget* RemovedParent = FoundWidget->GetParent();
	const FString RemovedParentName = RemovedParent ? RemovedParent->GetName() : FString();
	const bool bRemovedWasRoot = (WidgetBP->WidgetTree->RootWidget == FoundWidget);
	int32 RemovedChildCount = 0;
	if (const UPanelWidget* RemovedPanel = Cast<UPanelWidget>(FoundWidget))
	{
		RemovedChildCount = RemovedPanel->GetChildrenCount();
	}

	// Hand the removal to the engine FIRST, while the parent link is still
	// intact: UWidgetTree::RemoveWidget detaches the widget from its parent
	// itself and only then drops the tree's own bookkeeping for it. Clearing
	// the parent (or the root pointer) beforehand makes that call a no-op,
	// which is how a widget ends up half removed.
	WidgetBP->WidgetTree->RemoveWidget(FoundWidget);

	// Whatever the engine did not do, do here.
	if (WidgetBP->WidgetTree->RootWidget == FoundWidget)
	{
		WidgetBP->WidgetTree->RootWidget = nullptr;
	}
	if (UPanelWidget* StillParented = FoundWidget->GetParent())
	{
		StillParented->RemoveChild(FoundWidget);
	}

	// #728: unparenting is not removal. The WidgetBlueprintCompiler generates a
	// variable for every widget the WidgetTree OWNS, so a detached widget still
	// outered to the tree is still compiled, still needs a GUID entry, and
	// still holds its name against a later add of the same name. Move the whole
	// removed subtree out of the tree so it stops being part of the blueprint.
	int32 Evicted = 0;
	const TArray<FName> Stuck = MCPWidgetGuidMap::EvictUnreachableWidgets(WidgetBP, Evicted);
	if (Stuck.Num() > 0)
	{
		TArray<FString> StuckNames;
		for (const FName& Name : Stuck) StuckNames.Add(Name.ToString());
		return MCPError(FString::Printf(
			TEXT("Removed '%s' from the hierarchy of '%s' but could not move %s out of the WidgetTree, ")
			TEXT("so the blueprint still owns it. Nothing was compiled or saved: compiling in that state ")
			TEXT("leaves the asset reporting a failure in the UMG editor."),
			*WidgetName, *AssetPath, *FString::Join(StuckNames, TEXT(", "))));
	}

	WidgetBP->MarkPackageDirty();
	MCPWidgetGuidMap::FSyncReport GuidSync = MCPWidgetGuidMap::CompileChecked(WidgetBP);
	if (!GuidSync.bCompiled)
	{
		return MCPWidgetGuidMap::BlockedError(AssetPath, GuidSync);
	}
	GuidSync.Evicted = Evicted;
	FString SaveError;
	const bool bSaved = SaveAssetPackageChecked(WidgetBP, SaveError);

	auto Result = MCPSuccess();
	MCPNoteSaveOutcome(Result, AssetPath, bSaved, SaveError);
	Result->SetBoolField(TEXT("deleted"), true);
	Result->SetStringField(TEXT("widgetName"), WidgetName);
	Result->SetStringField(TEXT("widgetClass"), RemovedClass);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("widgetClassPath"), RemovedClassPath);
	Result->SetStringField(TEXT("previousParentWidgetName"), RemovedParentName);
	Result->SetBoolField(TEXT("wasRoot"), bRemovedWasRoot);
	Result->SetNumberField(TEXT("removedChildCount"), RemovedChildCount);
	MCPSetWidgetGuidOutcome(Result, GuidSync, AssetPath);
	MCPSetWidgetCompileOutcome(Result, WidgetBP, AssetPath,
		FString::Printf(TEXT("Widget '%s' was removed"), *WidgetName));

	// add_widget puts a widget of the same class back under the same parent and
	// under the same name: the removal evicted the old object into the transient
	// package, so the name is free again. What it cannot put back is the state
	// that lived on the removed widget - its property values, its slot layout,
	// its bindings and its whole subtree, all of which went with it. An empty
	// parentWidgetName means "no parent", which is what add_widget needs to see
	// to make the replacement the root again.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	Payload->SetStringField(TEXT("widgetClass"), RemovedClassPath);
	Payload->SetStringField(TEXT("widgetName"), WidgetName);
	if (!RemovedParentName.IsEmpty())
	{
		Payload->SetStringField(TEXT("parentWidgetName"), RemovedParentName);
	}
	MCPSetRollback(Result, TEXT("add_widget"), Payload);
	Result->SetBoolField(TEXT("rollbackLossy"), true);

	FString Placement;
	if (!RemovedParentName.IsEmpty())
	{
		Placement = FString::Printf(TEXT("under '%s'"), *RemovedParentName);
	}
	else if (bRemovedWasRoot)
	{
		Placement = TEXT("as the tree root, which is where add_widget puts a widget when the tree has no root");
	}
	else
	{
		Placement = TEXT("under the tree root - it had no parent panel when it was removed, and add_widget with no parentWidgetName ")
			TEXT("parents to the root rather than leaving it detached");
	}

	Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
		TEXT("widget(add_widget) restores a DEFAULT %s named '%s' %s. Its property values, its slot layout, its designer bindings and its ")
		TEXT("%d direct child widget(s) are NOT restored - the removal moved that whole subtree out of the Widget Blueprint. ")
		TEXT("Read the subtree with widget(read_tree) or widget(get_properties) before removing if any of it matters."),
		*RemovedClass, *WidgetName, *Placement, RemovedChildCount));

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FWidgetHandlers::MoveWidget(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;

	FString WidgetName;
	if (auto Err = RequireString(Params, TEXT("widgetName"), WidgetName)) return Err;

	FString NewParentName;
	if (auto Err = RequireString(Params, TEXT("newParentWidgetName"), NewParentName)) return Err;

	TSharedPtr<FJsonValue> ResolveError;
	UWidgetBlueprint* WidgetBP = MCPWidget::ResolveWidgetBlueprintOrError(AssetPath, ResolveError);
	if (!WidgetBP) return ResolveError;
	if (!WidgetBP->WidgetTree) return MCPWidget::MissingWidgetTreeError(AssetPath);

	// Find the widget to move
	UWidget* WidgetToMove = nullptr;
	UWidget* NewParentRaw = nullptr;
	WidgetBP->WidgetTree->ForEachWidget([&](UWidget* Widget)
	{
		if (Widget && Widget->GetName() == WidgetName) WidgetToMove = Widget;
		if (Widget && Widget->GetName() == NewParentName) NewParentRaw = Widget;
	});

	if (!WidgetToMove)
	{
		return MCPError(FString::Printf(TEXT("Widget not found: '%s'"), *WidgetName));
	}

	if (!NewParentRaw)
	{
		return MCPError(FString::Printf(TEXT("New parent not found: '%s'"), *NewParentName));
	}

	UPanelWidget* NewParentPanel = Cast<UPanelWidget>(NewParentRaw);
	if (!NewParentPanel)
	{
		return MCPError(FString::Printf(TEXT("New parent '%s' (%s) is not a panel widget"), *NewParentName, *NewParentRaw->GetClass()->GetName()));
	}

	// #315: refuse self-parenting and cyclic moves. Walking the WBP root chain
	// down from the new parent and stopping at WidgetToMove would let the move
	// succeed silently while orphaning the entire subtree (read_tree returns
	// empty, the asset cannot reload). Reject before mutating.
	if (NewParentPanel == WidgetToMove)
	{
		return MCPError(FString::Printf(
			TEXT("Refusing cyclic move: cannot reparent '%s' into itself"), *WidgetName));
	}
	{
		UWidget* Ancestor = NewParentPanel;
		while (Ancestor)
		{
			if (Ancestor == WidgetToMove)
			{
				return MCPError(FString::Printf(
					TEXT("Refusing cyclic move: '%s' is an ancestor of '%s' (would create a cycle)"),
					*WidgetName, *NewParentName));
			}
			Ancestor = Ancestor->GetParent();
		}
	}

	// #315: moving the root widget into any other panel orphans the tree (the
	// move clears RootWidget then adds it as a child with no root above it).
	// Use the dedicated wrap/set_root action for that workflow (#365).
	if (WidgetBP->WidgetTree->RootWidget == WidgetToMove)
	{
		return MCPError(FString::Printf(
			TEXT("Cannot move the root widget '%s' via move_widget - use widget(set_root) or widget(wrap_root) instead"),
			*WidgetName));
	}

	// Idempotency: already child of the target parent?
	UPanelWidget* OldParent = WidgetToMove->GetParent();
	FString OldParentName = OldParent ? OldParent->GetName() : TEXT("(root)");
	if (OldParent == NewParentPanel)
	{
		auto Noop = MCPSuccess();
		MCPSetExisted(Noop);
		Noop->SetStringField(TEXT("widgetName"), WidgetName);
		Noop->SetStringField(TEXT("oldParent"), OldParentName);
		Noop->SetStringField(TEXT("newParent"), NewParentName);
		return MCPResult(Noop);
	}

	// Remove from current parent
	if (OldParent)
	{
		OldParent->RemoveChild(WidgetToMove);
	}

	// Add to new parent
	NewParentPanel->AddChild(WidgetToMove);

	WidgetBP->MarkPackageDirty();
	// A reparent changes nothing about which widgets the blueprint owns, but it
	// is still a compile, and the compiler ensures on any widget it generates a
	// variable for without a GUID entry - including one an earlier build left
	// unregistered. Compiling through CompileChecked repairs that instead of
	// turning this call into the one that reports the failure (#728).
	const MCPWidgetGuidMap::FSyncReport GuidSync = MCPWidgetGuidMap::CompileChecked(WidgetBP);
	if (!GuidSync.bCompiled)
	{
		return MCPWidgetGuidMap::BlockedError(AssetPath, GuidSync);
	}
	FString SaveError;
	const bool bSaved = SaveAssetPackageChecked(WidgetBP, SaveError);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	MCPNoteSaveOutcome(Result, AssetPath, bSaved, SaveError);
	Result->SetStringField(TEXT("widgetName"), WidgetName);
	Result->SetStringField(TEXT("oldParent"), OldParentName);
	Result->SetStringField(TEXT("newParent"), NewParentName);
	MCPSetWidgetGuidOutcome(Result, GuidSync, AssetPath);

	// Rollback: move back to old parent if it was a panel
	if (OldParent)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), AssetPath);
		Payload->SetStringField(TEXT("widgetName"), WidgetName);
		Payload->SetStringField(TEXT("newParentWidgetName"), OldParentName);
		MCPSetRollback(Result, TEXT("move_widget"), Payload);
	}

	return MCPResult(Result);
}

// #365: replace the WBP's RootWidget with an existing widget by name. The
// previous root is removed from the tree along with its descendants. Used
// when an authoring step needs to swap a placeholder root (e.g. the
// auto-created CanvasPanel) for a different layout.
TSharedPtr<FJsonValue> FWidgetHandlers::SetRoot(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;

	FString WidgetName;
	if (auto Err = RequireString(Params, TEXT("widgetName"), WidgetName)) return Err;

	TSharedPtr<FJsonValue> ResolveError;
	UWidgetBlueprint* WidgetBP = MCPWidget::ResolveWidgetBlueprintOrError(AssetPath, ResolveError);
	if (!WidgetBP) return ResolveError;
	if (!WidgetBP->WidgetTree) return MCPWidget::MissingWidgetTreeError(AssetPath);

	UWidget* NewRoot = nullptr;
	WidgetBP->WidgetTree->ForEachWidget([&](UWidget* W)
	{
		if (W && W->GetName() == WidgetName) NewRoot = W;
	});
	if (!NewRoot)
	{
		return MCPError(FString::Printf(TEXT("Widget not found: '%s'"), *WidgetName));
	}

	UWidget* OldRoot = WidgetBP->WidgetTree->RootWidget;
	if (OldRoot == NewRoot)
	{
		auto Noop = MCPSuccess();
		MCPSetExisted(Noop);
		Noop->SetStringField(TEXT("rootWidget"), WidgetName);
		return MCPResult(Noop);
	}

	WidgetBP->Modify();
	WidgetBP->WidgetTree->Modify();

	// Detach NewRoot from its current parent so the engine doesn't keep it as
	// a descendant of whatever was hosting it (avoids leaving the new root
	// double-parented when AddChild later reassigns it elsewhere).
	if (UPanelWidget* CurrentParent = NewRoot->GetParent())
	{
		CurrentParent->RemoveChild(NewRoot);
	}

	WidgetBP->WidgetTree->RootWidget = NewRoot;

	// #728: the previous root and its descendants are out of the hierarchy, but
	// the WidgetTree still owns them, and ownership is what makes the compiler
	// generate a variable for a widget. Move them out so the swap actually
	// removes them, rather than leaving a subtree that compiles into variables
	// nothing can reach. The new root was detached from its parent above, so it
	// is reachable from the new root pointer and is never swept up here.
	const FString PreviousRootName = OldRoot ? OldRoot->GetName() : FString(TEXT("(none)"));
	int32 Evicted = 0;
	const TArray<FName> Stuck = MCPWidgetGuidMap::EvictUnreachableWidgets(WidgetBP, Evicted);
	if (Stuck.Num() > 0)
	{
		TArray<FString> StuckNames;
		for (const FName& Name : Stuck) StuckNames.Add(Name.ToString());
		return MCPError(FString::Printf(
			TEXT("'%s' is the new root of '%s' but %s could not be moved out of the WidgetTree, so the ")
			TEXT("blueprint still owns the old subtree. Nothing was compiled or saved: compiling in that ")
			TEXT("state leaves the asset reporting a failure in the UMG editor."),
			*WidgetName, *AssetPath, *FString::Join(StuckNames, TEXT(", "))));
	}

	WidgetBP->MarkPackageDirty();
	MCPWidgetGuidMap::FSyncReport GuidSync = MCPWidgetGuidMap::CompileChecked(WidgetBP);
	if (!GuidSync.bCompiled)
	{
		return MCPWidgetGuidMap::BlockedError(AssetPath, GuidSync);
	}
	GuidSync.Evicted = Evicted;
	FString SaveError;
	const bool bSaved = SaveAssetPackageChecked(WidgetBP, SaveError);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	MCPNoteSaveOutcome(Result, AssetPath, bSaved, SaveError);
	Result->SetStringField(TEXT("rootWidget"), WidgetName);
	Result->SetStringField(TEXT("previousRoot"), PreviousRootName);
	Result->SetNumberField(TEXT("evictedWidgets"), Evicted);
	MCPSetWidgetGuidOutcome(Result, GuidSync, AssetPath);

	// No inverse, and naming one would be a lie. Swapping the root moves the old
	// root and everything under it OUT of the WidgetTree and into the transient
	// package under fresh unique names, so nothing in the asset answers to
	// '<previousRoot>' any more and set_root_widget replayed with that name
	// would fail on "Widget not found". Rebuilding the old subtree would take
	// one add_widget per widget plus every property it carried, which is not one
	// call and not something this action captured.
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
		TEXT("Swapping the root evicted the previous root '%s' and %d widget(s) with it out of the Widget Blueprint, so no call can name ")
		TEXT("them again. Read the tree with widget(read_tree) BEFORE a root swap if it has to be recoverable, or use widget(wrap_root) ")
		TEXT("instead, which keeps the old root as a child and does emit an inverse."),
		*PreviousRootName, Evicted));
	return MCPResult(Result);
}

// #365: insert a new container around the current root - mirrors UMG's
// "Wrap With" context-menu action. The current root becomes a child of the
// new wrapping widget.
TSharedPtr<FJsonValue> FWidgetHandlers::WrapRoot(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;

	FString WrapperClassName;
	if (auto Err = RequireString(Params, TEXT("wrapperClass"), WrapperClassName)) return Err;

	// Read before anything can fail (#1057).
	const FString NewName = OptionalString(Params, TEXT("wrapperName"));

	TSharedPtr<FJsonValue> ResolveError;
	UWidgetBlueprint* WidgetBP = MCPWidget::ResolveWidgetBlueprintOrError(AssetPath, ResolveError);
	if (!WidgetBP) return ResolveError;
	if (!WidgetBP->WidgetTree) return MCPWidget::MissingWidgetTreeError(AssetPath);

	UWidget* OldRoot = WidgetBP->WidgetTree->RootWidget;
	if (!OldRoot)
	{
		return MCPError(TEXT("WBP has no root widget yet - use add_widget to set a root first"));
	}

	UClass* WrapperCls = MCPWidget::ResolveWidgetClass(WrapperClassName);
	if (!WrapperCls)
	{
		return MCPError(FString::Printf(TEXT("Widget class not found: %s"), *WrapperClassName));
	}
	if (!WrapperCls->IsChildOf(UPanelWidget::StaticClass()))
	{
		return MCPError(FString::Printf(
			TEXT("Wrapper class '%s' is not a UPanelWidget - cannot host children"), *WrapperClassName));
	}

	WidgetBP->Modify();
	WidgetBP->WidgetTree->Modify();

	UPanelWidget* Wrapper = Cast<UPanelWidget>(WidgetBP->WidgetTree->ConstructWidget<UWidget>(
		WrapperCls, NewName.IsEmpty() ? NAME_None : FName(*NewName)));
	if (!Wrapper)
	{
		return MCPError(TEXT("Failed to construct wrapper widget"));
	}

	// Widgets the tree OWNS but no longer reaches from the root. They matter
	// because the rollback below runs set_root_widget, and that action calls
	// EvictUnreachableWidgets, which sweeps every unreachable widget rather
	// than only the wrapper this call is about to create. A blueprint already
	// carrying an orphan would lose it permanently on rollback, so it is
	// counted here and reported rather than glossed over. Counted BEFORE the
	// wrap, since the wrap changes what the root reaches.
	int32 PreExistingOrphans = 0;
	{
		TSet<const UObject*> Reachable;
		WidgetBP->WidgetTree->ForEachWidget([&Reachable](UWidget* Widget)
		{
			if (Widget) { Reachable.Add(Widget); }
		});
		for (const auto& Binding : WidgetBP->WidgetTree->NamedSlotBindings)
		{
			if (!Binding.Value) continue;
			Reachable.Add(Binding.Value);
			UWidgetTree::ForWidgetAndChildren(Binding.Value, [&Reachable](UWidget* Widget)
			{
				if (Widget) { Reachable.Add(Widget); }
			});
		}
		TArray<UObject*> Owned;
		MCPGetDirectSubobjects(WidgetBP->WidgetTree, Owned);
		for (UObject* Object : Owned)
		{
			UWidget* Widget = Cast<UWidget>(Object);
			// The wrapper is excluded by POINTER IDENTITY, not by name: a rename
			// cannot break this test. ConstructWidget above already outered the
			// wrapper to the tree, so it shows up in the owned set, and it is not
			// the root yet, so the reachability walk above cannot reach it. Without
			// this exclusion this call's own new widget would be counted as a
			// pre-existing orphan.
			if (Widget && Widget != Wrapper && !Reachable.Contains(Widget)) { ++PreExistingOrphans; }
		}
	}

	WidgetBP->WidgetTree->RootWidget = Wrapper;
	Wrapper->AddChild(OldRoot);

	TWeakObjectPtr<UPanelWidget> AddedWrapper(Wrapper);
	// Read BEFORE the compile, for the same reason the child's name is: if the
	// compile replaces the widget objects the weak pointer goes stale, and the
	// fallback has to be the name this call knew rather than a dereference of a
	// raw pointer that is exactly what went stale.
	FString WrapperName = Wrapper->GetName();
	// Held the same way, and for the same reason: the compile below can
	// replace the widget objects, so the name is read back off a weak pointer
	// rather than off a raw one that may no longer be the live widget.
	TWeakObjectPtr<UWidget> WrappedChild(OldRoot);
	FString WrappedChildName = OldRoot->GetName();

	WidgetBP->MarkPackageDirty();
	// #728: the wrapper is a new widget variable and needs a GUID before the
	// compile that checks for one. CompileChecked writes it, compiles, then
	// writes it again under whatever name the compile settled on.
	const MCPWidgetGuidMap::FSyncReport GuidSync = MCPWidgetGuidMap::CompileChecked(WidgetBP);
	if (!GuidSync.bCompiled)
	{
		return MCPWidgetGuidMap::BlockedError(AssetPath, GuidSync);
	}
	FString SaveError;
	const bool bSaved = SaveAssetPackageChecked(WidgetBP, SaveError);

	if (WrappedChild.IsValid())
	{
		WrappedChildName = WrappedChild->GetName();
	}
	if (AddedWrapper.IsValid())
	{
		WrapperName = AddedWrapper->GetName();
	}

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	MCPNoteSaveOutcome(Result, AssetPath, bSaved, SaveError);
	Result->SetStringField(TEXT("wrapperName"), WrapperName);
	Result->SetStringField(TEXT("wrapperClass"), WrapperCls->GetName());
	Result->SetStringField(TEXT("wrappedChild"), WrappedChildName);
	MCPSetWidgetGuidOutcome(Result, GuidSync, AssetPath);

	// Rooting the wrapped child again is the inverse: set_root_widget detaches
	// the new root from its parent (the wrapper) first, then evicts whatever
	// the tree no longer reaches, and the wrapper is what that is.
	//
	// It is only EXACT when the tree held no orphan already, because the
	// eviction sweeps every unreachable widget rather than the wrapper alone.
	// An orphan that was there before this call is reachable from nothing
	// after the rollback either, so it goes with the wrapper.
	Result->SetNumberField(TEXT("preExistingOrphans"), PreExistingOrphans);
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	Payload->SetStringField(TEXT("widgetName"), WrappedChildName);
	MCPSetRollback(Result, TEXT("set_root_widget"), Payload);
	Result->SetBoolField(TEXT("rollbackLossy"), PreExistingOrphans > 0);
	if (PreExistingOrphans > 0)
	{
		Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
			TEXT("This Widget Blueprint already owns %d widget(s) the tree does not reach from its root. The rollback runs ")
			TEXT("widget(set_root), which moves EVERY unreachable widget out of the blueprint, so those %d would be lost along with ")
			TEXT("the wrapper this call created. Clear them first with widget(remove_widget), or accept the loss."),
			PreExistingOrphans, PreExistingOrphans));
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FWidgetHandlers::ListWidgetClasses(const TSharedPtr<FJsonObject>& Params)
{
	// This used to be a hardcoded array of 21 UMG names, which made every widget
	// outside UMG invisible: a caller could not learn that CommonButtonBase, or
	// the project's own C++ widget, existed at all, and add_widget's short-name
	// resolution only ever looked in /Script/UMG. So the answer is now the real
	// set of loaded UWidget subclasses, grouped by the module that defines them.
	//
	// "Loaded" is the honest word and the result says so. A Widget Blueprint
	// class that nothing has touched this session is not in memory; find those
	// with widget(list) or asset(search). A class from a disabled plugin does
	// not exist at all until project(enable_plugin) and a restart.
	const FString Filter = OptionalString(Params, TEXT("filter"));
	const FString ModuleFilter = OptionalString(Params, TEXT("module"));
	const bool bIncludeAbstract = OptionalBool(Params, TEXT("includeAbstract"), false);
	const bool bIncludeBlueprint = OptionalBool(Params, TEXT("includeBlueprint"), false);

	// T3: paged. Every loaded UWidget subclass is enumerated here, which on a
	// project with UMG, CommonUI and its own widget module runs to four figures.
	// It used to stop at `limit` rows and set `truncated`, which told a caller
	// there were more without giving it any way to read them.
	MCPPagination::FPageRequest Page;
	if (auto Err = MCPPagination::ReadPageRequest(
			Params,
			FString::Printf(TEXT("list_widget_classes|filter=%s|module=%s|includeAbstract=%d|includeBlueprint=%d"),
				*Filter, *ModuleFilter, bIncludeAbstract ? 1 : 0, bIncludeBlueprint ? 1 : 0),
			/*DefaultLimit*/ 300, /*MaxLimit*/ 5000, Page))
	{
		return Err;
	}

	// The slot a panel gives its children is the one thing a name does not tell
	// you, and it is what the next call has to write. Kept from the old curated
	// list, now attached to whatever panel the enumeration finds.
	auto SlotHintFor = [](UClass* PanelClass) -> FString
	{
		for (UClass* C = PanelClass; C; C = C->GetSuperClass())
		{
			const FString Name = C->GetName();
			if (Name == TEXT("CanvasPanel"))
				return TEXT("slot.anchors, slot.alignment, slot.position, slot.size, slot.autoSize, slot.zOrder");
			if (Name == TEXT("HorizontalBox") || Name == TEXT("VerticalBox") || Name == TEXT("ScrollBox"))
				return TEXT("slot.padding, slot.hAlign, slot.vAlign, slot.sizeRule (auto|fill), slot.fillWeight");
			if (Name == TEXT("Overlay") || Name == TEXT("Border") || Name == TEXT("SizeBox") || Name == TEXT("ScaleBox"))
				return TEXT("slot.padding, slot.hAlign, slot.vAlign");
			if (Name == TEXT("GridPanel") || Name == TEXT("UniformGridPanel"))
				return TEXT("slot.row, slot.column, slot.rowSpan, slot.columnSpan, slot.padding, slot.hAlign, slot.vAlign");
			if (Name == TEXT("WidgetSwitcher"))
				return TEXT("slot.padding, slot.hAlign, slot.vAlign");
		}
		return FString();
	};

	struct FRow { UClass* Class = nullptr; FString Module; };
	TArray<FRow> Rows;
	int32 TotalWidgetClasses = 0;

	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Candidate = *It;
		if (!Candidate->IsChildOf(UWidget::StaticClass())) continue;
		if (Candidate == UWidget::StaticClass()) continue;
		++TotalWidgetClasses;

		const bool bIsBlueprint = Candidate->ClassGeneratedBy != nullptr;
		if (bIsBlueprint && !bIncludeBlueprint) continue;
		if (Candidate->HasAnyClassFlags(CLASS_Abstract) && !bIncludeAbstract) continue;
		// Deprecated and editor-hidden classes are not offers a caller should act on.
		if (Candidate->HasAnyClassFlags(CLASS_Deprecated | CLASS_NewerVersionExists)) continue;

		// The package a class lives in IS its module for a native class
		// (/Script/UMG), and its content path for a Blueprint one.
		FString Module = Candidate->GetOutermost()->GetName();
		Module.RemoveFromStart(TEXT("/Script/"));

		if (!ModuleFilter.IsEmpty() && !Module.Contains(ModuleFilter, ESearchCase::IgnoreCase)) continue;
		if (!Filter.IsEmpty() && !Candidate->GetName().Contains(Filter, ESearchCase::IgnoreCase)) continue;

		Rows.Add({ Candidate, MoveTemp(Module) });
	}

	// TObjectIterator walks the object hash, whose order is not a contract, so
	// the rows are sorted before paging. The class PATH is the last tiebreak:
	// two modules can each define a Button, and without it those two swap
	// places between calls and no anchor can resume into the sequence.
	Rows.Sort([](const FRow& A, const FRow& B)
	{
		if (A.Module != B.Module) return A.Module < B.Module;
		if (A.Class->GetName() != B.Class->GetName()) return A.Class->GetName() < B.Class->GetName();
		return A.Class->GetPathName() < B.Class->GetPathName();
	});

	TArray<MCPPagination::FPageRow> PageRows;
	PageRows.Reserve(Rows.Num());
	TSet<FString> ModulesSeen;
	for (const FRow& Row : Rows)
	{
		const bool bIsPanel = Row.Class->IsChildOf(UPanelWidget::StaticClass());
		ModulesSeen.Add(Row.Module);

		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("name"), Row.Class->GetName());
		Obj->SetStringField(TEXT("path"), Row.Class->GetPathName());
		Obj->SetStringField(TEXT("module"), Row.Module);
		Obj->SetStringField(TEXT("parentClass"), Row.Class->GetSuperClass() ? Row.Class->GetSuperClass()->GetName() : FString());
		Obj->SetBoolField(TEXT("isPanel"), bIsPanel);
		Obj->SetBoolField(TEXT("isUserWidget"), Row.Class->IsChildOf(UUserWidget::StaticClass()));
		Obj->SetBoolField(TEXT("isAbstract"), Row.Class->HasAnyClassFlags(CLASS_Abstract));
		Obj->SetBoolField(TEXT("isBlueprint"), Row.Class->ClassGeneratedBy != nullptr);
		// A UserWidget subclass takes its children through BindWidget properties
		// rather than through AddChild, which is why add_widget cannot parent
		// into one. widget(get_bind_widget_contract) reports what it wants.
		Obj->SetBoolField(TEXT("acceptsChildren"), bIsPanel);
#if WITH_EDITOR
		const FString Category = Row.Class->GetMetaData(TEXT("Category"));
		if (!Category.IsEmpty()) Obj->SetStringField(TEXT("category"), Category);
#endif
		if (bIsPanel)
		{
			const FString Hint = SlotHintFor(Row.Class);
			if (!Hint.IsEmpty()) Obj->SetStringField(TEXT("slotProperties"), Hint);
		}

		// The class PATH is the page anchor, not the short name.
		PageRows.Add({ Row.Class->GetPathName(), MakeShared<FJsonValueObject>(Obj) });
	}

	// Every module the WHOLE listing covers, not just this page's, because it
	// is what a caller narrows the next query with.
	TArray<FString> ModuleList = ModulesSeen.Array();
	ModuleList.Sort();

	auto Result = MCPSuccess();
	Result->SetNumberField(TEXT("matched"), PageRows.Num());
	Result->SetNumberField(TEXT("totalLoadedWidgetClasses"), TotalWidgetClasses);
	Result->SetArrayField(TEXT("modules"), MCPStringListToJson(ModuleList));
	MCPPagination::EmitPage(Page, PageRows, TEXT("classes"), Result);
	Result->SetStringField(TEXT("note"), TEXT(
		"Loaded classes only. A Widget Blueprint class nothing has opened this session is absent from "
		"this list; find those with widget(list). A class from a disabled plugin does not exist at all "
		"until project(enable_plugin) and an editor restart. Pass the `name` of any row to "
		"widget(add_widget) as widgetClass, or the `path` when two modules share a name."));

	return MCPResult(Result);
}

// ─────────────────────────────────────────────────────────────
// #160  Runtime widget inspection - live PIE UUserWidget probing
// ─────────────────────────────────────────────────────────────
namespace WidgetRuntime_Internal
{
	// These names are deliberately widget-specific. This namespace is opened with
	// a block-scope using-directive at several call sites, which injects its names
	// into the global namespace at exactly the point where a unity-blob neighbour's
	// anonymous-namespace definitions live. GasHandlers_Runtime.cpp defines a
	// ResolveRuntimeWorld and EditorHandlers_PIERuntime.cpp a VectorJson; those
	// pairs resolved as overloads only by arity and by the absence of an implicit
	// FVector/FVector2D conversion. audit:unity cannot see this class of collision,
	// because it walks anonymous-namespace bodies only.
	struct FDerivedClipState
	{
		bool bHasRect = false;
		bool bAlwaysClip = false;
		FSlateRect Rect;
		FString SourcePath;
	};

	struct FRuntimeLayoutSample
	{
		FVector2D DesiredSize = FVector2D::ZeroVector;
		FVector2D LocalSize = FVector2D::ZeroVector;
		FVector2D AbsoluteSize = FVector2D::ZeroVector;
		FVector2D AbsolutePosition = FVector2D::ZeroVector;
		FSlateRect RenderRect;
		FString SlotSignature;
		bool bHasCanvasSlot = false;
		bool bCanvasAutoSize = false;
		FAnchors CanvasAnchors;
		FMargin CanvasOffsets;
	};

	// Per-call state for the optional layout pass. Bundled into one struct so the
	// recursive walk keeps a readable signature, and so a call that did not ask
	// for layout can skip the whole block by checking a single flag.
	struct FRuntimeScanContext
	{
		bool bIncludeLayout = false;
		TOptional<FSlateRect> ViewportRect;
		const TMap<FString, FRuntimeLayoutSample>* PreviousSamples = nullptr;
		TMap<FString, FRuntimeLayoutSample> CurrentSamples;
		int32 WarningCount = 0;
		int32 ChangedNodeCount = 0;
	};

	static TMap<FString, TMap<FString, FRuntimeLayoutSample>> PreviousLayoutCaptures;
	static TMap<FString, uint64> PreviousLayoutCaptureFrames;
	static uint64 LayoutCaptureSequence = 0;

	static UWorld* ResolveWidgetRuntimeWorld()
	{
		if (!GEditor) return nullptr;
		FWorldContext* PIE = GEditor->GetPIEWorldContext();
		return PIE ? PIE->World() : nullptr;
	}

	static FString SafeGetText(UWidget* Widget)
	{
		if (UTextBlock* T = Cast<UTextBlock>(Widget))       return T->GetText().ToString();
		if (URichTextBlock* R = Cast<URichTextBlock>(Widget)) return R->GetText().ToString();
		if (UEditableTextBox* E = Cast<UEditableTextBox>(Widget)) return E->GetText().ToString();
		if (UButton* B = Cast<UButton>(Widget))
		{
			if (B->GetChildrenCount() > 0)
			{
				return SafeGetText(B->GetChildAt(0));
			}
		}
		return FString();
	}

	static FString VisibilityToString(ESlateVisibility V)
	{
		switch (V)
		{
			case ESlateVisibility::Visible: return TEXT("Visible");
			case ESlateVisibility::Collapsed: return TEXT("Collapsed");
			case ESlateVisibility::Hidden: return TEXT("Hidden");
			case ESlateVisibility::HitTestInvisible: return TEXT("HitTestInvisible");
			case ESlateVisibility::SelfHitTestInvisible: return TEXT("SelfHitTestInvisible");
		}
		return TEXT("Unknown");
	}

	static TSharedPtr<FJsonObject> WidgetVector2DJson(const FVector2D& Value)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetNumberField(TEXT("x"), Value.X);
		Obj->SetNumberField(TEXT("y"), Value.Y);
		return Obj;
	}

	static TSharedPtr<FJsonObject> RectJson(const FSlateRect& Rect)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetNumberField(TEXT("left"), Rect.Left);
		Obj->SetNumberField(TEXT("top"), Rect.Top);
		Obj->SetNumberField(TEXT("right"), Rect.Right);
		Obj->SetNumberField(TEXT("bottom"), Rect.Bottom);
		Obj->SetNumberField(TEXT("width"), Rect.Right - Rect.Left);
		Obj->SetNumberField(TEXT("height"), Rect.Bottom - Rect.Top);
		Obj->SetBoolField(TEXT("valid"), Rect.IsValid());
		return Obj;
	}

	static TSharedPtr<FJsonObject> MarginJson(const FMargin& Margin)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetNumberField(TEXT("left"), Margin.Left);
		Obj->SetNumberField(TEXT("top"), Margin.Top);
		Obj->SetNumberField(TEXT("right"), Margin.Right);
		Obj->SetNumberField(TEXT("bottom"), Margin.Bottom);
		return Obj;
	}

	static FString ClippingToString(EWidgetClipping Clipping)
	{
		if (const UEnum* Enum = StaticEnum<EWidgetClipping>())
		{
			return Enum->GetNameStringByValue(static_cast<int64>(Clipping));
		}
		return TEXT("Unknown");
	}

	static TSharedPtr<FJsonObject> BuildSlotJson(UWidget* Widget, FString& OutSignature)
	{
		UPanelSlot* Slot = Widget ? Widget->Slot : nullptr;
		if (!Slot)
		{
			OutSignature.Reset();
			return nullptr;
		}

		TSharedPtr<FJsonObject> SlotObj = MakeShared<FJsonObject>();
		SlotObj->SetStringField(TEXT("class"), Slot->GetClass()->GetName());

		TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();
		TArray<FString> SignatureParts;
		for (TFieldIterator<FProperty> It(Slot->GetClass()); It; ++It)
		{
			FProperty* Property = *It;
			FString Value;
			const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Slot);
			Property->ExportText_Direct(Value, ValuePtr, ValuePtr, Slot, PPF_None);
			Properties->SetStringField(Property->GetName(), Value);
			SignatureParts.Add(Property->GetName() + TEXT("=") + Value);
		}
		SignatureParts.Sort();
		OutSignature = FString::Join(SignatureParts, TEXT("|"));
		SlotObj->SetObjectField(TEXT("properties"), Properties);

		if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Slot))
		{
			const FAnchors Anchors = CanvasSlot->GetAnchors();
			const FMargin Offsets = CanvasSlot->GetOffsets();
			const FVector2D Alignment = CanvasSlot->GetAlignment();
			TSharedPtr<FJsonObject> Canvas = MakeShared<FJsonObject>();
			TSharedPtr<FJsonObject> AnchorsObj = MakeShared<FJsonObject>();
			AnchorsObj->SetObjectField(TEXT("minimum"), WidgetVector2DJson(Anchors.Minimum));
			AnchorsObj->SetObjectField(TEXT("maximum"), WidgetVector2DJson(Anchors.Maximum));
			AnchorsObj->SetBoolField(TEXT("stretchedHorizontally"), !FMath::IsNearlyEqual(Anchors.Minimum.X, Anchors.Maximum.X));
			AnchorsObj->SetBoolField(TEXT("stretchedVertically"), !FMath::IsNearlyEqual(Anchors.Minimum.Y, Anchors.Maximum.Y));
			Canvas->SetObjectField(TEXT("anchors"), AnchorsObj);
			Canvas->SetObjectField(TEXT("offsets"), MarginJson(Offsets));
			Canvas->SetObjectField(TEXT("alignment"), WidgetVector2DJson(Alignment));
			Canvas->SetBoolField(TEXT("autoSize"), CanvasSlot->GetAutoSize());
			Canvas->SetNumberField(TEXT("zOrder"), CanvasSlot->GetZOrder());
			SlotObj->SetObjectField(TEXT("canvas"), Canvas);
		}

		return SlotObj;
	}

	static void AddWarning(
		TArray<TSharedPtr<FJsonValue>>& Warnings,
		const FString& Code,
		const FString& Severity,
		const FString& Message)
	{
		TSharedPtr<FJsonObject> Warning = MakeShared<FJsonObject>();
		Warning->SetStringField(TEXT("code"), Code);
		Warning->SetStringField(TEXT("severity"), Severity);
		Warning->SetStringField(TEXT("message"), Message);
		Warnings.Add(MakeShared<FJsonValueObject>(Warning));
	}

	static bool VectorNearlyEqual(const FVector2D& A, const FVector2D& B, double Tolerance = 0.05)
	{
		return A.Equals(B, Tolerance);
	}

	static FDerivedClipState ResolveClipState(
		UWidget* Widget,
		const FString& WidgetPath,
		const FGeometry& Geometry,
		const FVector2D& DesiredSize,
		const FDerivedClipState& ParentClip)
	{
		FDerivedClipState Result = ParentClip;
		const EWidgetClipping Clipping = Widget->GetClipping();
		const FSlateRect WidgetBounds = Geometry.GetRenderBoundingRect();

		bool bApplyOwnBounds = false;
		bool bIntersectParent = true;
		bool bAlwaysClip = ParentClip.bAlwaysClip;
		switch (Clipping)
		{
			case EWidgetClipping::ClipToBounds:
				bApplyOwnBounds = true;
				break;
			case EWidgetClipping::ClipToBoundsWithoutIntersecting:
				bApplyOwnBounds = true;
				bIntersectParent = ParentClip.bAlwaysClip;
				break;
			case EWidgetClipping::ClipToBoundsAlways:
				bApplyOwnBounds = true;
				bAlwaysClip = true;
				break;
			case EWidgetClipping::OnDemand:
			{
				const FVector2D LocalSize = Geometry.GetLocalSize();
				bApplyOwnBounds = DesiredSize.X > LocalSize.X + 0.05 || DesiredSize.Y > LocalSize.Y + 0.05;
				break;
			}
			case EWidgetClipping::Inherit:
			default:
				break;
		}

		if (bApplyOwnBounds)
		{
			Result.bHasRect = true;
			Result.bAlwaysClip = bAlwaysClip;
			Result.SourcePath = WidgetPath;
			if (ParentClip.bHasRect && bIntersectParent)
			{
				Result.Rect = ParentClip.Rect.IntersectionWith(WidgetBounds);
			}
			else
			{
				Result.Rect = WidgetBounds;
			}
		}
		return Result;
	}

	// Seed lets the caller start from the hosting UUserWidget's clip state, which
	// is not reachable through GetParent() from a widget-tree root.
	static FDerivedClipState ResolveAncestorClipState(
		UWidget* Widget,
		const FDerivedClipState& Seed = FDerivedClipState(),
		const FString& PathPrefix = FString())
	{
		TArray<UWidget*> Ancestors;
		for (UPanelWidget* Parent = Widget ? Widget->GetParent() : nullptr; Parent; Parent = Parent->GetParent())
		{
			Ancestors.Add(Parent);
		}

		FDerivedClipState Result = Seed;
		FString AncestorPath = PathPrefix;
		for (int32 Index = Ancestors.Num() - 1; Index >= 0; --Index)
		{
			UWidget* Ancestor = Ancestors[Index];
			AncestorPath += TEXT("/") + Ancestor->GetName();
			Result = ResolveClipState(
				Ancestor,
				AncestorPath,
				Ancestor->GetCachedGeometry(),
				Ancestor->GetDesiredSize(),
				Result);
		}
		return Result;
	}

	static double ResolveAncestorOpacity(UWidget* Widget, double Seed = 1.0)
	{
		double Result = Seed;
		for (UPanelWidget* Parent = Widget ? Widget->GetParent() : nullptr; Parent; Parent = Parent->GetParent())
		{
			Result *= Parent->GetRenderOpacity();
		}
		return Result;
	}

	static TSharedPtr<FJsonObject> BuildRuntimeNode(
		UWidget* Widget,
		int32 Depth,
		int32 MaxDepth,
		const FString& WidgetPath,
		const FDerivedClipState& ParentClip,
		double ParentEffectiveOpacity,
		FRuntimeScanContext& Ctx)
	{
		if (!Widget) return nullptr;
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("name"), Widget->GetName());
		Obj->SetStringField(TEXT("class"), Widget->GetClass()->GetName());
		if (Ctx.bIncludeLayout)
		{
			// Named widgetPath, not path: everywhere else in this category
			// "path" is the asset path, and one name meaning two things inside
			// the same tool is what #798 was filed about.
			Obj->SetStringField(TEXT("widgetPath"), WidgetPath);
		}
		Obj->SetStringField(TEXT("visibility"), VisibilityToString(Widget->GetVisibility()));
		Obj->SetBoolField(TEXT("isVisible"), Widget->IsVisible());

		FString Text = SafeGetText(Widget);
		if (!Text.IsEmpty())
		{
			Obj->SetStringField(TEXT("text"), Text);
		}

		if (UImage* Image = Cast<UImage>(Widget))
		{
			const FSlateBrush& Brush = Image->GetBrush();
			TSharedPtr<FJsonObject> BrushObj = MakeShared<FJsonObject>();
			BrushObj->SetNumberField(TEXT("imageSizeX"), Brush.ImageSize.X);
			BrushObj->SetNumberField(TEXT("imageSizeY"), Brush.ImageSize.Y);
			if (UObject* Resource = Brush.GetResourceObject())
			{
				BrushObj->SetStringField(TEXT("resource"), Resource->GetPathName());
			}
			Obj->SetObjectField(TEXT("brush"), BrushObj);
		}
		else if (UProgressBar* PB = Cast<UProgressBar>(Widget))
		{
			Obj->SetNumberField(TEXT("percent"), PB->GetPercent());
		}
		else if (UCheckBox* CB = Cast<UCheckBox>(Widget))
		{
			Obj->SetBoolField(TEXT("isChecked"), CB->IsChecked());
		}
		else if (USlider* Slider = Cast<USlider>(Widget))
		{
			Obj->SetNumberField(TEXT("value"), Slider->GetValue());
		}

		// #592: style properties needed to verify visuals at runtime, not just
		// tree/text. RenderOpacity applies to every UWidget; ColorAndOpacity and
		// Border tint are per-type.
		{
			auto ColorJson = [](const FLinearColor& C)
			{
				TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
				O->SetNumberField(TEXT("r"), C.R); O->SetNumberField(TEXT("g"), C.G);
				O->SetNumberField(TEXT("b"), C.B); O->SetNumberField(TEXT("a"), C.A);
				return O;
			};
			Obj->SetNumberField(TEXT("renderOpacity"), Widget->GetRenderOpacity());
			if (UTextBlock* TextW = Cast<UTextBlock>(Widget))
			{
				Obj->SetObjectField(TEXT("colorAndOpacity"), ColorJson(TextW->GetColorAndOpacity().GetSpecifiedColor()));
			}
			else if (UImage* ImgW = Cast<UImage>(Widget))
			{
				Obj->SetObjectField(TEXT("colorAndOpacity"), ColorJson(ImgW->GetColorAndOpacity()));
			}
			else if (UBorder* BorderW = Cast<UBorder>(Widget))
			{
				Obj->SetObjectField(TEXT("brushColor"), ColorJson(BorderW->GetBrushColor()));
				Obj->SetObjectField(TEXT("contentColorAndOpacity"), ColorJson(BorderW->GetContentColorAndOpacity()));
			}
		}

		// Layout diagnostics are opt-in: the geometry, slot reflection and delta
		// blocks below multiply the size of a get_runtime payload, and only a caller
		// debugging layout needs them.
		FDerivedClipState EffectiveClip = ParentClip;
		double EffectiveOpacity = ParentEffectiveOpacity;
		if (Ctx.bIncludeLayout)
		{
			const bool bHasCachedSlateWidget = Widget->GetCachedWidget().IsValid();
			const FGeometry& Geometry = Widget->GetCachedGeometry();
			const FVector2D DesiredSize = Widget->GetDesiredSize();
			const FVector2D LocalSize = Geometry.GetLocalSize();
			const FVector2D AbsoluteSize = Geometry.GetAbsoluteSize();
			const FSlateRect LayoutRect = Geometry.GetLayoutBoundingRect();
			const FSlateRect RenderRect = Geometry.GetRenderBoundingRect();
			const FVector2D AbsolutePosition(RenderRect.Left, RenderRect.Top);
			const FWidgetTransform& RenderTransform = Widget->GetRenderTransform();
			EffectiveClip = ResolveClipState(Widget, WidgetPath, Geometry, DesiredSize, ParentClip);
			EffectiveOpacity = ParentEffectiveOpacity * Widget->GetRenderOpacity();

			TSharedPtr<FJsonObject> GeometryObj = MakeShared<FJsonObject>();
			GeometryObj->SetBoolField(TEXT("hasCachedSlateWidget"), bHasCachedSlateWidget);
			GeometryObj->SetObjectField(TEXT("desiredSize"), WidgetVector2DJson(DesiredSize));
			GeometryObj->SetObjectField(TEXT("localSize"), WidgetVector2DJson(LocalSize));
			GeometryObj->SetObjectField(TEXT("absoluteSize"), WidgetVector2DJson(AbsoluteSize));
			GeometryObj->SetObjectField(TEXT("absolutePosition"), WidgetVector2DJson(AbsolutePosition));
			GeometryObj->SetObjectField(TEXT("layoutBoundingRect"), RectJson(LayoutRect));
			GeometryObj->SetObjectField(TEXT("renderBoundingRect"), RectJson(RenderRect));
			GeometryObj->SetNumberField(TEXT("accumulatedLayoutScale"), Geometry.GetAccumulatedLayoutTransform().GetScale());
			Obj->SetObjectField(TEXT("geometry"), GeometryObj);

			TSharedPtr<FJsonObject> TransformObj = MakeShared<FJsonObject>();
			TransformObj->SetObjectField(TEXT("translation"), WidgetVector2DJson(RenderTransform.Translation));
			TransformObj->SetObjectField(TEXT("scale"), WidgetVector2DJson(RenderTransform.Scale));
			TransformObj->SetObjectField(TEXT("shear"), WidgetVector2DJson(RenderTransform.Shear));
			TransformObj->SetNumberField(TEXT("angleDegrees"), RenderTransform.Angle);
			TransformObj->SetObjectField(TEXT("pivot"), WidgetVector2DJson(Widget->GetRenderTransformPivot()));
			Obj->SetObjectField(TEXT("renderTransform"), TransformObj);

			TSharedPtr<FJsonObject> ClipObj = MakeShared<FJsonObject>();
			ClipObj->SetStringField(TEXT("authoredMode"), ClippingToString(Widget->GetClipping()));
			ClipObj->SetBoolField(TEXT("hasDerivedEffectiveRect"), EffectiveClip.bHasRect);
			ClipObj->SetBoolField(TEXT("alwaysClip"), EffectiveClip.bAlwaysClip);
			if (EffectiveClip.bHasRect)
			{
				ClipObj->SetObjectField(TEXT("derivedEffectiveRect"), RectJson(EffectiveClip.Rect));
				ClipObj->SetStringField(TEXT("sourcePath"), EffectiveClip.SourcePath);
				bool bOverlapping = false;
				const FSlateRect VisibleRect = RenderRect.IntersectionWith(EffectiveClip.Rect, bOverlapping);
				const bool bFullyClipped = !bOverlapping || VisibleRect.IsEmpty();
				const bool bPartiallyClipped = !bFullyClipped && VisibleRect.GetArea() + 0.05f < RenderRect.GetArea();
				ClipObj->SetBoolField(TEXT("fullyClipped"), bFullyClipped);
				ClipObj->SetBoolField(TEXT("partiallyClipped"), bPartiallyClipped);
				ClipObj->SetObjectField(TEXT("visibleRect"), RectJson(VisibleRect));
			}
			else
			{
				ClipObj->SetBoolField(TEXT("fullyClipped"), false);
				ClipObj->SetBoolField(TEXT("partiallyClipped"), false);
			}
			ClipObj->SetStringField(
				TEXT("derivation"),
				TEXT("Computed from UMG clipping modes and cached render bounds; use a native Widget Reflector snapshot for paint-element clip stacks."));
			Obj->SetObjectField(TEXT("clipping"), ClipObj);

			TSharedPtr<FJsonObject> ViewportObj = MakeShared<FJsonObject>();
			ViewportObj->SetBoolField(TEXT("available"), Ctx.ViewportRect.IsSet());
			if (Ctx.ViewportRect.IsSet())
			{
				ViewportObj->SetObjectField(TEXT("rect"), RectJson(Ctx.ViewportRect.GetValue()));
				bool bOverlapsViewport = false;
				const FSlateRect ViewportIntersection =
					RenderRect.IntersectionWith(Ctx.ViewportRect.GetValue(), bOverlapsViewport);
				const bool bOutsideViewport = !bOverlapsViewport || ViewportIntersection.IsEmpty();
				const bool bPartiallyOutsideViewport =
					!bOutsideViewport && ViewportIntersection.GetArea() + 0.05f < RenderRect.GetArea();
				ViewportObj->SetBoolField(TEXT("overlaps"), bOverlapsViewport);
				ViewportObj->SetBoolField(TEXT("fullyOutside"), bOutsideViewport);
				ViewportObj->SetBoolField(TEXT("partiallyOutside"), bPartiallyOutsideViewport);
				ViewportObj->SetObjectField(TEXT("intersectionRect"), RectJson(ViewportIntersection));
			}
			Obj->SetObjectField(TEXT("viewport"), ViewportObj);

			if (UPanelWidget* Parent = Widget->GetParent())
			{
				const FSlateRect ParentRect = Parent->GetCachedGeometry().GetRenderBoundingRect();
				TSharedPtr<FJsonObject> ParentLayout = MakeShared<FJsonObject>();
				ParentLayout->SetStringField(TEXT("name"), Parent->GetName());
				ParentLayout->SetStringField(TEXT("class"), Parent->GetClass()->GetName());
				ParentLayout->SetObjectField(TEXT("renderBoundingRect"), RectJson(ParentRect));
				bool bOverlapsParent = false;
				RenderRect.IntersectionWith(ParentRect, bOverlapsParent);
				ParentLayout->SetBoolField(TEXT("overlapsParentBounds"), bOverlapsParent);
				ParentLayout->SetBoolField(
					TEXT("extendsOutsideParentBounds"),
					RenderRect.Left < ParentRect.Left - 0.05f ||
					RenderRect.Top < ParentRect.Top - 0.05f ||
					RenderRect.Right > ParentRect.Right + 0.05f ||
					RenderRect.Bottom > ParentRect.Bottom + 0.05f);
				Obj->SetObjectField(TEXT("parentLayout"), ParentLayout);
			}

			FString SlotSignature;
			if (TSharedPtr<FJsonObject> SlotObj = BuildSlotJson(Widget, SlotSignature))
			{
				Obj->SetObjectField(TEXT("slot"), SlotObj);
			}

			FRuntimeLayoutSample Sample;
			Sample.DesiredSize = DesiredSize;
			Sample.LocalSize = LocalSize;
			Sample.AbsoluteSize = AbsoluteSize;
			Sample.AbsolutePosition = AbsolutePosition;
			Sample.RenderRect = RenderRect;
			Sample.SlotSignature = SlotSignature;
			if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Widget->Slot))
			{
				Sample.bHasCanvasSlot = true;
				Sample.bCanvasAutoSize = CanvasSlot->GetAutoSize();
				Sample.CanvasAnchors = CanvasSlot->GetAnchors();
				Sample.CanvasOffsets = CanvasSlot->GetOffsets();
			}
			Ctx.CurrentSamples.Add(WidgetPath, Sample);

			TArray<TSharedPtr<FJsonValue>> Warnings;
			if (!bHasCachedSlateWidget)
			{
				AddWarning(
					Warnings,
					TEXT("geometry_unavailable"),
					TEXT("warning"),
					TEXT("The Slate widget has not been constructed or painted, so cached geometry may be empty or stale."));
			}
			if (DesiredSize.X > LocalSize.X + 0.5 || DesiredSize.Y > LocalSize.Y + 0.5)
			{
				AddWarning(
					Warnings,
					TEXT("desired_size_exceeds_allocation"),
					TEXT("info"),
					FString::Printf(
						TEXT("Desired size %.2fx%.2f exceeds allocated local size %.2fx%.2f; clipping or compression may occur."),
						DesiredSize.X,
						DesiredSize.Y,
						LocalSize.X,
						LocalSize.Y));
			}
			if (Sample.bHasCanvasSlot)
			{
				const bool bStretchX = !FMath::IsNearlyEqual(Sample.CanvasAnchors.Minimum.X, Sample.CanvasAnchors.Maximum.X);
				const bool bStretchY = !FMath::IsNearlyEqual(Sample.CanvasAnchors.Minimum.Y, Sample.CanvasAnchors.Maximum.Y);
				if (!Sample.bCanvasAutoSize && bStretchX && !FMath::IsNearlyZero(Sample.CanvasOffsets.Right))
				{
					AddWarning(
						Warnings,
						TEXT("stretched_canvas_right_is_margin"),
						TEXT("info"),
						TEXT("This Canvas slot is horizontally stretched: Offsets.Right is a right margin, not a width."));
				}
				if (!Sample.bCanvasAutoSize && bStretchY && !FMath::IsNearlyZero(Sample.CanvasOffsets.Bottom))
				{
					AddWarning(
						Warnings,
						TEXT("stretched_canvas_bottom_is_margin"),
						TEXT("warning"),
						TEXT("This Canvas slot is vertically stretched: Offsets.Bottom is a bottom margin, not a height. SetSize can therefore make height position-dependent."));
				}
			}

			TSharedPtr<FJsonObject> DeltaObj = MakeShared<FJsonObject>();
			bool bChanged = false;
			if (Ctx.PreviousSamples)
			{
				if (const FRuntimeLayoutSample* Previous = Ctx.PreviousSamples->Find(WidgetPath))
				{
					const FVector2D PositionDelta = Sample.AbsolutePosition - Previous->AbsolutePosition;
					const FVector2D LocalSizeDelta = Sample.LocalSize - Previous->LocalSize;
					const FVector2D AbsoluteSizeDelta = Sample.AbsoluteSize - Previous->AbsoluteSize;
					const FVector2D DesiredSizeDelta = Sample.DesiredSize - Previous->DesiredSize;
					const bool bSlotChanged = Sample.SlotSignature != Previous->SlotSignature;
					bChanged =
						!VectorNearlyEqual(PositionDelta, FVector2D::ZeroVector) ||
						!VectorNearlyEqual(LocalSizeDelta, FVector2D::ZeroVector) ||
						!VectorNearlyEqual(AbsoluteSizeDelta, FVector2D::ZeroVector) ||
						!VectorNearlyEqual(DesiredSizeDelta, FVector2D::ZeroVector) ||
						bSlotChanged;
					DeltaObj->SetBoolField(TEXT("hasPreviousCapture"), true);
					DeltaObj->SetBoolField(TEXT("changed"), bChanged);
					DeltaObj->SetObjectField(TEXT("absolutePositionDelta"), WidgetVector2DJson(PositionDelta));
					DeltaObj->SetObjectField(TEXT("localSizeDelta"), WidgetVector2DJson(LocalSizeDelta));
					DeltaObj->SetObjectField(TEXT("absoluteSizeDelta"), WidgetVector2DJson(AbsoluteSizeDelta));
					DeltaObj->SetObjectField(TEXT("desiredSizeDelta"), WidgetVector2DJson(DesiredSizeDelta));
					DeltaObj->SetBoolField(TEXT("slotPropertiesChanged"), bSlotChanged);
					if (bChanged)
					{
						++Ctx.ChangedNodeCount;
					}

					if (Sample.bHasCanvasSlot && Previous->bHasCanvasSlot)
					{
						const bool bStretchY =
							!FMath::IsNearlyEqual(Sample.CanvasAnchors.Minimum.Y, Sample.CanvasAnchors.Maximum.Y);
						const bool bMovedVertically = !FMath::IsNearlyZero(PositionDelta.Y, 0.25);
						const bool bHeightChanged = !FMath::IsNearlyZero(LocalSizeDelta.Y, 0.25);
						const bool bInverseMovement =
							FMath::IsNearlyEqual(LocalSizeDelta.Y, -PositionDelta.Y, 1.0);
						if (bStretchY && !Sample.bCanvasAutoSize && bMovedVertically && bHeightChanged && bInverseMovement)
						{
							AddWarning(
								Warnings,
								TEXT("position_dependent_canvas_height"),
								TEXT("error"),
								FString::Printf(
									TEXT("Moving the widget by %.2f px changed its height by %.2f px in the opposite direction. A vertically stretched Canvas slot is treating Bottom as a margin."),
									PositionDelta.Y,
									LocalSizeDelta.Y));
						}
					}
				}
				else
				{
					DeltaObj->SetBoolField(TEXT("hasPreviousCapture"), false);
					DeltaObj->SetBoolField(TEXT("changed"), false);
					DeltaObj->SetStringField(TEXT("reason"), TEXT("Widget path was not present in the previous capture."));
				}
			}
			else
			{
				DeltaObj->SetBoolField(TEXT("hasPreviousCapture"), false);
				DeltaObj->SetBoolField(TEXT("changed"), false);
				DeltaObj->SetStringField(TEXT("reason"), TEXT("This is the baseline capture for the runtime widget instance."));
			}
			Obj->SetObjectField(TEXT("deltaSincePreviousCapture"), DeltaObj);
			Obj->SetNumberField(TEXT("effectiveRenderOpacity"), EffectiveOpacity);
			Obj->SetArrayField(TEXT("diagnostics"), Warnings);
			Ctx.WarningCount += Warnings.Num();
		}

		if (Depth >= MaxDepth) return Obj;

		if (UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
		{
			TArray<TSharedPtr<FJsonValue>> ChildrenArr;
			for (int32 i = 0; i < Panel->GetChildrenCount(); ++i)
			{
				UWidget* Child = Panel->GetChildAt(i);
				const FString ChildPath = WidgetPath + TEXT("/") + (Child ? Child->GetName() : FString::Printf(TEXT("child_%d"), i));
				TSharedPtr<FJsonObject> ChildObj = BuildRuntimeNode(
					Child,
					Depth + 1,
					MaxDepth,
					ChildPath,
					EffectiveClip,
					EffectiveOpacity,
					Ctx);
				if (ChildObj.IsValid())
				{
					ChildrenArr.Add(MakeShared<FJsonValueObject>(ChildObj));
				}
			}
			Obj->SetArrayField(TEXT("children"), ChildrenArr);
		}
		else if (UUserWidget* User = Cast<UUserWidget>(Widget))
		{
			// Nested UUserWidget: descend into its WidgetTree's root.
			if (User->WidgetTree && User->WidgetTree->RootWidget)
			{
				UWidget* RootWidget = User->WidgetTree->RootWidget;
				const FString RootPath = WidgetPath + TEXT("/root:") + RootWidget->GetName();
				TSharedPtr<FJsonObject> RootObj = BuildRuntimeNode(
					RootWidget,
					Depth + 1,
					MaxDepth,
					RootPath,
					EffectiveClip,
					EffectiveOpacity,
					Ctx);
				if (RootObj.IsValid())
				{
					Obj->SetObjectField(TEXT("root"), RootObj);
				}
			}
		}

		return Obj;
	}
}

TSharedPtr<FJsonValue> FWidgetHandlers::ListRuntimeWidgets(const TSharedPtr<FJsonObject>& Params)
{
	using namespace WidgetRuntime_Internal;

	// Every parameter is read before anything can fail (#1057).
	// Optional filter: class name (contains) / name prefix
	const FString ClassFilter = OptionalString(Params, TEXT("classFilter"), TEXT(""));
	const FString NamePrefix  = OptionalString(Params, TEXT("namePrefix"), TEXT(""));
	const bool bInViewportOnly = OptionalBool(Params, TEXT("viewportOnly"), false);

	// T3: paged.
	MCPPagination::FPageRequest Page;
	if (auto Err = MCPPagination::ReadPageRequest(
			Params,
			FString::Printf(TEXT("list_runtime_widgets|classFilter=%s|namePrefix=%s|viewportOnly=%d"),
				*ClassFilter, *NamePrefix, bInViewportOnly ? 1 : 0),
			/*DefaultLimit*/ 200, /*MaxLimit*/ 2000, Page))
	{
		return Err;
	}

	UWorld* World = ResolveWidgetRuntimeWorld();
	if (!World)
	{
		return MCPError(TEXT("No PIE world available. Is Play-In-Editor running?"));
	}

	TArray<MCPPagination::FPageRow> Rows;
	for (TObjectIterator<UUserWidget> It; It; ++It)
	{
		UUserWidget* Widget = *It;
		if (!IsValid(Widget)) continue;
		if (Widget->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject)) continue;

		UWorld* WidgetWorld = Widget->GetWorld();
		if (WidgetWorld != World) continue;

		const FString ClassName = Widget->GetClass()->GetName();
		const FString Name = Widget->GetName();
		if (!ClassFilter.IsEmpty() && !ClassName.Contains(ClassFilter)) continue;
		if (!NamePrefix.IsEmpty()  && !Name.StartsWith(NamePrefix)) continue;
		if (bInViewportOnly && !Widget->IsInViewport()) continue;

		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("name"), Name);
		Obj->SetStringField(TEXT("class"), ClassName);
		Obj->SetStringField(TEXT("visibility"), VisibilityToString(Widget->GetVisibility()));
		Obj->SetBoolField(TEXT("isVisible"), Widget->IsVisible());
		Obj->SetBoolField(TEXT("inViewport"), Widget->IsInViewport());
		if (Widget->WidgetTree && Widget->WidgetTree->RootWidget)
		{
			Obj->SetStringField(TEXT("rootWidgetName"), Widget->WidgetTree->RootWidget->GetName());
			Obj->SetStringField(TEXT("rootWidgetClass"), Widget->WidgetTree->RootWidget->GetClass()->GetName());
		}
		// The widget instance's OBJECT PATH is the page anchor. Two PIE widgets
		// can share a display name, and only the path names one of them.
		Rows.Add({ Widget->GetPathName(), MakeShared<FJsonValueObject>(Obj) });
	}

	// TObjectIterator walks the object hash, whose order is not a contract and
	// which moves as widgets are constructed and torn down during play, so the
	// rows are sorted by path before paging.
	Rows.Sort([](const MCPPagination::FPageRow& A, const MCPPagination::FPageRow& B)
		{ return A.Id < B.Id; });

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("world"), World->GetName());
	MCPPagination::EmitPage(Page, Rows, TEXT("widgets"), Result);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FWidgetHandlers::GetRuntimeWidget(const TSharedPtr<FJsonObject>& Params)
{
	using namespace WidgetRuntime_Internal;

	FString WidgetName;
	TryGetStringParam(Params, TEXT("widgetName"), WidgetName);
	FString ClassFilter;
	TryGetStringParam(Params, TEXT("className"), ClassFilter);
	const int32 MaxDepth = OptionalInt(Params, TEXT("maxDepth"), 6);
	const FString ChildName = OptionalString(Params, TEXT("childName"), TEXT(""));
	const bool bIncludeLayout = OptionalBool(Params, TEXT("includeLayout"), false);

	UWorld* World = ResolveWidgetRuntimeWorld();
	if (!World)
	{
		return MCPError(TEXT("No PIE world available. Is Play-In-Editor running?"));
	}

	if (WidgetName.IsEmpty() && ClassFilter.IsEmpty())
	{
		return MCPError(TEXT("Provide widgetName (exact instance name) or className (first match)."));
	}

	UUserWidget* Found = nullptr;
	for (TObjectIterator<UUserWidget> It; It; ++It)
	{
		UUserWidget* Widget = *It;
		if (!IsValid(Widget) || Widget->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject)) continue;
		if (Widget->GetWorld() != World) continue;

		if (!WidgetName.IsEmpty() && Widget->GetName() != WidgetName) continue;
		if (!ClassFilter.IsEmpty() && !Widget->GetClass()->GetName().Contains(ClassFilter)) continue;

		Found = Widget;
		break;
	}

	if (!Found)
	{
		return MCPError(TEXT("Runtime widget not found. Try list_runtime_widgets to see available instances."));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("name"), Found->GetName());
	Result->SetStringField(TEXT("class"), Found->GetClass()->GetName());
	Result->SetStringField(TEXT("visibility"), VisibilityToString(Found->GetVisibility()));
	Result->SetBoolField(TEXT("inViewport"), Found->IsInViewport());

	// `tree` stays rooted at the widget-tree root (or the named child) exactly as
	// before, so existing consumers keep indexing the same node and maxDepth keeps
	// counting from the same place. The hosting UUserWidget is reported separately
	// under `host` when layout diagnostics are requested.
	UWidget* ScanRoot = nullptr;
	if (!ChildName.IsEmpty())
	{
		if (!Found->WidgetTree)
		{
			return MCPError(FString::Printf(
				TEXT("Runtime widget '%s' has no UMG WidgetTree, so childName cannot be resolved."),
				*Found->GetName()));
		}

		// Search the widget tree for the named child.
		UWidget* Target = nullptr;
		Found->WidgetTree->ForEachWidget([&](UWidget* W)
		{
			if (W && W->GetName() == ChildName && !Target)
			{
				Target = W;
			}
		});
		if (!Target)
		{
			return MCPError(FString::Printf(TEXT("Child widget '%s' not found inside '%s'"), *ChildName, *Found->GetName()));
		}
		ScanRoot = Target;
	}
	else if (Found->WidgetTree)
	{
		ScanRoot = Found->WidgetTree->RootWidget;
	}

	FRuntimeScanContext Ctx;
	Ctx.bIncludeLayout = bIncludeLayout;

	FString CaptureKey;
	TOptional<uint64> PreviousFrame;
	FDerivedClipState HostClip;
	double HostOpacity = 1.0;
	if (bIncludeLayout)
	{
		Result->SetNumberField(TEXT("instanceId"), Found->GetUniqueID());

		CaptureKey =
			World->GetName() + TEXT("|") + Found->GetPathName() + TEXT("|") +
			FString::FromInt(Found->GetUniqueID()) + TEXT("|") +
			(ChildName.IsEmpty() ? TEXT("<root>") : ChildName);
		Ctx.PreviousSamples = PreviousLayoutCaptures.Find(CaptureKey);
		if (const uint64* Frame = PreviousLayoutCaptureFrames.Find(CaptureKey))
		{
			PreviousFrame = *Frame;
		}

		if (UGameViewportClient* ViewportClient = World->GetGameViewport())
		{
			if (TSharedPtr<SViewport> ViewportWidget = ViewportClient->GetGameViewportWidget())
			{
				Ctx.ViewportRect = ViewportWidget->GetCachedGeometry().GetRenderBoundingRect();
			}
		}

		// The host UUserWidget is not a UPanelWidget parent, so its geometry,
		// clipping and opacity are unreachable from the tree root by GetParent().
		// Capture it once and seed the tree walk with it. Passing MaxDepth as the
		// starting depth stops the walk after this node, so the subtree is not
		// duplicated under `host`.
		TSharedPtr<FJsonObject> HostNode = BuildRuntimeNode(
			Found,
			MaxDepth,
			MaxDepth,
			Found->GetName(),
			ResolveAncestorClipState(Found),
			ResolveAncestorOpacity(Found),
			Ctx);
		if (HostNode.IsValid())
		{
			Result->SetObjectField(TEXT("host"), HostNode);
		}
		HostClip = ResolveClipState(
			Found,
			Found->GetName(),
			Found->GetCachedGeometry(),
			Found->GetDesiredSize(),
			ResolveAncestorClipState(Found));
		HostOpacity = ResolveAncestorOpacity(Found) * Found->GetRenderOpacity();
	}

	if (ScanRoot)
	{
		const FString ScanPath = Found->GetName() + TEXT("/") + ScanRoot->GetName();
		TSharedPtr<FJsonObject> Tree = BuildRuntimeNode(
			ScanRoot,
			0,
			MaxDepth,
			ScanPath,
			ResolveAncestorClipState(ScanRoot, HostClip, Found->GetName()),
			ResolveAncestorOpacity(ScanRoot, HostOpacity),
			Ctx);
		if (Tree.IsValid())
		{
			Result->SetObjectField(TEXT("tree"), Tree);
		}
	}
	else
	{
		Result->SetStringField(TEXT("tree"), TEXT("empty"));
	}

	if (bIncludeLayout)
	{
		TSharedPtr<FJsonObject> Capture = MakeShared<FJsonObject>();
		Capture->SetNumberField(TEXT("sequence"), static_cast<double>(++LayoutCaptureSequence));
		Capture->SetNumberField(TEXT("frame"), static_cast<double>(GFrameCounter));
		Capture->SetNumberField(TEXT("timeSeconds"), FApp::GetCurrentTime());
		Capture->SetBoolField(TEXT("isBaseline"), Ctx.PreviousSamples == nullptr);
		Capture->SetNumberField(TEXT("nodeCount"), Ctx.CurrentSamples.Num());
		Capture->SetNumberField(TEXT("changedNodeCount"), Ctx.ChangedNodeCount);
		Capture->SetNumberField(TEXT("diagnosticCount"), Ctx.WarningCount);
		Capture->SetBoolField(TEXT("hasViewportGeometry"), Ctx.ViewportRect.IsSet());
		if (Ctx.ViewportRect.IsSet())
		{
			Capture->SetObjectField(TEXT("viewportRect"), RectJson(Ctx.ViewportRect.GetValue()));
		}
		if (PreviousFrame.IsSet())
		{
			Capture->SetNumberField(TEXT("previousFrame"), static_cast<double>(PreviousFrame.GetValue()));
		}
		Capture->SetStringField(
			TEXT("usage"),
			TEXT("Call widget.get_runtime again with includeLayout after moving, resizing, toggling, or changing resolution to populate deltaSincePreviousCapture."));
		Result->SetObjectField(TEXT("layoutCapture"), Capture);

		PreviousLayoutCaptures.Add(CaptureKey, MoveTemp(Ctx.CurrentSamples));
		PreviousLayoutCaptureFrames.Add(CaptureKey, GFrameCounter);
		if (PreviousLayoutCaptures.Num() > 64)
		{
			PreviousLayoutCaptures.Reset();
			PreviousLayoutCaptureFrames.Reset();
		}
	}

	return MCPResult(Result);
}

// ─────────────────────────────────────────────────────────────
// #602  Instantiate a WidgetBlueprint into the live PIE viewport.
// ─────────────────────────────────────────────────────────────
TSharedPtr<FJsonValue> FWidgetHandlers::AddWidgetToViewport(const TSharedPtr<FJsonObject>& Params)
{
	using namespace WidgetRuntime_Internal;

	// Every parameter is read before anything can fail (#1057).
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	const int32 ZOrder = OptionalInt(Params, TEXT("zOrder"), 0);

	UWorld* World = ResolveWidgetRuntimeWorld();
	if (!World)
	{
		return MCPError(TEXT("No PIE world available. Start Play-In-Editor first (editor pie_control action=play)."));
	}

	// Resolve the WidgetBlueprint's generated UUserWidget class.
	UClass* WidgetClass = LoadClass<UUserWidget>(nullptr, *AssetPath);
	if (!WidgetClass)
	{
		if (UWidgetBlueprint* WBP = LoadObject<UWidgetBlueprint>(nullptr, *AssetPath))
		{
			WidgetClass = WBP->GeneratedClass;
		}
		else if (!AssetPath.EndsWith(TEXT("_C")))
		{
			WidgetClass = LoadClass<UUserWidget>(nullptr, *(AssetPath + TEXT("_C")));
		}
	}
	if (!WidgetClass || !WidgetClass->IsChildOf(UUserWidget::StaticClass()))
	{
		return MCPError(FString::Printf(TEXT("Could not resolve a UserWidget class from '%s'"), *AssetPath));
	}

	APlayerController* PC = World->GetFirstPlayerController();
	UUserWidget* Widget = PC
		? CreateWidget<UUserWidget>(PC, WidgetClass)
		: CreateWidget<UUserWidget>(World, WidgetClass);
	if (!Widget)
	{
		return MCPError(TEXT("CreateWidget returned null"));
	}
	Widget->AddToViewport(ZOrder);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("instanceName"), Widget->GetName());
	Result->SetStringField(TEXT("class"), WidgetClass->GetName());
	Result->SetBoolField(TEXT("inViewport"), Widget->IsInViewport());
	Result->SetNumberField(TEXT("zOrder"), ZOrder);
	// The bridge registers no action that takes a widget back off the viewport,
	// so there is no inverse call to name. What this created is a transient PIE
	// object that dies with the PIE session; nothing on disk changed.
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("No action removes a widget from the viewport, so there is no inverse to run. The instance is transient PIE state: it goes ")
		TEXT("away when Play-In-Editor stops, and nothing on disk was changed by this call."));
	return MCPResult(Result);
}

// ─────────────────────────────────────────────────────────────
// #559  Fire a UFUNCTION or a child-widget interaction on a live PIE UUserWidget.
//   Params: widgetName|className (locate the UserWidget), functionName
//   (a parameterless UFUNCTION on the widget), OR childName (+ optional value,
//   functionName, commitMethod) to drive an interactive child widget (#812).
// ─────────────────────────────────────────────────────────────
TSharedPtr<FJsonValue> FWidgetHandlers::InvokeRuntimeWidgetFunction(const TSharedPtr<FJsonObject>& Params)
{
	using namespace WidgetRuntime_Internal;
	FString WidgetName = OptionalString(Params, TEXT("widgetName"));
	FString ClassFilter = OptionalString(Params, TEXT("className"));
	const FString ChildName = OptionalString(Params, TEXT("childName"));
	const FString FunctionName = OptionalString(Params, TEXT("functionName"));
	// Read by the child interaction, on the paths that take them.
	MCPReadParamsAhead(Params, { TEXT("value"), TEXT("commitMethod") });

	UWorld* World = ResolveWidgetRuntimeWorld();
	if (!World)
	{
		return MCPError(TEXT("No PIE world available. Is Play-In-Editor running?"));
	}

	if (WidgetName.IsEmpty() && ClassFilter.IsEmpty())
	{
		return MCPError(TEXT("Provide widgetName (exact instance name) or className (first match)."));
	}

	UUserWidget* Found = nullptr;
	for (TObjectIterator<UUserWidget> It; It; ++It)
	{
		UUserWidget* Widget = *It;
		if (!IsValid(Widget) || Widget->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject)) continue;
		if (Widget->GetWorld() != World) continue;
		if (!WidgetName.IsEmpty() && Widget->GetName() != WidgetName) continue;
		if (!ClassFilter.IsEmpty() && !Widget->GetClass()->GetName().Contains(ClassFilter)) continue;
		Found = Widget;
		break;
	}
	if (!Found)
	{
		return MCPError(TEXT("Runtime widget not found. Try list_runtime_widgets."));
	}

	// Child-interaction path: childName names an interactive child widget. The
	// simulation lives in WidgetHandlers_Interaction.cpp and covers buttons,
	// checkboxes, sliders, spin boxes, text entry and combo boxes (#812).
	// functionName, when given here, selects which of the child's delegates to
	// fire rather than naming a UFUNCTION on the parent.
	if (!ChildName.IsEmpty())
	{
		UWidget* Target = nullptr;
		if (Found->WidgetTree)
		{
			Found->WidgetTree->ForEachWidget([&](UWidget* W)
			{
				if (W && W->GetName() == ChildName && !Target) Target = W;
			});
		}
		if (!Target)
		{
			return MCPError(FString::Printf(TEXT("Child widget '%s' not found inside '%s'"), *ChildName, *Found->GetName()));
		}

		auto Result = MCPSuccess();
		Result->SetStringField(TEXT("widget"), Found->GetName());
		Result->SetStringField(TEXT("child"), ChildName);
		if (TSharedPtr<FJsonValue> Err = SimulateRuntimeChildInteraction(Target, Params, Result))
		{
			return Err;
		}
		// Whether the widget's own value moved. The interactions that carry a
		// value record what it was and what it became, so this is a reading
		// rather than an assumption; a click has neither, and gets no flag.
		//
		// It says nothing about the blueprint behind the widget. The delegate
		// is broadcast whether or not the value moved, deliberately, because
		// re-driving a slider to the value it already holds is still a request
		// to run the graph. So `valueUnchanged` is about the widget, and the
		// graph ran either way.
		//
		// Compared on the JSON type the interaction wrote rather than through a
		// string or number accessor: a text field holding "42" and one holding
		// "042" are different text, and a numeric accessor would call them the
		// same value.
		const TSharedPtr<FJsonValue> Before = Result->TryGetField(TEXT("previousValue"));
		const TSharedPtr<FJsonValue> After = Result->TryGetField(TEXT("value"));
		if (Before.IsValid() && After.IsValid())
		{
			bool bSameValue = false;
			if (Before->Type == EJson::Number && After->Type == EJson::Number)
			{
				bSameValue = Before->AsNumber() == After->AsNumber();
			}
			else if (Before->Type == EJson::String && After->Type == EJson::String)
			{
				bSameValue = Before->AsString().Equals(After->AsString(), ESearchCase::CaseSensitive);
			}
			Result->SetBoolField(TEXT("valueUnchanged"), bSameValue);
		}
		else
		{
			// A click, a hover, a press: an event, not a state write. Firing it
			// twice is two events rather than one repeated change, so there is
			// no value to compare and no no-op to report.
			Result->SetStringField(TEXT("idempotencyNote"),
				TEXT("This interaction delivers an event rather than writing a value, so there is nothing to compare "
				     "against and no valueUnchanged is reported. Sending it twice fires the delegate twice."));
		}

		// A simulated click, commit or value change fires the child's delegates,
		// and what those handlers then do is decided by the running blueprint.
		// Nothing here knows what changed, so nothing here can name an inverse.
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("This fires the child widget's delegates in the live PIE session, and what the bound handlers do is the running game's ")
			TEXT("business. The effects are not captured and no action reverses them."));
		return MCPResult(Result);
	}

	// UFUNCTION path: call a parameterless function on the UserWidget.
	if (FunctionName.IsEmpty())
	{
		return MCPError(TEXT("Provide functionName (parameterless UFUNCTION) or childName (button click)."));
	}
	UFunction* Func = Found->FindFunction(FName(*FunctionName));
	if (!Func)
	{
		return MCPError(FString::Printf(TEXT("Function '%s' not found on widget '%s'"), *FunctionName, *Found->GetClass()->GetName()));
	}
	if (Func->NumParms != 0)
	{
		return MCPError(FString::Printf(TEXT("Function '%s' takes %d parameter(s); only parameterless functions are supported here"), *FunctionName, Func->NumParms));
	}
	Found->ProcessEvent(Func, nullptr);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("widget"), Found->GetName());
	Result->SetStringField(TEXT("invoked"), FunctionName);
	// No valueUnchanged here either: calling a UFUNCTION is an invocation, not a
	// write of a value this action chose, so there is no before and after to
	// compare. Whether the function did anything the second time is the
	// function's own business.
	Result->SetStringField(TEXT("idempotencyNote"),
		TEXT("This calls a function rather than setting a value, so no unchanged flag is reported. Calling it twice "
		     "runs it twice, and whether the second run does anything is decided inside the function."));
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("This calls a UFUNCTION the widget's author wrote. What it changed is the function's business, not this action's, so there is ")
		TEXT("nothing captured to restore and no inverse to name."));
	return MCPResult(Result);
}

// ─────────────────────────────────────────────────────────────
// #161  Runtime delegate inspection - list FMulticastDelegateProperty fields on a live UUserWidget
// ─────────────────────────────────────────────────────────────
TSharedPtr<FJsonValue> FWidgetHandlers::GetRuntimeDelegates(const TSharedPtr<FJsonObject>& Params)
{
	using namespace WidgetRuntime_Internal;

	// Read before anything can fail (#1057).
	FString WidgetName;
	TryGetStringParam(Params, TEXT("widgetName"), WidgetName);
	FString ClassFilter;
	TryGetStringParam(Params, TEXT("className"), ClassFilter);

	UWorld* World = ResolveWidgetRuntimeWorld();
	if (!World)
	{
		return MCPError(TEXT("No PIE world available. Is Play-In-Editor running?"));
	}

	if (WidgetName.IsEmpty() && ClassFilter.IsEmpty())
	{
		return MCPError(TEXT("Provide 'widgetName' (exact instance name) or 'className' (first match)."));
	}

	UUserWidget* Found = nullptr;
	for (TObjectIterator<UUserWidget> It; It; ++It)
	{
		UUserWidget* Widget = *It;
		if (!IsValid(Widget) || Widget->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject)) continue;
		if (Widget->GetWorld() != World) continue;

		if (!WidgetName.IsEmpty() && Widget->GetName() != WidgetName) continue;
		if (!ClassFilter.IsEmpty() && !Widget->GetClass()->GetName().Contains(ClassFilter)) continue;

		Found = Widget;
		break;
	}

	if (!Found)
	{
		return MCPError(TEXT("Runtime widget not found. Try list_runtime_widgets to see available instances."));
	}

	TArray<TSharedPtr<FJsonValue>> DelegatesArr;
	for (TFieldIterator<FMulticastDelegateProperty> It(Found->GetClass()); It; ++It)
	{
		FMulticastDelegateProperty* DelegateProp = *It;
		if (!DelegateProp) continue;

		const void* DelegateAddr = DelegateProp->ContainerPtrToValuePtr<void>(Found);
		const FMulticastScriptDelegate* ScriptDelegate = DelegateProp->GetMulticastDelegate(DelegateAddr);

		TSharedPtr<FJsonObject> DelegateObj = MakeShared<FJsonObject>();
		DelegateObj->SetStringField(TEXT("delegateName"), DelegateProp->GetName());

		bool bIsBound = false;
		int32 NumBindings = 0;
		if (ScriptDelegate)
		{
			bIsBound = ScriptDelegate->IsBound();
			// Use export text to estimate the number of bindings
			FString ExportedStr;
			DelegateProp->ExportTextItem_Direct(ExportedStr, DelegateAddr, nullptr, Found, PPF_None);
			if (!ExportedStr.IsEmpty() && bIsBound)
			{
				// Count comma-separated entries in the exported delegate text
				NumBindings = 1;
				for (const TCHAR& Ch : ExportedStr)
				{
					if (Ch == TEXT(',')) ++NumBindings;
				}
			}
		}

		DelegateObj->SetBoolField(TEXT("isBound"), bIsBound);
		DelegateObj->SetNumberField(TEXT("numBindings"), NumBindings);
		DelegatesArr.Add(MakeShared<FJsonValueObject>(DelegateObj));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("widgetName"), Found->GetName());
	Result->SetStringField(TEXT("widgetClass"), Found->GetClass()->GetName());
	Result->SetArrayField(TEXT("delegates"), DelegatesArr);
	Result->SetNumberField(TEXT("delegateCount"), DelegatesArr.Num());
	return MCPResult(Result);
}
