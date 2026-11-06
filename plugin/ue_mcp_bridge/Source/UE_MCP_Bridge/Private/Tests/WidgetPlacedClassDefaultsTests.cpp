// #1542: a user widget placed into another Widget Blueprint holds the class
// defaults its class had at placement, in the designer and compiled trees.
#if WITH_DEV_AUTOMATION_TESTS

#include "Handlers/Widget/WidgetClassDefaults.h"
#include "Handlers/Widget/WidgetHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"
#include "Tests/MCPScopedTestMount.h"

namespace MCPWidgetPlacedClassDefaultsTests
{
UWidgetBlueprint* MakeWidgetBlueprint(const FString& PackageName, const TCHAR* Name)
{
	UPackage* Package = CreatePackage(*PackageName);
	return Cast<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
		UUserWidget::StaticClass(), Package, FName(Name), BPTYPE_Normal,
		UWidgetBlueprint::StaticClass(), UWidgetBlueprintGeneratedClass::StaticClass()));
}

FString ReadName(const UObject* Object, FName Property)
{
	const FNameProperty* Prop = Object ? CastField<FNameProperty>(Object->GetClass()->FindPropertyByName(Property)) : nullptr;
	return Prop ? Prop->GetPropertyValue_InContainer(Object).ToString() : TEXT("<missing>");
}

