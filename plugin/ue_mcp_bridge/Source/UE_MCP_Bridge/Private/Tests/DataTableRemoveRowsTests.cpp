// remove_datatable_rows on a table whose row struct is native (#1999).
//
// On UE 5.6, UDataTable::RemoveRow on such a table asserted in
// HandleDataTableChanged and took the editor down. These tests remove rows
// from a native-row table, check that every row not named keeps its place and
// value, that missing names are reported, that a dry run changes nothing, that
// editor undo brings the rows back, and that a kept row changed during the
// removal makes the call restore the whole table. Everything is unsaved and
// under a private mount.

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/MCPDataTableTestTypes.h"

#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Handlers/Asset/AssetHandlers.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Engine/DataTable.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"
#include "Tests/MCPScopedTestMount.h"

namespace UEMCPRemoveRowsTest
{
	const TCHAR* const Root = TEXT("/UEMCPDataTableRemoveRowsTest/");

	/** A native-row table under the private mount, rows Row1..RowN with Value = index. */
	UDataTable* MakeTable(const TCHAR* Name, int32 RowCount)
	{
		UPackage* Package = CreatePackage(*(FString(Root) + Name));
		if (!Package) return nullptr;
		UDataTable* Table = NewObject<UDataTable>(Package, FName(Name), RF_Public | RF_Standalone | RF_Transactional);
		Table->RowStruct = FUEMCPDataTableDerivedRow::StaticStruct();
		for (int32 Index = 1; Index <= RowCount; ++Index)
		{
			FUEMCPDataTableDerivedRow Row;
			Row.Value = Index;
			Row.Derived = Index * 2;
			Table->AddRow(FName(*FString::Printf(TEXT("Row%d"), Index)), Row);
		}
		return Table;
	}

