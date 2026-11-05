// Coverage for blueprint(compare_class_defaults).
//
// A Blueprint subclass of ACharacter is built under a private mount and given
// two overrides: one on the class default object itself (InitialLifeSpan) and
// one on a native default subobject (CharacterMovement's MaxWalkSpeed), which
// is where a Blueprint edit to an inherited native component lands. The
// assertions are about the contract: exactly those differences are reported
// with both exported values, an untouched component is not reported, the
// filters narrow the rows, a Blueprint-only variable is listed as onlyIn, and
// an unknown class is refused.

#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Handlers/Blueprint/BlueprintHandlers.h"
#include "Tests/MCPScopedTestMount.h"

#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "UObject/Package.h"

namespace
{
	TSharedPtr<FJsonObject> MCPCompareDefaultsRun(FMCPHandlerRegistry& Registry, const TSharedPtr<FJsonObject>& Request)
	{
		const TSharedPtr<FJsonValue> Response = Registry.ExecuteHandler(TEXT("compare_class_defaults"), Request);
		return (Response.IsValid() && Response->Type == EJson::Object) ? Response->AsObject() : nullptr;
	}

	bool MCPCompareDefaultsSucceeded(const TSharedPtr<FJsonObject>& Response)
	{
		bool bSuccess = false;
		return Response.IsValid() && Response->TryGetBoolField(TEXT("success"), bSuccess) && bSuccess;
	}

	TSharedPtr<FJsonObject> MCPCompareDefaultsRow(const TSharedPtr<FJsonObject>& Response, const FString& Name)
	{
		const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
		if (!Response.IsValid() || !Response->TryGetArrayField(TEXT("differences"), Rows) || !Rows) return nullptr;
		for (const TSharedPtr<FJsonValue>& Value : *Rows)
		{
			const TSharedPtr<FJsonObject> Row = Value.IsValid() ? Value->AsObject() : nullptr;
			if (Row.IsValid() && Row->GetStringField(TEXT("name")) == Name) return Row;
		}
		return nullptr;
	}

	bool MCPCompareDefaultsListed(const TSharedPtr<FJsonObject>& Response, const TCHAR* Field, const FString& Name)
	{
		const TArray<TSharedPtr<FJsonValue>>* Names = nullptr;
		if (!Response.IsValid() || !Response->TryGetArrayField(Field, Names) || !Names) return false;
		for (const TSharedPtr<FJsonValue>& Value : *Names)
		{
			if (Value.IsValid() && Value->AsString() == Name) return true;
		}
		return false;
	}