void WriteName(UObject* Object, FName Property, FName Value)
{
	if (FNameProperty* Prop = Object ? CastField<FNameProperty>(Object->GetClass()->FindPropertyByName(Property)) : nullptr)
	{
		Prop->SetPropertyValue_InContainer(Object, Value);
	}
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetPlacedClassDefaultsTest,
	"UE.MCP.Widget.PlacedClassDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWidgetPlacedClassDefaultsTest::RunTest(const FString& Parameters)
{
	using namespace MCPWidgetPlacedClassDefaultsTests;
	FMCPScopedTestMount Mount(
		TEXT("/UEMCPWidgetDefaults_") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/"), TEXT("UEMCPWidgetDefaults"));
	const FString ChildPath = Mount.RootPath + TEXT("WBP_DefaultsChild");
	const FString ParentPath = Mount.RootPath + TEXT("WBP_DefaultsParent");
	const FName TabKey(TEXT("TabKey"));

	// A child page with a Name variable whose class default is set after compile.
	UWidgetBlueprint* Child = MakeWidgetBlueprint(ChildPath, TEXT("WBP_DefaultsChild"));
	if (!TestNotNull(TEXT("child widget blueprint created"), Child)) return false;
	FEdGraphPinType NameType;
	NameType.PinCategory = UEdGraphSchema_K2::PC_Name;
	TestTrue(TEXT("TabKey variable added"), FBlueprintEditorUtils::AddMemberVariable(Child, TabKey, NameType));
	FKismetEditorUtilities::CompileBlueprint(Child);
	UObject* ChildCDO = Child->GeneratedClass ? Child->GeneratedClass->GetDefaultObject() : nullptr;
	if (!TestNotNull(TEXT("child class default object"), ChildCDO)) return false;
	ChildCDO->Modify();
	WriteName(ChildCDO, TabKey, FName(TEXT("Eat")));
	FString SaveError;
	TestTrue(TEXT("child saves: ") + SaveError, SaveAssetPackageChecked(Child, SaveError));

	UWidgetBlueprint* Parent = MakeWidgetBlueprint(ParentPath, TEXT("WBP_DefaultsParent"));
	if (!TestNotNull(TEXT("parent widget blueprint created"), Parent)) return false;
	TestTrue(TEXT("parent saves: ") + SaveError, SaveAssetPackageChecked(Parent, SaveError));

	FMCPHandlerRegistry Registry;
	FWidgetHandlers::RegisterHandlers(Registry);
	auto Add = [&Registry, &ParentPath](const FString& Class, const TCHAR* Name, const TCHAR* ParentName)
	{
		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("assetPath"), ParentPath);
		Params->SetStringField(TEXT("widgetClass"), Class);
		Params->SetStringField(TEXT("widgetName"), Name);
		if (ParentName) Params->SetStringField(TEXT("parentWidgetName"), ParentName);
		const TSharedPtr<FJsonValue> Value = Registry.ExecuteHandler(TEXT("add_widget"), Params);
		return Value.IsValid() && Value->Type == EJson::Object ? Value->AsObject() : TSharedPtr<FJsonObject>();
	};
	bool bOk = false;
	const TSharedPtr<FJsonObject> Root = Add(TEXT("CanvasPanel"), TEXT("RootPanel"), nullptr);
	TestTrue(TEXT("root panel added"), Root.IsValid() && Root->TryGetBoolField(TEXT("success"), bOk) && bOk);

	// 1. add_widget places the child with the default it has now.
	const TSharedPtr<FJsonObject> Placed = Add(ChildPath, TEXT("EatPage"), TEXT("RootPanel"));
	bOk = false;
	if (!TestTrue(TEXT("child page placed"), Placed.IsValid() && Placed->TryGetBoolField(TEXT("success"), bOk) && bOk)) return false;
	UWidget* Designer = Parent->WidgetTree->FindWidget(TEXT("EatPage"));
	TestEqual(TEXT("designer template holds the class default"), ReadName(Designer, TabKey), FString(TEXT("Eat")));
	UWidgetBlueprintGeneratedClass* ParentClass = Cast<UWidgetBlueprintGeneratedClass>(Parent->GeneratedClass);
	UWidget* Compiled = ParentClass && ParentClass->GetWidgetTreeArchetype()
		? ParentClass->GetWidgetTreeArchetype()->FindWidget(TEXT("EatPage")) : nullptr;
	TestEqual(TEXT("compiled template holds the class default"), ReadName(Compiled, TabKey), FString(TEXT("Eat")));

	// 2. A placed template that lost the value between placement and save gets
	// it back, in both trees, and the result names it.
	MCPWidgetClassDefaults::FSnapshot Snapshot;
	MCPWidgetClassDefaults::Capture(Designer, {}, Snapshot);
	TestTrue(TEXT("TabKey is tracked"), Snapshot.Entries.ContainsByPredicate(
		[&TabKey](const MCPWidgetClassDefaults::FEntry& E) { return E.Property == TabKey; }));
	WriteName(Designer, TabKey, NAME_None);
	WriteName(Compiled, TabKey, NAME_None);
	const TArray<TSharedPtr<FJsonValue>> Rows = MCPWidgetClassDefaults::Restore(Parent, Snapshot);
	TestEqual(TEXT("one restored row"), Rows.Num(), 1);
	TestEqual(TEXT("designer value restored"), ReadName(Designer, TabKey), FString(TEXT("Eat")));
	TestEqual(TEXT("compiled value restored"), ReadName(Compiled, TabKey), FString(TEXT("Eat")));
	if (Rows.Num() == 1)
	{
		const TSharedPtr<FJsonObject> Row = Rows[0]->AsObject();
		TestEqual(TEXT("row names the property"), Row->GetStringField(TEXT("property")), FString(TEXT("TabKey")));
		TestEqual(TEXT("row names the value found"), Row->GetStringField(TEXT("found")), FString(TEXT("None")));
	}

	// 3. Nothing differs, nothing is reported.
	TestEqual(TEXT("second pass restores nothing"), MCPWidgetClassDefaults::Restore(Parent, Snapshot).Num(), 0);

	// 4. A property the caller sets explicitly is not captured.
	MCPWidgetClassDefaults::FSnapshot Skipped;
	MCPWidgetClassDefaults::Capture(Designer, { TabKey }, Skipped);
	TestFalse(TEXT("explicit property skipped"), Skipped.Entries.ContainsByPredicate(
		[&TabKey](const MCPWidgetClassDefaults::FEntry& E) { return E.Property == TabKey; }));
	return true;
}

#endif
