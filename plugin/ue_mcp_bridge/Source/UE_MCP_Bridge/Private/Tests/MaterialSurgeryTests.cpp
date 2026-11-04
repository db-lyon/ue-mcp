#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "Handlers/Material/MaterialHandlers.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionMakeMaterialAttributes.h"
#include "Materials/MaterialExpressionSetMaterialAttributes.h"
#include "MaterialAttributeDefinitionMap.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMaterialSurgeryTest,
	"UE.MCP.Material.Insertion.WiringRollbackAndAttributes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMaterialSurgeryTest::RunTest(const FString& Parameters)
{
	UPackage* Package = CreatePackage(*FString::Printf(TEXT("/Game/MCPInsertion_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	TStrongObjectPtr<UMaterial> Material(NewObject<UMaterial>(Package, TEXT("Material"), RF_Transient));
	auto* Source = NewObject<UMaterialExpressionVectorParameter>(Material.Get(), NAME_None, RF_Transient);
	auto* Target = NewObject<UMaterialExpressionAdd>(Material.Get(), NAME_None, RF_Transient);
	auto* Make = NewObject<UMaterialExpressionMakeMaterialAttributes>(Material.Get(), NAME_None, RF_Transient);
	auto* Set = NewObject<UMaterialExpressionSetMaterialAttributes>(Material.Get(), NAME_None, RF_Transient);
	Material->GetExpressionCollection().AddExpression(Source);
	Material->GetExpressionCollection().AddExpression(Target);
	Material->GetExpressionCollection().AddExpression(Make);
	Material->GetExpressionCollection().AddExpression(Set);
	Target->A.Connect(2, Source);
	Make->BaseColor.Connect(2, Source);
	Set->ConnectInputAttribute(MP_BaseColor, Source, 2);
	Material->GetExpressionInputForProperty(MP_BaseColor)->Connect(2, Source);
	Material->GetExpressionInputForProperty(MP_MaterialAttributes)->Connect(0, Make);
	FMCPHandlerRegistry Registry;
	FMaterialHandlers::RegisterHandlers(Registry);
	auto Request = MakeShared<FJsonObject>();
	Request->SetStringField(TEXT("materialPath"), Material->GetPathName());
	Request->SetStringField(TEXT("targetExpression"), Target->GetName());
	Request->SetStringField(TEXT("targetInput"), TEXT("A"));
	Request->SetStringField(TEXT("expressionType"), TEXT("Multiply"));
	Request->SetStringField(TEXT("inputForUpstream"), TEXT("A"));
	auto Properties = MakeShared<FJsonObject>();
	Properties->SetNumberField(TEXT("ConstB"), 0.5);
	Request->SetObjectField(TEXT("properties"), Properties);
	auto Inserted = Registry.ExecuteHandler(TEXT("insert_expression_between"), Request)->AsObject();
	if (!TestTrue(TEXT("insert succeeds"), Inserted->GetBoolField(TEXT("success")))) return false;
	auto* NewNode = Cast<UMaterialExpressionMultiply>(Target->A.Expression);
	if (!TestNotNull(TEXT("new node feeds target"), NewNode)) return false;
	TestTrue(TEXT("old source feeds new input"), NewNode->A.Expression == Source);
	TestEqual(TEXT("source output preserved"), NewNode->A.OutputIndex, 2);
	TestEqual(TEXT("source channel mask preserved"), NewNode->A.MaskG, 1);
	TestEqual(TEXT("properties applied"), NewNode->ConstB, 0.5f);
	TestEqual(TEXT("returned previous output"), Inserted->GetObjectField(TEXT("previousSource"))->GetIntegerField(TEXT("outputIndex")), 2);
	auto Repeated = Registry.ExecuteHandler(TEXT("insert_expression_between"), Request)->AsObject();
	TestTrue(TEXT("retry does not insert twice"), Repeated->GetBoolField(TEXT("existed")));
	auto Rollback = Inserted->GetObjectField(TEXT("rollback"));
	Target->A.Connect(0, Source);
	auto Conflict = Registry.ExecuteHandler(TEXT("restore_expression_insertion"), Rollback->GetObjectField(TEXT("payload")))->AsObject();
	TestFalse(TEXT("rollback refuses a changed wire"), Conflict->GetBoolField(TEXT("success")));
	TestTrue(TEXT("refused rollback keeps the inserted node"), Material->GetExpressions().Contains(NewNode));
	Target->A.Connect(0, NewNode);
	auto Restored = Registry.ExecuteHandler(TEXT("restore_expression_insertion"), Rollback->GetObjectField(TEXT("payload")))->AsObject();
	TestTrue(TEXT("rollback succeeds"), Restored->GetBoolField(TEXT("success")));
	TestTrue(TEXT("rollback restores source"), Target->A.Expression == Source);
	TestEqual(TEXT("rollback restores output"), Target->A.OutputIndex, 2);
	TestEqual(TEXT("rollback restores channel mask"), Target->A.MaskG, 1);
	TestFalse(TEXT("rollback removes inserted node"), Material->GetExpressions().Contains(NewNode));
	Request->SetStringField(TEXT("targetExpression"), Make->GetName());
	Request->SetStringField(TEXT("targetInput"), TEXT("Base Color"));
	Material->bUseMaterialAttributes = true;
	auto Attributes = Registry.ExecuteHandler(TEXT("insert_expression_between"), Request)->AsObject();
	TestTrue(TEXT("named MakeMaterialAttributes input supported"), Attributes->GetBoolField(TEXT("success")));
	TestTrue(TEXT("attributes wire replaced"), Make->BaseColor.Expression != Source);
	Request->SetStringField(TEXT("targetExpression"), Set->GetName());
	auto SetAttributes = Registry.ExecuteHandler(TEXT("insert_expression_between"), Request)->AsObject();
	TestTrue(TEXT("named SetMaterialAttributes input supported"), SetAttributes->GetBoolField(TEXT("success")));
	Request->RemoveField(TEXT("targetExpression"));
	Request->RemoveField(TEXT("targetInput"));
	Request->SetStringField(TEXT("property"), TEXT("BaseColor"));
	Material->bUseMaterialAttributes = false;
	auto Property = Registry.ExecuteHandler(TEXT("insert_expression_between"), Request)->AsObject();
	TestTrue(TEXT("material property insertion supported"), Property->GetBoolField(TEXT("success")));
	Request->RemoveField(TEXT("property"));
	Request->SetStringField(TEXT("targetExpression"), Target->GetName());
	Request->SetStringField(TEXT("targetInput"), TEXT("DoesNotExist"));
	const int32 Before = Material->GetExpressions().Num();
	auto Invalid = Registry.ExecuteHandler(TEXT("insert_expression_between"), Request)->AsObject();
	TestFalse(TEXT("unknown target input refused"), Invalid->GetBoolField(TEXT("success")));
	TestEqual(TEXT("failed preflight leaves graph intact"), Material->GetExpressions().Num(), Before);
	Request->SetStringField(TEXT("targetInput"), TEXT("B"));
	auto EmptyInsertion = Registry.ExecuteHandler(TEXT("insert_expression_between"), Request)->AsObject();
	if (!TestTrue(TEXT("unconnected input can be wrapped"), EmptyInsertion->GetBoolField(TEXT("success")))) return false;
	auto* EmptyNode = Cast<UMaterialExpressionMultiply>(Target->B.Expression);
	if (!TestNotNull(TEXT("empty input feeds the new node"), EmptyNode)) return false;
	TestNull(TEXT("upstream remains unconnected"), EmptyNode->A.Expression);
	auto EmptyRestore = Registry.ExecuteHandler(TEXT("restore_expression_insertion"), EmptyInsertion->GetObjectField(TEXT("rollback"))->GetObjectField(TEXT("payload")))->AsObject();
	TestTrue(TEXT("unconnected input rollback succeeds"), EmptyRestore->GetBoolField(TEXT("success")));
	TestNull(TEXT("unconnected input restored"), Target->B.Expression);
	Package->SetDirtyFlag(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMaterialSummaryTest,
	"UE.MCP.Material.Summary.FiltersDepthAndAttributes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMaterialSummaryTest::RunTest(const FString& Parameters)
{
	UPackage* Package = CreatePackage(*FString::Printf(TEXT("/Temp/MCPSummary_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	TStrongObjectPtr<UMaterial> Material(NewObject<UMaterial>(Package, TEXT("Material"), RF_Transient));
	auto* Parameter = NewObject<UMaterialExpressionVectorParameter>(Material.Get(), NAME_None, RF_Transient);
	auto* Multiply = NewObject<UMaterialExpressionMultiply>(Material.Get(), NAME_None, RF_Transient);
	auto* Make = NewObject<UMaterialExpressionMakeMaterialAttributes>(Material.Get(), NAME_None, RF_Transient);
	Parameter->ParameterName = TEXT("Tint");
	Parameter->Group = TEXT("Surface");
	Multiply->A.Connect(2, Parameter);
	Make->BaseColor.Connect(0, Multiply);
	Material->GetExpressionCollection().AddExpression(Parameter);
	Material->GetExpressionCollection().AddExpression(Multiply);
	Material->GetExpressionCollection().AddExpression(Make);
	Material->bUseMaterialAttributes = true;
	Material->GetExpressionInputForProperty(MP_MaterialAttributes)->Connect(0, Make);
	Package->SetDirtyFlag(false);
	FMCPHandlerRegistry Registry;
	FMaterialHandlers::RegisterHandlers(Registry);
	auto Request = MakeShared<FJsonObject>();
	Request->SetStringField(TEXT("materialPath"), Material->GetPathName());
	Request->SetArrayField(TEXT("classes"), { MakeShared<FJsonValueString>(TEXT("VectorParameter")) });
	Request->SetArrayField(TEXT("parameterNames"), { MakeShared<FJsonValueString>(TEXT("Tint")) });
	auto Chain = MakeShared<FJsonObject>();
	Chain->SetStringField(TEXT("targetExpression"), Make->GetName());
	Chain->SetStringField(TEXT("targetInput"), TEXT("BaseColor"));
	Chain->SetNumberField(TEXT("depthLimit"), 0);
	Request->SetObjectField(TEXT("propertyChain"), Chain);
	auto Summary = Registry.ExecuteHandler(TEXT("read_material_summary"), Request)->AsObject();
	if (!TestTrue(TEXT("summary succeeds"), Summary->GetBoolField(TEXT("success")))) return false;
	TestEqual(TEXT("class count"), Summary->GetObjectField(TEXT("countsByClass"))->GetIntegerField(TEXT("MaterialExpressionMultiply")), 1);
	const auto& Nodes = Summary->GetArrayField(TEXT("nodes"));
	if (!TestEqual(TEXT("only requested parameter returned"), Nodes.Num(), 1)) return false;
	TestEqual(TEXT("parameter name returned"), Nodes[0]->AsObject()->GetStringField(TEXT("parameterName")), FString(TEXT("Tint")));
	TestEqual(TEXT("parameter group returned"), Nodes[0]->AsObject()->GetStringField(TEXT("group")), FString(TEXT("Surface")));
	TestEqual(TEXT("depth zero includes only source"), Summary->GetObjectField(TEXT("propertyChain"))->GetArrayField(TEXT("nodes")).Num(), 1);
	TestTrue(TEXT("depth truncation reported"), Summary->GetObjectField(TEXT("propertyChain"))->GetBoolField(TEXT("truncated")));
	Chain->RemoveField(TEXT("targetExpression"));
	Chain->RemoveField(TEXT("targetInput"));
	Chain->SetStringField(TEXT("property"), TEXT("MaterialAttributes"));
	Chain->SetNumberField(TEXT("depthLimit"), 8);
	auto Full = Registry.ExecuteHandler(TEXT("read_material_summary"), Request)->AsObject();
	TestEqual(TEXT("attributes property follows whole upstream chain"), Full->GetObjectField(TEXT("propertyChain"))->GetArrayField(TEXT("nodes")).Num(), 3);
	TestFalse(TEXT("read did not dirty material"), Package->IsDirty());
	TestNull(TEXT("read did not create an editor graph"), Material->MaterialGraph.Get());
	return true;
}

#endif
