#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"

class UWidgetBlueprint;

/**
 * Shared WidgetBlueprint resolution for every widget action (#972).
 *
 * Symptom: building a WidgetBlueprint incrementally failed every second or
 * third add_widget with "Failed to load WidgetBlueprint", on an asset that was
 * on disk, was in the AssetRegistry, and that asset(search) could see. Then it
 * wedged permanently, and asset(force_reload) made it worse while
 * editor(reload_bridge) cleared it instantly.
 *
 * Mechanism, confirmed by reading every widget translation unit: the handlers
 * hold NO pointer between calls. Each one re-resolved from the path through
 * UEditorAssetLibrary::LoadAsset, which is not a plain load. It converts the
 * path, looks the asset up in the AssetRegistry, and loads the FAssetData it
 * got back. That round trip is what intermittently answers null: the registry
 * entry a call resolved through can be mid-rescan, and after a package reload
 * it names an object that has been consigned to oblivion (RF_NewerVersionExists)
 * while a new one holds the name. So the stale handle is real, it just lives in
 * the engine's asset plumbing rather than in a static in this plugin, and
 * "resolve fresh per call" was already true and not enough on its own.
 *
 * The resolver below therefore does three things the old one-liner did not:
 *
 *   1. Asks the object hash FIRST (FindObject on the object path). An asset
 *      already in memory answers without touching the registry round trip at
 *      all, which is the step that removes the intermittency.
 *   2. Revalidates whatever it gets. IsValid plus an RF_NewerVersionExists
 *      check, so a reload's corpse is never handed to a caller who would then
 *      mutate an object the editor no longer consults.
 *   3. Falls through progressively (EditorAssetLibrary, direct LoadObject,
 *      explicit LoadPackage then look inside it) instead of giving up on the
 *      first null.
 *
 * It also separates "there is no such asset" from "the asset is there and the
 * handle went stale", because the caller's next move differs: fix the path, or
 * retry / reload the bridge.
 */
namespace MCPWidget
{

enum class EWidgetBlueprintResolveFailure : uint8
{
	/** Resolved. */
	None,
	/** Nothing in the AssetRegistry and no package of that name on disk. */
	NotFound,
	/** The path names something real that is not a WidgetBlueprint. */
	WrongType,
	/** The asset exists but no live object could be reached this call. */
	Unresolvable,
};

struct FWidgetBlueprintResolve
{
	UWidgetBlueprint* Blueprint = nullptr;
	EWidgetBlueprintResolveFailure Failure = EWidgetBlueprintResolveFailure::None;
	/** The normalised object path that was searched for. */
	FString ObjectPath;
	/** Class of the object that was found, when Failure is WrongType. */
	FString FoundClass;
	/** True when the registry or the filesystem says the asset is really there. */
	bool bAssetExists = false;
};

/** Resolve fresh, revalidate, and say why when it fails. Never caches. */
FWidgetBlueprintResolve ResolveWidgetBlueprint(const FString& AssetPath);

/** Error JSON for a failed resolve, worded per failure kind. */
TSharedPtr<FJsonValue> WidgetBlueprintResolveError(
	const FString& AssetPath,
	const FWidgetBlueprintResolve& Resolved);

/** The `if (!WidgetBP) return Err;` shape every handler wants. */
UWidgetBlueprint* ResolveWidgetBlueprintOrError(
	const FString& AssetPath,
	TSharedPtr<FJsonValue>& OutError);

/** A resolved blueprint whose WidgetTree is missing is a broken asset, not a
 *  failed load. Separate message so the two stop looking identical. */
TSharedPtr<FJsonValue> MissingWidgetTreeError(const FString& AssetPath);

}

