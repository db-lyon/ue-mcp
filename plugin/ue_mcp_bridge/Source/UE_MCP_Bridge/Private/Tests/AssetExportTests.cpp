// Export handler coverage uses a private temp mount for assets and output
// files. It never writes into the project attached to the automation runner.

#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Handlers/Asset/AssetHandlers.h"
#include "Tests/MCPDataTableTestTypes.h"
#include "Tests/MCPScopedTestMount.h"

#include "Engine/CompositeDataTable.h"
#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"

namespace UEMCPAssetExportTests
{
	UDataTable* MakeTable(const FString& PackageName, const TCHAR* Name)
	{
		UDataTable* Table = NewObject<UDataTable>(CreatePackage(*PackageName), FName(Name), RF_Public | RF_Standalone);
		Table->RowStruct = FUEMCPDataTableReferenceRow::StaticStruct();
		return Table;
	}

	void AddRow(UDataTable* Table, const TCHAR* Name, int32 Count)
	{
		FUEMCPDataTableReferenceRow Row;
		Row.Count = Count;
		Table->AddRow(FName(Name), Row);
	}

	TSharedPtr<FJsonObject> Export(FMCPHandlerRegistry& Registry, UObject* Asset, const FString& OutputPath, const FString& Format = FString())
	{
		auto Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("assetPath"), Asset->GetPathName());
		Params->SetStringField(TEXT("outputPath"), OutputPath);
		if (!Format.IsEmpty()) Params->SetStringField(TEXT("format"), Format);
		const TSharedPtr<FJsonValue> Response = Registry.ExecuteHandler(TEXT("export_asset"), Params);
		return Response.IsValid() && Response->Type == EJson::Object ? Response->AsObject() : nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPDataTableExportFormatTest,
	"UE.MCP.Asset.Export.DataTableFormats",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPDataTableExportFormatTest::RunTest(const FString& Parameters)
{
	const FMCPScopedTestMount Mount{ TEXT("/UEMCPAssetExportFormatTest/"), TEXT("UEMCPAssetExportFormatTest") };
	UDataTable* Table = UEMCPAssetExportTests::MakeTable(Mount.RootPath + TEXT("DT_Formats"), TEXT("DT_Formats"));
	const FGCRootScope KeepTableAlive(Table);
	UEMCPAssetExportTests::AddRow(Table, TEXT("First"), 7);
	UEMCPAssetExportTests::AddRow(Table, TEXT("Caf\u00e9"), 13);

	FMCPHandlerRegistry Registry;
	FAssetHandlers::RegisterHandlers(Registry);
	struct FCase { const TCHAR* File; const TCHAR* Format; const TCHAR* Expected; };
	const FCase Cases[] = {
		{ TEXT("nested/inferred.json"), TEXT(""), TEXT("json") },
		{ TEXT("nested/inferred.csv"), TEXT(""), TEXT("csv") },
		{ TEXT("nested/upper.CSV"), TEXT(""), TEXT("csv") },
		{ TEXT("nested/default.txt"), TEXT(""), TEXT("json") },
		{ TEXT("nested/no_extension"), TEXT(""), TEXT("json") },
		{ TEXT("nested/json_override.csv"), TEXT("json"), TEXT("json") },
		{ TEXT("nested/csv_override.json"), TEXT("csv"), TEXT("csv") },
	};
	for (const FCase& Case : Cases)
	{
		const FString File = FPaths::Combine(Mount.ContentPath, Case.File);
		const auto Result = UEMCPAssetExportTests::Export(Registry, Table, File, Case.Format);
		if (!TestTrue(FString::Printf(TEXT("export %s succeeded"), Case.File), Result.IsValid() && Result->GetBoolField(TEXT("success")))) return false;
		TestEqual(TEXT("format selected"), Result->GetStringField(TEXT("format")), FString(Case.Expected));
		TestEqual(TEXT("asset path returned"), Result->GetStringField(TEXT("assetPath")), Table->GetPathName());
		TestEqual(TEXT("output path returned"), Result->GetStringField(TEXT("outputPath")), File);
		TestEqual(TEXT("all rows counted"), Result->GetNumberField(TEXT("rowCount")), 2.0);
		TestTrue(TEXT("new file marked created"), Result->GetBoolField(TEXT("created")));
		TestFalse(TEXT("rows are not returned inline"), Result->HasField(TEXT("rows")));
		TestFalse(TEXT("row names are not returned inline"), Result->HasField(TEXT("rowNames")));

		FString Contents;
		if (!TestTrue(TEXT("export file exists and can be read"), FFileHelper::LoadFileToString(Contents, *File))) return false;
		const bool bCSV = FString(Case.Expected) == TEXT("csv");
		TestEqual(TEXT("engine serializer's complete output written"), Contents, bCSV
			? Table->GetTableAsCSV() : Table->GetTableAsJSON(EDataTableExportFlags::UseJsonObjectsForStructs));
		const FTCHARToUTF8 UTF8(*Contents);
		TestEqual(TEXT("bytes report UTF-8 file size without BOM"), Result->GetNumberField(TEXT("bytes")), static_cast<double>(UTF8.Length()));
		TestEqual(TEXT("reported size equals the actual file size"), Result->GetNumberField(TEXT("bytes")), static_cast<double>(IFileManager::Get().FileSize(*File)));
		if (!bCSV)
		{
			TArray<TSharedPtr<FJsonValue>> Rows;
			if (!TestTrue(TEXT("JSON is a valid row array"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Contents), Rows))) return false;
			TestEqual(TEXT("two rows serialized"), Rows.Num(), 2);
		}
	}

