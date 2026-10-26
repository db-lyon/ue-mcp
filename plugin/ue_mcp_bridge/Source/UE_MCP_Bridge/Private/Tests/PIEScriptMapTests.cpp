// A test script names the level it plays in: map round-trips through
// pie_script_create and pie_script_read, a missing level is refused by name,
// and pie_script_run opens the script's level before PIE.
//
// The run half opens two levels it creates and then reopens the one that was
// open, so it is skipped while that level has unsaved changes.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Handlers/PIE/PIEHandlers.h"
#include "Dom/JsonObject.h"
#include "EditorAssetLibrary.h"
#include "LevelEditorSubsystem.h"
#include "Misc/Guid.h"

namespace MCPPIEScriptMapTests
{
	TSharedPtr<FJsonObject> Run(FMCPHandlerRegistry& Registry, const TCHAR* Method, const TSharedPtr<FJsonObject>& Request)
	{
		const TSharedPtr<FJsonValue> Response = Registry.ExecuteHandler(Method, Request);
		return Response.IsValid() && Response->Type == EJson::Object ? Response->AsObject() : MakeShared<FJsonObject>();
	}

	bool Succeeded(const TSharedPtr<FJsonObject>& Response)
	{
		bool bSuccess = false;
		return Response->TryGetBoolField(TEXT("success"), bSuccess) && bSuccess;
	}

	FString Field(const TSharedPtr<FJsonObject>& Response, const TCHAR* Name)
	{
		FString Value;
		Response->TryGetStringField(Name, Value);
		return Value;
	}