class FWidgetHandlers
{
public:
	static void RegisterHandlers(class FMCPHandlerRegistry& Registry);

private:
	static TSharedPtr<FJsonValue> ListWidgetBlueprints(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreateWidgetBlueprint(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ReadWidgetTree(const TSharedPtr<FJsonObject>& Params);
	// Extract an authored designer subtree into a new or empty WidgetBlueprint.
	// The implementation uses UMG's clipboard serializer so editable widget
	// properties, hierarchy, and internal panel slot data stay intact.
	static TSharedPtr<FJsonValue> ExtractWidgetSubtree(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreateEditorUtilityWidget(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreateEditorUtilityBlueprint(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetWidgetProperties(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetWidgetFullProperties(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListWidgetBindings(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ClearWidgetBinding(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetWidgetProperty(const TSharedPtr<FJsonObject>& Params);
	// #563: set a full/nested style struct (FButtonStyle, FEditableTextBoxStyle,
	// FSlateFontInfo, ...) on a widget from JSON, and a bulk multi-widget variant.
	static TSharedPtr<FJsonValue> SetWidgetStyle(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> BulkSetWidgetProperties(const TSharedPtr<FJsonObject>& Params);
	// #635/#21: reorder a widget among its parent panel's children by index.
	static TSharedPtr<FJsonValue> ReorderChild(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ReadWidgetAnimations(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RunEditorUtilityWidget(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RunEditorUtilityBlueprint(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddWidget(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RemoveWidget(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> MoveWidget(const TSharedPtr<FJsonObject>& Params);
	// #365: root-widget swap + "Wrap With" container insertion. Required to
	// reshape an existing WBP root without rebuilding the whole tree.
	static TSharedPtr<FJsonValue> SetRoot(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> WrapRoot(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListWidgetClasses(const TSharedPtr<FJsonObject>& Params);

	// Runtime (PIE) widget inspection (#160)
	static TSharedPtr<FJsonValue> ListRuntimeWidgets(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetRuntimeWidget(const TSharedPtr<FJsonObject>& Params);
	// Read selected reflected properties from every matching live widget instance.
	// Unlike GetRuntimeWidget, this never silently selects the first class match.
	static TSharedPtr<FJsonValue> InspectRuntimeInstances(const TSharedPtr<FJsonObject>& Params);
	// #161: Runtime delegate inspection
	static TSharedPtr<FJsonValue> GetRuntimeDelegates(const TSharedPtr<FJsonObject>& Params);
	// #602: instantiate a WidgetBlueprint into the live PIE viewport.
	static TSharedPtr<FJsonValue> AddWidgetToViewport(const TSharedPtr<FJsonObject>& Params);
	// #559: fire a UFUNCTION / button click on a live PIE UUserWidget.
	static TSharedPtr<FJsonValue> InvokeRuntimeWidgetFunction(const TSharedPtr<FJsonObject>& Params);
	// #812: drive an interactive child of a live PIE widget (button, checkbox,
	// slider, spin box, text entry, combo box) and broadcast the delegate the
	// real interaction fires. Returns an error value on failure, otherwise an
	// unset pointer with the interaction record written into OutInfo.
	// Implemented in WidgetHandlers_Interaction.cpp.
	static TSharedPtr<FJsonValue> SimulateRuntimeChildInteraction(
		class UWidget* Target,
		const TSharedPtr<FJsonObject>& Params,
		const TSharedPtr<FJsonObject>& OutInfo);

	// ── UMG animation authoring, navigation rules, focus and accessibility ──
	// Defined in WidgetHandlers_Animation.cpp, a translation-unit partition of
	// this class: still FWidgetHandlers members, still registered above in
	// WidgetHandlers.cpp::RegisterHandlers.
	//
	// These earn handlers because a UWidgetAnimation, its UMovieScene tracks and
	// sections, and a UWidgetNavigation subobject all have to be CONSTRUCTED
	// before any property on them exists for set_property to write. The two
	// audits evaluate rules across the whole widget tree, which is not a
	// property read at all.
	static TSharedPtr<FJsonValue> CreateWidgetAnimation(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> DeleteWidgetAnimation(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetWidgetAnimation(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddWidgetAnimationTrack(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RemoveWidgetAnimationTrack(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddWidgetAnimationKey(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RemoveWidgetAnimationKey(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddWidgetAnimationEventKey(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RemoveWidgetAnimationEventKey(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> BindWidgetAnimationEvent(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> UnbindWidgetAnimationEvent(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetWidgetNavigation(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ClearWidgetNavigation(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RestoreWidgetNavigation(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AuditWidgetFocusChain(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AuditWidgetAccessibility(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetRuntimeFocusPath(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetRuntimeFocus(const TSharedPtr<FJsonObject>& Params);

	// ── CommonUI ───────────────────────────────────────────────────────────
	// Defined in WidgetHandlers_CommonUI.cpp, same partition arrangement. Every
	// CommonUI class is reached by name at runtime rather than by linking the
	// module, because the plugin ships disabled and a Build.cs dependency would
	// take the whole bridge down in a project that has it off.
	static TSharedPtr<FJsonValue> GetBindWidgetContract(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AuditCommonUI(const TSharedPtr<FJsonObject>& Params);

	// Helper: recursively search for a widget by name in the tree
	static class UWidget* FindWidgetByNameRecursive(class UWidget* Root, const FString& WidgetName);
};
