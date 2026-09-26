// Which asset editors are open, and which one the user is actually looking at.
//
// #1112: Unreal exposes no single answer to "what tab is focused". The pieces
// exist and nothing joins them: UAssetEditorSubsystem knows what is open,
// FGlobalTabmanager knows the active tab, and SDockTab knows when it was last
// activated. This joins the three and says which of them it answered from,
// because an agent driven from a terminal has moved OS focus away from Unreal,
// and "focused" then means "where the user was last", not "where the cursor is".

#include "EditorHandlers.h"

#include "HandlerUtils.h"

#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/Package.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SWindow.h"

// A NAMED namespace: this module is a unity build, so an anonymous one here
// would merge with whatever other .cpp shares the blob.
namespace MCPOpenAssetEditors
{
	/** Bounds on the Slate walk. It runs over a live widget tree on the game
	 *  thread, so it is capped rather than trusted to terminate quickly. */
	constexpr int32 MaxTabsPerEditor = 64;
	constexpr int32 MaxWalkDepth = 48;

	/** The layout identifier the Level Editor's major tab carries. */
	const TCHAR* const LevelEditorTabType = TEXT("LevelEditor");

	/**
	 * Every SDockTab beneath a widget, in layout order.
	 *
	 * FTabManager publishes no enumeration of its live tabs - FindExistingLiveTab
	 * answers one FTabId at a time and a Blueprint's graph documents are not
	 * known ids - so the widget tree is the only place the open tabs exist as a
	 * set. Matched by type name because Slate's docking widgets are private to
	 * their module; SNew stamps the type, so anything built as an SDockTab
	 * reports one.
	 */
	void GatherTabs(const TSharedRef<SWidget>& Widget, TArray<TSharedRef<SDockTab>>& Out, int32 Depth = 0)
	{
		if (Depth > MaxWalkDepth || Out.Num() >= MaxTabsPerEditor)
		{
			return;
		}
		if (Widget->GetType() == TEXT("SDockTab"))
		{
			Out.AddUnique(StaticCastSharedRef<SDockTab>(Widget));
		}
		if (FChildren* Children = Widget->GetChildren())
		{
			for (int32 i = 0; i < Children->Num(); ++i)
			{
				GatherTabs(Children->GetChildAt(i), Out, Depth + 1);
			}
		}
	}

	/** The major tab a minor tab lives under, or the tab itself when it already
	 *  is one. A major tab's own manager is the global one, which owns no major
	 *  tab of its own, which is what separates the two cases. */
	TSharedPtr<SDockTab> MajorTabFor(const TSharedPtr<SDockTab>& Tab)
	{
		if (!Tab.IsValid())
		{
			return nullptr;
		}
		const TSharedPtr<FTabManager> Manager = Tab->GetTabManagerPtr();
		if (Manager.IsValid())
		{
			if (TSharedPtr<SDockTab> Major = FGlobalTabmanager::Get()->GetMajorTabForTabManager(Manager.ToSharedRef()))
			{
				return Major;
			}
		}
		return Tab;
	}

	FString WindowTitleOf(const TSharedPtr<SDockTab>& Tab)
	{
		if (!Tab.IsValid())
		{
			return FString();
		}
		const TSharedPtr<SWindow> Window = Tab->GetParentWindow();
		return Window.IsValid() ? Window->GetTitle().ToString() : FString();
	}

	/** One open asset editor, gathered before anything is serialized so the
	 *  focus questions can be answered against the whole set. */
	struct FOpenEditor
	{
		IAssetEditorInstance* Instance = nullptr;
		TArray<TWeakObjectPtr<UObject>> Assets;
		FString EditorName;
		double LastActivationTime = 0.0;
		bool bPrimary = false;
		TSharedPtr<SDockTab> MajorTab;
		TArray<TSharedRef<SDockTab>> Tabs;
	};
}

