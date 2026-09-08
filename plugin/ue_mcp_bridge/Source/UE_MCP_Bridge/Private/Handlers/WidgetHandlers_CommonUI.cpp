// CommonUI support, and the BindWidget contract it depends on. A translation-unit
// partition of FWidgetHandlers; registration stays in WidgetHandlers.cpp.
//
// ── Why so little is here ────────────────────────────────────────────────────
//
// An audit came first, because most of CommonUI turned out to be reachable
// already and a handler that duplicates a property write is a liability. What
// was checked, and where the existing route is:
//
//   Adding a CommonUI widget      widget(add_widget). ResolveWidgetClass
//                                 (WidgetHandlers.cpp) resolves a full path and,
//                                 as of this ticket, an unambiguous short name
//                                 over every loaded UWidget subclass.
//   Discovering the class names   widget(list_classes), which this ticket turned
//                                 from a hardcoded 21-entry UMG array into a real
//                                 enumeration grouped by module.
//   Runtime push / pop            UCommonActivatableWidgetContainerBase::
//                                 BP_AddWidget and RemoveWidget are
//                                 BlueprintCallable (CommonActivatableWidgetContainer.h:156,
//                                 :72), so editor(invoke_object_function) calls them.
//   Tab registration              UCommonTabListWidgetBase::RegisterTab /
//                                 RemoveTab / SelectTabByID are BlueprintCallable
//                                 (CommonTabListWidgetBase.h:107, :111, :123).
//   Every style, border, text,    Plain UPROPERTYs: UCommonButtonBase::Style
//   lazy-image and load-guard     (CommonButtonBase.h:826), UCommonBorder::Style
//   setting                       (CommonBorder.h:46), UCommonTextBlock::Style
//                                 (CommonTextBlock.h:207),
//                                 UCommonBoundActionBar::ActionButtonClass
//                                 (CommonBoundActionBar.h:68). widget(set_property)
//                                 and widget(set_style) already write all of them.
//   Style asset creation          blueprint(create) resolves any parent class, and
//                                 UCommonButtonStyle / UCommonTextStyle are plain
//                                 UObject subclasses.
//   Input action data             UCommonInputSettings is config=Game, so
//                                 project(set_config) writes it.
//
// That left exactly two things no existing action can answer:
//
//   1. BindWidget metadata. A native UUserWidget subclass states its contract in
//      UPROPERTY meta, and metadata is not a property value. reflection(reflect_class)
//      reports a fixed allowlist of meta keys (ReflectionHandlers.cpp:350-357:
//      Category, DisplayName, EditCondition, Clamp*, UI*, Units) and BindWidget is
//      not among them. So a caller subclassing CommonButtonBase had no way to learn
//      which child widgets it must name, and found out from a compile error.
//      This generalises well past CommonUI: it is the contract of ANY native
//      UserWidget parent.
//   2. The CommonUI wiring rules. They are relationships between a project
//      setting, an engine class override and a widget tree, evaluated together.
//      Each half is readable; nothing joins them, and every one of them fails
//      silently at runtime rather than at compile time.
//
// ── Why nothing here links CommonUI ──────────────────────────────────────────
//
// CommonUI ships disabled. A Build.cs dependency on it would fail to link the
// whole bridge in every project that has it off, which is most of them, so every
// CommonUI class below is reached by path at runtime through FindObject and every
// property by name through FindFProperty. When the plugin is off the actions say
// so, and say how to turn it on, rather than failing to load.

#include "WidgetHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"

#include "WidgetBlueprint.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Animation/WidgetAnimation.h"
#include "Components/Widget.h"
#include "Components/PanelWidget.h"

#include "Interfaces/IPluginManager.h"

#include "Engine/Engine.h"
#include "Engine/Blueprint.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"
#include "UObject/SoftObjectPtr.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

