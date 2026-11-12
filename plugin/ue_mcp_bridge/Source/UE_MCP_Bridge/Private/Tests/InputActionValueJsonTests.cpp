// FInputActionValue written from JSON (#1449).
//
// The struct keeps its value in an unreflected member, so a function argument of that type used to be
// refused as "has no field". Every call path that fills arguments from JSON (invoke_function,
// invoke_object_function) goes through SetJsonOnProperty, which now builds the value from the JSON shape.
// The fixture is the RawValue parameter of an Enhanced Input UFUNCTION, found by type.

#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerJsonProperty.h"
#include "InputActionValue.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"
#include "UObject/UObjectIterator.h"

namespace
{
	FStructProperty* FindInputActionValueParameter()
	{
		for (TObjectIterator<UFunction> It; It; ++It)
		{
			for (TFieldIterator<FStructProperty> PropIt(*It); PropIt; ++PropIt)
			{
				if (PropIt->Struct == FInputActionValue::StaticStruct() && PropIt->HasAnyPropertyFlags(CPF_Parm)
					&& !PropIt->HasAnyPropertyFlags(CPF_ReturnParm | CPF_OutParm))
				{
					return *PropIt;
				}
			}
		}
		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPInputActionValueJsonTest,
	"UE.MCP.Property.InputActionValue.WritesFromJson",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPInputActionValueJsonTest::RunTest(const FString& Parameters)
{
	FStructProperty* Prop = FindInputActionValueParameter();
	if (!TestNotNull(TEXT("an Enhanced Input function takes an FInputActionValue argument"), Prop)) return false;

	FInputActionValue Value;
	auto Write = [&](const TSharedPtr<FJsonValue>& Json, FString& Error)
	{
		Value = FInputActionValue();
		return MCPJsonProperty::SetJsonOnProperty(Prop, &Value, Json, Error);
	};

	{
		FString Error;
		TestTrue(FString::Printf(TEXT("true writes (%s)"), *Error), Write(MakeShared<FJsonValueBoolean>(true), Error));
		TestTrue(TEXT("true is a Boolean value"), Value.GetValueType() == EInputActionValueType::Boolean);
		TestTrue(TEXT("true is pressed"), Value.Get<bool>());
	}
	{
		FString Error;
		TestTrue(FString::Printf(TEXT("a number writes (%s)"), *Error), Write(MakeShared<FJsonValueNumber>(0.5), Error));
		TestTrue(TEXT("a number is Axis1D"), Value.GetValueType() == EInputActionValueType::Axis1D);
		TestEqual(TEXT("a number keeps its value"), Value.Get<FVector>().X, 0.5);
	}
	{
		FString Error;
		TArray<TSharedPtr<FJsonValue>> Pair = { MakeShared<FJsonValueNumber>(0.0), MakeShared<FJsonValueNumber>(1.0) };
		TestTrue(FString::Printf(TEXT("[x,y] writes (%s)"), *Error), Write(MakeShared<FJsonValueArray>(Pair), Error));
		TestTrue(TEXT("[x,y] is Axis2D"), Value.GetValueType() == EInputActionValueType::Axis2D);
		TestEqual(TEXT("[x,y] keeps y"), Value.Get<FVector>().Y, 1.0);
	}
	{
		FString Error;
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetNumberField(TEXT("x"), 1.0);
		Obj->SetNumberField(TEXT("y"), -1.0);
		Obj->SetNumberField(TEXT("z"), 0.25);
		TestTrue(FString::Printf(TEXT("{x,y,z} writes (%s)"), *Error), Write(MakeShared<FJsonValueObject>(Obj), Error));
		TestTrue(TEXT("{x,y,z} is Axis3D"), Value.GetValueType() == EInputActionValueType::Axis3D);
		TestEqual(TEXT("{x,y,z} keeps z"), Value.Get<FVector>().Z, 0.25);
	}
	{
		FString Error;
		TestTrue(TEXT("a JSON string of an object writes"), Write(MakeShared<FJsonValueString>(TEXT("{\"X\": 0.5, \"Y\": 0.5}")), Error));
		TestTrue(TEXT("an upper-case {X,Y} is Axis2D"), Value.GetValueType() == EInputActionValueType::Axis2D);
	}
	{
		FString Error;
		TSharedPtr<FJsonObject> Empty = MakeShared<FJsonObject>();
		Empty->SetStringField(TEXT("Value"), TEXT("1"));
		TestFalse(TEXT("an object with no axis is refused"), Write(MakeShared<FJsonValueObject>(Empty), Error));
		TestTrue(TEXT("the refusal names the accepted shapes"), Error.Contains(TEXT("FInputActionValue takes")));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