	TSharedPtr<FJsonObject> Remove(FMCPHandlerRegistry& Registry, const UDataTable* Table,
		std::initializer_list<const TCHAR*> Names, bool bDryRun = false)
	{
		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("assetPath"), Table->GetPathName());
		TArray<TSharedPtr<FJsonValue>> Values;
		for (const TCHAR* Name : Names) Values.Add(MakeShared<FJsonValueString>(Name));
		Params->SetArrayField(TEXT("rowNames"), Values);
		Params->SetBoolField(TEXT("save"), false);
		if (bDryRun) Params->SetBoolField(TEXT("dryRun"), true);
		const TSharedPtr<FJsonValue> Response = Registry.ExecuteHandler(TEXT("remove_datatable_rows"), Params);
		return (Response.IsValid() && Response->Type == EJson::Object) ? Response->AsObject() : MakeShared<FJsonObject>();
	}

	bool Flag(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, bool bDefault = false)
	{
		bool bValue = bDefault;
		Object->TryGetBoolField(Field, bValue);
		return bValue;
	}

	FString Text(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		FString Value;
		Object->TryGetStringField(Field, Value);
		return Value;
	}

	FString Names(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		TArray<FString> Out;
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (Object->TryGetArrayField(Field, Values))
			for (const TSharedPtr<FJsonValue>& Value : *Values) Out.Add(Value->AsString());
		return FString::Join(Out, TEXT(","));
	}

	FString RowOrder(const UDataTable* Table)
	{
		TArray<FString> Out;
		for (const TPair<FName, uint8*>& Row : Table->GetRowMap()) Out.Add(Row.Key.ToString());
		return FString::Join(Out, TEXT(","));
	}

	FUEMCPDataTableDerivedRow* Find(UDataTable* Table, const TCHAR* Name)
	{
		uint8* const* Found = Table->GetRowMap().Find(FName(Name));
		return (Found && *Found) ? reinterpret_cast<FUEMCPDataTableDerivedRow*>(*Found) : nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDataTableRemoveRowsNativeTest,
	"UE.MCP.Asset.DataTable.RemoveRowsKeepsEveryOtherRow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDataTableRemoveRowsNativeTest::RunTest(const FString& Parameters)
{
	using namespace UEMCPRemoveRowsTest;
	const FMCPScopedTestMount Mount{ FString(Root), TEXT("UEMCPDataTableRemoveRowsTest") };
	UDataTable* Table = MakeTable(TEXT("DT_RemoveRows"), 5);
	if (!TestNotNull(TEXT("fixture table created"), Table)) return false;
	const FGCRootScope KeepAlive(Table);
	ON_SCOPE_EXIT { Table->GetOutermost()->SetDirtyFlag(false); };
	// A table-wide notification would recompute this; removal must not send one.
	Find(Table, TEXT("Row4"))->Derived = -1;

	FMCPHandlerRegistry Registry;
	FAssetHandlers::RegisterHandlers(Registry);
	TestTrue(TEXT("remove_datatable_rows is registered"), Registry.HasHandler(TEXT("remove_datatable_rows")));

	// A dry run names what would go and what is missing, and changes nothing.
	{
		const TSharedPtr<FJsonObject> Preview = Remove(Registry, Table, { TEXT("Row2"), TEXT("Nope"), TEXT("row5") }, true);
		TestTrue(FString::Printf(TEXT("dry run succeeds (%s)"), *Text(Preview, TEXT("error"))), Flag(Preview, TEXT("success")));
		TestEqual(TEXT("dry run lists the rows it would remove in table order"), Names(Preview, TEXT("wouldRemove")), FString(TEXT("Row2,Row5")));
		TestEqual(TEXT("dry run lists the missing name"), Names(Preview, TEXT("missingRowNames")), FString(TEXT("Nope")));
		double After = -1;
		Preview->TryGetNumberField(TEXT("rowCountAfter"), After);
		TestEqual(TEXT("dry run predicts three rows left"), static_cast<int32>(After), 3);
		TestEqual(TEXT("dry run leaves the table alone"), RowOrder(Table), FString(TEXT("Row1,Row2,Row3,Row4,Row5")));
	}

	// The real call removes the named rows and only those.
	{
		const TSharedPtr<FJsonObject> Removed = Remove(Registry, Table, { TEXT("Row2"), TEXT("Nope"), TEXT("row5") });
		TestTrue(FString::Printf(TEXT("removal succeeds (%s)"), *Text(Removed, TEXT("error"))), Flag(Removed, TEXT("success")));
		TestEqual(TEXT("removedRows uses the table's spelling"), Names(Removed, TEXT("removedRows")), FString(TEXT("Row2,Row5")));
		TestEqual(TEXT("missingRowNames lists the absent name"), Names(Removed, TEXT("missingRowNames")), FString(TEXT("Nope")));
		TestEqual(TEXT("the kept rows stay in order"), RowOrder(Table), FString(TEXT("Row1,Row3,Row4")));
		if (FUEMCPDataTableDerivedRow* Row3 = Find(Table, TEXT("Row3"))) TestEqual(TEXT("Row3 keeps its value"), Row3->Value, 3);
		if (FUEMCPDataTableDerivedRow* Row4 = Find(Table, TEXT("Row4")))
			TestEqual(TEXT("no row hook ran on a kept row"), Row4->Derived, -1);
		TestFalse(TEXT("save=false keeps the removal in memory"), Flag(Removed, TEXT("saved"), true));
		const TSharedPtr<FJsonObject>* Rollback = nullptr;
		const TSharedPtr<FJsonObject>* Payload = nullptr;
		const TSharedPtr<FJsonObject>* Rows = nullptr;
		if (TestTrue(TEXT("removal carries a rollback"), Removed->TryGetObjectField(TEXT("rollback"), Rollback))
			&& TestTrue(TEXT("rollback has a payload"), (*Rollback)->TryGetObjectField(TEXT("payload"), Payload))
			&& TestTrue(TEXT("rollback payload has rows"), (*Payload)->TryGetObjectField(TEXT("rows"), Rows)))
		{
			TestEqual(TEXT("rollback re-adds through fill_datatable_from_json"), Text(*Rollback, TEXT("method")), FString(TEXT("fill_datatable_from_json")));
			TestTrue(TEXT("rollback carries Row2"), (*Rows)->HasField(TEXT("Row2")));
			TestTrue(TEXT("rollback carries Row5"), (*Rows)->HasField(TEXT("Row5")));
		}
	}

	// Editor undo restores both rows in place.
	if (TestNotNull(TEXT("the editor is available for undo"), GEditor))
	{
		GEditor->UndoTransaction();
		TestEqual(TEXT("undo restores the rows in their places"), RowOrder(Table), FString(TEXT("Row1,Row2,Row3,Row4,Row5")));
		if (FUEMCPDataTableDerivedRow* Row5 = Find(Table, TEXT("Row5"))) TestEqual(TEXT("undo restores Row5's value"), Row5->Value, 5);
	}

	// Names the table lacks entirely remove nothing.
	{
		const TSharedPtr<FJsonObject> Missed = Remove(Registry, Table, { TEXT("Ghost") });
		TestTrue(TEXT("an all-missing call succeeds"), Flag(Missed, TEXT("success")));
		TestTrue(TEXT("it reports alreadyDeleted"), Flag(Missed, TEXT("alreadyDeleted")));
		TestEqual(TEXT("it lists the missing name"), Names(Missed, TEXT("missingRowNames")), FString(TEXT("Ghost")));
		TestEqual(TEXT("it leaves every row"), Table->GetRowMap().Num(), 5);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDataTableRemoveRowsRestoresTest,
	"UE.MCP.Asset.DataTable.RemoveRowsRestoresAChangedTable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDataTableRemoveRowsRestoresTest::RunTest(const FString& Parameters)
{
	using namespace UEMCPRemoveRowsTest;
	const FMCPScopedTestMount Mount{ FString(Root), TEXT("UEMCPDataTableRemoveRowsTest") };
	UDataTable* Table = MakeTable(TEXT("DT_RemoveRowsRestore"), 4);
	if (!TestNotNull(TEXT("fixture table created"), Table)) return false;
	const FGCRootScope KeepAlive(Table);
	ON_SCOPE_EXIT { Table->GetOutermost()->SetDirtyFlag(false); };

	// A listener that edits a kept row the first time the table changes, the
	// way a careless row-list hook could.
	bool bArmed = true;
	const FDelegateHandle Handle = Table->OnDataTableChanged().AddLambda([Table, &bArmed]()
	{
		if (!bArmed) return;
		if (FUEMCPDataTableDerivedRow* Row = Find(Table, TEXT("Row3"))) { Row->Value = 99; bArmed = false; }
	});
	ON_SCOPE_EXIT { Table->OnDataTableChanged().Remove(Handle); };

	FMCPHandlerRegistry Registry;
	FAssetHandlers::RegisterHandlers(Registry);
	const TSharedPtr<FJsonObject> Refused = Remove(Registry, Table, { TEXT("Row1"), TEXT("Row4") });
	TestFalse(TEXT("a changed kept row fails the call"), Flag(Refused, TEXT("success"), true));
	TestTrue(TEXT("the call says it rolled back"), Flag(Refused, TEXT("rolledBack")));
	TestTrue(FString::Printf(TEXT("the error names the changed row (%s)"), *Text(Refused, TEXT("error"))),
		Text(Refused, TEXT("error")).Contains(TEXT("Row3")));
	TestEqual(TEXT("every row is back in its place"), RowOrder(Table), FString(TEXT("Row1,Row2,Row3,Row4")));
	if (FUEMCPDataTableDerivedRow* Row3 = Find(Table, TEXT("Row3"))) TestEqual(TEXT("the changed row has its old value"), Row3->Value, 3);
	if (FUEMCPDataTableDerivedRow* Row1 = Find(Table, TEXT("Row1"))) TestEqual(TEXT("a removed row has its old value"), Row1->Value, 1);
	TestFalse(TEXT("nothing was saved"), Flag(Refused, TEXT("saved"), true));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