// ─────────────────────────────────────────────────────────────────────────────
// File-local helpers.
//
// The module is a unity build, so two .cpp files in one blob share their
// anonymous namespace and a duplicated helper is a redefinition (C2084). Every
// name below is prefixed WCui_ inside a named namespace and exists nowhere else
// in the tree; nothing was copied out of WidgetHandlers.cpp or
// WidgetHandlers_Animation.cpp, both of which are neighbours in this module.
// ─────────────────────────────────────────────────────────────────────────────
namespace MCPWidgetCommonUI
{

/** The four BindWidget meta keys, and what each one means. */
struct FWCui_BindKind
{
	const TCHAR* MetaKey;
	bool bOptional;
	bool bAnimation;
};

static const FWCui_BindKind WCui_BindKinds[] = {
	{ TEXT("BindWidget"),             false, false },
	{ TEXT("BindWidgetOptional"),     true,  false },
	{ TEXT("BindWidgetAnim"),         false, true  },
	{ TEXT("BindWidgetAnimOptional"), true,  true  },
};

/**
 * Is this property part of a UserWidget's bind contract, and how strict is it?
 *
 * `meta=(BindWidget, OptionalWidget=true)` is the older spelling of
 * BindWidgetOptional and the engine still honours it, so both are read here.
 * Missing that would report a property as required when the compiler does not.
 */
static bool WCui_ReadBindKind(const FProperty* Prop, bool& bOutOptional, bool& bOutAnimation)
{
	// WITH_METADATA, not WITH_EDITOR: metadata is what this reads and that macro
	// is what gates the API. Both are on in an editor build; only one of them
	// says why this code needs the guard.
#if WITH_METADATA
	for (const FWCui_BindKind& Kind : WCui_BindKinds)
	{
		if (!Prop->HasMetaData(Kind.MetaKey)) continue;
		bOutAnimation = Kind.bAnimation;
		bOutOptional = Kind.bOptional
			|| Prop->GetBoolMetaData(TEXT("OptionalWidget"))
			|| Prop->HasMetaData(TEXT("BindWidgetOptional"))
			|| Prop->HasMetaData(TEXT("BindWidgetAnimOptional"));
		return true;
	}
#endif // WITH_METADATA
	return false;
}

/** Resolve a class spec: short name, /Script path, or a Widget Blueprint path. */
static UClass* WCui_ResolveWidgetClassSpec(const FString& Spec)
{
	if (Spec.IsEmpty()) return nullptr;

	if (UClass* Direct = MCPResolveClass(Spec, /*bAllowLoad*/ true))
	{
		if (Direct->IsChildOf(UWidget::StaticClass())) return Direct;
	}
	// A Widget Blueprint path names the asset; its contract lives on the class.
	if (UObject* Asset = LoadObject<UObject>(nullptr, *Spec))
	{
		if (UBlueprint* BP = Cast<UBlueprint>(Asset))
		{
			if (BP->GeneratedClass && BP->GeneratedClass->IsChildOf(UWidget::StaticClass()))
			{
				return BP->GeneratedClass;
			}
		}
	}
	return nullptr;
}

/** A CommonUI class by name, or null when the plugin is not loaded. */
static UClass* WCui_CommonClass(const TCHAR* Module, const TCHAR* ClassName)
{
	const FString Path = FString::Printf(TEXT("/Script/%s.%s"), Module, ClassName);
	return FindObject<UClass>(nullptr, *Path);
}

static bool WCui_PluginLoaded()
{
	return IPluginManager::Get().FindEnabledPlugin(TEXT("CommonUI")).IsValid();
}

/** Every widget in the tree, parents before children. */
static void WCui_CollectWidgets(UWidgetTree* Tree, TArray<UWidget*>& Out)
{
	if (!Tree) return;
	Tree->ForEachWidget([&Out](UWidget* W) { if (W) Out.Add(W); });
}

/** The class a TSubclassOf property currently points at, or null. */
static UClass* WCui_ReadClassProperty(UObject* Object, const TCHAR* PropertyName)
{
	FClassProperty* Prop = FindFProperty<FClassProperty>(Object->GetClass(), PropertyName);
	if (!Prop) return nullptr;
	return Cast<UClass>(Prop->GetObjectPropertyValue_InContainer(Object));
}

/** True when the named TSubclassOf property exists on the object AND is unset. */
static bool WCui_ClassPropertyIsUnset(UObject* Object, const TCHAR* PropertyName)
{
	FClassProperty* Prop = FindFProperty<FClassProperty>(Object->GetClass(), PropertyName);
	if (!Prop) return false;   // Property absent: a different engine version, not a fault.
	return WCui_ReadClassProperty(Object, PropertyName) == nullptr;
}

/** One entry in the audit's problems array, in one shape for every rule. */
static TSharedPtr<FJsonValue> WCui_Problem(
	const TCHAR* Rule, const FString& Where, const FString& What, const FString& Fix)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetStringField(TEXT("rule"), Rule);
	Obj->SetStringField(TEXT("where"), Where);
	Obj->SetStringField(TEXT("problem"), What);
	Obj->SetStringField(TEXT("fix"), Fix);
	return MakeShared<FJsonValueObject>(Obj);
}