	const FString OverwriteFile = FPaths::Combine(Mount.ContentPath, TEXT("nested/inferred.json"));
	UEMCPAssetExportTests::AddRow(Table, TEXT("Third"), 21);
	const auto Overwrite = UEMCPAssetExportTests::Export(Registry, Table, OverwriteFile);
	if (!TestTrue(TEXT("existing output overwritten"), Overwrite.IsValid() && Overwrite->GetBoolField(TEXT("success")))) return false;
	TestTrue(TEXT("overwrite marked updated"), Overwrite->GetBoolField(TEXT("updated")));
	TestTrue(TEXT("overwrite reported"), Overwrite->GetBoolField(TEXT("overwroteExistingFile")));
	TestEqual(TEXT("new row count reported"), Overwrite->GetNumberField(TEXT("rowCount")), 3.0);
	FString Rewritten;
	TestTrue(TEXT("rewritten file readable"), FFileHelper::LoadFileToString(Rewritten, *OverwriteFile));
	TestTrue(TEXT("new row reached disk"), Rewritten.Contains(TEXT("Third")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPCompositeDataTableExportTest,
	"UE.MCP.Asset.Export.CompositeDataTableMergedRows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPCompositeDataTableExportTest::RunTest(const FString& Parameters)
{
	const FMCPScopedTestMount Mount{ TEXT("/UEMCPCompositeExportTest/"), TEXT("UEMCPCompositeExportTest") };
	UDataTable* First = UEMCPAssetExportTests::MakeTable(Mount.RootPath + TEXT("DT_First"), TEXT("DT_First"));
	const FGCRootScope KeepFirstAlive(First);
	UEMCPAssetExportTests::AddRow(First, TEXT("Shared"), 7);
	UEMCPAssetExportTests::AddRow(First, TEXT("FirstOnly"), 13);
	UDataTable* Second = UEMCPAssetExportTests::MakeTable(Mount.RootPath + TEXT("DT_Second"), TEXT("DT_Second"));
	const FGCRootScope KeepSecondAlive(Second);
	UEMCPAssetExportTests::AddRow(Second, TEXT("Shared"), 21);
	UEMCPAssetExportTests::AddRow(Second, TEXT("SecondOnly"), 34);
	UCompositeDataTable* Composite = NewObject<UCompositeDataTable>(
		CreatePackage(*(Mount.RootPath + TEXT("DT_Composite"))), TEXT("DT_Composite"), RF_Public | RF_Standalone);
	const FGCRootScope KeepCompositeAlive(Composite);
	Composite->RowStruct = FUEMCPDataTableReferenceRow::StaticStruct();
	Composite->AddParentTable(First);
	Composite->AddParentTable(Second);

	FMCPHandlerRegistry Registry;
	FAssetHandlers::RegisterHandlers(Registry);
	for (const TCHAR* Format : { TEXT("json"), TEXT("csv") })
	{
		const FString File = FPaths::Combine(Mount.ContentPath, FString(TEXT("composite.")) + Format);
		const auto Result = UEMCPAssetExportTests::Export(Registry, Composite, File);
		if (!TestTrue(TEXT("composite export succeeded"), Result.IsValid() && Result->GetBoolField(TEXT("success")))) return false;
		TestEqual(TEXT("composite format selected"), Result->GetStringField(TEXT("format")), FString(Format));
		TestEqual(TEXT("count reflects merged rows"), Result->GetNumberField(TEXT("rowCount")), 3.0);
		FString Contents;
		if (!TestTrue(TEXT("composite file readable"), FFileHelper::LoadFileToString(Contents, *File))) return false;
		TestTrue(TEXT("first parent's row exported"), Contents.Contains(TEXT("FirstOnly")));
		TestTrue(TEXT("second parent's row exported"), Contents.Contains(TEXT("SecondOnly")));
		TestTrue(TEXT("overriding row value exported"), Contents.Contains(TEXT("21")));
		TestFalse(TEXT("overridden row value absent"), Contents.Contains(TEXT("7")));
		TestEqual(TEXT("composite bytes reflect disk"), Result->GetNumberField(TEXT("bytes")), static_cast<double>(IFileManager::Get().FileSize(*File)));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPDataTableExportValidationTest,
	"UE.MCP.Asset.Export.DataTableValidationAndEmptyRows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPDataTableExportValidationTest::RunTest(const FString& Parameters)
{
	const FMCPScopedTestMount Mount{ TEXT("/UEMCPExportValidationTest/"), TEXT("UEMCPExportValidationTest") };
	UDataTable* Table = UEMCPAssetExportTests::MakeTable(Mount.RootPath + TEXT("DT_Empty"), TEXT("DT_Empty"));
	const FGCRootScope KeepTableAlive(Table);
	FMCPHandlerRegistry Registry;
	FAssetHandlers::RegisterHandlers(Registry);
	const FString File = FPaths::Combine(Mount.ContentPath, TEXT("empty.json"));
	const auto Empty = UEMCPAssetExportTests::Export(Registry, Table, File);
	if (!TestTrue(TEXT("empty table export succeeded"), Empty.IsValid() && Empty->GetBoolField(TEXT("success")))) return false;
	TestEqual(TEXT("empty table count"), Empty->GetNumberField(TEXT("rowCount")), 0.0);
	FString Before;
	TestTrue(TEXT("empty JSON file readable"), FFileHelper::LoadFileToString(Before, *File));
	TArray<TSharedPtr<FJsonValue>> Rows;
	TestTrue(TEXT("empty JSON parses"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Before), Rows));
	TestEqual(TEXT("empty array exported"), Rows.Num(), 0);

	const auto Invalid = UEMCPAssetExportTests::Export(Registry, Table, File, TEXT("xml"));
	if (!TestTrue(TEXT("invalid format rejected"), Invalid.IsValid() && !Invalid->GetBoolField(TEXT("success")))) return false;
	TestTrue(TEXT("invalid format explains accepted values"), Invalid->GetStringField(TEXT("error")).Contains(TEXT("'json' or 'csv'")));
	FString After;
	TestTrue(TEXT("existing output remains readable"), FFileHelper::LoadFileToString(After, *File));
	TestEqual(TEXT("invalid format leaves existing output intact"), After, Before);

	Table->RowStruct = nullptr;
	const FString MissingStructFile = FPaths::Combine(Mount.ContentPath, TEXT("invalid/no_struct.csv"));
	const auto MissingStruct = UEMCPAssetExportTests::Export(Registry, Table, MissingStructFile);
	if (!TestTrue(TEXT("missing row struct rejected"), MissingStruct.IsValid() && !MissingStruct->GetBoolField(TEXT("success")))) return false;
	TestFalse(TEXT("no file written for invalid table"), IFileManager::Get().FileExists(*MissingStructFile));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPAssetExportRegisteredExporterTest,
	"UE.MCP.Asset.Export.RegisteredTextureExporter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPAssetExportRegisteredExporterTest::RunTest(const FString& Parameters)
{
	const FMCPScopedTestMount Mount{ TEXT("/UEMCPTextureExportTest/"), TEXT("UEMCPTextureExportTest") };
	UTexture2D* Texture = NewObject<UTexture2D>(
		CreatePackage(*(Mount.RootPath + TEXT("T_Probe"))), TEXT("T_Probe"), RF_Public | RF_Standalone);
	const FGCRootScope KeepTextureAlive(Texture);
	const FColor Pixel(32, 64, 128, 255);
	Texture->Source.Init(1, 1, 1, 1, TSF_BGRA8, reinterpret_cast<const uint8*>(&Pixel));
	FMCPHandlerRegistry Registry;
	FAssetHandlers::RegisterHandlers(Registry);
	const FString File = FPaths::Combine(Mount.ContentPath, TEXT("nested/probe.png"));
	// format is only meaningful for DataTables. A PNG still goes through the
	// registered texture exporter even when a table format is passed.
	const auto Result = UEMCPAssetExportTests::Export(Registry, Texture, File, TEXT("csv"));
	if (!TestTrue(TEXT("registered texture exporter succeeded"), Result.IsValid() && Result->GetBoolField(TEXT("success")))) return false;
	TestEqual(TEXT("asset class preserved"), Result->GetStringField(TEXT("assetClass")), FString(TEXT("Texture2D")));
	TestFalse(TEXT("no table format added to old response"), Result->HasField(TEXT("format")));
	TestFalse(TEXT("no table count added to old response"), Result->HasField(TEXT("rowCount")));
	TArray<uint8> Bytes;
	if (!TestTrue(TEXT("PNG written"), FFileHelper::LoadFileToArray(Bytes, *File))) return false;
	const uint8 Signature[] = { 137, 80, 78, 71, 13, 10, 26, 10 };
	TestTrue(TEXT("output is a PNG from the existing exporter"), Bytes.Num() >= 8 && FMemory::Memcmp(Bytes.GetData(), Signature, 8) == 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
