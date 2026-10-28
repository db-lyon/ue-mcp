// Coverage for request_editor_shutdown's dirtyPackages choice.
//
// The caller names what happens to unsaved work before the close, so the
// shutdown save prompt is never raised. Every case here is one the handler must
// REFUSE: an accepted call schedules the close of the editor this suite runs
// in. What is asserted is that a refusal acts on nothing - the probe package is
// still dirty and was not written - because the refusal exists to stop a save
// or a discard of work the caller did not name.
//
// The probe lives under a private mount point in the system temp area:
// run_automation_tests dispatches every EditorContext test against whatever
// project the bridge is attached to, and nothing here may touch it.

#if WITH_DEV_AUTOMATION_TESTS

#include "Editor.h"
#include "Engine/DataTable.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Handlers/Editor/EditorHandlers.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include "Tests/MCPScopedTestMount.h"

namespace
{
	const TCHAR* const MCPShutdownChoiceTestRoot = TEXT("/UEMCPShutdownChoiceTest/");

	TSharedPtr<FJsonObject> MCPShutdownChoiceRun(FMCPHandlerRegistry& Registry, const TCHAR* Method, const TSharedPtr<FJsonObject>& Params)
	{
		const TSharedPtr<FJsonValue> Response = Registry.ExecuteHandler(Method, Params);
		return Response.IsValid() && Response->Type == EJson::Object ? Response->AsObject() : nullptr;
	}

	/** Does Field, an array of strings or of {package} objects, name Package? */
	bool MCPShutdownChoiceListNames(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Field, const FString& Package)
	{
		const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
		if (!Obj.IsValid() || !Obj->TryGetArrayField(Field, List) || !List) return false;
		for (const TSharedPtr<FJsonValue>& Value : *List)
		{
			if (!Value.IsValid()) continue;
			FString Name;
			if (Value->Type == EJson::Object) Value->AsObject()->TryGetStringField(TEXT("package"), Name);
			else Value->TryGetString(Name);
			if (Name == Package) return true;
		}
		return false;
	}

	TArray<TSharedPtr<FJsonValue>> MCPShutdownChoiceNames(const FString& Package)
	{
		TArray<TSharedPtr<FJsonValue>> Names;
		Names.Add(MakeShared<FJsonValueString>(Package));
		return Names;
	}

	bool MCPShutdownChoiceScheduled(const TSharedPtr<FJsonObject>& Obj)
	{
		bool bScheduled = true;
		return !Obj.IsValid() || !Obj->TryGetBoolField(TEXT("scheduled"), bScheduled) || bScheduled;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPShutdownDirtyChoiceRefusesTest,
	"UE.MCP.Editor.Shutdown.DirtyPackageChoiceRefusesBeforeActing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPShutdownDirtyChoiceRefusesTest::RunTest(const FString& Parameters)
{
	FMCPHandlerRegistry Registry;
	FEditorHandlers::RegisterHandlers(Registry);

	// Refused on the parameters alone, before any package is read.
	{
		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("dirtyPackages"), TEXT("keep"));
		const TSharedPtr<FJsonObject> Response = MCPShutdownChoiceRun(Registry, TEXT("request_editor_shutdown"), Params);
		TestFalse(TEXT("an unknown dirtyPackages value schedules nothing"), MCPShutdownChoiceScheduled(Response));
		TestTrue(TEXT("the refusal names the accepted values"),
			Response.IsValid() && Response->GetStringField(TEXT("error")).Contains(TEXT("refuse, save or discard")));
	}
	{
		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetArrayField(TEXT("packages"), MCPShutdownChoiceNames(TEXT("/Game/Anything")));
		const TSharedPtr<FJsonObject> Response = MCPShutdownChoiceRun(Registry, TEXT("request_editor_shutdown"), Params);
		TestFalse(TEXT("packages without save or discard schedules nothing"), MCPShutdownChoiceScheduled(Response));
	}

	if (!GEditor || GEditor->PlayWorld != nullptr || GEditor->bIsSimulatingInEditor)
	{
		AddInfo(TEXT("PIE is running or there is no editor; the dirty-package assertions need an idle editor."));
		return true;
	}

	const FMCPScopedTestMount Mount{ FString(MCPShutdownChoiceTestRoot), TEXT("UEMCPShutdownChoiceTest") };
	const FString PackageName = FString(MCPShutdownChoiceTestRoot) + TEXT("DT_ShutdownChoiceProbe");
	UPackage* Package = CreatePackage(*PackageName);
	if (!TestNotNull(TEXT("probe package created"), Package)) return false;
	UDataTable* Probe = NewObject<UDataTable>(Package, FName(TEXT("DT_ShutdownChoiceProbe")), RF_Public | RF_Standalone);
	if (!TestNotNull(TEXT("probe asset created"), Probe)) return false;
	Package->MarkPackageDirty();

	// The calls below are refusals only because the probe is dirty and not
	// named. If the editor's dirty list cannot see it, a clean editor would
	// accept them and close, so stop here instead.
	const TSharedPtr<FJsonObject> Listed = MCPShutdownChoiceRun(Registry, TEXT("list_dirty_packages"), MakeShared<FJsonObject>());
	if (!MCPShutdownChoiceListNames(Listed, TEXT("content"), PackageName))
	{
		AddError(TEXT("list_dirty_packages does not report the dirty probe, so the refusal cases cannot be run safely."));
		Package->SetDirtyFlag(false);
		return false;
	}

	FString PackageFileName;
	ResolvePackageFileName(Package, PackageFileName);

	for (const TCHAR* Choice : { TEXT("save"), TEXT("discard") })
	{
		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("dirtyPackages"), Choice);
		Params->SetArrayField(TEXT("packages"), MCPShutdownChoiceNames(FString(MCPShutdownChoiceTestRoot) + TEXT("NotTheProbe")));
		const TSharedPtr<FJsonObject> Response = MCPShutdownChoiceRun(Registry, TEXT("request_editor_shutdown"), Params);

		const FString What = FString::Printf(TEXT("dirtyPackages=%s with the probe unnamed"), Choice);
		TestFalse(What + TEXT(" schedules nothing"), MCPShutdownChoiceScheduled(Response));
		TestTrue(What + TEXT(" names the probe as unexpected"),
			MCPShutdownChoiceListNames(Response, TEXT("unexpectedDirtyPackages"), PackageName));
		TestTrue(What + TEXT(" leaves the probe dirty"), Package->IsDirty());
		TestFalse(What + TEXT(" writes nothing"),
			!PackageFileName.IsEmpty() && IFileManager::Get().FileExists(*PackageFileName));
	}

	Package->SetDirtyFlag(false);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
