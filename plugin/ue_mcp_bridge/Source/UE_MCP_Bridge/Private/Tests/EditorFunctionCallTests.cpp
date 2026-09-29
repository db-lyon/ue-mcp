#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerFunctionCall.h"
#include "HandlerRegistry.h"
#include "Handlers/Editor/EditorHandlers.h"

#include "Engine/BrushBuilder.h"
#include "Kismet/KismetStringLibrary.h"
#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"

/**
 * #885: a UFUNCTION returning a container has to arrive as a container.
 *
 * Export text renders a TArray<FString> return as an empty string, so an
 * array-returning accessor could not be read through the bridge at all. These
 * assert the marshalling directly rather than through a socket, because the
 * defect is in what the frame is turned into, not in how it is transported.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEditorFunctionCallContainerReturnTest,
	"UE.MCP.Editor.FunctionCall.ContainerReturnMarshalling",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEditorFunctionCallContainerReturnTest::RunTest(const FString& Parameters)
{
	// A real static UFUNCTION returning TArray<FString>, called the way
	// invoke_static_function calls one: a frame, ProcessEvent on the CDO, then
	// the outputs read back out of the frame.
	UClass* LibClass = UKismetStringLibrary::StaticClass();
	UFunction* Func = LibClass->FindFunctionByName(FName(TEXT("ParseIntoArray")));
	if (!TestNotNull(TEXT("UKismetStringLibrary::ParseIntoArray is reflected"), Func))
	{
		return false;
	}

	TArray<uint8> Frame;
	Frame.SetNumZeroed(Func->ParmsSize);
	for (TFieldIterator<FProperty> It(Func); It && (It->PropertyFlags & CPF_Parm); ++It)
	{
		It->InitializeValue_InContainer(Frame.GetData());
	}

	if (FStrProperty* SourceProp = CastField<FStrProperty>(Func->FindPropertyByName(FName(TEXT("SourceString")))))
	{
		SourceProp->SetPropertyValue_InContainer(Frame.GetData(), TEXT("alpha,beta,gamma"));
	}
	if (FStrProperty* DelimProp = CastField<FStrProperty>(Func->FindPropertyByName(FName(TEXT("Delimiter")))))
	{
		DelimProp->SetPropertyValue_InContainer(Frame.GetData(), TEXT(","));
	}
	if (FBoolProperty* CullProp = CastField<FBoolProperty>(Func->FindPropertyByName(FName(TEXT("CullEmptyStrings")))))
	{
		CullProp->SetPropertyValue_InContainer(Frame.GetData(), true);
	}

	UObject* CDO = LibClass->GetDefaultObject();
	CDO->ProcessEvent(Func, Frame.GetData());

	TSharedPtr<FJsonObject> OutVals = MakeShared<FJsonObject>();
	MCPFunctionCall::WriteOutputs(OutVals, Func, Frame.GetData(), CDO);
	MCPFunctionCall::DestroyFrame(Func, Frame.GetData());

	const TSharedPtr<FJsonValue> Returned = OutVals->TryGetField(TEXT("ReturnValue"));
	if (!TestTrue(TEXT("the return value is present"), Returned.IsValid()))
	{
		return false;
	}
	// The whole bug: this used to be EJson::String, and empty at that.
	if (!TestEqual(TEXT("a TArray<FString> return is a JSON array"), (int32)Returned->Type, (int32)EJson::Array))
	{
		return false;
	}
	const TArray<TSharedPtr<FJsonValue>>& Items = Returned->AsArray();
	TestEqual(TEXT("every element survived"), Items.Num(), 3);
	if (Items.Num() == 3)
	{
		TestEqual(TEXT("element 0"), Items[0]->AsString(), FString(TEXT("alpha")));
		TestEqual(TEXT("element 1"), Items[1]->AsString(), FString(TEXT("beta")));
		TestEqual(TEXT("element 2"), Items[2]->AsString(), FString(TEXT("gamma")));
	}

	return true;
}

/**
 * The other half of #885: nothing but containers moved. Every existing reader
 * of returnValues parses export text, so a scalar or a struct has to keep
 * arriving as a JSON string.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEditorFunctionCallScalarReturnTest,
	"UE.MCP.Editor.FunctionCall.ScalarReturnStaysExportText",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEditorFunctionCallScalarReturnTest::RunTest(const FString& Parameters)
{
	FBuilderPoly Poly;
	Poly.Direction = 7;
	Poly.ItemName = TEXT("Face");

	UScriptStruct* Struct = FBuilderPoly::StaticStruct();
	FProperty* DirectionProp = Struct->FindPropertyByName(FName(TEXT("Direction")));
	FProperty* NameProp = Struct->FindPropertyByName(FName(TEXT("ItemName")));
	if (!TestNotNull(TEXT("FBuilderPoly::Direction is reflected"), DirectionProp)) return false;
	if (!TestNotNull(TEXT("FBuilderPoly::ItemName is reflected"), NameProp)) return false;

	const TSharedPtr<FJsonValue> DirectionJson =
		MCPFunctionCall::OutputToJson(DirectionProp, DirectionProp->ContainerPtrToValuePtr<void>(&Poly), nullptr);
	TestEqual(TEXT("an int return stays a JSON string"), (int32)DirectionJson->Type, (int32)EJson::String);
	TestEqual(TEXT("an int return keeps its export text"), DirectionJson->AsString(), FString(TEXT("7")));

	const TSharedPtr<FJsonValue> NameJson =
		MCPFunctionCall::OutputToJson(NameProp, NameProp->ContainerPtrToValuePtr<void>(&Poly), nullptr);
	TestEqual(TEXT("an FName return stays a JSON string"), (int32)NameJson->Type, (int32)EJson::String);
	TestEqual(TEXT("an FName return keeps its value"), NameJson->AsString(), FString(TEXT("Face")));

	return true;
}

/** #885: container elements are typed, so a TArray<int32> reads as numbers. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEditorFunctionCallNumericArrayTest,
	"UE.MCP.Editor.FunctionCall.NumericArrayElementsAreNumbers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEditorFunctionCallNumericArrayTest::RunTest(const FString& Parameters)
{
	FBuilderPoly Poly;
	Poly.VertexIndices = { 3, 5, 8 };

	FArrayProperty* ArrayProp =
		CastField<FArrayProperty>(FBuilderPoly::StaticStruct()->FindPropertyByName(FName(TEXT("VertexIndices"))));
	if (!TestNotNull(TEXT("FBuilderPoly::VertexIndices is a reflected TArray<int32>"), ArrayProp)) return false;

	const TSharedPtr<FJsonValue> Json =
		MCPFunctionCall::OutputToJson(ArrayProp, ArrayProp->ContainerPtrToValuePtr<void>(&Poly), nullptr);
	if (!TestEqual(TEXT("a TArray<int32> is a JSON array"), (int32)Json->Type, (int32)EJson::Array)) return false;

	const TArray<TSharedPtr<FJsonValue>>& Items = Json->AsArray();
	TestEqual(TEXT("every element survived"), Items.Num(), 3);
	if (Items.Num() == 3)
	{
		TestEqual(TEXT("elements are numbers, not strings"), (int32)Items[0]->Type, (int32)EJson::Number);
		TestEqual(TEXT("element 0"), (int32)Items[0]->AsNumber(), 3);
		TestEqual(TEXT("element 1"), (int32)Items[1]->AsNumber(), 5);
		TestEqual(TEXT("element 2"), (int32)Items[2]->AsNumber(), 8);
	}

	// An empty container is an empty array, not the empty string the defect
	// reported: those two read very differently to a caller.
	FBuilderPoly Empty;
	const TSharedPtr<FJsonValue> EmptyJson =
		MCPFunctionCall::OutputToJson(ArrayProp, ArrayProp->ContainerPtrToValuePtr<void>(&Empty), nullptr);
	TestEqual(TEXT("an empty TArray is still a JSON array"), (int32)EmptyJson->Type, (int32)EJson::Array);
	TestEqual(TEXT("an empty TArray has no elements"), EmptyJson->AsArray().Num(), 0);

	return true;
}

/**
 * #969: a path may carry literal arguments, e.g. GetTallyWeight(overclock).
 * The parser has to keep a dot inside an argument list out of the path split,
 * or a float literal would silently become two path segments.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEditorRuntimeValuePathParseTest,
	"UE.MCP.Editor.FunctionCall.RuntimeValuePathParsing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEditorRuntimeValuePathParseTest::RunTest(const FString& Parameters)
{
	TArray<FString> Segments;

	MCPFunctionCall::SplitPathSegments(TEXT("PowerConnector.GetRequired"), Segments);
	TestEqual(TEXT("a plain dotted path splits as before"), Segments.Num(), 2);

	MCPFunctionCall::SplitPathSegments(TEXT("Tally.GetMirroredTallyWeight(overclock)"), Segments);
	TestEqual(TEXT("an argument list does not add a segment"), Segments.Num(), 2);
	if (Segments.Num() == 2)
	{
		TestEqual(TEXT("the call segment stays whole"), Segments[1], FString(TEXT("GetMirroredTallyWeight(overclock)")));
	}

	MCPFunctionCall::SplitPathSegments(TEXT("GetWeightAt(1.5)"), Segments);
	TestEqual(TEXT("a float literal's dot is not a path separator"), Segments.Num(), 1);

	FString Name;
	TArray<FString> Args;
	bool bHasArgList = false;
	FString Error;

	TestTrue(TEXT("a bare name parses"), MCPFunctionCall::ParseCallSegment(TEXT("GetRequired"), Name, Args, bHasArgList, Error));
	TestEqual(TEXT("a bare name keeps its name"), Name, FString(TEXT("GetRequired")));
	TestFalse(TEXT("a bare name has no argument list"), bHasArgList);
	TestEqual(TEXT("a bare name has no arguments"), Args.Num(), 0);

	TestTrue(TEXT("an empty list parses"), MCPFunctionCall::ParseCallSegment(TEXT("GetRequired()"), Name, Args, bHasArgList, Error));
	TestTrue(TEXT("an empty list is still an argument list"), bHasArgList);
	TestEqual(TEXT("an empty list has no arguments"), Args.Num(), 0);

	TestTrue(TEXT("a keyed accessor parses"),
		MCPFunctionCall::ParseCallSegment(TEXT("GetMirroredTallyWeight(overclock)"), Name, Args, bHasArgList, Error));
	TestEqual(TEXT("the function name is separated"), Name, FString(TEXT("GetMirroredTallyWeight")));
	TestEqual(TEXT("one argument was read"), Args.Num(), 1);
	if (Args.Num() == 1) TestEqual(TEXT("the literal is unquoted"), Args[0], FString(TEXT("overclock")));

	TestTrue(TEXT("multiple arguments parse"),
		MCPFunctionCall::ParseCallSegment(TEXT("GetAt(\"a, b\", 2, true)"), Name, Args, bHasArgList, Error));
	TestEqual(TEXT("a comma inside quotes does not split"), Args.Num(), 3);
	if (Args.Num() == 3)
	{
		TestEqual(TEXT("the quoted literal keeps its comma"), Args[0], FString(TEXT("a, b")));
		TestEqual(TEXT("the numeric literal"), Args[1], FString(TEXT("2")));
		TestEqual(TEXT("the bool literal"), Args[2], FString(TEXT("true")));
	}

	TestFalse(TEXT("an unbalanced list is refused"),
		MCPFunctionCall::ParseCallSegment(TEXT("GetAt(overclock"), Name, Args, bHasArgList, Error));
	TestTrue(TEXT("an unbalanced list explains itself"), !Error.IsEmpty());

	return true;
}

/**
 * A project function library with FString& out-params (a planner that returns
 * its manifest and error by reference) is reachable through
 * invoke_static_function, and an agent fell back to Python for want of knowing
 * it. UKismetStringLibrary::Split has the same shape: const FString& inputs
 * passed by name, two FString& outputs, a bool return. The source is large so
 * the output is shown to arrive whole.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEditorInvokeStaticOutParamsTest,
	"UE.MCP.Editor.FunctionCall.StaticLibraryOutParamsReturnWhole",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEditorInvokeStaticOutParamsTest::RunTest(const FString& Parameters)
{
	FMCPHandlerRegistry Registry;
	FEditorHandlers::RegisterHandlers(Registry);

	const FString Right = FString::ChrN(200000, TEXT('x'));
	TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
	Args->SetStringField(TEXT("SourceString"), TEXT("zone=") + Right);
	Args->SetStringField(TEXT("InStr"), TEXT("="));

	// The U prefix is optional, as it is for a project class.
	for (const TCHAR* ClassName : { TEXT("KismetStringLibrary"), TEXT("UKismetStringLibrary") })
	{
		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("className"), ClassName);
		Params->SetStringField(TEXT("functionName"), TEXT("Split"));
		Params->SetObjectField(TEXT("args"), Args);

		const TSharedPtr<FJsonValue> Response = Registry.ExecuteHandler(TEXT("invoke_static_function"), Params);
		const TSharedPtr<FJsonObject> Result = Response.IsValid() && Response->Type == EJson::Object ? Response->AsObject() : nullptr;
		if (!TestTrue(FString::Printf(TEXT("%s resolves and the call answers"), ClassName), Result.IsValid() && Result->GetBoolField(TEXT("success"))))
		{
			continue;
		}
		const TSharedPtr<FJsonObject>* Outputs = nullptr;
		if (!TestTrue(TEXT("returnValues is present"), Result->TryGetObjectField(TEXT("returnValues"), Outputs))) continue;
		FString Left, RightOut, Returned;
		(*Outputs)->TryGetStringField(TEXT("LeftS"), Left);
		(*Outputs)->TryGetStringField(TEXT("RightS"), RightOut);
		(*Outputs)->TryGetStringField(TEXT("ReturnValue"), Returned);
		TestEqual(TEXT("the first out-param is captured"), Left, FString(TEXT("zone")));
		TestEqual(TEXT("the large out-param arrives whole"), RightOut.Len(), Right.Len());
		TestTrue(TEXT("the bool return is captured"), Returned.ToBool());
	}
	return true;
}

#endif