	TSharedPtr<FJsonObject> MCPCompareDefaultsRequest(const FString& ClassName, const FString& OtherClassName)
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("className"), ClassName);
		Request->SetStringField(TEXT("otherClassName"), OtherClassName);
		return Request;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintCompareClassDefaultsTest,
	"UE.MCP.Blueprint.CompareClassDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintCompareClassDefaultsTest::RunTest(const FString& Parameters)
{
	FMCPHandlerRegistry Registry;
	FBlueprintHandlers::RegisterHandlers(Registry);
	if (!TestTrue(TEXT("compare_class_defaults is registered"), Registry.HasHandler(TEXT("compare_class_defaults")))) return false;

	FMCPScopedTestMount Mount(
		TEXT("/UEMCPCompareDefaults_") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/"), TEXT("UEMCPCompareDefaults"));
	const FString PackageName = Mount.RootPath + TEXT("BP_CompareDefaults");
	UPackage* Package = CreatePackage(*PackageName);
	UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
		ACharacter::StaticClass(), Package, FName(TEXT("BP_CompareDefaults")), BPTYPE_Normal,
		UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
	if (!TestNotNull(TEXT("the fixture Blueprint is created"), Blueprint)) return false;

	FEdGraphPinType FloatType;
	FloatType.PinCategory = UEdGraphSchema_K2::PC_Real;
	FloatType.PinSubCategory = UEdGraphSchema_K2::PC_Float;
	FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("UEMCPOnlyInBlueprint"), FloatType);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);

	UClass* Generated = Blueprint->GeneratedClass;
	if (!TestNotNull(TEXT("the fixture compiles to a class"), Generated)) return false;
	ACharacter* Defaults = Cast<ACharacter>(Generated->GetDefaultObject());
	if (!TestNotNull(TEXT("its default object is a Character"), Defaults)) return false;
	Defaults->InitialLifeSpan = 7.5f;
	if (!TestNotNull(TEXT("the default object has its CharacterMovement subobject"), Defaults->GetCharacterMovement())) return false;
	const float NativeWalkSpeed = GetDefault<ACharacter>()->GetCharacterMovement()->MaxWalkSpeed;
	Defaults->GetCharacterMovement()->MaxWalkSpeed = NativeWalkSpeed + 123.0f;

	const FString NativeClass = ACharacter::StaticClass()->GetPathName();

	// The whole comparison, addressed by the Blueprint asset path.
	{
		const TSharedPtr<FJsonObject> Response = MCPCompareDefaultsRun(Registry, MCPCompareDefaultsRequest(NativeClass, PackageName));
		if (!TestTrue(TEXT("native against Blueprint succeeds"), MCPCompareDefaultsSucceeded(Response))) return false;
		TestEqual(TEXT("the relation is reported"), Response->GetStringField(TEXT("relation")), FString(TEXT("otherDerivesFromClass")));
		TestEqual(TEXT("the common ancestor is the native class"), Response->GetStringField(TEXT("commonAncestor")), NativeClass);

		const TSharedPtr<FJsonObject> LifeSpan = MCPCompareDefaultsRow(Response, TEXT("InitialLifeSpan"));
		if (TestNotNull(TEXT("the default-object override is reported"), LifeSpan.Get()))
		{
			TestEqual(TEXT("with the native value"), FCString::Atof(*LifeSpan->GetStringField(TEXT("value"))), 0.0f);
			TestFalse(TEXT("a zero value is written out, not left blank"), LifeSpan->GetStringField(TEXT("value")).IsEmpty());
			TestEqual(TEXT("and the Blueprint value"), FCString::Atof(*LifeSpan->GetStringField(TEXT("otherValue"))), 7.5f);
			TestEqual(TEXT("and where it is declared"), LifeSpan->GetStringField(TEXT("declaredIn")), FString(TEXT("Actor")));
		}

		const TSharedPtr<FJsonObject> WalkSpeed = MCPCompareDefaultsRow(Response, TEXT("CharacterMovement.MaxWalkSpeed"));
		if (TestNotNull(TEXT("the override on a native component is reported field by field"), WalkSpeed.Get()))
		{
			TestEqual(TEXT("with the Blueprint value"),
				FCString::Atof(*WalkSpeed->GetStringField(TEXT("otherValue"))), NativeWalkSpeed + 123.0f);
		}
		TestNull(TEXT("the component itself is not reported as a changed reference"),
			MCPCompareDefaultsRow(Response, TEXT("CharacterMovement")).Get());
		TestNull(TEXT("an untouched component reports nothing"),
			MCPCompareDefaultsRow(Response, TEXT("CapsuleComponent.CapsuleHalfHeight")).Get());
		TestTrue(TEXT("a Blueprint-only variable is listed as onlyInOtherClass"),
			MCPCompareDefaultsListed(Response, TEXT("onlyInOtherClass"), TEXT("UEMCPOnlyInBlueprint")));
	}

	// Filters: a property name keeps one row; a category keeps its rows.
	{
		TSharedPtr<FJsonObject> Request = MCPCompareDefaultsRequest(PackageName, NativeClass);
		TArray<TSharedPtr<FJsonValue>> Names;
		Names.Add(MakeShared<FJsonValueString>(TEXT("initiallifespan")));
		Request->SetArrayField(TEXT("propertyNames"), Names);
		const TSharedPtr<FJsonObject> Response = MCPCompareDefaultsRun(Registry, Request);
		if (TestTrue(TEXT("a filtered comparison succeeds"), MCPCompareDefaultsSucceeded(Response)))
		{
			TestEqual(TEXT("in the other direction"), Response->GetStringField(TEXT("relation")), FString(TEXT("classDerivesFromOther")));
			TestEqual(TEXT("propertyNames keeps exactly the named property"), static_cast<int32>(Response->GetNumberField(TEXT("differenceCount"))), 1);
			const TSharedPtr<FJsonObject> LifeSpan = MCPCompareDefaultsRow(Response, TEXT("InitialLifeSpan"));
			if (TestNotNull(TEXT("and it is the one asked for"), LifeSpan.Get()))
			{
				TestEqual(TEXT("value is the first class's"), FCString::Atof(*LifeSpan->GetStringField(TEXT("value"))), 7.5f);
			}
		}
	}
	{
		TSharedPtr<FJsonObject> Request = MCPCompareDefaultsRequest(NativeClass, Generated->GetPathName());
		Request->SetStringField(TEXT("propertyFilter"), TEXT("walkspeed"));
		const TSharedPtr<FJsonObject> Response = MCPCompareDefaultsRun(Registry, Request);
		if (TestTrue(TEXT("a generated class path resolves"), MCPCompareDefaultsSucceeded(Response)))
		{
			TestNotNull(TEXT("propertyFilter matches the nested path"), MCPCompareDefaultsRow(Response, TEXT("CharacterMovement.MaxWalkSpeed")).Get());
			TestNull(TEXT("and drops the rest"), MCPCompareDefaultsRow(Response, TEXT("InitialLifeSpan")).Get());
		}
	}
	{
		TSharedPtr<FJsonObject> Request = MCPCompareDefaultsRequest(NativeClass, PackageName);
		Request->SetNumberField(TEXT("subobjectDepth"), 0);
		const TSharedPtr<FJsonObject> Response = MCPCompareDefaultsRun(Registry, Request);
		if (TestTrue(TEXT("subobjectDepth 0 succeeds"), MCPCompareDefaultsSucceeded(Response)))
		{
			TestNotNull(TEXT("and compares the component whole"), MCPCompareDefaultsRow(Response, TEXT("CharacterMovement")).Get());
		}
	}

	// A class compared with itself has no differences.
	{
		const TSharedPtr<FJsonObject> Response = MCPCompareDefaultsRun(Registry, MCPCompareDefaultsRequest(NativeClass, TEXT("Character")));
		if (TestTrue(TEXT("a class against itself succeeds"), MCPCompareDefaultsSucceeded(Response)))
		{
			TestEqual(TEXT("and reports no differences"), static_cast<int32>(Response->GetNumberField(TEXT("differenceCount"))), 0);
		}
	}

	TestFalse(TEXT("an unknown class is refused"),
		MCPCompareDefaultsSucceeded(MCPCompareDefaultsRun(Registry, MCPCompareDefaultsRequest(NativeClass, TEXT("UEMCP_NoSuchClass_Anywhere")))));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