TSharedPtr<FJsonValue> FEditorHandlers::GetOpenAssetEditors(const TSharedPtr<FJsonObject>& Params)
{
	using namespace MCPOpenAssetEditors;

	if (!GEditor)
	{
		return MCPError(TEXT("GEditor not available"));
	}
	UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	if (!AssetEditorSubsystem)
	{
		return MCPError(TEXT("AssetEditorSubsystem not available"));
	}

	const bool bSlate = FSlateApplication::IsInitialized();
	const double Now = bSlate ? FSlateApplication::Get().GetCurrentTime() : 0.0;

	// The active tab is a MINOR tab (the Event Graph, the Details panel). The
	// asset it belongs to is the one its major tab hosts, so both are needed.
	const TSharedPtr<SDockTab> ActiveTab = bSlate ? FGlobalTabmanager::Get()->GetActiveTab() : nullptr;
	const TSharedPtr<SDockTab> ActiveMajorTab = MajorTabFor(ActiveTab);

	// Grouped by editor instance, not by asset: one toolkit can edit several
	// assets and reporting it once per asset would claim several open editors.
	TArray<FOpenEditor> Editors;
	for (UObject* Asset : AssetEditorSubsystem->GetAllEditedAssets())
	{
		if (!Asset)
		{
			continue;
		}
		// bFocusIfOpen=false: this is a read. Asking for focus here would move
		// the user's editor as a side effect of a question about it.
		IAssetEditorInstance* Instance = AssetEditorSubsystem->FindEditorForAsset(Asset, /*bFocusIfOpen=*/false);
		if (!Instance)
		{
			continue;
		}
		if (FOpenEditor* Existing = Editors.FindByPredicate(
			[Instance](const FOpenEditor& Candidate) { return Candidate.Instance == Instance; }))
		{
			Existing->Assets.Add(Asset);
			continue;
		}

		FOpenEditor Entry;
		Entry.Instance = Instance;
		Entry.Assets.Add(Asset);
		Entry.EditorName = Instance->GetEditorName().ToString();
		Entry.LastActivationTime = Instance->GetLastActivationTime();
		Entry.bPrimary = Instance->IsPrimaryEditor();
		if (bSlate)
		{
			if (const TSharedPtr<FTabManager> Manager = Instance->GetAssociatedTabManager())
			{
				Entry.MajorTab = FGlobalTabmanager::Get()->GetMajorTabForTabManager(Manager.ToSharedRef());
			}
			if (Entry.MajorTab.IsValid())
			{
				// From the major tab's own content, not from its window: several
				// asset editors can be docked into one window and walking the
				// window would hand every editor every other editor's tabs.
				GatherTabs(Entry.MajorTab->GetContent(), Entry.Tabs);
			}
		}
		Editors.Add(MoveTemp(Entry));
	}

	// Most recently activated wins the "where was the user" question. 0.0 means
	// the toolkit has never reported an activation, so it is not an answer.
	int32 MostRecentIndex = INDEX_NONE;
	int32 FocusedIndex = INDEX_NONE;
	for (int32 i = 0; i < Editors.Num(); ++i)
	{
		if (Editors[i].LastActivationTime > 0.0
			&& (MostRecentIndex == INDEX_NONE || Editors[i].LastActivationTime > Editors[MostRecentIndex].LastActivationTime))
		{
			MostRecentIndex = i;
		}
		if (ActiveMajorTab.IsValid() && Editors[i].MajorTab == ActiveMajorTab)
		{
			FocusedIndex = i;
		}
	}

	auto DescribeEditor = [&](int32 Index) -> TSharedPtr<FJsonObject>
	{
		const FOpenEditor& Entry = Editors[Index];
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetNumberField(TEXT("index"), Index);
		Obj->SetStringField(TEXT("editorName"), Entry.EditorName);
		Obj->SetBoolField(TEXT("isPrimaryEditor"), Entry.bPrimary);

		TArray<TSharedPtr<FJsonValue>> AssetsArray;
		for (const TWeakObjectPtr<UObject>& WeakAsset : Entry.Assets)
		{
			UObject* Asset = WeakAsset.Get();
			if (!Asset)
			{
				continue;
			}
			const FString ObjectPath = Asset->GetPathName();
			const FMCPAssetPathForms Forms = MCPAssetPathForms(ObjectPath);
			// A transient or in-memory object has no package path to derive, and
			// an empty assetPath would read as "no asset" rather than "not one
			// you can reopen by path".
			const FString AssetPath = Forms.PackagePath.IsEmpty() ? ObjectPath : Forms.PackagePath;
			TSharedPtr<FJsonObject> AssetObj = MakeShared<FJsonObject>();
			AssetObj->SetStringField(TEXT("assetPath"), AssetPath);
			AssetObj->SetStringField(TEXT("objectPath"), ObjectPath);
			AssetObj->SetStringField(TEXT("assetName"), Asset->GetName());
			AssetObj->SetStringField(TEXT("assetClass"), Asset->GetClass()->GetName());
			AssetsArray.Add(MakeShared<FJsonValueObject>(AssetObj));
			if (AssetsArray.Num() == 1)
			{
				// The first asset, hoisted: it is what a caller feeds back to
				// editor(open_asset) or to a category action, and making them
				// index into assets[] for the common one-asset case is friction.
				Obj->SetStringField(TEXT("assetPath"), AssetPath);
				Obj->SetStringField(TEXT("objectPath"), ObjectPath);
				Obj->SetStringField(TEXT("assetName"), Asset->GetName());
				Obj->SetStringField(TEXT("assetClass"), Asset->GetClass()->GetName());
			}
		}
		Obj->SetArrayField(TEXT("assets"), AssetsArray);

		Obj->SetNumberField(TEXT("lastActivationTime"), Entry.LastActivationTime);
		if (Entry.LastActivationTime > 0.0 && Now > 0.0)
		{
			Obj->SetNumberField(TEXT("secondsSinceActivation"), Now - Entry.LastActivationTime);
		}
		Obj->SetBoolField(TEXT("holdsActiveTab"), Index == FocusedIndex);
		Obj->SetBoolField(TEXT("mostRecentlyActive"), Index == MostRecentIndex);

		if (Entry.MajorTab.IsValid())
		{
			Obj->SetStringField(TEXT("tabLabel"), Entry.MajorTab->GetTabLabel().ToString());
			Obj->SetStringField(TEXT("windowTitle"), WindowTitleOf(Entry.MajorTab));
		}

		TArray<TSharedPtr<FJsonValue>> TabsArray;
		for (const TSharedRef<SDockTab>& Tab : Entry.Tabs)
		{
			TSharedPtr<FJsonObject> TabObj = MakeShared<FJsonObject>();
			TabObj->SetStringField(TEXT("label"), Tab->GetTabLabel().ToString());
			TabObj->SetStringField(TEXT("tabId"), Tab->GetLayoutIdentifier().TabType.ToString());
			// Foreground is what is drawn in its tab well; active is the one tab
			// in the whole editor holding focus. A Blueprint with four graph
			// documents open has one foreground graph tab and, once the terminal
			// took focus, usually no active one at all.
			TabObj->SetBoolField(TEXT("foreground"), Tab->IsForeground());
			TabObj->SetBoolField(TEXT("active"), ActiveTab.IsValid() && Tab == ActiveTab.ToSharedRef());
			TabsArray.Add(MakeShared<FJsonValueObject>(TabObj));
		}
		Obj->SetArrayField(TEXT("tabs"), TabsArray);
		Obj->SetBoolField(TEXT("tabsTruncated"), Entry.Tabs.Num() >= MaxTabsPerEditor);
		return Obj;
	};

	auto Result = MCPSuccess();
	Result->SetNumberField(TEXT("count"), Editors.Num());

	TArray<TSharedPtr<FJsonValue>> EditorsArray;
	for (int32 i = 0; i < Editors.Num(); ++i)
	{
		EditorsArray.Add(MakeShared<FJsonValueObject>(DescribeEditor(i)));
	}
	Result->SetArrayField(TEXT("editors"), EditorsArray);

	// Whether Unreal is the foreground application at all. This decides how much
	// "focused" is worth: false means Slate's active tab is a record of where the
	// user last was, which is still the useful answer when the question arrives
	// from a terminal.
	Result->SetBoolField(TEXT("editorApplicationFocused"), bSlate && FSlateApplication::Get().IsActive());
	Result->SetBoolField(TEXT("slateAvailable"), bSlate);

	if (ActiveTab.IsValid())
	{
		TSharedPtr<FJsonObject> ActiveObj = MakeShared<FJsonObject>();
		ActiveObj->SetStringField(TEXT("label"), ActiveTab->GetTabLabel().ToString());
		ActiveObj->SetStringField(TEXT("tabId"), ActiveTab->GetLayoutIdentifier().TabType.ToString());
		// A docked minor tab has no parent window of its own; the window belongs
		// to the major tab it sits under, and an empty title here would read as
		// "no window" rather than "ask the tab above".
		FString ActiveWindowTitle = WindowTitleOf(ActiveTab);
		if (ActiveWindowTitle.IsEmpty())
		{
			ActiveWindowTitle = WindowTitleOf(ActiveMajorTab);
		}
		ActiveObj->SetStringField(TEXT("windowTitle"), ActiveWindowTitle);
		if (ActiveMajorTab.IsValid())
		{
			ActiveObj->SetStringField(TEXT("majorTabLabel"), ActiveMajorTab->GetTabLabel().ToString());
			ActiveObj->SetStringField(TEXT("majorTabId"), ActiveMajorTab->GetLayoutIdentifier().TabType.ToString());
		}
		const bool bLevelEditor = ActiveMajorTab.IsValid()
			&& ActiveMajorTab->GetLayoutIdentifier().TabType == FName(LevelEditorTabType);
		ActiveObj->SetStringField(TEXT("context"),
			FocusedIndex != INDEX_NONE ? TEXT("assetEditor")
			: bLevelEditor ? TEXT("levelEditor")
			: TEXT("other"));
		Result->SetObjectField(TEXT("activeTab"), ActiveObj);
	}

	if (FocusedIndex != INDEX_NONE)
	{
		Result->SetObjectField(TEXT("focusedAssetEditor"), DescribeEditor(FocusedIndex));
	}
	if (MostRecentIndex != INDEX_NONE)
	{
		Result->SetObjectField(TEXT("mostRecentlyActiveAssetEditor"), DescribeEditor(MostRecentIndex));
	}

	// Which question was answerable, in words, so a caller does not have to
	// infer it from which fields came back.
	FString FocusNote;
	if (Editors.Num() == 0)
	{
		FocusNote = TEXT("No asset editors are open, so nothing the user is looking at is an asset. Whatever is on screen is the Level Editor or another non-asset tab.");
	}
	else if (FocusedIndex != INDEX_NONE)
	{
		const UObject* FocusedAsset = Editors[FocusedIndex].Assets[0].Get();
		FocusNote = FString::Printf(
			TEXT("The active tab belongs to the %s editing '%s'."),
			*Editors[FocusedIndex].EditorName,
			FocusedAsset ? *FocusedAsset->GetName() : TEXT("an asset that has since been unloaded"));
	}
	else if (ActiveTab.IsValid())
	{
		FocusNote = FString::Printf(
			TEXT("The active tab ('%s') is not inside an asset editor, so no open asset is focused. mostRecentlyActiveAssetEditor is the last asset editor the user was in."),
			*ActiveTab->GetTabLabel().ToString());
	}
	else
	{
		FocusNote = TEXT("Slate reports no active tab, so nothing is focused. mostRecentlyActiveAssetEditor is the last asset editor the user was in.");
	}
	if (bSlate && !FSlateApplication::Get().IsActive())
	{
		FocusNote += TEXT(" Unreal is not the foreground application, so this is where the user last was, not where their cursor is now.");
	}
	Result->SetStringField(TEXT("focusNote"), FocusNote);

	return MCPResult(Result);
}
