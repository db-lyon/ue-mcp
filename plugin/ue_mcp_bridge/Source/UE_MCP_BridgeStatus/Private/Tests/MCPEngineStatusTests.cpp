#if WITH_DEV_AUTOMATION_TESTS

#include "MCPEngineStatus.h"
#include "CoreGlobals.h"
#include "Dom/JsonObject.h"
#include "HAL/PlatformProcess.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

#if PLATFORM_WINDOWS
#include "Async/Async.h"
#include "HAL/Event.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/ScopeExit.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Windows/WindowsHWrapper.h"
#endif

/**
 * What the status writer puts on disk, and what it refuses to put there.
 *
 * #990: every process that loads this plugin ran a writer thread that wrote
 * `status.json` through one shared `status.json.tmp`. A distributed HLOD build
 * runs 32 WorldPartitionBuilderCommandlet slots against one project directory,
 * so 32 processes deleted and renamed the same temp file four times a second.
 * One run produced 3,743 Error-severity log lines, and every slot ended with
 * "Failure - 112 error(s)" even where the HLOD work succeeded, which breaks
 * error-count success detection for CI.
 *
 * Nothing here binds a port or writes into the live state directory: these are
 * assertions about paths and about the commandlet decision, both of which are
 * pure.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPEngineStatusPathsTest,
	"UE.MCP.BridgeStatus.StatusPaths",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPEngineStatusPathsTest::RunTest(const FString& Parameters)
{
	const FString Shared = FMCPEngineStatus::StatusFilePath();
	const FString Own = FMCPEngineStatus::InstanceStatusFilePath();

	TestEqual(TEXT("the shared path keeps its historical name"),
		FPaths::GetCleanFilename(Shared), FString(TEXT("status.json")));

	// The per-process name is what makes two editors of one project describable
	// at the same time, and what a reader falls back across.
	TestEqual(TEXT("the per-process path is named for this pid"),
		FPaths::GetCleanFilename(Own),
		FString::Printf(TEXT("status.%u.json"), FPlatformProcess::GetCurrentProcessId()));
	TestNotEqual(TEXT("the two are different files"), Own, Shared);
	TestEqual(TEXT("and live in the same directory"), FPaths::GetPath(Own), FPaths::GetPath(Shared));
	TestEqual(TEXT("which is the bridge state directory"),
		FPaths::GetPath(Own), FMCPEngineStatus::StatusDir());

	// The publish decision. An editor publishes; a commandlet does not, because
	// nothing polls a commandlet's engine state and dozens of them share one
	// project directory.
	TestTrue(TEXT("publishing follows the commandlet flag, and only that"),
		FMCPEngineStatus::ShouldPublishStatus() == !IsRunningCommandlet());

	// The snapshot has to name its subject: a document that does not say which
	// process it describes cannot be told apart from one that does.
	const TSharedPtr<FJsonObject> Snapshot = FMCPEngineStatus::Get().Snapshot();
	TestTrue(TEXT("a snapshot exists"), Snapshot.IsValid());
	double Pid = 0.0;
	TestTrue(TEXT("the snapshot names its process"), Snapshot.IsValid() && Snapshot->TryGetNumberField(TEXT("pid"), Pid));
	TestEqual(TEXT("and it is this one"), (int32)Pid, (int32)FPlatformProcess::GetCurrentProcessId());

	return true;
}

#if PLATFORM_WINDOWS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPEngineStatusConcurrentPublicationTest,
	"UE.MCP.BridgeStatus.ConcurrentPublication",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPEngineStatusConcurrentPublicationTest::RunTest(const FString& Parameters)
{
	// A private directory: never race the running editor's real status writer.
	const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectIntermediateDir(),
		TEXT("MCPStatusTests"), FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	const FString Path = FPaths::Combine(Directory, TEXT("status.json"));
	IFileManager& Files = IFileManager::Get();
	if (!TestTrue(TEXT("create isolated status directory"), Files.MakeDirectory(*Directory, true))) return false;
	ON_SCOPE_EXIT { Files.DeleteDirectory(*Directory, false, true); };

	const auto Payload = [](int32 Writer) { return FString::ChrN(2048, static_cast<TCHAR>('A' + Writer)); };
	const auto Document = [&Payload](int32 Writer, int32 Sequence)
	{
		return FString::Printf(TEXT("{\"writer\":%d,\"sequence\":%d,\"payload\":\"%s\"}"), Writer, Sequence, *Payload(Writer));
	};
	const FString Initial = Document(0, 0);
	if (!TestTrue(TEXT("publish to absent path"), FMCPEngineStatus::PublishAtomically(Path, Initial))) return false;

	// Non-sharing readers can deny replacement; failure must preserve the old complete document.
	HANDLE Locked = ::CreateFileW(*Path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (!TestTrue(TEXT("open snapshot without delete sharing"), Locked != INVALID_HANDLE_VALUE)) return false;
	const bool bPublishedWhileLocked = FMCPEngineStatus::PublishAtomically(Path, Document(1, 0));
	::CloseHandle(Locked);
	TestFalse(TEXT("locked destination rejects publication without deletion"), bPublishedWhileLocked);
	FString Readback;
	TestTrue(TEXT("old snapshot remains readable"), FFileHelper::LoadFileToString(Readback, *Path));
	TestEqual(TEXT("failed replacement preserves previous bytes"), Readback, Initial);
	TestTrue(TEXT("publication recovers after reader closes"), FMCPEngineStatus::PublishAtomically(Path, Document(1, 0)));
	TestTrue(TEXT("duplicate separators resolve to the same status path"),
		FMCPEngineStatus::PublishAtomically(Directory + TEXT("//status.json"), Initial));
	TestTrue(TEXT("canonical path reads publication through duplicate separators"), FFileHelper::LoadFileToString(Readback, *Path));
	TestEqual(TEXT("duplicate-separator publication remains complete"), Readback, Initial);

	FEvent* Start = FPlatformProcess::GetSynchEventFromPool(true);
	ON_SCOPE_EXIT { FPlatformProcess::ReturnSynchEventToPool(Start); };
	TArray<TFuture<bool>> Writers;
	for (int32 Writer = 0; Writer < 2; ++Writer)
	{
		Writers.Add(Async(EAsyncExecution::Thread, [Start, Path, Writer, &Document]()
		{
			Start->Wait();
			bool bSucceeded = true;
			for (int32 Sequence = 0; Sequence < 128; ++Sequence)
			{
				bSucceeded &= FMCPEngineStatus::PublishAtomically(Path, Document(Writer, Sequence));
			}
			return bSucceeded;
		}));
	}
	Start->Trigger();
	int32 Reads = 0;
	bool bReadable = true;
	bool bComplete = true;
	const double Deadline = FPlatformTime::Seconds() + 10.0;
	do
	{
		// Sharing delete lets Windows keep this old handle alive while the name changes to a new snapshot.
		HANDLE Reader = ::CreateFileW(*Path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (Reader == INVALID_HANDLE_VALUE)
		{
			bReadable = false;
			break;
		}
		uint8 Bytes[8192];
		DWORD Size = 0;
		const bool bRead = !!::ReadFile(Reader, Bytes, sizeof(Bytes), &Size, nullptr);
		::CloseHandle(Reader);
		FString Text;
		if (bRead) FFileHelper::BufferToString(Text, Bytes, Size);
		TSharedPtr<FJsonObject> Json;
		double Writer = -1;
		double Sequence = -1;
		FString Body;
		bComplete &= bRead && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) && Json.IsValid()
			&& Json->TryGetNumberField(TEXT("writer"), Writer) && (Writer == 0 || Writer == 1)
			&& Json->TryGetNumberField(TEXT("sequence"), Sequence) && Sequence >= 0 && Sequence < 128
			&& Json->TryGetStringField(TEXT("payload"), Body) && Body == Payload(static_cast<int32>(Writer));
		++Reads;
	} while ((!Writers[0].IsReady() || !Writers[1].IsReady()) && FPlatformTime::Seconds() < Deadline);
	TestTrue(TEXT("concurrent publishers finish within ten seconds"), Writers[0].IsReady() && Writers[1].IsReady());
	// Join before any captured locals or the event are released, even if an assertion failed.
	for (TFuture<bool>& Writer : Writers) TestTrue(TEXT("every concurrent publication succeeds"), Writer.Get());
	TestTrue(TEXT("reader observed snapshots during publication"), Reads > 0);
	TestTrue(TEXT("published destination never disappears"), bReadable);
	TestTrue(TEXT("every observed document is complete JSON from one writer"), bComplete);
	TArray<FString> Temporaries;
	Files.FindFiles(Temporaries, *FPaths::Combine(Directory, TEXT("*.tmp")), true, false);
	TestEqual(TEXT("success and failure remove their own temporary files"), Temporaries.Num(), 0);

	// Keep the long-path support that Unreal's generic file operations provided.
	const FString LongDirectory = FPaths::Combine(Directory, FString::ChrN(220, TEXT('L')));
	const FString LongPath = FPaths::Combine(LongDirectory, TEXT("status.json"));
	TestTrue(TEXT("create long status directory"), Files.MakeDirectory(*LongDirectory, true));
	TestTrue(TEXT("publish beyond MAX_PATH"), FMCPEngineStatus::PublishAtomically(LongPath, Initial));
	TestTrue(TEXT("read long-path publication"), FFileHelper::LoadFileToString(Readback, *LongPath));
	TestEqual(TEXT("long-path publication remains complete"), Readback, Initial);
	return true;
}
#endif // PLATFORM_WINDOWS

#endif // WITH_DEV_AUTOMATION_TESTS
