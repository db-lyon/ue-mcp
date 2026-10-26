#if WITH_DEV_AUTOMATION_TESTS

#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "HandlerPackageSave.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Handlers/Diff/DiffHandlers.h"
#include "MCPScopedTestMount.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Misc/AutomationTest.h"
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
	const TSharedPtr<FJsonObject> Opened = CkDiffTestCall(*this, Registry, TEXT("checkpoint_open_native_diff"), Params);
	if (!Opened.IsValid() || !TestTrue(TEXT("native diff opens"), Opened->GetBoolField(TEXT("success")))) return false;
	TestEqual(TEXT("it is a window"), Opened->GetStringField(TEXT("opened")), FString(TEXT("window")));
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

#endif // WITH_DEV_AUTOMATION_TESTS
