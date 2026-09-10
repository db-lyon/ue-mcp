// Regression coverage for #932.
//
// blueprint(reparent) reparents, recompiles AND saves, and the save is not
// optional. A .uasset that was never checked out of source control is read-only
// on disk; asking the engine to write it turned the failed save into a FATAL
// error that took the whole editor process down. The asset was undamaged and
// the call replayed cleanly after a checkout, so the only thing missing was the
// question asked here: can this package be written at all.
//
// The answer lives in one place, MCPPackageWriteBlocked, and SaveAssetPackage
// asks it before handing the file to UPackage::SavePackage. That is what makes
// the crash unreachable from every handler that saves as a side effect, not
// just from reparent. These assertions cover the guard itself and the three
// shapes built on it.
//
// The exercise happens under a private mount point in the system temp area:
// run_automation_tests dispatches every EditorContext test against whatever
// project the bridge is attached to, and nothing here may touch it.

#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerUtils.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/DataTable.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "MCPEngineCompat.h"

namespace
{
	const TCHAR* const MCPWriteGuardTestRoot = TEXT("/UEMCPPackageWriteGuardTest/");

	struct FScopedWriteGuardMount
	{
		FString RootPath;
		FString ContentPath;

		FScopedWriteGuardMount()
			: RootPath(MCPWriteGuardTestRoot)
			, ContentPath(FPaths::Combine(
				FPaths::ConvertRelativePathToFull(FString(FPlatformProcess::UserTempDir())),
				FString(TEXT("UEMCPPackageWriteGuardTest")),
				FGuid::NewGuid().ToString(EGuidFormats::Digits)))
		{
			IFileManager::Get().MakeDirectory(*ContentPath, /*Tree=*/true);
			FPackageName::RegisterMountPoint(RootPath, ContentPath);
		}

		~FScopedWriteGuardMount()
		{
			FPackageName::UnRegisterMountPoint(RootPath, ContentPath);
			IFileManager::Get().DeleteDirectory(*ContentPath, /*RequireExists=*/false, /*Tree=*/true);
		}

		FScopedWriteGuardMount(const FScopedWriteGuardMount&) = delete;
		FScopedWriteGuardMount& operator=(const FScopedWriteGuardMount&) = delete;
	};

	/** Clear the read-only bit again however the test leaves, so the temp
	 *  directory can be deleted on the way out. */
	struct FScopedReadOnlyFile
	{
		FString FileName;
		bool bApplied = false;

		explicit FScopedReadOnlyFile(const FString& InFileName)
			: FileName(InFileName)
		{
			bApplied = FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*FileName, true);
		}

