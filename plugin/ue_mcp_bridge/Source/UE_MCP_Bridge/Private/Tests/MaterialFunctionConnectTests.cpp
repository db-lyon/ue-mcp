// connect_expressions_in_function resolves an expression reference by the same
// rule connect_material_expressions does, so a class name or parameter name
// that finds a node in a material finds it in a function too.
#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Handlers/Material/MaterialHandlers.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Materials/MaterialFunction.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "UObject/Package.h"
#include "Tests/MCPScopedTestMount.h"

namespace MCPMaterialFunctionConnectTests
{
	TSharedPtr<FJsonObject> Call(FMCPHandlerRegistry& Registry, const TCHAR* Method, const TSharedPtr<FJsonObject>& Params)
	{
		const TSharedPtr<FJsonValue> Value = Registry.ExecuteHandler(Method, Params);
		return Value.IsValid() && Value->Type == EJson::Object ? Value->AsObject() : MakeShared<FJsonObject>();
	}

	bool Succeeded(const TSharedPtr<FJsonObject>& Result)
	{
		bool bSuccess = false;
		return Result.IsValid() && Result->TryGetBoolField(TEXT("success"), bSuccess) && bSuccess;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPMaterialFunctionConnectLookupTest,
	"UE.MCP.Material.FunctionConnect.SharesTheExpressionLookup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPMaterialFunctionConnectLookupTest::RunTest(const FString& Parameters)
{
	using namespace MCPMaterialFunctionConnectTests;
	FMCPScopedTestMount Mount(
		TEXT("/UEMCPFunctionConnect_") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/"), TEXT("UEMCPFunctionConnect"));
	const FString FunctionPath = Mount.RootPath + TEXT("MF_ConnectProbe");

	FMCPHandlerRegistry Registry;
	FMaterialHandlers::RegisterHandlers(Registry);

	auto Create = MakeShared<FJsonObject>();
	Create->SetStringField(TEXT("name"), TEXT("MF_ConnectProbe"));
	Create->SetStringField(TEXT("packagePath"), Mount.RootPath.LeftChop(1));
	if (!TestTrue(TEXT("material function created"), Succeeded(Call(Registry, TEXT("create_material_function"), Create)))) return false;

	for (const TCHAR* Type : { TEXT("Constant"), TEXT("Multiply") })
	{
		auto Add = MakeShared<FJsonObject>();
		Add->SetStringField(TEXT("functionPath"), FunctionPath);
		Add->SetStringField(TEXT("expressionType"), Type);
		if (!TestTrue(FString::Printf(TEXT("%s added"), Type), Succeeded(Call(Registry, TEXT("add_expression_in_function"), Add)))) return false;
	}

	// Class names resolve in a material; they must resolve here too.
	auto Connect = MakeShared<FJsonObject>();
	Connect->SetStringField(TEXT("functionPath"), FunctionPath);
	Connect->SetStringField(TEXT("sourceExpression"), TEXT("Constant"));
	Connect->SetStringField(TEXT("targetExpression"), TEXT("Multiply"));
	Connect->SetStringField(TEXT("targetInput"), TEXT("A"));
	const TSharedPtr<FJsonObject> Result = Call(Registry, TEXT("connect_expressions_in_function"), Connect);
	FString Error;
	Result->TryGetStringField(TEXT("error"), Error);
	TestTrue(FString::Printf(TEXT("a class-name reference connects in a function (%s)"), *Error), Succeeded(Result));

	if (UPackage* Package = FindPackage(nullptr, *FunctionPath)) Package->SetDirtyFlag(false);
	return true;
}

#endif
