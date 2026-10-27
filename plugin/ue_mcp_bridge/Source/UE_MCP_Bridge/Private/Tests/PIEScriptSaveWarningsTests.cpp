// pie_script_create and pie_script_update save a script whose steps the static
// check cannot resolve, returning the findings as warnings, and refuse only a
// script that cannot be written.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Handlers/PIE/PIEHandlers.h"
#include "Dom/JsonObject.h"
#include "EditorAssetLibrary.h"
#include "Misc/Guid.h"

namespace MCPPIEScriptSaveWarningsTests
{
	const TCHAR* const Dir = TEXT("/Game/UEMCPTests/ScriptWarnings");
	// A value only a running game has: the pawn's PlayerState is empty before PIE.
	const TCHAR* const PIEOnlyValue = TEXT("PlayerState.SigilAttributes.Sigil.CurrentValue");

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

	TArray<TSharedPtr<FJsonValue>> Array(const TSharedPtr<FJsonObject>& Response, const TCHAR* Name)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		return Response->TryGetArrayField(Name, Values) && Values ? *Values : TArray<TSharedPtr<FJsonValue>>();
	}

	TSharedPtr<FJsonValue> AutoStep(const FString& Value, const FString& Op)
	{
		TSharedPtr<FJsonObject> Expect = MakeShared<FJsonObject>();
		Expect->SetStringField(TEXT("value"), Value);
		Expect->SetStringField(TEXT("op"), Op);
		Expect->SetNumberField(TEXT("target"), 0);
		TSharedPtr<FJsonObject> Step = MakeShared<FJsonObject>();
		Step->SetStringField(TEXT("instruction"), TEXT("The sigil charges"));
		Step->SetObjectField(TEXT("expect"), Expect);
		return MakeShared<FJsonValueObject>(Step);
	}

	TSharedPtr<FJsonValue> ManualStep()
	{
		TSharedPtr<FJsonObject> Step = MakeShared<FJsonObject>();
		Step->SetStringField(TEXT("instruction"), TEXT("The sigil glows"));
		Step->SetStringField(TEXT("judge"), TEXT("manual"));
		return MakeShared<FJsonValueObject>(Step);
	}

	// The step and field of the first warning, or an empty pair.
	TPair<int32, FString> FirstWarning(const TSharedPtr<FJsonObject>& Response)
	{
		const TArray<TSharedPtr<FJsonValue>> Warnings = Array(Response, TEXT("warnings"));
		if (Warnings.Num() == 0 || !Warnings[0]->AsObject().IsValid()) return { INDEX_NONE, FString() };
		const TSharedPtr<FJsonObject> W = Warnings[0]->AsObject();
		double Step = -1;
		W->TryGetNumberField(TEXT("step"), Step);
		return { static_cast<int32>(Step), Field(W, TEXT("field")) };
	}

	int32 StoredStepCount(FMCPHandlerRegistry& Registry, const FString& Path)
	{
		TSharedPtr<FJsonObject> Read = MakeShared<FJsonObject>();
		Read->SetStringField(TEXT("path"), Path);
		return Array(Run(Registry, TEXT("pie_script_read"), Read), TEXT("steps")).Num();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPIEScriptSaveWarningsTest,
	"UE.MCP.PIE.Script.SaveWithWarnings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPIEScriptSaveWarningsTest::RunTest(const FString& /*Parameters*/)
{
	using namespace MCPPIEScriptSaveWarningsTests;

	FMCPHandlerRegistry Registry;
	FPIEHandlers::RegisterHandlers(Registry);
	const FString Name = TEXT("S_SaveWarnings_") + FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8);
	const FString Path = FString(Dir) / Name;

	// A step the check cannot resolve before PIE is saved, with the finding as a warning.
	TSharedPtr<FJsonObject> Create = MakeShared<FJsonObject>();
	Create->SetStringField(TEXT("name"), Name);
	Create->SetStringField(TEXT("package_path"), Dir);
	Create->SetArrayField(TEXT("steps"), { AutoStep(PIEOnlyValue, TEXT("gt")) });
	const TSharedPtr<FJsonObject> Created = Run(Registry, TEXT("pie_script_create"), Create);
	TestTrue(FString::Printf(TEXT("script_create saves despite the finding: %s"), *Field(Created, TEXT("error"))), Succeeded(Created));
	TestTrue(TEXT("script_create reports created"), Created->HasTypedField<EJson::Boolean>(TEXT("created")));
	TestEqual(TEXT("the warning names step 0"), FirstWarning(Created).Key, 0);
	TestEqual(TEXT("the warning names expect.value"), FirstWarning(Created).Value, FString(TEXT("expect.value")));
	TestTrue(TEXT("the asset exists"), UEditorAssetLibrary::DoesAssetExist(Path));

	TSharedPtr<FJsonObject> Read = MakeShared<FJsonObject>();
	Read->SetStringField(TEXT("path"), Path);
	const TArray<TSharedPtr<FJsonValue>> Stored = Array(Run(Registry, TEXT("pie_script_read"), Read), TEXT("steps"));
	if (TestEqual(TEXT("one step stored"), Stored.Num(), 1))
	{
		const TSharedPtr<FJsonObject>* Expect = nullptr;
		Stored[0]->AsObject()->TryGetObjectField(TEXT("expect"), Expect);
		TestEqual(TEXT("the unresolved value is stored as written"), Expect ? Field(*Expect, TEXT("value")) : FString(), FString(PIEOnlyValue));
	}

	// Editing a script that still has the finding saves, and keeps warning.
	TSharedPtr<FJsonObject> Grow = MakeShared<FJsonObject>();
	Grow->SetStringField(TEXT("path"), Path);
	Grow->SetArrayField(TEXT("steps"), { AutoStep(PIEOnlyValue, TEXT("gt")), ManualStep() });
	const TSharedPtr<FJsonObject> Grown = Run(Registry, TEXT("pie_script_update"), Grow);
	TestTrue(FString::Printf(TEXT("script_update saves despite the finding: %s"), *Field(Grown, TEXT("error"))), Succeeded(Grown));
	TestEqual(TEXT("script_update warns about step 0"), FirstWarning(Grown).Key, 0);
	TestEqual(TEXT("two steps stored"), StoredStepCount(Registry, Path), 2);

	// Deleting a step saves too.
	TSharedPtr<FJsonObject> Shrink = MakeShared<FJsonObject>();
	Shrink->SetStringField(TEXT("path"), Path);
	Shrink->SetArrayField(TEXT("steps"), { AutoStep(PIEOnlyValue, TEXT("gt")) });
	TestTrue(TEXT("script_update removing a step saves"), Succeeded(Run(Registry, TEXT("pie_script_update"), Shrink)));
	TestEqual(TEXT("one step stored after the removal"), StoredStepCount(Registry, Path), 1);

	// A step that does not parse cannot be written: refused, nothing stored changes.
	TSharedPtr<FJsonObject> Broken = MakeShared<FJsonObject>();
	Broken->SetStringField(TEXT("path"), Path);
	Broken->SetArrayField(TEXT("steps"), { AutoStep(PIEOnlyValue, TEXT("gt")), AutoStep(TEXT("BaseEyeHeight"), TEXT("sideways")) });
	const TSharedPtr<FJsonObject> Refused = Run(Registry, TEXT("pie_script_update"), Broken);
	TestFalse(TEXT("an unparseable step is refused"), Succeeded(Refused));
	TestEqual(TEXT("refusal code"), Field(Refused, TEXT("errorCode")), FString(TEXT("INVALID_SCRIPT_STEPS")));
	const TArray<TSharedPtr<FJsonValue>> Errors = Array(Refused, TEXT("errors"));
	if (TestEqual(TEXT("one blocking error"), Errors.Num(), 1))
	{
		TestEqual(TEXT("the error names expect.op"), Field(Errors[0]->AsObject(), TEXT("field")), FString(TEXT("expect.op")));
	}
	TestEqual(TEXT("the refusal still lists the warning"), FirstWarning(Refused).Key, 0);
	TestEqual(TEXT("the refused update stored nothing"), StoredStepCount(Registry, Path), 1);

	// Nothing to save at all is refused.
	TSharedPtr<FJsonObject> Empty = MakeShared<FJsonObject>();
	Empty->SetStringField(TEXT("path"), Path);
	Empty->SetArrayField(TEXT("steps"), {});
	TestFalse(TEXT("an empty steps list is refused"), Succeeded(Run(Registry, TEXT("pie_script_update"), Empty)));

	UEditorAssetLibrary::DeleteAsset(Path);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