	TSharedPtr<FJsonObject> CreateRequest(const FString& Name, const FString& Map)
	{
		TSharedPtr<FJsonObject> Step = MakeShared<FJsonObject>();
		Step->SetStringField(TEXT("instruction"), TEXT("The level is the one the script names"));
		Step->SetStringField(TEXT("judge"), TEXT("manual"));
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("name"), Name);
		P->SetStringField(TEXT("package_path"), TEXT("/Game/UEMCPTests/ScriptMap"));
		P->SetArrayField(TEXT("steps"), { MakeShared<FJsonValueObject>(Step) });
		P->SetBoolField(TEXT("manual_only"), true);
		P->SetStringField(TEXT("map"), Map);
		return P;
	}

	FString OpenLevel()
	{
		const UWorld* World = GetEditorWorld();
		return World ? World->GetOutermost()->GetName() : FString();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPIEScriptMapTest,
	"UE.MCP.PIE.Script.MapOpensBeforeRun",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPIEScriptMapTest::RunTest(const FString& /*Parameters*/)
{
	using namespace MCPPIEScriptMapTests;

	FMCPHandlerRegistry Registry;
	FPIEHandlers::RegisterHandlers(Registry);
	const FString Suffix = FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8);

	// A level that does not exist is refused, and the refusal names it.
	const FString Missing = FString::Printf(TEXT("/Game/UEMCPTests/ScriptMap/L_Missing_%s"), *Suffix);
	const TSharedPtr<FJsonObject> Refused = Run(Registry, TEXT("pie_script_create"), CreateRequest(TEXT("S_MapMissing_") + Suffix, Missing));
	TestFalse(TEXT("script_create refuses a map that does not exist"), Succeeded(Refused));
	TestTrue(TEXT("the refusal names the missing map"), Field(Refused, TEXT("error")).Contains(Missing));
	TestFalse(TEXT("no script was saved for the refused create"),
		UEditorAssetLibrary::DoesAssetExist(TEXT("/Game/UEMCPTests/ScriptMap/S_MapMissing_") + Suffix));

	const FString Original = OpenLevel();
	const UWorld* OriginalWorld = GetEditorWorld();
	if (!OriginalWorld || OriginalWorld->GetOutermost()->IsDirty() || MCPIsPlayInEditorActive())
	{
		AddInfo(FString::Printf(TEXT("The open level %s has unsaved changes or PIE is running, so the run half is skipped: it opens other levels."), *Original));
		return true;
	}

	ULevelEditorSubsystem* LevelEditor = GEditor ? GEditor->GetEditorSubsystem<ULevelEditorSubsystem>() : nullptr;
	if (!TestNotNull(TEXT("level editor subsystem"), LevelEditor)) return false;

	// Two saved levels: the script plays in Level, and Other is open when it runs.
	const FString Level = FString::Printf(TEXT("/Game/UEMCPTests/ScriptMap/L_ScriptMap_%s"), *Suffix);
	const FString Other = FString::Printf(TEXT("/Game/UEMCPTests/ScriptMap/L_Other_%s"), *Suffix);
	TestTrue(TEXT("script level created"), LevelEditor->NewLevel(Level, /*bIsPartitionedWorld*/ false));
	TestTrue(TEXT("script level saved"), LevelEditor->SaveCurrentLevel());
	TestTrue(TEXT("other level created"), LevelEditor->NewLevel(Other, /*bIsPartitionedWorld*/ false));
	TestTrue(TEXT("other level saved"), LevelEditor->SaveCurrentLevel());
	TestEqual(TEXT("the other level is open before the run"), OpenLevel(), Other);

	const FString ScriptName = TEXT("S_MapRoundTrip_") + Suffix;
	const FString ScriptPath = TEXT("/Game/UEMCPTests/ScriptMap/") + ScriptName;
	const TSharedPtr<FJsonObject> Created = Run(Registry, TEXT("pie_script_create"), CreateRequest(ScriptName, Level));
	TestTrue(FString::Printf(TEXT("script_create with map succeeds: %s"), *Field(Created, TEXT("error"))), Succeeded(Created));
	TestEqual(TEXT("script_create answers the map"), Field(Created, TEXT("map")), Level);

	TSharedPtr<FJsonObject> ReadRequest = MakeShared<FJsonObject>();
	ReadRequest->SetStringField(TEXT("path"), ScriptPath);
	const TSharedPtr<FJsonObject> Read = Run(Registry, TEXT("pie_script_read"), ReadRequest);
	TestTrue(TEXT("script_read succeeds"), Succeeded(Read));
	TestEqual(TEXT("script_read returns the stored map"), Field(Read, TEXT("map")), Level);

	// Running it opens the script's level before PIE.
	TSharedPtr<FJsonObject> RunRequest = MakeShared<FJsonObject>();
	RunRequest->SetStringField(TEXT("script"), ScriptPath);
	RunRequest->SetBoolField(TEXT("record"), false);
	RunRequest->SetBoolField(TEXT("start_pie"), false);
	const TSharedPtr<FJsonObject> Started = Run(Registry, TEXT("pie_script_run"), RunRequest);
	TestTrue(FString::Printf(TEXT("script_run succeeds: %s"), *Field(Started, TEXT("error"))), Succeeded(Started));
	TestEqual(TEXT("script_run opened the script's level"), OpenLevel(), Level);
	TestEqual(TEXT("script_run answers the map it opened"), Field(Started, TEXT("map")), Level);

	TSharedPtr<FJsonObject> StopRequest = MakeShared<FJsonObject>();
	StopRequest->SetStringField(TEXT("run_id"), Field(Started, TEXT("run_id")));
	StopRequest->SetStringField(TEXT("reason"), TEXT("test cleanup"));
	Run(Registry, TEXT("pie_script_stop"), StopRequest);

	// Put the editor back on the level it had open; an unsaved one comes back as a new untitled map.
	FString Error;
	if (Original.StartsWith(TEXT("/Temp/")) || !MCPCheckMapExists(Original).IsEmpty())
	{
		GEditor->NewMap();
	}
	else
	{
		const bool bReopened = MCPLoadMapIfNeeded(Original, Error);
		TestTrue(FString::Printf(TEXT("original level reopened: %s"), *Error), bReopened);
	}
	UEditorAssetLibrary::DeleteAsset(ScriptPath);
	UEditorAssetLibrary::DeleteAsset(Level);
	UEditorAssetLibrary::DeleteAsset(Other);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
