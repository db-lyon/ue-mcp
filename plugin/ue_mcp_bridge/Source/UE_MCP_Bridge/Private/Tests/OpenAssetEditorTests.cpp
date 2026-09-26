// Coverage for editor(get_open_asset_editors) (#1112).
//
// Two things are worth asserting and neither needs a particular editor layout:
// an asset whose editor is open is reported, with a path a caller can act on;
// and asking the question changes nothing, because the subsystem call that
// finds an editor will also focus it if asked and a read that moved the user's
// editor would be worse than no answer.

#if WITH_DEV_AUTOMATION_TESTS

#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "HandlerRegistry.h"
#include "Handlers/Editor/EditorHandlers.h"
#include "Misc/AutomationTest.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/UObjectGlobals.h"
#include "Widgets/Docking/SDockTab.h"

namespace
{
	TSharedPtr<FJsonObject> MCPOpenEditorsTestRun(FMCPHandlerRegistry& Registry)
	{
		const TSharedPtr<FJsonValue> Response =
			Registry.ExecuteHandler(TEXT("get_open_asset_editors"), MakeShared<FJsonObject>());
		if (!Response.IsValid() || Response->Type != EJson::Object)
		{
			return nullptr;
		}
		return Response->AsObject();
	}

	/** Does the answer name this asset among the editors it reports? */
	bool MCPOpenEditorsMentions(const TSharedPtr<FJsonObject>& Response, const FString& AssetName)
	{
		const TArray<TSharedPtr<FJsonValue>>* Editors = nullptr;
		if (!Response.IsValid() || !Response->TryGetArrayField(TEXT("editors"), Editors))
		{
			return false;
		}
		for (const TSharedPtr<FJsonValue>& Value : *Editors)
		{
			const TSharedPtr<FJsonObject> Entry = Value.IsValid() ? Value->AsObject() : nullptr;
			if (Entry.IsValid() && Entry->GetStringField(TEXT("assetName")) == AssetName)
			{
				return true;
			}
		}
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEditorOpenAssetEditorsTest,
	"UE.MCP.Editor.OpenAssetEditors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEditorOpenAssetEditorsTest::RunTest(const FString& Parameters)
{
	FMCPHandlerRegistry Registry;
	FEditorHandlers::RegisterHandlers(Registry);
	TestTrue(TEXT("get_open_asset_editors is registered"), Registry.HasHandler(TEXT("get_open_asset_editors")));

	// The shape, whatever the run happens to have open. An editor with nothing
	// open still has to answer, because "nothing is open" is an answer.
	{
		const TSharedPtr<FJsonObject> Response = MCPOpenEditorsTestRun(Registry);
		if (!Response.IsValid())
		{
			AddError(TEXT("get_open_asset_editors returned no object"));
			return false;
		}
		TestTrue(TEXT("the read succeeds"), Response->GetBoolField(TEXT("success")));
		TestTrue(TEXT("editors is always present"), Response->HasField(TEXT("editors")));
		TestTrue(TEXT("count is always present"), Response->HasField(TEXT("count")));
		TestTrue(TEXT("focusNote says which question was answered"),
			!Response->GetStringField(TEXT("focusNote")).IsEmpty());
		// The caveat the whole action turns on: whether Unreal has OS focus
		// decides whether "focused" means now or last.
		TestTrue(TEXT("editorApplicationFocused is always reported"),
			Response->HasField(TEXT("editorApplicationFocused")));
	}

	UAssetEditorSubsystem* AssetEditorSubsystem =
		GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
	if (!AssetEditorSubsystem || !FSlateApplication::IsInitialized())
	{
		AddInfo(TEXT("No asset editor subsystem or no Slate: the open-editor assertions need a running editor."));
		return true;
	}

	UObject* Asset = StaticLoadObject(UObject::StaticClass(), nullptr, TEXT("/Engine/BasicShapes/Cube"));
	if (!Asset)
	{
		AddInfo(TEXT("/Engine/BasicShapes/Cube did not load; skipping the open-editor assertions."));
		return true;
	}

	// Leave the editor as it was found. An asset editor this test opened must
	// not outlive it, and one the user already had open must not be closed.
	const bool bWasAlreadyOpen = AssetEditorSubsystem->FindEditorForAsset(Asset, /*bFocusIfOpen=*/false) != nullptr;
	if (!bWasAlreadyOpen && !AssetEditorSubsystem->OpenEditorForAsset(Asset))
	{
		AddInfo(TEXT("The Static Mesh editor would not open; skipping the open-editor assertions."));
		return true;
	}

	const TSharedPtr<SDockTab> ActiveTabBefore = FGlobalTabmanager::Get()->GetActiveTab();

	const TSharedPtr<FJsonObject> Response = MCPOpenEditorsTestRun(Registry);
	if (Response.IsValid())
	{
		TestTrue(TEXT("an open asset editor is reported"), MCPOpenEditorsMentions(Response, TEXT("Cube")));
		TestTrue(TEXT("at least one editor is counted"),
			Response->GetIntegerField(TEXT("count")) >= 1);
	}
	else
	{
		AddError(TEXT("get_open_asset_editors returned no object with an editor open"));
	}

	// The read is a read: FindEditorForAsset focuses when asked, and this action
	// must never ask.
	TestTrue(TEXT("reading does not move the active tab"),
		FGlobalTabmanager::Get()->GetActiveTab() == ActiveTabBefore);

	if (!bWasAlreadyOpen)
	{
		AssetEditorSubsystem->CloseAllEditorsForAsset(Asset);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