static TSharedPtr<FJsonValue> WCui_Note(const TCHAR* Rule, const FString& Why)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetStringField(TEXT("rule"), Rule);
	Obj->SetStringField(TEXT("reason"), Why);
	return MakeShared<FJsonValueObject>(Obj);
}

} // namespace MCPWidgetCommonUI

using namespace MCPWidgetCommonUI;

// ═════════════════════════════════════════════════════════════════════════════
// get_bind_widget_contract
//
// What a native UserWidget parent requires of the Widget Blueprint below it.
// ═════════════════════════════════════════════════════════════════════════════

TSharedPtr<FJsonValue> FWidgetHandlers::GetBindWidgetContract(const TSharedPtr<FJsonObject>& Params)
{
	const FString ClassName = OptionalString(Params, TEXT("className"));
	const FString AssetPath = OptionalString(Params, TEXT("assetPath"));

	if (ClassName.IsEmpty() && AssetPath.IsEmpty())
	{
		return MCPError(TEXT(
			"Pass className to read a parent class's contract (a short name like CommonButtonBase, a full "
			"path like /Script/CommonUI.CommonButtonBase, or a Widget Blueprint path), or assetPath to read "
			"a Widget Blueprint's contract AND check its own tree against it. Neither was given."));
	}

	// ── Resolve the class whose contract this is.
	UWidgetBlueprint* WidgetBP = nullptr;
	UClass* ContractClass = nullptr;

	if (!AssetPath.IsEmpty())
	{
		TSharedPtr<FJsonValue> ResolveError;
		WidgetBP = MCPWidget::ResolveWidgetBlueprintOrError(AssetPath, ResolveError);
		if (!WidgetBP) return ResolveError;
		// The contract is what the PARENT demands. Reading the generated class
		// would also pick up the Blueprint's own widget variables, which are the
		// answer rather than the question.
		ContractClass = WidgetBP->ParentClass;
		if (!ContractClass)
		{
			return MCPError(FString::Printf(
				TEXT("Widget Blueprint '%s' has no parent class, which means the asset is broken."), *AssetPath));
		}
	}

	if (!ClassName.IsEmpty())
	{
		UClass* Named = WCui_ResolveWidgetClassSpec(ClassName);
		if (!Named)
		{
			return MCPError(FString::Printf(
				TEXT("'%s' does not name a loaded UWidget subclass. Short names of loaded classes resolve, as "
				     "does a full path (/Script/CommonUI.CommonButtonBase) or a Widget Blueprint path. List "
				     "what this editor has with widget(list_classes). A class from a plugin that is off does "
				     "not exist until project(enable_plugin) and an editor restart."), *ClassName));
		}
		ContractClass = Named;
	}

	// ── Gather the contract, walking up so an inherited requirement is reported
	// with the class that actually declares it.
	TArray<TSharedPtr<FJsonValue>> Requirements;
	int32 RequiredCount = 0;
	int32 OptionalCount = 0;

	for (TFieldIterator<FProperty> It(ContractClass); It; ++It)
	{
		FProperty* Prop = *It;
		bool bOptional = false;
		bool bAnimation = false;
		if (!WCui_ReadBindKind(Prop, bOptional, bAnimation)) continue;

		FString ExpectedType;
		if (FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Prop))
		{
			ExpectedType = ObjProp->PropertyClass ? ObjProp->PropertyClass->GetName() : FString();
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("widgetName"), Prop->GetName());
		Entry->SetStringField(TEXT("expectedClass"), ExpectedType);
		Entry->SetStringField(TEXT("declaredBy"),
			Prop->GetOwnerClass() ? Prop->GetOwnerClass()->GetName() : FString());
		Entry->SetBoolField(TEXT("optional"), bOptional);
		Entry->SetStringField(TEXT("kind"), bAnimation ? TEXT("animation") : TEXT("widget"));
		Entry->SetStringField(TEXT("satisfyWith"), bAnimation
			? TEXT("widget(create_animation) with animationName set to widgetName")
			: TEXT("widget(add_widget) with widgetName set to widgetName and widgetClass to expectedClass"));

		if (bOptional) ++OptionalCount; else ++RequiredCount;
		Requirements.Add(MakeShared<FJsonValueObject>(Entry));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("className"), ContractClass->GetName());
	Result->SetStringField(TEXT("classPath"), ContractClass->GetPathName());
	Result->SetArrayField(TEXT("requirements"), Requirements);
	Result->SetNumberField(TEXT("requiredCount"), RequiredCount);
	Result->SetNumberField(TEXT("optionalCount"), OptionalCount);

	// ── When a Blueprint was named, say which requirements it already meets.
	// A contract with no way to check it against an asset is only half an answer.
	if (WidgetBP)
	{
		TArray<UWidget*> Tree;
		WCui_CollectWidgets(WidgetBP->WidgetTree, Tree);

		TArray<FString> AnimationNames;
		for (UWidgetAnimation* Anim : WidgetBP->Animations)
		{
			if (Anim) AnimationNames.Add(Anim->GetName());
		}

		TArray<TSharedPtr<FJsonValue>> Missing;
		TArray<TSharedPtr<FJsonValue>> WrongType;
		int32 Satisfied = 0;

		for (const TSharedPtr<FJsonValue>& Entry : Requirements)
		{
			const TSharedPtr<FJsonObject>* Obj = nullptr;
			if (!Entry->TryGetObject(Obj) || !Obj) continue;
			FString Name, Expected;
			bool bOptional = false;
			bool bAnimation = false;
			(*Obj)->TryGetStringField(TEXT("widgetName"), Name);
			(*Obj)->TryGetStringField(TEXT("expectedClass"), Expected);
			(*Obj)->TryGetBoolField(TEXT("optional"), bOptional);
			FString Kind;
			(*Obj)->TryGetStringField(TEXT("kind"), Kind);
			bAnimation = Kind == TEXT("animation");

			if (bAnimation)
			{
				if (AnimationNames.Contains(Name)) { ++Satisfied; continue; }
				if (!bOptional) Missing.Add(MakeShared<FJsonValueObject>(*Obj));
				continue;
			}

			UWidget* Found = nullptr;
			for (UWidget* W : Tree)
			{
				if (W->GetName() == Name) { Found = W; break; }
			}
			if (!Found)
			{
				if (!bOptional) Missing.Add(MakeShared<FJsonValueObject>(*Obj));
				continue;
			}

			// A widget of the right name but the wrong class fails the compile
			// exactly as loudly as a missing one, and is far harder to spot.
			UClass* ExpectedClass = Expected.IsEmpty() ? nullptr : MCPResolveClass(Expected, false);
			if (ExpectedClass && !Found->GetClass()->IsChildOf(ExpectedClass))
			{
				TSharedPtr<FJsonObject> Bad = MakeShared<FJsonObject>();
				Bad->SetStringField(TEXT("widgetName"), Name);
				Bad->SetStringField(TEXT("expectedClass"), Expected);
				Bad->SetStringField(TEXT("actualClass"), Found->GetClass()->GetName());
				Bad->SetBoolField(TEXT("optional"), bOptional);
				WrongType.Add(MakeShared<FJsonValueObject>(Bad));
				continue;
			}
			++Satisfied;
		}

		TSharedPtr<FJsonObject> Check = MakeShared<FJsonObject>();
		Check->SetStringField(TEXT("assetPath"), AssetPath);
		Check->SetStringField(TEXT("parentClass"), ContractClass->GetName());
		Check->SetNumberField(TEXT("satisfied"), Satisfied);
		Check->SetArrayField(TEXT("missing"), Missing);
		Check->SetArrayField(TEXT("wrongType"), WrongType);
		Check->SetBoolField(TEXT("compiles"), Missing.Num() == 0 && WrongType.Num() == 0);
		Result->SetObjectField(TEXT("check"), Check);
	}

	Result->SetStringField(TEXT("note"), TEXT(
		"A required entry with no widget of that exact name and a compatible class fails the Widget "
		"Blueprint compile. The name is the property name, matched exactly and case-sensitively."));

	return MCPResult(Result);
}

