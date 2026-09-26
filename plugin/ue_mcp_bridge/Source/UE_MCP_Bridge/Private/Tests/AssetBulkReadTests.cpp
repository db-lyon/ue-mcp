// Coverage for asset(bulk_read_properties) that is safe to run anywhere.
//
// run_automation_tests dispatches every EditorContext/EngineFilter test in the
// process against whatever project the bridge is attached to. This action
// never writes, so the assertions can run it for real; they are written about
// the contract (registration, argument rejection, the absent-versus-null
// distinction, the caps) rather than about any particular project's content.

#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "Handlers/Asset/AssetHandlers_BulkRead.h"
#include "Misc/AutomationTest.h"

namespace
{
	TSharedPtr<FJsonObject> MCPBulkReadTestRun(FMCPHandlerRegistry& Registry, const TSharedPtr<FJsonObject>& Request)
	{
		const TSharedPtr<FJsonValue> Response =
			Registry.ExecuteHandler(TEXT("bulk_read_asset_properties"), Request);
		if (!Response.IsValid() || Response->Type != EJson::Object)
		{
			return nullptr;
		}
		return Response->AsObject();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetBulkReadContractTest,
	"UE.MCP.Asset.BulkRead.Contract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetBulkReadContractTest::RunTest(const FString& Parameters)
{
	FMCPHandlerRegistry Registry;
	FAssetBulkReadHandlers::RegisterHandlers(Registry);

	TestTrue(TEXT("bulk_read_asset_properties is registered"),
		Registry.HasHandler(TEXT("bulk_read_asset_properties")));
	TestTrue(
		TEXT("it carries its own timeout, because loading hundreds of assets outlives the default"),
		Registry.GetHandlerTimeout(TEXT("bulk_read_asset_properties")) > 0.0f);

	// No propertyNames means there is nothing to read, and returning an empty
	// success would look like "no assets have these properties".
	{
		const TSharedPtr<FJsonObject> Response = MCPBulkReadTestRun(Registry, MakeShared<FJsonObject>());
		TestTrue(TEXT("a request with no properties returns an object"), Response.IsValid());
		if (Response.IsValid())
		{
			TestFalse(TEXT("a request with no properties fails"), Response->GetBoolField(TEXT("success")));
			TestTrue(TEXT("the error names propertyNames"),
				Response->GetStringField(TEXT("error")).Contains(TEXT("propertyNames")));
		}
	}

	// Properties but no selection is the other half of the same mistake.
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetArrayField(TEXT("propertyNames"), { MakeShared<FJsonValueString>(TEXT("bCanEverTick")) });
		const TSharedPtr<FJsonObject> Response = MCPBulkReadTestRun(Registry, Request);
		if (Response.IsValid())
		{
			TestFalse(TEXT("a request with no selection fails"), Response->GetBoolField(TEXT("success")));
			TestTrue(TEXT("the error offers both selection shapes"),
				Response->GetStringField(TEXT("error")).Contains(TEXT("assetPaths")) &&
				Response->GetStringField(TEXT("error")).Contains(TEXT("directory")));
		}
	}

	// An unresolvable class filter is reported as such rather than silently
	// matching nothing, which would read as "the directory is empty".
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetArrayField(TEXT("propertyNames"), { MakeShared<FJsonValueString>(TEXT("bCanEverTick")) });
		Request->SetStringField(TEXT("directory"), TEXT("/Game"));
		Request->SetArrayField(TEXT("classNames"), { MakeShared<FJsonValueString>(TEXT("NotARealUnrealClass")) });
		const TSharedPtr<FJsonObject> Response = MCPBulkReadTestRun(Registry, Request);
		if (Response.IsValid())
		{
			TestFalse(TEXT("an unresolvable class fails"), Response->GetBoolField(TEXT("success")));
			TestEqual(TEXT("and says why"),
				Response->GetStringField(TEXT("reason")), FString(TEXT("class_not_found")));
		}
	}

