#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/MCPInstancedStructPathTestTypes.h"

#include "HandlerRegistry.h"
#include "Handlers/EditorHandlers.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
	TStrongObjectPtr<UUEMCPInstancedStructPathTestObject> MakeFixture()
	{
		const FString FixtureId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
		UPackage* FixturePackage = CreatePackage(*FString::Printf(
			TEXT("/Temp/UEMCPInstancedStructPathTests/%s"),
			*FixtureId));
		FixturePackage->SetDirtyFlag(false);
		TStrongObjectPtr<UUEMCPInstancedStructPathTestObject> Fixture(NewObject<UUEMCPInstancedStructPathTestObject>(
			FixturePackage,
			TEXT("Fixture")));

		Fixture->Payload.InitializeAs<FUEMCPInstancedStructPathPayload>();
		FUEMCPInstancedStructPathPayload& Payload = Fixture->Payload.GetMutable<FUEMCPInstancedStructPathPayload>();
		Payload.Scalar = 11;
		Payload.Sibling = TEXT("payload-sibling");
		Payload.Elements.SetNum(2);
		Payload.Elements[0].Scalar = 21;
		Payload.Elements[0].Sibling = TEXT("first-sibling");
		Payload.Elements[1].Scalar = 31;
		Payload.Elements[1].Sibling = TEXT("second-sibling");

		Fixture->OpStack.SetNum(2);
		for (int32 Index = 0; Index < Fixture->OpStack.Num(); ++Index)
		{
			Fixture->OpStack[Index].InitializeAs<FUEMCPInstancedStructPathPayload>();
			FUEMCPInstancedStructPathPayload& Op = Fixture->OpStack[Index].GetMutable<FUEMCPInstancedStructPathPayload>();
			Op.Scalar = 101 + (Index * 100);
			Op.Sibling = FString::Printf(TEXT("op-%d-sibling"), Index);
			Op.Elements.SetNum(2);
			Op.Elements[0].Scalar = 111 + (Index * 100);
			Op.Elements[0].Sibling = FString::Printf(TEXT("op-%d-first-sibling"), Index);
			Op.Elements[1].Scalar = 121 + (Index * 100);
			Op.Elements[1].Sibling = FString::Printf(TEXT("op-%d-second-sibling"), Index);
		}
		return Fixture;
	}

	TSharedPtr<FJsonObject> MakeSetRequest(
		const UObject& Target,
		const TCHAR* PropertyPath,
		const TSharedPtr<FJsonValue>& Value)
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("objectPath"), Target.GetPathName());
		Request->SetStringField(TEXT("propertyName"), PropertyPath);
		Request->SetField(TEXT("value"), Value);
		return Request;
	}

	TSharedPtr<FJsonObject> ExecuteSet(
		FMCPHandlerRegistry& Registry,
		const UObject& Target,
		const TCHAR* PropertyPath,
		const TSharedPtr<FJsonValue>& Value)
	{
		const TSharedPtr<FJsonValue> Response = Registry.ExecuteHandler(
			TEXT("set_object_property"),
			MakeSetRequest(Target, PropertyPath, Value));
		if (!Response.IsValid() || Response->Type != EJson::Object)
		{
			return nullptr;
		}
		return Response->AsObject();
	}

	TSharedPtr<FJsonObject> ExecuteAssetSet(
		FMCPHandlerRegistry& Registry,
		const UObject& Target,
		const TCHAR* PropertyPath,
		const TSharedPtr<FJsonValue>& Value)
	{
		TSharedPtr<FJsonObject> Request = MakeSetRequest(Target, PropertyPath, Value);
		Request->SetBoolField(TEXT("save"), false);
		const TSharedPtr<FJsonValue> Response = Registry.ExecuteHandler(TEXT("set_property"), Request);
		if (!Response.IsValid() || Response->Type != EJson::Object)
		{
			return nullptr;
		}
		return Response->AsObject();
	}

	TSharedPtr<FJsonObject> ExecuteAssetGet(
		FMCPHandlerRegistry& Registry,
		const UObject& Target,
		const TCHAR* PropertyPath)
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("objectPath"), Target.GetPathName());
		Request->SetStringField(TEXT("propertyName"), PropertyPath);
		const TSharedPtr<FJsonValue> Response = Registry.ExecuteHandler(TEXT("get_property"), Request);
		if (!Response.IsValid() || Response->Type != EJson::Object)
		{
			return nullptr;
		}
		return Response->AsObject();
	}

	bool IsSuccess(const TSharedPtr<FJsonObject>& Response)
	{
		bool bSuccess = false;
		return Response.IsValid() && Response->TryGetBoolField(TEXT("success"), bSuccess) && bSuccess;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPInstancedStructScalarPathTest,
	"UE.MCP.Editor.SetObjectProperty.InstancedStruct.ScalarLeaf",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPInstancedStructScalarPathTest::RunTest(const FString& Parameters)
{
	FMCPHandlerRegistry Registry;
	FEditorHandlers::RegisterHandlers(Registry);
	TestTrue(TEXT("set_object_property is registered"), Registry.HasHandler(TEXT("set_object_property")));

	TStrongObjectPtr<UUEMCPInstancedStructPathTestObject> Fixture = MakeFixture();
	FUEMCPInstancedStructPathPayload& Payload = Fixture->Payload.GetMutable<FUEMCPInstancedStructPathPayload>();
	const bool bPackageWasDirty = Fixture->GetOutermost()->IsDirty();

	const TSharedPtr<FJsonObject> Response = ExecuteSet(
		Registry,
		*Fixture,
		TEXT("Payload.Scalar"),
		MakeShared<FJsonValueNumber>(47));
	TestTrue(TEXT("scalar payload leaf write succeeds"), IsSuccess(Response));
	TestEqual(TEXT("scalar payload leaf is updated"), Payload.Scalar, 47);
	TestEqual(TEXT("payload sibling is unchanged"), Payload.Sibling, FString(TEXT("payload-sibling")));
	if (Response.IsValid())
	{
		FString LeafPropertyName;
		FString CurrentValue;
		FString PreviousValue;
		TestTrue(TEXT("handler reports a scalar leaf field"), Response->TryGetStringField(TEXT("leafPropertyName"), LeafPropertyName));
		TestTrue(TEXT("handler reports a scalar value field"), Response->TryGetStringField(TEXT("value"), CurrentValue));
		TestTrue(TEXT("handler reports a previous value field"), Response->TryGetStringField(TEXT("previousValue"), PreviousValue));
		TestEqual(TEXT("handler reports the scalar leaf"), LeafPropertyName, FString(TEXT("Scalar")));
		TestEqual(TEXT("handler reads back the current value"), CurrentValue, FString(TEXT("47")));
		TestEqual(TEXT("handler reports the previous value"), PreviousValue, FString(TEXT("11")));
	}
	TestEqual(TEXT("live-object write does not dirty the isolated package"), Fixture->GetOutermost()->IsDirty(), bPackageWasDirty);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPInstancedStructArrayPathTest,
	"UE.MCP.Editor.SetObjectProperty.InstancedStruct.NestedArrayLeaf",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPInstancedStructArrayPathTest::RunTest(const FString& Parameters)
{
	FMCPHandlerRegistry Registry;
	FEditorHandlers::RegisterHandlers(Registry);
	TStrongObjectPtr<UUEMCPInstancedStructPathTestObject> Fixture = MakeFixture();
	FUEMCPInstancedStructPathPayload& Payload = Fixture->Payload.GetMutable<FUEMCPInstancedStructPathPayload>();
	const bool bPackageWasDirty = Fixture->GetOutermost()->IsDirty();

	const TSharedPtr<FJsonObject> Response = ExecuteSet(
		Registry,
		*Fixture,
		TEXT("Payload.Elements[1].Scalar"),
		MakeShared<FJsonValueNumber>(99));
	TestTrue(TEXT("nested array payload leaf write succeeds"), IsSuccess(Response));
	TestEqual(TEXT("selected array element is updated"), Payload.Elements[1].Scalar, 99);
	TestEqual(TEXT("other array element is unchanged"), Payload.Elements[0].Scalar, 21);
	TestEqual(TEXT("selected element sibling is unchanged"), Payload.Elements[1].Sibling, FString(TEXT("second-sibling")));
	TestEqual(TEXT("payload sibling is unchanged"), Payload.Sibling, FString(TEXT("payload-sibling")));
	if (Response.IsValid())
	{
		FString ResolvedPropertyName;
		FString CurrentValue;
		TestTrue(TEXT("handler reports an indexed resolved path"), Response->TryGetStringField(TEXT("resolvedPropertyName"), ResolvedPropertyName));
		TestTrue(TEXT("handler reports an indexed leaf value"), Response->TryGetStringField(TEXT("value"), CurrentValue));
		TestEqual(TEXT("indexed path is preserved in response"), ResolvedPropertyName, FString(TEXT("Payload.Elements[1].Scalar")));
		TestEqual(TEXT("indexed leaf readback is current"), CurrentValue, FString(TEXT("99")));
	}
	TestEqual(TEXT("indexed live-object write does not dirty the isolated package"), Fixture->GetOutermost()->IsDirty(), bPackageWasDirty);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPInstancedStructWrapperArrayPathTest,
	"UE.MCP.Editor.Property.InstancedStruct.WrapperArrayPaths",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPInstancedStructWrapperArrayPathTest::RunTest(const FString& Parameters)
{
	FMCPHandlerRegistry Registry;
	FEditorHandlers::RegisterHandlers(Registry);
	TestTrue(TEXT("live setter is registered"), Registry.HasHandler(TEXT("set_object_property")));
	TestTrue(TEXT("asset setter is registered"), Registry.HasHandler(TEXT("set_property")));
	TestTrue(TEXT("asset getter is registered"), Registry.HasHandler(TEXT("get_property")));

	TStrongObjectPtr<UUEMCPInstancedStructPathTestObject> Fixture = MakeFixture();
	UPackage* FixturePackage = Fixture->GetOutermost();
	TestFalse(TEXT("isolated fixture package starts clean"), FixturePackage->IsDirty());
	FUEMCPInstancedStructPathPayload& Op0 = Fixture->OpStack[0].GetMutable<FUEMCPInstancedStructPathPayload>();
	FUEMCPInstancedStructPathPayload& Op1 = Fixture->OpStack[1].GetMutable<FUEMCPInstancedStructPathPayload>();

	const TSharedPtr<FJsonObject> LiveResponse = ExecuteSet(
		Registry,
		*Fixture,
		TEXT("OpStack[0].Scalar"),
		MakeShared<FJsonValueNumber>(151));
	TestTrue(TEXT("live setter reaches OpStack[0] payload scalar"), IsSuccess(LiveResponse));
	TestEqual(TEXT("selected wrapper scalar changes"), Op0.Scalar, 151);
	TestEqual(TEXT("unselected wrapper scalar stays unchanged"), Op1.Scalar, 201);
	TestEqual(TEXT("selected wrapper sibling stays unchanged"), Op0.Sibling, FString(TEXT("op-0-sibling")));
	TestEqual(TEXT("selected wrapper nested data stays unchanged"), Op0.Elements[1].Scalar, 121);
	TestFalse(TEXT("live setter leaves isolated package clean"), FixturePackage->IsDirty());

	const TSharedPtr<FJsonObject> AssetResponse = ExecuteAssetSet(
		Registry,
		*Fixture,
		TEXT("OpStack[1].Elements[1].Scalar"),
		MakeShared<FJsonValueNumber>(299));
	TestTrue(TEXT("asset setter reaches nested OpStack payload leaf"), IsSuccess(AssetResponse));
	if (AssetResponse.IsValid())
	{
		bool bSaved = true;
		FString PropertyName;
		TestTrue(TEXT("asset setter reports saved state"), AssetResponse->TryGetBoolField(TEXT("saved"), bSaved));
		TestFalse(TEXT("asset setter honors save=false"), bSaved);
		TestTrue(TEXT("asset setter reports property path"), AssetResponse->TryGetStringField(TEXT("propertyName"), PropertyName));
		TestEqual(TEXT("asset setter preserves exact property path"), PropertyName, FString(TEXT("OpStack[1].Elements[1].Scalar")));
	}
	TestEqual(TEXT("nested selected leaf changes"), Op1.Elements[1].Scalar, 299);
	TestEqual(TEXT("same wrapper other element stays unchanged"), Op1.Elements[0].Scalar, 211);
	TestEqual(TEXT("nested selected element sibling stays unchanged"), Op1.Elements[1].Sibling, FString(TEXT("op-1-second-sibling")));
	TestEqual(TEXT("same wrapper top-level sibling stays unchanged"), Op1.Sibling, FString(TEXT("op-1-sibling")));
	TestEqual(TEXT("other wrapper retains prior selected write"), Op0.Scalar, 151);
	TestTrue(TEXT("asset setter leaves save=false package dirty in memory"), FixturePackage->IsDirty());

	const TSharedPtr<FJsonObject> ReadResponse = ExecuteAssetGet(
		Registry,
		*Fixture,
		TEXT("OpStack[1].Elements[1].Scalar"));
	TestTrue(TEXT("asset getter reads nested OpStack payload leaf"), IsSuccess(ReadResponse));
	if (ReadResponse.IsValid())
	{
		double ReadValue = 0.0;
		FString LeafPropertyName;
		TestTrue(TEXT("asset getter reports numeric leaf"), ReadResponse->TryGetNumberField(TEXT("value"), ReadValue));
		TestEqual(TEXT("asset getter returns updated leaf"), ReadValue, 299.0);
		TestTrue(TEXT("asset getter reports leaf name"), ReadResponse->TryGetStringField(TEXT("leafPropertyName"), LeafPropertyName));
		TestEqual(TEXT("asset getter resolves scalar leaf"), LeafPropertyName, FString(TEXT("Scalar")));
	}

	FixturePackage->SetDirtyFlag(false);
	TestFalse(TEXT("test clears its isolated package dirtiness"), FixturePackage->IsDirty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPInstancedStructPathRefusalTest,
	"UE.MCP.Editor.SetObjectProperty.InstancedStruct.PathRefusals",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPInstancedStructPathRefusalTest::RunTest(const FString& Parameters)
{
	FMCPHandlerRegistry Registry;
	FEditorHandlers::RegisterHandlers(Registry);
	TStrongObjectPtr<UUEMCPInstancedStructPathTestObject> Fixture = MakeFixture();
	FUEMCPInstancedStructPathPayload& Payload = Fixture->Payload.GetMutable<FUEMCPInstancedStructPathPayload>();
	const bool bPackageWasDirty = Fixture->GetOutermost()->IsDirty();

	const TSharedPtr<FJsonObject> EmptyResponse = ExecuteSet(
		Registry,
		*Fixture,
		TEXT("EmptyPayload.Scalar"),
		MakeShared<FJsonValueNumber>(7));
	TestTrue(TEXT("empty wrapper returns a response"), EmptyResponse.IsValid());
	TestFalse(TEXT("empty wrapper write fails"), IsSuccess(EmptyResponse));
	if (EmptyResponse.IsValid())
	{
		FString Error;
		TestTrue(TEXT("empty wrapper response reports an error"), EmptyResponse->TryGetStringField(TEXT("error"), Error));
		TestTrue(TEXT("empty wrapper failure identifies the wrapper"), Error.Contains(TEXT("EmptyPayload")));
		TestTrue(TEXT("empty wrapper failure is deterministic"), Error.Contains(TEXT("payload is empty - cannot descend")));
	}

	Fixture->OpStack.AddDefaulted();
	const TSharedPtr<FJsonObject> EmptyIndexedResponse = ExecuteSet(
		Registry,
		*Fixture,
		TEXT("OpStack[2].Scalar"),
		MakeShared<FJsonValueNumber>(7));
	TestTrue(TEXT("empty indexed wrapper returns a response"), EmptyIndexedResponse.IsValid());
	TestFalse(TEXT("empty indexed wrapper write fails"), IsSuccess(EmptyIndexedResponse));
	if (EmptyIndexedResponse.IsValid())
	{
		FString Error;
		TestTrue(TEXT("empty indexed wrapper response reports an error"), EmptyIndexedResponse->TryGetStringField(TEXT("error"), Error));
		TestTrue(TEXT("empty indexed wrapper failure preserves the index"), Error.Contains(TEXT("OpStack[2]")));
		TestTrue(TEXT("empty indexed wrapper failure is deterministic"), Error.Contains(TEXT("payload is empty - cannot descend")));
	}

	const TSharedPtr<FJsonObject> UnknownFieldResponse = ExecuteSet(
		Registry,
		*Fixture,
		TEXT("Payload.UnknownField"),
		MakeShared<FJsonValueNumber>(8));
	TestTrue(TEXT("unknown payload field returns a response"), UnknownFieldResponse.IsValid());
	TestFalse(TEXT("unknown payload field fails"), IsSuccess(UnknownFieldResponse));
	if (UnknownFieldResponse.IsValid())
	{
		FString Error;
		TestTrue(TEXT("unknown payload response reports an error"), UnknownFieldResponse->TryGetStringField(TEXT("error"), Error));
		TestTrue(TEXT("unknown payload failure names the field"), Error.Contains(TEXT("UnknownField")));
		TestTrue(TEXT("unknown payload failure names the full path"), Error.Contains(TEXT("Payload.UnknownField")));
	}

	const TSharedPtr<FJsonObject> UnknownNestedResponse = ExecuteSet(
		Registry,
		*Fixture,
		TEXT("Payload.Elements[0].MissingLeaf"),
		MakeShared<FJsonValueNumber>(9));
	TestTrue(TEXT("unknown nested path returns a response"), UnknownNestedResponse.IsValid());
	TestFalse(TEXT("unknown nested path fails"), IsSuccess(UnknownNestedResponse));
	if (UnknownNestedResponse.IsValid())
	{
		FString Error;
		TestTrue(TEXT("unknown nested response reports an error"), UnknownNestedResponse->TryGetStringField(TEXT("error"), Error));
		TestTrue(
			TEXT("unknown nested path failure names the missing leaf"),
			Error.Contains(TEXT("MissingLeaf")));
	}

	TSharedPtr<FJsonObject> ValidFirstElement = MakeShared<FJsonObject>();
	ValidFirstElement->SetNumberField(TEXT("Scalar"), 777);
	TSharedPtr<FJsonObject> InvalidSecondElement = MakeShared<FJsonObject>();
	InvalidSecondElement->SetBoolField(TEXT("UnknownField"), true);
	TArray<TSharedPtr<FJsonValue>> PartiallyInvalidElements;
	PartiallyInvalidElements.Add(MakeShared<FJsonValueObject>(ValidFirstElement));
	PartiallyInvalidElements.Add(MakeShared<FJsonValueObject>(InvalidSecondElement));
	const TSharedPtr<FJsonObject> PartialArrayResponse = ExecuteSet(
		Registry,
		*Fixture,
		TEXT("OpStack[0].Elements"),
		MakeShared<FJsonValueArray>(PartiallyInvalidElements));
	TestTrue(TEXT("partially invalid array write returns a response"), PartialArrayResponse.IsValid());
	TestFalse(TEXT("partially invalid array write fails"), IsSuccess(PartialArrayResponse));
	if (PartialArrayResponse.IsValid())
	{
		FString Error;
		TestTrue(TEXT("partially invalid array response reports an error"), PartialArrayResponse->TryGetStringField(TEXT("error"), Error));
		TestTrue(TEXT("partially invalid array error identifies item one"), Error.Contains(TEXT("[1]")));
		TestTrue(TEXT("partially invalid array error names the bad field"), Error.Contains(TEXT("UnknownField")));
	}

	TestEqual(TEXT("failed writes leave scalar unchanged"), Payload.Scalar, 11);
	TestEqual(TEXT("failed writes leave nested scalar unchanged"), Payload.Elements[0].Scalar, 21);
	TestEqual(TEXT("failed writes leave payload sibling unchanged"), Payload.Sibling, FString(TEXT("payload-sibling")));
	TestEqual(TEXT("failed writes leave nested sibling unchanged"), Payload.Elements[0].Sibling, FString(TEXT("first-sibling")));
	const FUEMCPInstancedStructPathPayload& Op0 = Fixture->OpStack[0].Get<FUEMCPInstancedStructPathPayload>();
	TestEqual(TEXT("failed array conversion restores element count"), Op0.Elements.Num(), 2);
	TestEqual(TEXT("failed array conversion rolls first scalar back"), Op0.Elements[0].Scalar, 111);
	TestEqual(TEXT("failed array conversion preserves first sibling"), Op0.Elements[0].Sibling, FString(TEXT("op-0-first-sibling")));
	TestEqual(TEXT("failed array conversion restores second scalar"), Op0.Elements[1].Scalar, 121);
	TestEqual(TEXT("failed array conversion preserves second sibling"), Op0.Elements[1].Sibling, FString(TEXT("op-0-second-sibling")));
	TestEqual(TEXT("failed writes do not dirty the isolated package"), Fixture->GetOutermost()->IsDirty(), bPackageWasDirty);
	return true;
}

#endif
