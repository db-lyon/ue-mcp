// Coverage for level(convert_brushes_to_static_mesh) (#911).
//
// The committing path destroys actors and writes new mesh packages, so nothing
// here goes near it. What is asserted is the set of refusals, because those
// are the whole feature: ConvertActors itself is one engine call.

#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "Handlers/Level/LevelHandlers.h"
#include "Misc/AutomationTest.h"

namespace
{
	TSharedPtr<FJsonObject> MCPConvertTestRun(FMCPHandlerRegistry& Registry, const TSharedPtr<FJsonObject>& Request)
	{
		const TSharedPtr<FJsonValue> Response =
			Registry.ExecuteHandler(TEXT("convert_brushes_to_static_mesh"), Request);
		if (!Response.IsValid() || Response->Type != EJson::Object)
		{
			return nullptr;
		}
		return Response->AsObject();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLevelConvertBrushesGuardrailTest,
	"UE.MCP.Level.ConvertBrushes.Guardrails",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLevelConvertBrushesGuardrailTest::RunTest(const FString& Parameters)
{
	FMCPHandlerRegistry Registry;
	FLevelHandlers::RegisterHandlers(Registry);

	TestTrue(TEXT("convert_brushes_to_static_mesh is registered"),
		Registry.HasHandler(TEXT("convert_brushes_to_static_mesh")));
	TestTrue(TEXT("it carries its own timeout, because generating meshes is slow"),
		Registry.GetHandlerTimeout(TEXT("convert_brushes_to_static_mesh")) > 0.0f);

	// No selector would mean "every brush in the level", and this destroys what
	// it converts.
	{
		const TSharedPtr<FJsonObject> Response = MCPConvertTestRun(Registry, MakeShared<FJsonObject>());
		if (Response.IsValid())
		{
			TestFalse(TEXT("an unbounded conversion fails"), Response->GetBoolField(TEXT("success")));
			TestTrue(TEXT("and says it will not run against a whole level"),
				Response->GetStringField(TEXT("error")).Contains(TEXT("implicitly")));
		}
	}

	// The generated meshes must not land in engine content.
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("folderPath"), TEXT("Blockout"));
		Request->SetStringField(TEXT("destinationPath"), TEXT("/Engine/Meshes"));
		const TSharedPtr<FJsonObject> Response = MCPConvertTestRun(Registry, Request);
		if (Response.IsValid())
		{
			TestFalse(TEXT("an engine destination fails"), Response->GetBoolField(TEXT("success")));
			TestTrue(TEXT("and says why"),
				Response->GetStringField(TEXT("error")).Contains(TEXT("writable")));
		}
	}

	// A class filter that is not a brush class is a caller error, not an empty
	// result set.
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("folderPath"), TEXT("Blockout"));
		Request->SetStringField(TEXT("classFilter"), TEXT("StaticMeshActor"));
		const TSharedPtr<FJsonObject> Response = MCPConvertTestRun(Registry, Request);
		if (Response.IsValid())
		{
			TestFalse(TEXT("a non-brush class filter fails"), Response->GetBoolField(TEXT("success")));
		}
	}

	// And the default is a preview, on a selector that is valid but matches
	// nothing in an arbitrary project.
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("folderPath"), TEXT("UEMCPNoSuchFolderForTests"));
		const TSharedPtr<FJsonObject> Response = MCPConvertTestRun(Registry, Request);
		if (Response.IsValid() && Response->GetBoolField(TEXT("success")))
		{
			TestTrue(TEXT("dryRun defaults to true"), Response->GetBoolField(TEXT("dryRun")));
			TestEqual(TEXT("an empty folder is eligible for nothing"),
				static_cast<int32>(Response->GetNumberField(TEXT("eligible"))), 0);
			TestTrue(TEXT("and explains what the exclusions are"),
				Response->HasField(TEXT("zeroMatchNote")));
		}
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