	// suspectOnly is an AND, so pairing it with whereMode 'any' would change
	// what the other predicates mean.
	{
		TSharedPtr<FJsonObject> Predicate = MakeShared<FJsonObject>();
		Predicate->SetStringField(TEXT("field"), TEXT("className"));
		Predicate->SetStringField(TEXT("op"), TEXT("contains"));
		Predicate->SetStringField(TEXT("value"), TEXT("Sound"));

		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetArrayField(TEXT("propertyNames"), { MakeShared<FJsonValueString>(TEXT("bCanEverTick")) });
		Request->SetStringField(TEXT("directory"), TEXT("/Game"));
		Request->SetBoolField(TEXT("suspectOnly"), true);
		Request->SetStringField(TEXT("whereMode"), TEXT("any"));
		Request->SetArrayField(TEXT("where"), { MakeShared<FJsonValueObject>(Predicate) });
		const TSharedPtr<FJsonObject> Response = MCPBulkReadTestRun(Registry, Request);
		if (Response.IsValid())
		{
			TestFalse(TEXT("suspectOnly with whereMode any fails"), Response->GetBoolField(TEXT("success")));
		}
	}

	// An unknown operator is named, with the valid ones listed. This comes
	// from the shared query header, so this assertion is also what stops the
	// level and asset vocabularies from drifting apart unnoticed.
	{
		TSharedPtr<FJsonObject> Predicate = MakeShared<FJsonObject>();
		Predicate->SetStringField(TEXT("field"), TEXT("className"));
		Predicate->SetStringField(TEXT("op"), TEXT("soundsLike"));

		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetArrayField(TEXT("propertyNames"), { MakeShared<FJsonValueString>(TEXT("bCanEverTick")) });
		Request->SetStringField(TEXT("directory"), TEXT("/Game"));
		Request->SetArrayField(TEXT("where"), { MakeShared<FJsonValueObject>(Predicate) });
		const TSharedPtr<FJsonObject> Response = MCPBulkReadTestRun(Registry, Request);
		if (Response.IsValid())
		{
			TestFalse(TEXT("an unknown operator fails"), Response->GetBoolField(TEXT("success")));
			TestTrue(TEXT("and lists the valid operators"),
				Response->GetStringField(TEXT("error")).Contains(TEXT("notContains")));
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetBulkReadAbsentVersusNullTest,
	"UE.MCP.Asset.BulkRead.AbsentIsNotNull",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetBulkReadAbsentVersusNullTest::RunTest(const FString& Parameters)
{
	FMCPHandlerRegistry Registry;
	FAssetBulkReadHandlers::RegisterHandlers(Registry);

	// A property no class has, read off an engine asset that certainly exists.
	// "this class has no such property" and "this property is unset" are
	// different findings; collapsing them is what makes an audit unactionable.
	TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
	Request->SetArrayField(TEXT("assetPaths"), {
		MakeShared<FJsonValueString>(TEXT("/Engine/BasicShapes/Cube.Cube")) });
	Request->SetArrayField(TEXT("propertyNames"), {
		MakeShared<FJsonValueString>(TEXT("ThisPropertyDoesNotExistAnywhere")) });

	const TSharedPtr<FJsonObject> Response = MCPBulkReadTestRun(Registry, Request);
	if (!Response.IsValid() || !Response->GetBoolField(TEXT("success")))
	{
		// The engine content pack is not guaranteed to be mounted in every
		// context this test runs in, and this test may not create an asset of
		// its own to stand in for it.
		return true;
	}

	TestFalse(TEXT("reading an asset dirties nothing"), Response->GetBoolField(TEXT("dirtiedPackages")));

	const TArray<TSharedPtr<FJsonValue>>& Rows = Response->GetArrayField(TEXT("rows"));
	if (Rows.Num() == 1 && Rows[0].IsValid() && Rows[0]->Type == EJson::Object)
	{
		const TSharedPtr<FJsonObject> Row = Rows[0]->AsObject();
		TestEqual(TEXT("the unknown property is reported as missing, not as null"),
			Row->GetArrayField(TEXT("missingProperties")).Num(), 1);
		TestTrue(TEXT("and the row is flagged suspect"), Row->GetBoolField(TEXT("suspect")));
		TestEqual(TEXT("props carries no invented entry for it"),
			Row->GetObjectField(TEXT("props"))->Values.Num(), 0);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
