// Coverage for level(save) reporting the truth about the packages it touched
// (#964).
//
// The failure this guards is a FALSE failure: the old action answered
// {"success": false, "error": "Failed to save current level"} on a map that
// editor(save_dirty) then wrote seconds later. An agent that believes a save
// did not happen redoes work that was already on disk, so a save reporting
// failure when nothing failed is more expensive than one that reports nothing.
//
// The test deliberately writes nothing. It runs only when the level and its
// external packages are already clean, and then asserts that a save with
// nothing to do is reported as the success it is, names the package it
// considered, and carries no error. A test that saved the map to check saving
// would leave exactly the kind of unasked-for commit this repo works to avoid.

#if WITH_DEV_AUTOMATION_TESTS

#include "Editor.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "HandlerRegistry.h"
#include "Handlers/Level/LevelHandlers.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

namespace
{
	TSharedPtr<FJsonObject> MCPLevelSaveTestRun(
		FMCPHandlerRegistry& Registry,
		const TSharedPtr<FJsonObject>& Request)
	{
		const TSharedPtr<FJsonValue> Response = Registry.ExecuteHandler(TEXT("save_level"), Request);
		if (!Response.IsValid() || Response->Type != EJson::Object)
		{
			return nullptr;
		}
		return Response->AsObject();
	}

	/** True when saving would write nothing at all, so the test is safe to run. */
	bool MCPLevelSaveTestNothingIsDirty(UWorld* World)
	{
		if (!World) return false;
		ULevel* Level = World->GetCurrentLevel();
		if (!Level) Level = World->PersistentLevel;
		if (!Level) return false;
		UPackage* Package = Level->GetOutermost();
		if (!Package || Package->IsDirty()) return false;
		if (Level->IsUsingExternalObjects())
		{
			for (UPackage* External : Level->GetLoadedExternalObjectPackages())
			{
				if (External && External->IsDirty()) return false;
			}
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLevelSaveReportsWhatHappenedTest,
	"UE.MCP.Level.Save.ReportsWhatHappenedToEachPackage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLevelSaveReportsWhatHappenedTest::RunTest(const FString& Parameters)
{
	FMCPHandlerRegistry Registry;
	FLevelHandlers::RegisterHandlers(Registry);
	TestTrue(TEXT("save_level is registered"), Registry.HasHandler(TEXT("save_level")));

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) return true;
	if (GEditor->PlayWorld != nullptr || GEditor->bIsSimulatingInEditor) return true;
	if (!MCPLevelSaveTestNothingIsDirty(World))
	{
		// Something is unsaved. Running the action here would write it, and
		// this test is not permitted to commit anybody's work for them.
		AddInfo(TEXT("Skipped: the open level has unsaved changes, and this test never writes."));
		return true;
	}

	const TSharedPtr<FJsonObject> Response = MCPLevelSaveTestRun(Registry, MakeShared<FJsonObject>());
	if (!Response.IsValid()) return true;

	// The regression: nothing failed, so nothing may be reported as a failure.
	TestTrue(TEXT("a save with nothing to do succeeds"), Response->GetBoolField(TEXT("success")));
	FString Error;
	TestFalse(TEXT("and carries no error"), Response->TryGetStringField(TEXT("error"), Error));
	TestEqual(TEXT("and reports that nothing failed"),
		static_cast<int32>(Response->GetNumberField(TEXT("failedCount"))), 0);
	TestEqual(TEXT("and that nothing needed writing"),
		static_cast<int32>(Response->GetNumberField(TEXT("savedCount"))), 0);

	// The diagnostic half: the response names what it considered, so a caller
	// can tell "already saved" from "saved nothing because it looked nowhere".
	FString LevelPackage;
	TestTrue(TEXT("the response names the level package"),
		Response->TryGetStringField(TEXT("levelPackage"), LevelPackage));
	TestTrue(TEXT("which is not empty"), !LevelPackage.IsEmpty());
	FString Message;
	TestTrue(TEXT("and says in words that nothing needed writing"),
		Response->TryGetStringField(TEXT("message"), Message));

	// A clean package appears as skipped with its reason, never as a failure.
	const TArray<TSharedPtr<FJsonValue>>* SkippedRows = nullptr;
	if (Response->TryGetArrayField(TEXT("skipped"), SkippedRows) && SkippedRows)
	{
		TestTrue(TEXT("the clean level package is listed as skipped"), SkippedRows->Num() >= 1);
		if (SkippedRows->Num() > 0)
		{
			const TSharedPtr<FJsonObject> Row = (*SkippedRows)[0]->AsObject();
			if (Row.IsValid())
			{
				TestFalse(TEXT("a skipped package is not marked saved"), Row->GetBoolField(TEXT("saved")));
				TestFalse(TEXT("and was not dirty"), Row->GetBoolField(TEXT("wasDirty")));
				FString RowError;
				TestFalse(TEXT("and is not an error"), Row->TryGetStringField(TEXT("error"), RowError));
			}
		}
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