// ═════════════════════════════════════════════════════════════════════════════
// audit_commonui
//
// The wiring rules that fail silently at runtime instead of loudly at compile.
// ═════════════════════════════════════════════════════════════════════════════

TSharedPtr<FJsonValue> FWidgetHandlers::AuditCommonUI(const TSharedPtr<FJsonObject>& Params)
{
	const FString AssetPath = OptionalString(Params, TEXT("assetPath"));

	TArray<TSharedPtr<FJsonValue>> Problems;
	TArray<TSharedPtr<FJsonValue>> NotChecked;
	TArray<FString> Checked;

	// ── Rule 1: the plugin itself.
	const bool bPluginLoaded = WCui_PluginLoaded();
	Checked.Add(TEXT("plugin_enabled"));
	if (!bPluginLoaded)
	{
		Problems.Add(WCui_Problem(
			TEXT("plugin_enabled"), TEXT("project"),
			TEXT("The CommonUI plugin is not enabled in this editor, so no CommonUI class exists and no "
			     "CommonUI widget can be added or configured."),
			TEXT("project(enable_plugin, pluginName=\"CommonUI\") then editor(restart_editor). Everything "
			     "below is unknowable until then.")));

		auto Early = MCPSuccess();
		Early->SetBoolField(TEXT("pluginEnabled"), false);
		Early->SetArrayField(TEXT("problems"), Problems);
		Early->SetNumberField(TEXT("problemCount"), Problems.Num());
		Early->SetArrayField(TEXT("checked"), MCPStringListToJson(Checked));
		Early->SetArrayField(TEXT("notChecked"), NotChecked);
		return MCPResult(Early);
	}

	// ── Rule 2: the game viewport client.
	// CommonUI routes input to the UI before the game through its own viewport
	// client. With the engine default in place, gamepad navigation and the Back
	// action do nothing, and nothing anywhere reports why.
	Checked.Add(TEXT("game_viewport_client"));
	UClass* CommonViewport = WCui_CommonClass(TEXT("CommonUI"), TEXT("CommonGameViewportClient"));
	if (!CommonViewport)
	{
		NotChecked.Add(WCui_Note(TEXT("game_viewport_client"),
			TEXT("UCommonGameViewportClient is not loaded even though the plugin is enabled.")));
	}
	else if (GEngine)
	{
		UClass* Current = GEngine->GameViewportClientClass;
		if (!Current || !Current->IsChildOf(CommonViewport))
		{
			Problems.Add(WCui_Problem(
				TEXT("game_viewport_client"), TEXT("project"),
				FString::Printf(
					TEXT("GameViewportClientClass is %s, which is not a CommonGameViewportClient. CommonUI "
					     "input routing, the Back action and gamepad navigation are inactive."),
					Current ? *Current->GetName() : TEXT("unset")),
				TEXT("project(set_config, configName=\"Engine\", section=\"/Script/Engine.Engine\", "
				     "key=\"GameViewportClientClassName\", "
				     "value=\"/Script/CommonUI.CommonGameViewportClient\") then restart the editor.")));
		}
	}
	else
	{
		NotChecked.Add(WCui_Note(TEXT("game_viewport_client"), TEXT("GEngine is not available.")));
	}

	// ── Rule 3: the input action data.
	// Without it, every CommonUI input action resolves to nothing, so bound
	// action bars come up empty and Back is unhandled.
	Checked.Add(TEXT("input_action_data"));
	UClass* InputSettingsClass = WCui_CommonClass(TEXT("CommonInput"), TEXT("CommonInputSettings"));
	if (!InputSettingsClass)
	{
		NotChecked.Add(WCui_Note(TEXT("input_action_data"),
			TEXT("UCommonInputSettings is not loaded, so the project's CommonUI input data cannot be read.")));
	}
	else
	{
		UObject* SettingsCDO = InputSettingsClass->GetDefaultObject();
		FSoftObjectProperty* InputData =
			FindFProperty<FSoftObjectProperty>(InputSettingsClass, TEXT("InputData"));
		if (!SettingsCDO || !InputData)
		{
			NotChecked.Add(WCui_Note(TEXT("input_action_data"),
				TEXT("UCommonInputSettings has no InputData property in this engine version.")));
		}
		else
		{
			const FSoftObjectPtr* Value = InputData->ContainerPtrToValuePtr<FSoftObjectPtr>(SettingsCDO);
			if (!Value || Value->IsNull())
			{
				Problems.Add(WCui_Problem(
					TEXT("input_action_data"), TEXT("project"),
					TEXT("CommonInputSettings.InputData is unset, so no CommonUI input action resolves. "
					     "Bound action bars render empty and the Back action is never handled."),
					TEXT("Create a UCommonUIInputData subclass with blueprint(create), then "
					     "project(set_config, configName=\"Game\", "
					     "section=\"/Script/CommonInput.CommonInputSettings\", key=\"InputData\", "
					     "value=\"<the class path>\").")));
			}
		}
	}

	// ── Per-asset rules. Everything above is about the project; these are about
	// one widget tree, and they only run when a caller names one.
	if (AssetPath.IsEmpty())
	{
		NotChecked.Add(WCui_Note(TEXT("widget_tree"),
			TEXT("No assetPath was given, so the per-widget rules (activatable focus target, bound action "
			     "bar button class, unstyled CommonUI widgets) did not run. Pass assetPath to include them.")));
	}
	else
	{
		TSharedPtr<FJsonValue> ResolveError;
		UWidgetBlueprint* WidgetBP = MCPWidget::ResolveWidgetBlueprintOrError(AssetPath, ResolveError);
		if (!WidgetBP) return ResolveError;
		if (!WidgetBP->WidgetTree) return MCPWidget::MissingWidgetTreeError(AssetPath);

		// Rule 4: an activatable widget with nothing to focus. On gamepad,
		// activating it leaves focus wherever it was, which reads as the UI
		// being dead rather than as a missing setting.
		Checked.Add(TEXT("activatable_focus_target"));
		UClass* ActivatableClass = WCui_CommonClass(TEXT("CommonUI"), TEXT("CommonActivatableWidget"));
		if (ActivatableClass && WidgetBP->ParentClass && WidgetBP->ParentClass->IsChildOf(ActivatableClass))
		{
			UClass* GeneratedClass = WidgetBP->GeneratedClass;
			UObject* CDO = GeneratedClass ? GeneratedClass->GetDefaultObject() : nullptr;
			FStructProperty* FocusProp = CDO
				? FindFProperty<FStructProperty>(CDO->GetClass(), TEXT("DesiredFocusWidget"))
				: nullptr;
			if (!CDO || !FocusProp)
			{
				NotChecked.Add(WCui_Note(TEXT("activatable_focus_target"),
					TEXT("DesiredFocusWidget is not a property of UUserWidget in this engine version.")));
			}
			else
			{
				const void* FocusValue = FocusProp->ContainerPtrToValuePtr<void>(CDO);
				FNameProperty* NameProp =
					FindFProperty<FNameProperty>(FocusProp->Struct, TEXT("WidgetName"));
				const FName FocusName = NameProp
					? NameProp->GetPropertyValue_InContainer(FocusValue)
					: NAME_None;
				if (FocusName.IsNone())
				{
					Problems.Add(WCui_Problem(
						TEXT("activatable_focus_target"), AssetPath,
						TEXT("This is a CommonActivatableWidget with no DesiredFocusWidget, so activating it "
						     "on a gamepad hands focus to nothing and the screen appears unresponsive."),
						TEXT("asset(set_property, assetPath=<this asset>, "
						     "propertyName=\"DesiredFocusWidget.WidgetName\", value=<a focusable widget "
						     "name from widget(read_tree)>).")));
				}
			}
		}

		// Rules 5 and 6 walk the tree once.
		Checked.Add(TEXT("bound_action_bar_button_class"));
		Checked.Add(TEXT("commonui_widget_style"));
		UClass* ActionBarClass = WCui_CommonClass(TEXT("CommonUI"), TEXT("CommonBoundActionBar"));

		// Every CommonUI widget whose look comes entirely from a Style class:
		// unset, they render as nothing at all, which looks like a layout bug.
		struct FStyled { const TCHAR* ClassName; const TCHAR* PropertyName; };
		static const FStyled StyledClasses[] = {
			{ TEXT("CommonButtonBase"), TEXT("Style") },
			{ TEXT("CommonBorder"),     TEXT("Style") },
			{ TEXT("CommonTextBlock"),  TEXT("Style") },
		};

		TArray<UWidget*> Tree;
		WCui_CollectWidgets(WidgetBP->WidgetTree, Tree);
		for (UWidget* Widget : Tree)
		{
			if (ActionBarClass && Widget->IsA(ActionBarClass)
				&& WCui_ClassPropertyIsUnset(Widget, TEXT("ActionButtonClass")))
			{
				Problems.Add(WCui_Problem(
					TEXT("bound_action_bar_button_class"), FString::Printf(TEXT("%s.%s"), *AssetPath, *Widget->GetName()),
					TEXT("CommonBoundActionBar has no ActionButtonClass, so it has nothing to build its "
					     "entries from and renders empty however many actions are bound."),
					FString::Printf(TEXT("widget(set_property, assetPath=%s, widgetName=%s, "
						"propertyName=\"ActionButtonClass\", value=<a CommonButtonBase subclass that "
						"implements CommonBoundActionButtonInterface>)."), *AssetPath, *Widget->GetName())));
			}

			for (const FStyled& Styled : StyledClasses)
			{
				UClass* StyledClass = WCui_CommonClass(TEXT("CommonUI"), Styled.ClassName);
				if (!StyledClass || !Widget->IsA(StyledClass)) continue;
				if (!WCui_ClassPropertyIsUnset(Widget, Styled.PropertyName)) continue;
				Problems.Add(WCui_Problem(
					TEXT("commonui_widget_style"), FString::Printf(TEXT("%s.%s"), *AssetPath, *Widget->GetName()),
					FString::Printf(
						TEXT("%s '%s' has no %s set. A CommonUI widget draws entirely from its style class, "
						     "so with none it renders as nothing and reads as a layout fault."),
						Styled.ClassName, *Widget->GetName(), Styled.PropertyName),
					FString::Printf(TEXT("widget(set_property, assetPath=%s, widgetName=%s, "
						"propertyName=\"%s\", value=<a style asset path>). Create one with "
						"blueprint(create) on the matching Common*Style parent class."),
						*AssetPath, *Widget->GetName(), Styled.PropertyName)));
			}
		}
	}

	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("pluginEnabled"), true);
	if (!AssetPath.IsEmpty()) Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetArrayField(TEXT("problems"), Problems);
	Result->SetNumberField(TEXT("problemCount"), Problems.Num());
	Result->SetArrayField(TEXT("checked"), MCPStringListToJson(Checked));
	Result->SetArrayField(TEXT("notChecked"), NotChecked);
	Result->SetStringField(TEXT("note"), TEXT(
		"Every rule here fails silently at runtime rather than at compile time, which is why they are "
		"worth a report. Authored values only: this reads the asset and the project settings, not a "
		"running game."));

	return MCPResult(Result);
}