		~FScopedReadOnlyFile()
		{
			if (bApplied)
			{
				FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*FileName, false);
			}
		}

		FScopedReadOnlyFile(const FScopedReadOnlyFile&) = delete;
		FScopedReadOnlyFile& operator=(const FScopedReadOnlyFile&) = delete;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPReadOnlyPackageIsRefusedNotWrittenTest,
	"UE.MCP.Asset.Save.ReadOnlyPackageIsRefusedBeforeTheWrite",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPReadOnlyPackageIsRefusedNotWrittenTest::RunTest(const FString& Parameters)
{
	const FScopedWriteGuardMount Mount;

	const FString PackageName = FString(MCPWriteGuardTestRoot) + TEXT("DT_WriteGuardProbe");
	UPackage* Package = CreatePackage(*PackageName);
	if (!TestNotNull(TEXT("probe package created"), Package)) return false;

	UDataTable* Probe = NewObject<UDataTable>(
		Package, FName(TEXT("DT_WriteGuardProbe")), RF_Public | RF_Standalone);
	if (!TestNotNull(TEXT("probe asset created"), Probe))
	{
		Package->SetDirtyFlag(false);
		return false;
	}
	Probe->RowStruct = FPerPlatformInt::StaticStruct();
	const FGCRootScope KeepProbeAlive(Probe);

	// A writable package is not blocked, and the first save is what puts a file
	// on disk for the read-only half below to act on.
	{
		FString Reason;
		TestFalse(TEXT("a writable package is not blocked"), MCPPackageWriteBlocked(Probe, Reason));
		TestTrue(TEXT("no reason is given when nothing is blocked"), Reason.IsEmpty());
		TestTrue(TEXT("the writable package saves"), SaveAssetPackage(Probe));
	}

	FString PackageFileName;
	if (!TestTrue(TEXT("the probe package resolves to a file"),
		ResolvePackageFileName(Package, PackageFileName)))
	{
		Package->SetDirtyFlag(false);
		return false;
	}
	if (!TestTrue(TEXT("the probe package file exists on disk"),
		IFileManager::Get().FileExists(*PackageFileName)))
	{
		Package->SetDirtyFlag(false);
		return false;
	}

	{
		const FScopedReadOnlyFile ReadOnly(PackageFileName);
		if (!TestTrue(TEXT("the probe package file could be made read-only"), ReadOnly.bApplied))
		{
			Package->SetDirtyFlag(false);
			return false;
		}

		FString Reason;
		TestTrue(TEXT("a read-only package is blocked"), MCPPackageWriteBlocked(Probe, Reason));
		TestTrue(TEXT("the block names the file on disk"), Reason.Contains(PackageFileName));
		TestTrue(TEXT("the block says what to do about it"),
			Reason.Contains(TEXT("read-only")));

		// The #932 assertion. Before the guard this call reached
		// UPackage::SavePackage on a file it could not open, and the process
		// went down with it. A false is the whole point.
		TestFalse(TEXT("saving a read-only package returns false rather than killing the editor"),
			SaveAssetPackage(Probe));

		FString CheckedReason;
		TestFalse(TEXT("the checked save agrees"), SaveAssetPackageChecked(Probe, CheckedReason));
		TestFalse(TEXT("the checked save carries the reason"), CheckedReason.IsEmpty());

		// The refusal a handler returns instead of mutating anything.
		const TSharedPtr<FJsonValue> Blocked =
			MCPAssetWriteBlockedError(Probe, PackageName, TEXT("reparent this Blueprint"));
		if (TestTrue(TEXT("a blocked write produces a refusal"), Blocked.IsValid()))
		{
			const TSharedPtr<FJsonObject> Obj = Blocked->AsObject();
			if (TestTrue(TEXT("the refusal is an object"), Obj.IsValid()))
			{
				bool bSuccess = true;
				Obj->TryGetBoolField(TEXT("success"), bSuccess);
				TestFalse(TEXT("the refusal is not a success"), bSuccess);

				FString ReasonField;
				Obj->TryGetStringField(TEXT("reason"), ReasonField);
				TestEqual(TEXT("the refusal names the cause"),
					ReasonField, FString(TEXT("package_not_writable")));

				FString ErrorField;
				Obj->TryGetStringField(TEXT("error"), ErrorField);
				TestTrue(TEXT("the refusal names the operation the caller asked for"),
					ErrorField.Contains(TEXT("reparent this Blueprint")));
				TestTrue(TEXT("the refusal says nothing was changed"),
					ErrorField.Contains(TEXT("Nothing was changed")));
			}
		}
	}

	// With the read-only bit cleared again the same package writes, so the
	// guard gates on the file's actual state rather than latching.
	{
		FString Reason;
		TestFalse(TEXT("a package that is writable again is not blocked"),
			MCPPackageWriteBlocked(Probe, Reason));
		TestTrue(TEXT("the package saves once it is writable again"), SaveAssetPackage(Probe));
	}

	Package->SetDirtyFlag(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPSaveOutcomeIsReportedTest,
	"UE.MCP.Asset.Save.AnUnwrittenSaveIsReportedAsAFailure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPSaveOutcomeIsReportedTest::RunTest(const FString& Parameters)
{
	// A save that did not reach disk used to read as a plain success, and the
	// caller's next read came off the in-memory object and looked correct right
	// up until the editor restarted.
	{
		TSharedPtr<FJsonObject> Result = MCPSuccess();
		MCPNoteSaveOutcome(Result, TEXT("/Game/Probe/BP_Thing"), /*bSaved=*/true, FString());

		bool bSaved = false;
		Result->TryGetBoolField(TEXT("saved"), bSaved);
		TestTrue(TEXT("a written save is reported as saved"), bSaved);

		bool bSuccess = false;
		Result->TryGetBoolField(TEXT("success"), bSuccess);
		TestTrue(TEXT("a written save stays a success"), bSuccess);

		TestFalse(TEXT("a written save adds no error"), Result->HasField(TEXT("saveError")));
	}

	{
		TSharedPtr<FJsonObject> Result = MCPSuccess();
		MCPNoteSaveOutcome(Result, TEXT("/Game/Probe/BP_Thing"), /*bSaved=*/false,
			TEXT("'D:/Probe/BP_Thing.uasset' is read-only on disk."));

		bool bSaved = true;
		Result->TryGetBoolField(TEXT("saved"), bSaved);
		TestFalse(TEXT("an unwritten save is reported as unsaved"), bSaved);

		bool bSuccess = true;
		Result->TryGetBoolField(TEXT("success"), bSuccess);
		TestFalse(TEXT("an unwritten save is not a success"), bSuccess);

		FString SaveError;
		Result->TryGetStringField(TEXT("saveError"), SaveError);
		TestTrue(TEXT("an unwritten save carries the reason"), SaveError.Contains(TEXT("read-only")));

		FString Error;
		Result->TryGetStringField(TEXT("error"), Error);
		TestTrue(TEXT("the error names the asset"), Error.Contains(TEXT("/Game/Probe/BP_Thing")));
	}

	return true;
}

#endif
