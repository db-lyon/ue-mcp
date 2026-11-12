#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "Handlers/Material/MaterialHandlers.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMaterialGraphReadTest,
	"UE.MCP.Material.GraphRead.WiringPagingAndReadOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMaterialGraphReadTest::RunTest(const FString& Parameters)
{
	// Only transient objects: no asset files, registry entries, or editor graph.
	UPackage* Package = CreatePackage(*FString::Printf(TEXT("/Temp/MCPGraph_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	TStrongObjectPtr<UMaterial> Material(NewObject<UMaterial>(Package, TEXT("Material"), RF_Transient));
	auto* Source = NewObject<UMaterialExpressionConstant>(Material.Get(), NAME_None, RF_Transient);
	auto* Target = NewObject<UMaterialExpressionAdd>(Material.Get(), NAME_None, RF_Transient);
	Material->GetExpressionCollection().AddExpression(Source);
	Material->GetExpressionCollection().AddExpression(Target);
	Target->A.Connect(0, Source);
	Material->GetExpressionInputForProperty(MP_BaseColor)->Connect(0, Target);
	Material->GetExpressionInputForProperty(MP_MaterialAttributes)->Connect(0, Target);
	Material->GetExpressionInputForProperty(MP_CustomizedUVs0)->Connect(0, Source);
	Package->SetDirtyFlag(false);
	TestNull(TEXT("test never opened the material editor"), Material->MaterialGraph.Get());

	FMCPHandlerRegistry Registry;
	FMaterialHandlers::RegisterHandlers(Registry);
	auto Call = [&](const TCHAR* Method, const TSharedPtr<FJsonObject>& Request) -> TSharedPtr<FJsonObject>
	{
		const auto Value = Registry.ExecuteHandler(Method, Request);
		if (!Value.IsValid() || Value->Type != EJson::Object)
		{
			AddError(TEXT("material graph handler did not return an object"));
			return nullptr;
		}
		return Value->AsObject();
	};
	auto Request = MakeShared<FJsonObject>();
	Request->SetStringField(TEXT("materialPath"), Material->GetPathName());
	Request->SetNumberField(TEXT("limit"), 1);
	auto First = Call(TEXT("read_material_graph"), Request);
	if (!First.IsValid() || !TestTrue(TEXT("graph read succeeds"), First->GetBoolField(TEXT("success")))) return false;
	const auto& FirstRows = First->GetArrayField(TEXT("expressions"));
	if (!TestEqual(TEXT("first page obeys limit"), FirstRows.Num(), 1)) return false;
	TestEqual(TEXT("first global node index"), FirstRows[0]->AsObject()->GetStringField(TEXT("nodeId")), FString(TEXT("0")));
	TestTrue(TEXT("source without pins has empty inputs"), FirstRows[0]->AsObject()->GetArrayField(TEXT("inputs")).IsEmpty());
	const auto& Roots = First->GetObjectField(TEXT("connections"));
	TestEqual(TEXT("root may reference a node outside this page"), Roots->GetObjectField(TEXT("BaseColor"))->GetIntegerField(TEXT("expressionIndex")), 1);
	TestEqual(TEXT("root names its source class like material(read)"), Roots->GetObjectField(TEXT("BaseColor"))->GetStringField(TEXT("expressionClass")), FString(TEXT("MaterialExpressionAdd")));
	TestFalse(TEXT("material attributes root is ignored while bUseMaterialAttributes is off"), Roots->HasField(TEXT("MaterialAttributes")));
	TestEqual(TEXT("custom UV root is present"), Roots->GetObjectField(TEXT("CustomizedUVs0"))->GetIntegerField(TEXT("expressionIndex")), 0);
	TestTrue(TEXT("disconnected roots are null like material(read)"), Roots->HasTypedField<EJson::Null>(TEXT("Roughness")));
	FString Cursor;
	if (!TestTrue(TEXT("first page has a cursor"), First->TryGetStringField(TEXT("nextCursor"), Cursor) && !Cursor.IsEmpty())) return false;
	Request->SetStringField(TEXT("cursor"), Cursor);
	auto WrongAction = Call(TEXT("list_material_expressions"), Request);
	if (!WrongAction.IsValid()) return false;
	TestFalse(TEXT("graph cursors cannot be used by list"), WrongAction->GetBoolField(TEXT("success")));
	auto Second = Call(TEXT("read_material_graph"), Request);
	if (!Second.IsValid() || !TestTrue(TEXT("next page succeeds"), Second->GetBoolField(TEXT("success")))) return false;
	const auto& SecondRows = Second->GetArrayField(TEXT("expressions"));
	if (!TestEqual(TEXT("second page has one node"), SecondRows.Num(), 1)) return false;
	const auto& Node = SecondRows[0]->AsObject();
	TestEqual(TEXT("second page keeps global index"), Node->GetStringField(TEXT("nodeId")), FString(TEXT("1")));
	const auto& Inputs = Node->GetArrayField(TEXT("inputs"));
	if (!TestEqual(TEXT("both connected and disconnected inputs reported"), Inputs.Num(), 2)) return false;
	TestEqual(TEXT("input index"), Inputs[0]->AsObject()->GetIntegerField(TEXT("inputIndex")), 0);
	TestEqual(TEXT("input name"), Inputs[0]->AsObject()->GetStringField(TEXT("inputName")), FString(TEXT("A")));
	TestEqual(TEXT("source index across page boundary"), Inputs[0]->AsObject()->GetIntegerField(TEXT("connectedExpressionIndex")), 0);
	TestEqual(TEXT("source output index"), Inputs[0]->AsObject()->GetIntegerField(TEXT("connectedOutputIndex")), 0);
	TestEqual(TEXT("source class like material(read)"), Inputs[0]->AsObject()->GetStringField(TEXT("connectedExpressionClass")), FString(TEXT("MaterialExpressionConstant")));
	TestFalse(TEXT("disconnected pin has no source"), Inputs[1]->AsObject()->HasField(TEXT("connectedExpressionIndex")));

	// expressionIndex: one node and its sources, no paging, cursor ignored.
	Request->SetNumberField(TEXT("expressionIndex"), 1);
	auto Single = Call(TEXT("read_material_graph"), Request);
	if (!Single.IsValid() || !TestTrue(TEXT("single node read succeeds"), Single->GetBoolField(TEXT("success")))) return false;
	TestFalse(TEXT("single node read is not paged"), Single->HasField(TEXT("expressions")));
	const auto& SingleNode = Single->GetObjectField(TEXT("expression"));
	TestEqual(TEXT("single node id"), SingleNode->GetStringField(TEXT("nodeId")), FString(TEXT("1")));
	TestEqual(TEXT("single node inputs"), SingleNode->GetArrayField(TEXT("inputs")).Num(), 2);
	const auto& Sources = Single->GetArrayField(TEXT("sources"));
	if (TestEqual(TEXT("one distinct source"), Sources.Num(), 1))
	{
		TestEqual(TEXT("source is the constant"), Sources[0]->AsObject()->GetStringField(TEXT("nodeId")), FString(TEXT("0")));
	}
	Request->SetNumberField(TEXT("expressionIndex"), 2);
	auto OutOfRange = Call(TEXT("read_material_graph"), Request);
	if (!OutOfRange.IsValid()) return false;
	TestFalse(TEXT("out of range index is refused"), OutOfRange->GetBoolField(TEXT("success")));
	Request->RemoveField(TEXT("expressionIndex"));

	Material->bUseMaterialAttributes = true;
	Request->RemoveField(TEXT("cursor"));
	auto WithAttributes = Call(TEXT("read_material_graph"), Request);
	if (!WithAttributes.IsValid()) return false;
	TestEqual(TEXT("material attributes root is reported once enabled"),
		WithAttributes->GetObjectField(TEXT("connections"))->GetObjectField(TEXT("MaterialAttributes"))->GetIntegerField(TEXT("expressionIndex")), 1);
	Material->bUseMaterialAttributes = false;

	Request->SetNumberField(TEXT("limit"), 10);
	auto PlainList = Call(TEXT("list_material_expressions"), Request);
	if (!PlainList.IsValid()) return false;
	TestFalse(TEXT("default list remains metadata only"), PlainList->GetArrayField(TEXT("expressions"))[1]->AsObject()->HasField(TEXT("inputs")));
	Request->SetBoolField(TEXT("includeInputs"), true);
	auto WiredList = Call(TEXT("list_material_expressions"), Request);
	if (!WiredList.IsValid()) return false;
	TestEqual(TEXT("opt-in list wiring"), WiredList->GetArrayField(TEXT("expressions"))[1]->AsObject()->GetArrayField(TEXT("inputs")).Num(), 2);
	TestFalse(TEXT("reads do not dirty the package"), Package->IsDirty());
	TestNull(TEXT("reads do not create a material editor graph"), Material->MaterialGraph.Get());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMaterialGraphReadCustomNodeTest,
	"UE.MCP.Material.GraphRead.CustomNodeCode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMaterialGraphReadCustomNodeTest::RunTest(const FString& Parameters)
{
	// #1645: a graph read carries each Custom node's HLSL and declarations, so
	// parity against a source file needs no per-node follow-up call.
	UPackage* Package = CreatePackage(*FString::Printf(TEXT("/Temp/MCPGraphCustom_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	TStrongObjectPtr<UMaterial> Material(NewObject<UMaterial>(Package, TEXT("Material"), RF_Transient));
	auto* Source = NewObject<UMaterialExpressionConstant>(Material.Get(), NAME_None, RF_Transient);
	auto* Custom = NewObject<UMaterialExpressionCustom>(Material.Get(), NAME_None, RF_Transient);
	Material->GetExpressionCollection().AddExpression(Source);
	Material->GetExpressionCollection().AddExpression(Custom);
	Custom->Code = TEXT("return Density * 2;");
	Custom->Description = TEXT("Doubler");
	Custom->OutputType = CMOT_Float3;
	Custom->Inputs.Empty();
	FCustomInput Input;
	Input.InputName = TEXT("Density");
	Input.Input.Connect(0, Source);
	Custom->Inputs.Add(Input);
	FCustomOutput Extra;
	Extra.OutputName = TEXT("Mask");
	Extra.OutputType = CMOT_Float1;
	Custom->AdditionalOutputs.Add(Extra);
	FCustomDefine Define;
	Define.DefineName = TEXT("USE_FAST");
	Define.DefineValue = TEXT("1");
	Custom->AdditionalDefines.Add(Define);
	Custom->IncludeFilePaths.Add(TEXT("/Project/Shaders/Common.ush"));
	Package->SetDirtyFlag(false);

	FMCPHandlerRegistry Registry;
	FMaterialHandlers::RegisterHandlers(Registry);
	auto Request = MakeShared<FJsonObject>();
	Request->SetStringField(TEXT("materialPath"), Material->GetPathName());
	const auto Value = Registry.ExecuteHandler(TEXT("read_material_graph"), Request);
	if (!TestTrue(TEXT("graph read returns an object"), Value.IsValid() && Value->Type == EJson::Object)) return false;
	const auto Result = Value->AsObject();
	if (!TestTrue(TEXT("graph read succeeds"), Result->GetBoolField(TEXT("success")))) return false;
	const auto& Rows = Result->GetArrayField(TEXT("expressions"));
	if (!TestEqual(TEXT("two nodes"), Rows.Num(), 2)) return false;
	TestFalse(TEXT("a non-Custom node has no custom block"), Rows[0]->AsObject()->HasField(TEXT("custom")));
	const auto CustomRow = Rows[1]->AsObject();
	const TSharedPtr<FJsonObject>* CustomObj = nullptr;
	if (!TestTrue(TEXT("Custom node carries its custom block"), CustomRow->TryGetObjectField(TEXT("custom"), CustomObj) && CustomObj)) return false;
	TestEqual(TEXT("code"), (*CustomObj)->GetStringField(TEXT("code")), FString(TEXT("return Density * 2;")));
	TestEqual(TEXT("description"), (*CustomObj)->GetStringField(TEXT("description")), FString(TEXT("Doubler")));
	TestEqual(TEXT("output type"), (*CustomObj)->GetStringField(TEXT("outputType")), FString(TEXT("float3")));
	const auto& InputNames = (*CustomObj)->GetArrayField(TEXT("inputNames"));
	if (TestEqual(TEXT("one input name"), InputNames.Num(), 1))
	{
		TestEqual(TEXT("input name"), InputNames[0]->AsString(), FString(TEXT("Density")));
	}
	const auto& Outputs = (*CustomObj)->GetArrayField(TEXT("additionalOutputs"));
	if (TestEqual(TEXT("one additional output"), Outputs.Num(), 1))
	{
		TestEqual(TEXT("additional output name"), Outputs[0]->AsObject()->GetStringField(TEXT("name")), FString(TEXT("Mask")));
		TestEqual(TEXT("additional output type"), Outputs[0]->AsObject()->GetStringField(TEXT("outputType")), FString(TEXT("float1")));
	}
	const auto& Defines = (*CustomObj)->GetArrayField(TEXT("additionalDefines"));
	if (TestEqual(TEXT("one define"), Defines.Num(), 1))
	{
		TestEqual(TEXT("define name"), Defines[0]->AsObject()->GetStringField(TEXT("name")), FString(TEXT("USE_FAST")));
		TestEqual(TEXT("define value"), Defines[0]->AsObject()->GetStringField(TEXT("value")), FString(TEXT("1")));
	}
	TestEqual(TEXT("include path"), (*CustomObj)->GetArrayField(TEXT("includeFilePaths")).Num(), 1);
	const auto& Inputs = CustomRow->GetArrayField(TEXT("inputs"));
	if (TestEqual(TEXT("Custom input pin listed"), Inputs.Num(), 1))
	{
		TestEqual(TEXT("Custom input wired to the constant"), Inputs[0]->AsObject()->GetIntegerField(TEXT("connectedExpressionIndex")), 0);
	}
	TestFalse(TEXT("read does not dirty the package"), Package->IsDirty());
	return true;
}

#endif
