// A Niagara stack edit that requests a compile waits for it before saving, so
// the package never carries stale compiled scripts. The system lives in a
// private mount.
#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Handlers/Niagara/NiagaraHandlers.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "NiagaraSystem.h"
#include "UObject/Package.h"
#include "Tests/MCPScopedTestMount.h"

namespace MCPNiagaraCompileTests
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

	FString ErrorOf(const TSharedPtr<FJsonObject>& Result)
	{
		FString Error;
		if (Result.IsValid()) Result->TryGetStringField(TEXT("error"), Error);
		return Error;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPNiagaraCompileBeforeSaveTest,
	"UE.MCP.Niagara.StackEdit.CompilesBeforeSave",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPNiagaraCompileBeforeSaveTest::RunTest(const FString& Parameters)
{
	using namespace MCPNiagaraCompileTests;
	FMCPScopedTestMount Mount(
		TEXT("/UEMCPNiagaraCompile_") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/"), TEXT("UEMCPNiagaraCompile"));
	const FString Folder = Mount.RootPath.LeftChop(1);
	const FString SystemPath = Mount.RootPath + TEXT("NS_CompileProbe");

	FMCPHandlerRegistry Registry;
	FNiagaraHandlers::RegisterHandlers(Registry);

	auto Emitter = MakeShared<FJsonObject>();
	Emitter->SetStringField(TEXT("name"), TEXT("NE_CompileProbe"));
	Emitter->SetStringField(TEXT("packagePath"), Folder);
	if (!TestTrue(TEXT("emitter created"), Succeeded(Call(Registry, TEXT("create_niagara_emitter"), Emitter)))) return false;

	auto System = MakeShared<FJsonObject>();
	System->SetStringField(TEXT("name"), TEXT("NS_CompileProbe"));
	System->SetStringField(TEXT("packagePath"), Folder);
	if (!TestTrue(TEXT("system created"), Succeeded(Call(Registry, TEXT("create_niagara_system"), System)))) return false;

	auto Add = MakeShared<FJsonObject>();
	Add->SetStringField(TEXT("systemPath"), SystemPath);
	Add->SetStringField(TEXT("emitterPath"), Mount.RootPath + TEXT("NE_CompileProbe"));
	if (!TestTrue(TEXT("emitter added to the system"), Succeeded(Call(Registry, TEXT("add_emitter_to_system"), Add)))) return false;

	UNiagaraSystem* Loaded = FindObject<UNiagaraSystem>(nullptr, *(SystemPath + TEXT(".NS_CompileProbe")));
	if (!TestNotNull(TEXT("system in memory"), Loaded)) return false;

	auto Stage = MakeShared<FJsonObject>();
	Stage->SetStringField(TEXT("systemPath"), SystemPath);
	Stage->SetStringField(TEXT("stageName"), TEXT("ProbeStage"));
	Stage->SetNumberField(TEXT("emitterIndex"), 0);
	const TSharedPtr<FJsonObject> Staged = Call(Registry, TEXT("add_niagara_simulation_stage"), Stage);
	if (TestTrue(FString::Printf(TEXT("simulation stage added (%s)"), *ErrorOf(Staged)), Succeeded(Staged)))
	{
		TestFalse(TEXT("the stack edit's compile finished before it returned"), Loaded->HasOutstandingCompilationRequests());
	}

	const TSharedPtr<FJsonObject> Unstaged = Call(Registry, TEXT("remove_niagara_simulation_stage"), Stage);
	if (TestTrue(FString::Printf(TEXT("simulation stage removed (%s)"), *ErrorOf(Unstaged)), Succeeded(Unstaged)))
	{
		TestFalse(TEXT("the removal's compile finished before it returned"), Loaded->HasOutstandingCompilationRequests());
	}

	if (UPackage* Package = Loaded->GetOutermost()) Package->SetDirtyFlag(false);
	return true;
}

#endif
