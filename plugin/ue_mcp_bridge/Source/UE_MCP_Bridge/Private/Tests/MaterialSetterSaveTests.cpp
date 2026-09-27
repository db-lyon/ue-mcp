// set_material_shading_model / domain / blend_mode write the material to disk
// like every sibling setter, and report saved. The material lives in a private
// mount, and the baseline is saved first so each write has to save again.
#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Handlers/Material/MaterialHandlers.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "UObject/Package.h"
#include "Tests/MCPScopedTestMount.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPMaterialSetterSavesTest,
	"UE.MCP.Material.Setters.SaveTheMaterial",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPMaterialSetterSavesTest::RunTest(const FString& Parameters)
{
	FMCPScopedTestMount Mount(
		TEXT("/UEMCPMaterialSetter_") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/"), TEXT("UEMCPMaterialSetter"));
	const FString MaterialPath = Mount.RootPath + TEXT("M_SetterProbe");

	FMCPHandlerRegistry Registry;
	FMaterialHandlers::RegisterHandlers(Registry);

	auto Run = [&Registry](const TCHAR* Method, const TSharedPtr<FJsonObject>& Args)
	{
		const TSharedPtr<FJsonValue> Value = Registry.ExecuteHandler(Method, Args);
		return Value.IsValid() && Value->Type == EJson::Object ? Value->AsObject() : MakeShared<FJsonObject>();
	};

	auto Create = MakeShared<FJsonObject>();
	Create->SetStringField(TEXT("name"), TEXT("M_SetterProbe"));
	Create->SetStringField(TEXT("packagePath"), Mount.RootPath.LeftChop(1));
	bool bCreated = false;
	if (!TestTrue(TEXT("material created"), Run(TEXT("create_material"), Create)->TryGetBoolField(TEXT("success"), bCreated) && bCreated)) return false;
	UPackage* Package = FindPackage(nullptr, *MaterialPath);
	if (!TestNotNull(TEXT("material package in memory"), Package)) return false;

	struct FCase { const TCHAR* Method; const TCHAR* Key; const TCHAR* Value; };
	for (const FCase& Case : { FCase{ TEXT("set_material_shading_model"), TEXT("shadingModel"), TEXT("Unlit") },
		FCase{ TEXT("set_material_blend_mode"), TEXT("blendMode"), TEXT("Masked") },
		FCase{ TEXT("set_material_domain"), TEXT("materialDomain"), TEXT("PostProcess") } })
	{
		Package->SetDirtyFlag(false);
		auto Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("assetPath"), MaterialPath);
		Args->SetStringField(Case.Key, Case.Value);
		const TSharedPtr<FJsonObject> Result = Run(Case.Method, Args);
		bool bSuccess = false;
		if (!TestTrue(FString::Printf(TEXT("%s succeeds"), Case.Method), Result->TryGetBoolField(TEXT("success"), bSuccess) && bSuccess)) continue;
		bool bSaved = false;
		TestTrue(FString::Printf(TEXT("%s reports saved"), Case.Method), Result->TryGetBoolField(TEXT("saved"), bSaved) && bSaved);
		TestFalse(FString::Printf(TEXT("%s left the package clean"), Case.Method), Package->IsDirty());
	}

	Package->SetDirtyFlag(false);
	return true;
}

#endif
