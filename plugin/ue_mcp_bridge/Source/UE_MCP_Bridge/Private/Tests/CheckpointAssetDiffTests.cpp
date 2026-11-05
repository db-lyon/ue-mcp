#if WITH_DEV_AUTOMATION_TESTS

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Components/BoxComponent.h"
#include "Components/CanvasPanel.h"
#include "Components/ProgressBar.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/StaticMesh.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/Actor.h"
#include "K2Node_CallFunction.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "WidgetBlueprint.h"
#include "HAL/FileManager.h"
#include "HandlerPackageSave.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Handlers/Diff/CheckpointThumbnail.h"
#include "Handlers/Diff/DiffHandlers.h"
#include "Handlers/Diff/DiffHandlers_Revision.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "MCPScopedTestMount.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "Widgets/SWindow.h"

namespace
{
	/** One saved version of the test material, copied where the editor loads packages for diffing. */
	FString CkDiffTestSnapshot(UPackage* Package, const FString& Name)
	{
		FString File;
		FPackageName::TryConvertLongPackageNameToFilename(Package->GetName(), File, FPackageName::GetAssetPackageExtension());
		const FString Copy = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::DiffDir(), TEXT("UE_MCP_CkDiffTest"), Name + FPackageName::GetAssetPackageExtension()));
		IFileManager::Get().Copy(*Copy, *File);
		return Copy;
	}

	TSharedPtr<FJsonObject> CkDiffTestCall(FAutomationTestBase& Test, FMCPHandlerRegistry& Registry, const TCHAR* Method, const TSharedPtr<FJsonObject>& Params)
	{
		const TSharedPtr<FJsonValue> Value = Registry.ExecuteHandler(Method, Params);
		if (!Value.IsValid() || Value->Type != EJson::Object)
		{
			Test.AddError(FString::Printf(TEXT("%s did not return an object"), Method));
			return nullptr;
		}
		return Value->AsObject();
	}

	const TSharedPtr<FJsonObject> CkDiffTestSection(const TSharedPtr<FJsonObject>& Result, const FString& Category)
	{
		for (const TSharedPtr<FJsonValue>& S : Result->GetArrayField(TEXT("sections")))
		{
			if (S->AsObject()->GetStringField(TEXT("category")) == Category) return S->AsObject();
		}
		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPCheckpointAssetDiffMaterialTest,
	"UE.MCP.Diff.CheckpointAsset.MaterialGraphPropertiesAndThumbnails",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPCheckpointAssetDiffMaterialTest::RunTest(const FString& Parameters)
{
	FMCPScopedTestMount Mount(TEXT("/UEMCPCkDiffTest/"), TEXT("UEMCPCkDiffTest"));
	const FString DiffDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::DiffDir(), TEXT("UE_MCP_CkDiffTest")));
	ON_SCOPE_EXIT { IFileManager::Get().DeleteDirectory(*DiffDir, false, true); };

	UPackage* Package = CreatePackage(TEXT("/UEMCPCkDiffTest/M_CkDiff"));
	UMaterial* Material = NewObject<UMaterial>(Package, TEXT("M_CkDiff"), RF_Public | RF_Standalone);
	UMaterialExpressionScalarParameter* Param = NewObject<UMaterialExpressionScalarParameter>(Material);
	Param->ParameterName = TEXT("Roughness");
	Param->DefaultValue = 0.1f;
	Material->GetExpressionCollection().AddExpression(Param);
	Material->GetExpressionInputForProperty(MP_Roughness)->Connect(0, Param);
	Material->PostEditChange();
	FString Reason;
	if (!TestTrue(TEXT("first version saves"), SaveAssetPackageChecked(Material, Reason))) { AddError(Reason); return false; }
	const FString Before = CkDiffTestSnapshot(Package, TEXT("M_CkDiff-before"));

	Param->DefaultValue = 0.8f;
	UMaterialExpressionConstant* Added = NewObject<UMaterialExpressionConstant>(Material);
	Added->R = 0.5f;
	Material->GetExpressionCollection().AddExpression(Added);
	Material->GetExpressionInputForProperty(MP_Metallic)->Connect(0, Added);
	Material->TwoSided = true;
	Material->PostEditChange();
	if (!TestTrue(TEXT("second version saves"), SaveAssetPackageChecked(Material, Reason))) { AddError(Reason); return false; }
	const FString After = CkDiffTestSnapshot(Package, TEXT("M_CkDiff-after"));

	FString Original;
	FPackageName::TryConvertLongPackageNameToFilename(Package->GetName(), Original, FPackageName::GetAssetPackageExtension());

	FMCPHandlerRegistry Registry;
	FDiffHandlers::RegisterHandlers(Registry);
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("originalFile"), Original);
	Params->SetStringField(TEXT("fromFile"), Before);
	Params->SetStringField(TEXT("toFile"), After);
	Params->SetStringField(TEXT("fromThumbnail"), DiffDir / TEXT("before.png"));
	Params->SetStringField(TEXT("toThumbnail"), DiffDir / TEXT("after.png"));
	const TSharedPtr<FJsonObject> Diff = CkDiffTestCall(*this, Registry, TEXT("checkpoint_diff_asset"), Params);
	if (!Diff.IsValid() || !TestTrue(TEXT("diff succeeds"), Diff->GetBoolField(TEXT("success")))) return false;
	TestEqual(TEXT("status"), Diff->GetStringField(TEXT("status")), FString(TEXT("modified")));
	TestEqual(TEXT("asset class"), Diff->GetStringField(TEXT("assetClass")), FString(TEXT("Material")));
	TestEqual(TEXT("engine diff named"), Diff->GetObjectField(TEXT("engine"))->GetStringField(TEXT("tool")), FString(TEXT("SMaterialDiff")));

	// The graph diff: FGraphDiffControl's records, the added node among them, and the changed default with both values.
	const TSharedPtr<FJsonObject> Graph = CkDiffTestSection(Diff, TEXT("Material Graph"));
	if (!TestTrue(TEXT("material graph section"), Graph.IsValid())) return false;
	bool bAddedNode = false;
	bool bDefaultChanged = false;
	for (const TSharedPtr<FJsonValue>& V : Graph->GetArrayField(TEXT("entries")))
	{
		const TSharedPtr<FJsonObject> R = V->AsObject();
		TestFalse(TEXT("every record carries the engine's DisplayString"), R->GetStringField(TEXT("displayString")).IsEmpty());
		if (R->GetStringField(TEXT("diffType")) == TEXT("NODE_ADDED"))
		{
			bAddedNode = true;
			// The added node carries what it holds: its full title, pins, and the expression's own properties.
			const TSharedPtr<FJsonObject>* Node = nullptr;
			if (TestTrue(TEXT("the added node is named"), R->TryGetObjectField(TEXT("node1"), Node)))
			{
				TestFalse(TEXT("the added node has a full title"), (*Node)->GetStringField(TEXT("fullTitle")).IsEmpty());
				const TArray<TSharedPtr<FJsonValue>>* Pins = nullptr;
				TestTrue(TEXT("the added node lists its pins"), (*Node)->TryGetArrayField(TEXT("pins"), Pins));
				TestEqual(TEXT("properties are the expression's"), (*Node)->GetStringField(TEXT("propertiesOf")), FString(TEXT("MaterialExpressionConstant")));
				bool bValue = false;
				const TArray<TSharedPtr<FJsonValue>>* NodeProps = nullptr;
				if (TestTrue(TEXT("the added node lists its properties"), (*Node)->TryGetArrayField(TEXT("properties"), NodeProps)))
				{
					for (const TSharedPtr<FJsonValue>& P : *NodeProps)
					{
						const TSharedPtr<FJsonObject> PO = P->AsObject();
						if (PO->GetStringField(TEXT("name")) == TEXT("R"))
						{
							bValue = PO->GetStringField(TEXT("value")).StartsWith(TEXT("0.5"))
								&& PO->GetStringField(TEXT("displayName")) == TEXT("Value")
								&& !PO->GetBoolField(TEXT("isDefault"));
						}
					}
				}
				TestTrue(TEXT("the added Constant reads Value 0.5, not its class default"), bValue);
			}
		}
		else
		{
			const TSharedPtr<FJsonObject>* Node = nullptr;
			if (R->TryGetObjectField(TEXT("node1"), Node) && R->GetStringField(TEXT("diffType")) != TEXT("NODE_REMOVED"))
			{
				TestFalse(TEXT("only an added or removed node carries its properties"), (*Node)->HasField(TEXT("properties")));
			}
		}
		const TArray<TSharedPtr<FJsonValue>>* Props = nullptr;
		if (R->TryGetArrayField(TEXT("properties"), Props))
		{
			for (const TSharedPtr<FJsonValue>& P : *Props)
			{
				const TSharedPtr<FJsonObject> PO = P->AsObject();
				if (PO->GetStringField(TEXT("property")) == TEXT("DefaultValue"))
				{
					bDefaultChanged = PO->GetStringField(TEXT("before")).StartsWith(TEXT("0.1")) && PO->GetStringField(TEXT("after")).StartsWith(TEXT("0.8"));
				}
			}
		}
	}
	TestTrue(TEXT("the added Constant is a NODE_ADDED record"), bAddedNode);
	TestTrue(TEXT("the parameter default reads 0.1 before and 0.8 after"), bDefaultChanged);

	// The details diff: TwoSided, with ExportText values.
	const TSharedPtr<FJsonObject> Details = CkDiffTestSection(Diff, TEXT("Details"));
	bool bTwoSided = false;
	if (TestTrue(TEXT("details section"), Details.IsValid()))
	{
		for (const TSharedPtr<FJsonValue>& V : Details->GetArrayField(TEXT("entries")))
		{
			const TSharedPtr<FJsonObject> R = V->AsObject();
			if (R->GetStringField(TEXT("property")) == TEXT("TwoSided"))
			{
				bTwoSided = R->GetStringField(TEXT("before")) == TEXT("False") && R->GetStringField(TEXT("after")) == TEXT("True")
					&& R->GetStringField(TEXT("diffType")) == TEXT("PropertyValueChanged");
			}
		}
	}
	TestTrue(TEXT("TwoSided False to True, as CompareUnrelatedObjects reports it"), bTwoSided);

	// A picture of each version, rendered from the objects loaded for diff.
	for (const TCHAR* Side : { TEXT("before"), TEXT("after") })
	{
		const TSharedPtr<FJsonObject> Thumb = Diff->GetObjectField(Side)->GetObjectField(TEXT("thumbnail"));
		TestEqual(FString::Printf(TEXT("%s thumbnail rendered"), Side), Thumb->GetStringField(TEXT("source")), FString(TEXT("rendered")));
		TestTrue(FString::Printf(TEXT("%s thumbnail written"), Side), IFileManager::Get().FileSize(*Thumb->GetStringField(TEXT("file"))) > 0);
	}

	// An added asset: no before, and what it is, against its class defaults.
	TSharedPtr<FJsonObject> AddedParams = MakeShared<FJsonObject>();
	AddedParams->SetStringField(TEXT("originalFile"), Original);
	AddedParams->SetStringField(TEXT("toFile"), After);
	const TSharedPtr<FJsonObject> AddedDiff = CkDiffTestCall(*this, Registry, TEXT("checkpoint_diff_asset"), AddedParams);
	if (AddedDiff.IsValid() && TestTrue(TEXT("added diff succeeds"), AddedDiff->GetBoolField(TEXT("success"))))
	{
		TestEqual(TEXT("added status"), AddedDiff->GetStringField(TEXT("status")), FString(TEXT("added")));
		TestTrue(TEXT("no version before"), AddedDiff->HasTypedField<EJson::Null>(TEXT("before")));
		TestTrue(TEXT("what it is, against class defaults"), CkDiffTestSection(AddedDiff, TEXT("Properties (against class defaults)")).IsValid());
	}

	// A file that is not a package fails with the reason.
	const FString Junk = DiffDir / TEXT("Junk.uasset");
	FFileHelper::SaveStringToFile(TEXT("not a package"), *Junk);
	TSharedPtr<FJsonObject> JunkParams = MakeShared<FJsonObject>();
	JunkParams->SetStringField(TEXT("originalFile"), Original);
	JunkParams->SetStringField(TEXT("fromFile"), Junk);
	JunkParams->SetStringField(TEXT("toFile"), After);
	const TSharedPtr<FJsonObject> JunkDiff = CkDiffTestCall(*this, Registry, TEXT("checkpoint_diff_asset"), JunkParams);
	if (JunkDiff.IsValid())
	{
		TestFalse(TEXT("an unloadable version fails"), JunkDiff->GetBoolField(TEXT("success")));
		TestTrue(TEXT("and says why"), JunkDiff->GetStringField(TEXT("error")).Contains(TEXT("could not be loaded for diff")));
	}

	// items: several diffs in one call, one result each in order, a failing item answered in its place.
	// Each item has files of its own, as list_changes sends them.
	TSharedPtr<FJsonObject> BatchAdded = MakeShared<FJsonObject>();
	BatchAdded->SetStringField(TEXT("originalFile"), Original);
	BatchAdded->SetStringField(TEXT("toFile"), CkDiffTestSnapshot(Package, TEXT("M_CkDiff-batch-added")));
	TSharedPtr<FJsonObject> BatchParams = MakeShared<FJsonObject>();
	BatchParams->SetArrayField(TEXT("items"), {
		MakeShared<FJsonValueObject>(Params), MakeShared<FJsonValueObject>(JunkParams), MakeShared<FJsonValueObject>(BatchAdded) });
	const TSharedPtr<FJsonObject> Batch = CkDiffTestCall(*this, Registry, TEXT("checkpoint_diff_asset"), BatchParams);
	const TArray<TSharedPtr<FJsonValue>>* Results = nullptr;
	if (Batch.IsValid() && TestTrue(TEXT("a batch answers results"), Batch->TryGetArrayField(TEXT("results"), Results))
		&& TestEqual(TEXT("one result per item"), Results->Num(), 3))
	{
		const TSharedPtr<FJsonObject> First = (*Results)[0]->AsObject();
		TestEqual(TEXT("the first item is the modified material"), First->GetStringField(TEXT("status")), FString(TEXT("modified")));
		TestEqual(TEXT("with the same records as on its own"), First->GetIntegerField(TEXT("recordCount")), Diff->GetIntegerField(TEXT("recordCount")));
		TestFalse(TEXT("the unloadable item fails in its place"), (*Results)[1]->AsObject()->GetBoolField(TEXT("success")));
		TestEqual(TEXT("and the item after it still runs"), (*Results)[2]->AsObject()->GetStringField(TEXT("status")), FString(TEXT("added")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPCheckpointAssetDiffWidgetBlueprintTest,
	"UE.MCP.Diff.CheckpointAsset.WidgetTreeDefaultsAndGraph",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPCheckpointAssetDiffWidgetBlueprintTest::RunTest(const FString& Parameters)
{
	FMCPScopedTestMount Mount(TEXT("/UEMCPCkWidgetTest/"), TEXT("UEMCPCkWidgetTest"));
	const FString DiffDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::DiffDir(), TEXT("UE_MCP_CkDiffTest")));
	ON_SCOPE_EXIT { IFileManager::Get().DeleteDirectory(*DiffDir, false, true); };

	UPackage* Package = CreatePackage(TEXT("/UEMCPCkWidgetTest/WBP_CkDiff"));
	UWidgetBlueprint* Widget = Cast<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
		UUserWidget::StaticClass(), Package, FName(TEXT("WBP_CkDiff")), BPTYPE_Normal,
		UWidgetBlueprint::StaticClass(), UWidgetBlueprintGeneratedClass::StaticClass()));
	if (!TestNotNull(TEXT("the fixture Widget Blueprint is created"), Widget)) return false;
	UCanvasPanel* Root = Widget->WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
	Widget->WidgetTree->RootWidget = Root;
	UProgressBar* Meter = Widget->WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("WardMeter"));
	Root->AddChild(Meter);
	FKismetEditorUtilities::CompileBlueprint(Widget);
	FString Reason;
	if (!TestTrue(TEXT("first version saves"), SaveAssetPackageChecked(Widget, Reason))) { AddError(Reason); return false; }
	const FString Before = CkDiffTestSnapshot(Package, TEXT("WBP_CkDiff-before"));

	// A property of a widget in the widget tree, a node in the event graph, and a class default.
	Meter->SetRenderOpacity(0.25f);
	UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(Widget);
	if (!TestNotNull(TEXT("the Widget Blueprint has an event graph"), Graph)) return false;
	UK2Node_CallFunction* Call = NewObject<UK2Node_CallFunction>(Graph);
	Call->CreateNewGuid();
	Call->FunctionReference.SetExternalMember(GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, PrintString), UKismetSystemLibrary::StaticClass());
	Graph->AddNode(Call, false, false);
	Call->PostPlacedNewNode();
	Call->AllocateDefaultPins();
	FKismetEditorUtilities::CompileBlueprint(Widget);
	UUserWidget* Defaults = Widget->GeneratedClass->GetDefaultObject<UUserWidget>();
	Defaults->SetColorAndOpacity(FLinearColor(1.f, 0.f, 0.f, 1.f));
	Defaults->MarkPackageDirty();
	if (!TestTrue(TEXT("second version saves"), SaveAssetPackageChecked(Widget, Reason))) { AddError(Reason); return false; }
	const FString After = CkDiffTestSnapshot(Package, TEXT("WBP_CkDiff-after"));
	FString Original;
	FPackageName::TryConvertLongPackageNameToFilename(Package->GetName(), Original, FPackageName::GetAssetPackageExtension());

	FMCPHandlerRegistry Registry;
	FDiffHandlers::RegisterHandlers(Registry);
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("originalFile"), Original);
	Params->SetStringField(TEXT("fromFile"), Before);
	Params->SetStringField(TEXT("toFile"), After);
	Params->SetStringField(TEXT("fromThumbnail"), DiffDir / TEXT("wbp-before.png"));
	Params->SetStringField(TEXT("toThumbnail"), DiffDir / TEXT("wbp-after.png"));
	const TSharedPtr<FJsonObject> Diff = CkDiffTestCall(*this, Registry, TEXT("checkpoint_diff_asset"), Params);
	if (!Diff.IsValid() || !TestTrue(TEXT("diff succeeds"), Diff->GetBoolField(TEXT("success")))) return false;
	TestEqual(TEXT("asset class"), Diff->GetStringField(TEXT("assetClass")), FString(TEXT("WidgetBlueprint")));
	TestTrue(TEXT("the type's own diff ran"), Diff->GetObjectField(TEXT("engine"))->GetBoolField(TEXT("blueprintTypeDiff")));

	// The widget tree: UWidgetBlueprint::FindDiffs pairs WardMeter in both versions, and its record carries path, label and both values.
	const TSharedPtr<FJsonObject> Tree = CkDiffTestSection(Diff, TEXT("Widget Blueprint"));
	bool bOpacity = false;
	if (TestTrue(TEXT("a Widget Blueprint section, named as SBlueprintDiff names it"), Tree.IsValid()))
	{
		TestEqual(TEXT("from the type's own diff"), Tree->GetStringField(TEXT("engineSource")), FString(TEXT("UBlueprint::FindDiffs")));
		for (const TSharedPtr<FJsonValue>& V : Tree->GetArrayField(TEXT("entries")))
		{
			const TSharedPtr<FJsonObject> R = V->AsObject();
			FString Property;
			if (!R->TryGetStringField(TEXT("property"), Property) || Property != TEXT("RenderOpacity")) continue;
			bOpacity = R->GetStringField(TEXT("group")).Contains(TEXT("WardMeter"))
				&& R->GetStringField(TEXT("path")) == TEXT("RenderOpacity")
				&& R->GetStringField(TEXT("label")) == TEXT("Render Opacity")
				&& R->GetStringField(TEXT("before")).StartsWith(TEXT("1.0"))
				&& R->GetStringField(TEXT("after")).StartsWith(TEXT("0.25"))
				&& R->GetStringField(TEXT("diffType")) == TEXT("PropertyValueChanged")
				&& !R->HasField(TEXT("hidden"));
		}
	}
	TestTrue(TEXT("Widget WardMeter: Render Opacity 1.0 to 0.25"), bOpacity);

	// Defaults: the class default object's ColorAndOpacity.
	const TSharedPtr<FJsonObject> Cdo = CkDiffTestSection(Diff, TEXT("Defaults"));
	bool bColor = false;
	if (TestTrue(TEXT("a Defaults section"), Cdo.IsValid()))
	{
		for (const TSharedPtr<FJsonValue>& V : Cdo->GetArrayField(TEXT("entries")))
		{
			const TSharedPtr<FJsonObject> R = V->AsObject();
			FString Property;
			if (R->TryGetStringField(TEXT("property"), Property) && Property == TEXT("ColorAndOpacity"))
			{
				bColor = R->GetStringField(TEXT("before")) != R->GetStringField(TEXT("after")) && R->GetStringField(TEXT("after")).Contains(TEXT("G=0.0"));
			}
		}
	}
	TestTrue(TEXT("Color and Opacity white to red, on the class defaults"), bColor);

	// The event graph: the added Print String node.
	bool bNode = false;
	for (const TSharedPtr<FJsonValue>& S : Diff->GetArrayField(TEXT("sections")))
	{
		const TSharedPtr<FJsonObject> SO = S->AsObject();
		if (!SO->GetStringField(TEXT("category")).StartsWith(TEXT("Graph: "))) continue;
		for (const TSharedPtr<FJsonValue>& V : SO->GetArrayField(TEXT("entries")))
		{
			if (V->AsObject()->GetStringField(TEXT("diffType")) == TEXT("NODE_ADDED")) bNode = true;
		}
	}
	TestTrue(TEXT("the added Print String is a NODE_ADDED record"), bNode);

	// A Widget Blueprint loaded for diff is not compiled, so its renderer draws nothing: never a blank picture, and the reason.
	for (const TCHAR* Side : { TEXT("before"), TEXT("after") })
	{
		const TSharedPtr<FJsonObject> Thumb = Diff->GetObjectField(Side)->GetObjectField(TEXT("thumbnail"));
		FString Source;
		if (Thumb->TryGetStringField(TEXT("source"), Source))
		{
			TestEqual(FString::Printf(TEXT("%s picture, if any, is the one saved with the version"), Side), Source, FString(TEXT("saved")));
		}
		else
		{
			TestTrue(FString::Printf(TEXT("%s has no picture, and says why"), Side), Thumb->GetStringField(TEXT("error")).Contains(TEXT("drew nothing")));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPCheckpointNativeDiffWindowTest,
	"UE.MCP.Diff.CheckpointAsset.NativeDiffOpensTheEngineWindow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPCheckpointNativeDiffWindowTest::RunTest(const FString& Parameters)
{
	if (!FSlateApplication::IsInitialized())
	{
		AddInfo(TEXT("No Slate application in this process; the diff window cannot open."));
		return true;
	}
	FMCPScopedTestMount Mount(TEXT("/UEMCPCkNativeTest/"), TEXT("UEMCPCkNativeTest"));
	const FString DiffDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::DiffDir(), TEXT("UE_MCP_CkDiffTest")));

	UPackage* Package = CreatePackage(TEXT("/UEMCPCkNativeTest/M_CkNative"));
	UMaterial* Material = NewObject<UMaterial>(Package, TEXT("M_CkNative"), RF_Public | RF_Standalone);
	FString Reason;
	if (!TestTrue(TEXT("first version saves"), SaveAssetPackageChecked(Material, Reason))) { AddError(Reason); return false; }
	const FString Before = CkDiffTestSnapshot(Package, TEXT("M_CkNative-before"));
	Material->TwoSided = true;
	if (!TestTrue(TEXT("second version saves"), SaveAssetPackageChecked(Material, Reason))) { AddError(Reason); return false; }
	const FString After = CkDiffTestSnapshot(Package, TEXT("M_CkNative-after"));
	FString Original;
	FPackageName::TryConvertLongPackageNameToFilename(Package->GetName(), Original, FPackageName::GetAssetPackageExtension());

	FMCPHandlerRegistry Registry;
	FDiffHandlers::RegisterHandlers(Registry);
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("originalFile"), Original);
	Params->SetStringField(TEXT("fromFile"), Before);
	Params->SetStringField(TEXT("toFile"), After);
	Params->SetStringField(TEXT("fromLabel"), TEXT("Checkpoint A"));
	Params->SetStringField(TEXT("toLabel"), TEXT("Checkpoint B"));
	Params->SetStringField(TEXT("fromTime"), TEXT("2026-10-27T09:15:00.000Z"));
	Params->SetStringField(TEXT("toTime"), TEXT("2026-10-28T14:30:00.000Z"));
	const TSharedPtr<FJsonObject> Opened = CkDiffTestCall(*this, Registry, TEXT("checkpoint_open_native_diff"), Params);
	if (!Opened.IsValid() || !TestTrue(TEXT("native diff opens"), Opened->GetBoolField(TEXT("success")))) return false;
	TestEqual(TEXT("it is a window"), Opened->GetStringField(TEXT("opened")), FString(TEXT("window")));

	// Each side's revision carries its checkpoint's time, not the unset date the window shows as "Dec 31, 1" (#1606).
	const TSharedPtr<FJsonObject>* Revisions = nullptr;
	if (TestTrue(TEXT("the revisions are reported"), Opened->TryGetObjectField(TEXT("revisions"), Revisions)))
	{
		const TSharedPtr<FJsonObject> RevBefore = (*Revisions)->GetObjectField(TEXT("before"));
		const TSharedPtr<FJsonObject> RevAfter = (*Revisions)->GetObjectField(TEXT("after"));
		TestEqual(TEXT("before is labelled"), RevBefore->GetStringField(TEXT("label")), FString(TEXT("Checkpoint A")));
		TestEqual(TEXT("before is dated with its checkpoint"), RevBefore->GetStringField(TEXT("date")), FDateTime(2026, 10, 27, 9, 15, 0).ToIso8601());
		TestEqual(TEXT("after is dated with its checkpoint"), RevAfter->GetStringField(TEXT("date")), FDateTime(2026, 10, 28, 14, 30, 0).ToIso8601());
	}
	const FString Title = Opened->GetStringField(TEXT("windowTitle"));
	TestFalse(TEXT("the window has a title"), Title.IsEmpty());

	// The window is registered with Slate under that title; close it.
	TArray<TSharedRef<SWindow>> Windows;
	FSlateApplication::Get().GetAllVisibleWindowsOrdered(Windows);
	TSharedPtr<SWindow> Found;
	for (const TSharedRef<SWindow>& W : Windows)
	{
		if (W->GetTitle().ToString() == Title) Found = W;
	}
	if (TestTrue(TEXT("the diff window is open"), Found.IsValid()))
	{
		Found->RequestDestroyWindow();
	}
	IFileManager::Get().DeleteDirectory(*DiffDir, false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPCheckpointRevisionDateTest,
	"UE.MCP.Diff.CheckpointAsset.RevisionDateIsSet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPCheckpointRevisionDateTest::RunTest(const FString& Parameters)
{
	const FDateTime Fallback(2026, 1, 2, 3, 4, 5);
	const FRevisionInfo Timed = MCPCheckpointRevision(TEXT("Checkpoint 1234567: turn start"), TEXT("2026-10-28T14:30:00.000Z"), Fallback);
	TestEqual(TEXT("the label is the revision"), Timed.Revision, FString(TEXT("Checkpoint 1234567: turn start")));
	TestEqual(TEXT("the checkpoint time is the date"), Timed.Date, FDateTime(2026, 10, 28, 14, 30, 0));

	TestEqual(TEXT("no time: the fallback"), MCPCheckpointRevision(TEXT("Current"), FString(), Fallback).Date, Fallback);
	TestEqual(TEXT("an unreadable time: the fallback"), MCPCheckpointRevision(TEXT("B"), TEXT("yesterday"), Fallback).Date, Fallback);
	TestNotEqual(TEXT("never the unset date"), MCPCheckpointRevision(TEXT("B"), FString(), Fallback).Date, FDateTime());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPCheckpointBlankThumbnailTest,
	"UE.MCP.Diff.CheckpointAsset.BlankRenderIsNoPicture",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPCheckpointBlankThumbnailTest::RunTest(const FString& Parameters)
{
	TArray<uint8> Black;
	Black.SetNumZeroed(4 * 4 * 4);
	TestTrue(TEXT("an all-black render is no picture"), MCPCkDiffIsBlankImage(Black, 4, 4));
	TArray<uint8> NearBlack = Black;
	NearBlack[5] = 2;
	TestTrue(TEXT("readback noise at black is still no picture"), MCPCkDiffIsBlankImage(NearBlack, 4, 4));
	TArray<uint8> OnePixel = Black;
	OnePixel[4 * 9 + 1] = 40;
	TestFalse(TEXT("one coloured pixel is a picture"), MCPCkDiffIsBlankImage(OnePixel, 4, 4));
	TArray<uint8> AlphaOnly = Black;
	AlphaOnly[3] = 255;
	TestTrue(TEXT("alpha alone is no picture"), MCPCkDiffIsBlankImage(AlphaOnly, 4, 4));
	TArray<uint8> Grey;
	Grey.Init(128, 4 * 4 * 4);
	TestTrue(TEXT("a flat background is no picture"), MCPCkDiffIsBlankImage(Grey, 4, 4));
	TestTrue(TEXT("no pixels is no picture"), MCPCkDiffIsBlankImage(TArray<uint8>(), 0, 0));
	TestTrue(TEXT("fewer bytes than the size says is no picture"), MCPCkDiffIsBlankImage(OnePixel, 8, 8));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPCheckpointBlueprintThumbnailTest,
	"UE.MCP.Diff.CheckpointAsset.ActorBlueprintThumbnailsRender",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPCheckpointBlueprintThumbnailTest::RunTest(const FString& Parameters)
{
	FMCPScopedTestMount Mount(TEXT("/UEMCPCkThumbTest/"), TEXT("UEMCPCkThumbTest"));
	const FString DiffDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::DiffDir(), TEXT("UE_MCP_CkDiffTest")));
	ON_SCOPE_EXIT { IFileManager::Get().DeleteDirectory(*DiffDir, false, true); };

	// An Actor Blueprint of engine basic shapes, as an agent builds one (#178).
	UPackage* Package = CreatePackage(TEXT("/UEMCPCkThumbTest/BP_CkThumbGate"));
	UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(AActor::StaticClass(), Package, TEXT("BP_CkThumbGate"), BPTYPE_Normal,
		UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
	if (!TestNotNull(TEXT("the Blueprint is created"), Blueprint)) return false;
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("the engine cube loads"), Cube)) return false;
	USCS_Node* Pillar = Blueprint->SimpleConstructionScript->CreateNode(UStaticMeshComponent::StaticClass(), TEXT("Pillar"));
	Cast<UStaticMeshComponent>(Pillar->ComponentTemplate)->SetStaticMesh(Cube);
	Cast<UStaticMeshComponent>(Pillar->ComponentTemplate)->SetRelativeScale3D(FVector(0.8, 0.8, 4.4));
	Blueprint->SimpleConstructionScript->AddNode(Pillar);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	FString Reason;
	if (!TestTrue(TEXT("first version saves"), SaveAssetPackageChecked(Blueprint, Reason))) { AddError(Reason); return false; }
	const FString Before = CkDiffTestSnapshot(Package, TEXT("BP_CkThumbGate-before"));

	USCS_Node* Trigger = Blueprint->SimpleConstructionScript->CreateNode(UBoxComponent::StaticClass(), TEXT("Trigger"));
	Cast<UBoxComponent>(Trigger->ComponentTemplate)->SetBoxExtent(FVector(700, 280, 150));
	Blueprint->SimpleConstructionScript->AddNode(Trigger);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	if (!TestTrue(TEXT("second version saves"), SaveAssetPackageChecked(Blueprint, Reason))) { AddError(Reason); return false; }
	const FString After = CkDiffTestSnapshot(Package, TEXT("BP_CkThumbGate-after"));

	FString Original;
	FPackageName::TryConvertLongPackageNameToFilename(Package->GetName(), Original, FPackageName::GetAssetPackageExtension());
	FMCPHandlerRegistry Registry;
	FDiffHandlers::RegisterHandlers(Registry);
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("originalFile"), Original);
	Params->SetStringField(TEXT("fromFile"), Before);
	Params->SetStringField(TEXT("toFile"), After);
	Params->SetStringField(TEXT("fromThumbnail"), DiffDir / TEXT("bp-before.png"));
	Params->SetStringField(TEXT("toThumbnail"), DiffDir / TEXT("bp-after.png"));
	Params->SetStringField(TEXT("fromGraphPicture"), DiffDir / TEXT("bp-graph-before.png"));
	Params->SetStringField(TEXT("toGraphPicture"), DiffDir / TEXT("bp-graph-after.png"));
	const TSharedPtr<FJsonObject> Diff = CkDiffTestCall(*this, Registry, TEXT("checkpoint_diff_asset"), Params);
	if (!Diff.IsValid() || !TestTrue(TEXT("diff succeeds"), Diff->GetBoolField(TEXT("success")))) return false;
	// A component change shows in the class picture, so no graph is drawn for it (#1569).
	TestFalse(TEXT("a component change has no graph picture"), Diff->GetObjectField(TEXT("after"))->HasField(TEXT("graphPicture")));

	// Each version is drawn from the Blueprint loaded for diff, and the picture shows something.
	IImageWrapperModule& Images = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
	for (const TCHAR* Side : { TEXT("before"), TEXT("after") })
	{
		const TSharedPtr<FJsonObject> Thumb = Diff->GetObjectField(Side)->GetObjectField(TEXT("thumbnail"));
		if (!TestEqual(FString::Printf(TEXT("%s thumbnail rendered"), Side), Thumb->GetStringField(TEXT("source")), FString(TEXT("rendered"))))
		{
			AddError(FString::Printf(TEXT("%s: %s"), Side, *Thumb->GetStringField(TEXT("error"))));
			continue;
		}
		TArray<uint8> Png;
		TArray<uint8> Raw;
		TSharedPtr<IImageWrapper> Reader = Images.CreateImageWrapper(EImageFormat::PNG);
		const bool bDecoded = FFileHelper::LoadFileToArray(Png, *Thumb->GetStringField(TEXT("file")))
			&& Reader.IsValid() && Reader->SetCompressed(Png.GetData(), Png.Num()) && Reader->GetRaw(ERGBFormat::BGRA, 8, Raw);
		if (TestTrue(FString::Printf(TEXT("%s thumbnail decodes"), Side), bDecoded))
		{
			TestFalse(FString::Printf(TEXT("%s thumbnail is not black"), Side),
				MCPCkDiffIsBlankImage(Raw, int32(Reader->GetWidth()), int32(Reader->GetHeight())));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPCheckpointBlueprintGraphPictureTest,
	"UE.MCP.Diff.CheckpointAsset.GraphOnlyChangeDrawsTheGraph",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPCheckpointBlueprintGraphPictureTest::RunTest(const FString& Parameters)
{
	FMCPScopedTestMount Mount(TEXT("/UEMCPCkGraphPicTest/"), TEXT("UEMCPCkGraphPicTest"));
	const FString DiffDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::DiffDir(), TEXT("UE_MCP_CkDiffTest")));
	ON_SCOPE_EXIT { IFileManager::Get().DeleteDirectory(*DiffDir, false, true); };

	UPackage* Package = CreatePackage(TEXT("/UEMCPCkGraphPicTest/BP_CkGraphPic"));
	UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(AActor::StaticClass(), Package, TEXT("BP_CkGraphPic"), BPTYPE_Normal,
		UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
	if (!TestNotNull(TEXT("the Blueprint is created"), Blueprint)) return false;
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	FString Reason;
	if (!TestTrue(TEXT("first version saves"), SaveAssetPackageChecked(Blueprint, Reason))) { AddError(Reason); return false; }
	const FString Before = CkDiffTestSnapshot(Package, TEXT("BP_CkGraphPic-before"));

	// Only the event graph changes: a Print String node, as in the report (#1569).
	UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
	if (!TestNotNull(TEXT("the Blueprint has an event graph"), Graph)) return false;
	UK2Node_CallFunction* Call = NewObject<UK2Node_CallFunction>(Graph);
	Call->CreateNewGuid();
	Call->FunctionReference.SetExternalMember(GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, PrintString), UKismetSystemLibrary::StaticClass());
	Call->NodePosX = 400;
	Call->NodePosY = 200;
	Graph->AddNode(Call, false, false);
	Call->PostPlacedNewNode();
	Call->AllocateDefaultPins();
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	if (!TestTrue(TEXT("second version saves"), SaveAssetPackageChecked(Blueprint, Reason))) { AddError(Reason); return false; }
	const FString After = CkDiffTestSnapshot(Package, TEXT("BP_CkGraphPic-after"));

	FString Original;
	FPackageName::TryConvertLongPackageNameToFilename(Package->GetName(), Original, FPackageName::GetAssetPackageExtension());
	FMCPHandlerRegistry Registry;
	FDiffHandlers::RegisterHandlers(Registry);
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("originalFile"), Original);
	Params->SetStringField(TEXT("fromFile"), Before);
	Params->SetStringField(TEXT("toFile"), After);
	Params->SetStringField(TEXT("fromGraphPicture"), DiffDir / TEXT("graph-before.png"));
	Params->SetStringField(TEXT("toGraphPicture"), DiffDir / TEXT("graph-after.png"));
	const TSharedPtr<FJsonObject> Diff = CkDiffTestCall(*this, Registry, TEXT("checkpoint_diff_asset"), Params);
	if (!Diff.IsValid() || !TestTrue(TEXT("diff succeeds"), Diff->GetBoolField(TEXT("success")))) return false;

	// Both versions of the event graph are drawn, and the two pictures differ where the node was added.
	IImageWrapperModule& Images = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
	TArray<uint8> Raws[2];
	int32 Index = 0;
	for (const TCHAR* Side : { TEXT("before"), TEXT("after") })
	{
		const TSharedPtr<FJsonObject>* Pic = nullptr;
		if (!TestTrue(FString::Printf(TEXT("%s has a graph picture"), Side), Diff->GetObjectField(Side)->TryGetObjectField(TEXT("graphPicture"), Pic))) return false;
		TestFalse(FString::Printf(TEXT("%s names the graph it pictures"), Side), (*Pic)->GetStringField(TEXT("graph")).IsEmpty());
		FString File;
		if (!(*Pic)->TryGetStringField(TEXT("file"), File))
		{
			AddError(FString::Printf(TEXT("%s graph picture: %s"), Side, *(*Pic)->GetStringField(TEXT("error"))));
			return false;
		}
		TArray<uint8> Png;
		TSharedPtr<IImageWrapper> Reader = Images.CreateImageWrapper(EImageFormat::PNG);
		const bool bDecoded = FFileHelper::LoadFileToArray(Png, *File)
			&& Reader.IsValid() && Reader->SetCompressed(Png.GetData(), Png.Num()) && Reader->GetRaw(ERGBFormat::BGRA, 8, Raws[Index]);
		if (!TestTrue(FString::Printf(TEXT("%s graph picture decodes"), Side), bDecoded)) return false;
		TestFalse(FString::Printf(TEXT("%s graph picture shows something"), Side),
			MCPCkDiffIsBlankImage(Raws[Index], int32(Reader->GetWidth()), int32(Reader->GetHeight())));
		++Index;
	}
	TestTrue(TEXT("after names the added node"), Diff->GetObjectField(TEXT("after"))->GetObjectField(TEXT("graphPicture"))->GetNumberField(TEXT("changedNodes")) >= 1);
	TestTrue(TEXT("the two versions are drawn differently"), Raws[0] != Raws[1]);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
