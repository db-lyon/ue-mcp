#if WITH_DEV_AUTOMATION_TESTS
#include "CoreMinimal.h"
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8

#include "Handlers/Fab/FabDownloads.h"
#include "Handlers/Fab/FabHandlers.h"
#include "HandlerRegistry.h"
#include "BuildPatchSettings.h"
#include "Interfaces/IBuildPatchServicesModule.h"
#include "Interfaces/IDirectoryChunker.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformProcess.h"
#include "Misc/ScopeExit.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFabVersionSelectionTest, "UE.MCP.Fab.Download.VersionAndPaths",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFabVersionSelectionTest::RunTest(const FString& Parameters)
{
	using namespace MCPFab;
	const FString Id = TEXT("01234567-89ab-cdef-0123-456789abcdef");
	const FString Text = TEXT("{\"results\":[{\"title\":\"Fixture\",\"url\":\"https://www.fab.com/listings/") + Id
		+ TEXT("\",\"assetId\":\"asset\",\"assetNamespace\":\"namespace\",\"projectVersions\":[{\"artifactId\":\"fixtureV1\",\"engineVersions\":[\"UE_5.8\"],\"targetPlatforms\":[\"Mac\"]}]}]}");
	TSharedPtr<FJsonObject> Json; FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json);
	FAssetVersion Version; FString Error;
	TestTrue(TEXT("select exact owned engine and platform"), ResolveVersion(Json->GetArrayField(TEXT("results")), Id, TEXT("5.8"), TEXT("Mac"), Version, Error));
	TestEqual(TEXT("artifact selected"), Version.ArtifactId, FString(TEXT("fixtureV1")));
	TestFalse(TEXT("unsupported engine cannot silently pick another version"), ResolveVersion(Json->GetArrayField(TEXT("results")), Id, TEXT("5.7"), TEXT("Mac"), Version, Error));
	TestFalse(TEXT("unsupported platform refused"), ResolveVersion(Json->GetArrayField(TEXT("results")), Id, TEXT("5.8"), TEXT("Windows"), Version, Error));
	TestTrue(TEXT("normal Content path"), SafePackPath(TEXT("Content/Fixture/M_Wall.uasset")));
	for (const FString& Path : {TEXT("../Content/x"), TEXT("Content/../x"), TEXT("/Content/x"), TEXT("Content\\x"), TEXT("Plugins/x.dll"), TEXT("Content//x"), TEXT("Content/x:stream")})
		TestFalse(TEXT("unsafe path rejected: ") + Path, SafePackPath(Path));
	TestEqual(TEXT("signature follows complete chunk path"), ChunkUrl(TEXT("https://cdn.example/pack/fixture.manifest?Signature=a%2Bb&Key=xyz"), TEXT(""), TEXT("ChunksV4/00/chunk.chunk")),
		FString(TEXT("https://cdn.example/pack/ChunksV4/00/chunk.chunk?Signature=a%2Bb&Key=xyz")));
	return true;
}

namespace MCPFabDownloadTests
{
	static bool RunNativeTransfer(FAutomationTestBase& Test)
	{
		FMCPHandlerRegistry Registry; FFabHandlers::RegisterHandlers(Registry);
		const FString Suffix = FGuid::NewGuid().ToString(EGuidFormats::Digits);
		const FString Root = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("UE_MCP/FabTests") / Suffix);
		const FString Relative = TEXT("Content/UEMCP/Fab_") + Suffix + TEXT("/Fixture.ubulk");
		const FString Destination = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / Relative.Mid(8));
		ON_SCOPE_EXIT
		{
			MCPFab::ShutdownDownloads();
			IFileManager::Get().Delete(*Destination);
			IFileManager::Get().DeleteDirectory(*Root, false, true);
			IFileManager::Get().DeleteDirectory(*FPaths::GetPath(Destination), false, true);
		};
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Root / TEXT("source") / Relative), true);
		IFileManager::Get().MakeDirectory(*(Root / TEXT("cloud")), true);
		TArray<uint8> Bytes; Bytes.SetNumUninitialized(3 * 1024 * 1024);
		uint32 Value = 1729;
		for (uint8& Byte : Bytes) { Value = Value * 1664525 + 1013904223; Byte = Value >> 24; }
		FFileHelper::SaveArrayToFile(Bytes, *(Root / TEXT("source") / Relative));
		// Chunk generation pumps a core ticker and must run in its own process,
		// never recursively inside the editor's bridge ticker.
		FString Tool = FPaths::ConvertRelativePathToFull(FPaths::EngineDir() / TEXT("Binaries") / FPlatformProcess::GetBinariesSubdirectory() / TEXT("BuildPatchTool"));
#if PLATFORM_WINDOWS
		Tool += TEXT(".exe");
#endif
		const FString Arguments = FString::Printf(TEXT("-mode=ChunkBuildDirectory -FeatureLevel=Latest -BuildRoot=\"%s\" -CloudDir=\"%s\" -ArtifactId=MCPFabFixture -BuildVersion=1 -AppLaunch=\"\" -AppArgs=\"\" -OutputFilename=fixture.manifest -abslog=\"%s\""),
			*(Root / TEXT("source")), *(Root / TEXT("cloud")), *(Root / TEXT("chunker.log")));
		int32 ExitCode = -1;
		if (!Test.TestTrue(TEXT("native fixture generator ran"), FPlatformProcess::ExecProcess(*Tool, *Arguments, &ExitCode, nullptr, nullptr))) return false;
		if (!Test.TestEqual(TEXT("native fixture generation succeeded"), ExitCode, 0)) return false;
		auto& Bps = FModuleManager::LoadModuleChecked<IBuildPatchServicesModule>(TEXT("BuildPatchServices"));
		auto Manifest = Bps.LoadManifestFromFile(Root / TEXT("cloud/fixture.manifest"));
		if (!Test.TestTrue(TEXT("native manifest parsed"), Manifest.IsValid())) return false;
		FString JobId = MCPFab::StartFixtureDownload(Manifest.ToSharedRef(), Root / TEXT("cloud"), Root / TEXT("output"));
		auto Params = MakeShared<FJsonObject>(); Params->SetStringField(TEXT("jobId"), JobId);
		ON_SCOPE_EXIT { Registry.ExecuteHandler(TEXT("fab_cancel_download"), Params); };
		const auto Paused = Registry.ExecuteHandler(TEXT("fab_pause_download"), Params)->AsObject();
		Test.TestTrue(TEXT("pause active native transfer"), Paused->GetBoolField(TEXT("success")));
		auto Wait = [&](const FString& Target)
		{
			const double Deadline = FPlatformTime::Seconds() + 15.0;
			TSharedPtr<FJsonObject> Job;
			do
			{
				Job = Registry.ExecuteHandler(TEXT("fab_download_status"), Params)->AsObject()->GetObjectField(TEXT("job"));
				const FString State = Job->GetStringField(TEXT("state"));
				if (State == Target || State == TEXT("failed") || State == TEXT("complete")) return Job;
				FPlatformProcess::Sleep(0.001f);
			} while (FPlatformTime::Seconds() < Deadline);
			Test.AddError(TEXT("Native transfer timed out waiting for ") + Target); return Job;
		};
		Test.TestEqual(TEXT("native transfer paused"), Wait(TEXT("paused"))->GetStringField(TEXT("state")), FString(TEXT("paused")));
		Test.TestTrue(TEXT("resume paused transfer"), Registry.ExecuteHandler(TEXT("fab_resume_download"), Params)->AsObject()->GetBoolField(TEXT("success")));
		const auto Status = Wait(TEXT("complete"));
		if (!Test.TestEqual(TEXT("native transfer succeeded"), Status->GetStringField(TEXT("state")), FString(TEXT("complete")))) return false;
		TArray<uint8> Downloaded; FFileHelper::LoadFileToArray(Downloaded, *(Root / TEXT("output") / Relative));
		Test.TestTrue(TEXT("chunk transfer reconstructed exact bytes"), Downloaded == Bytes);
		Test.TestTrue(TEXT("native downloader transferred data"), Status->GetNumberField(TEXT("downloadedBytes")) > 0);
		const auto Installed = Registry.ExecuteHandler(TEXT("fab_install_pack"), Params)->AsObject();
		if (!Test.TestTrue(TEXT("verified pack installs"), Installed->GetBoolField(TEXT("success")))) return false;
		Test.TestEqual(TEXT("one new project file"), Installed->GetIntegerField(TEXT("copiedFiles")), 1);
		const auto Again = Registry.ExecuteHandler(TEXT("fab_install_pack"), Params)->AsObject();
		Test.TestTrue(TEXT("identical installation is unchanged"), Again->GetBoolField(TEXT("unchanged")));
		FFileHelper::SaveStringToFile(TEXT("user edit"), *Destination);
		Test.TestFalse(TEXT("removal preserves modified files"), Registry.ExecuteHandler(TEXT("fab_remove_installation"), Params)->AsObject()->GetBoolField(TEXT("success")));
		Test.TestTrue(TEXT("edited file retained"), IFileManager::Get().FileExists(*Destination));
		FFileHelper::SaveArrayToFile(Bytes, *Destination);
		const auto Removed = Registry.ExecuteHandler(TEXT("fab_remove_installation"), Params)->AsObject();
		Test.TestTrue(TEXT("unchanged owned installation removable"), Removed->GetBoolField(TEXT("success")));
		Test.TestEqual(TEXT("only created file removed"), Removed->GetIntegerField(TEXT("removedFiles")), 1);
		TArray<uint8> Corrupt = Bytes; Corrupt[0] ^= 0x80;
		FFileHelper::SaveArrayToFile(Corrupt, *(Root / TEXT("output") / Relative));
		JobId = MCPFab::StartFixtureDownload(Manifest.ToSharedRef(), Root / TEXT("cloud"), Root / TEXT("output"), true);
		Params->SetStringField(TEXT("jobId"), JobId);
		const auto Verified = Wait(TEXT("failed"));
		Test.TestEqual(TEXT("corruption fails SHA verification"), Verified->GetStringField(TEXT("state")), FString(TEXT("failed")));
		Test.TestEqual(TEXT("verify-only downloads no data"), Verified->GetNumberField(TEXT("downloadedBytes")), 0.0);
		TArray<uint8> AfterVerify; FFileHelper::LoadFileToArray(AfterVerify, *(Root / TEXT("output") / Relative));
		Test.TestTrue(TEXT("verify-only preserves corrupt source bytes"), AfterVerify == Corrupt);
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFabNativeTransferTest, "UE.MCP.Fab.Download.NativeTransferAndInstallation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFabNativeTransferTest::RunTest(const FString& Parameters)
{
	return MCPFabDownloadTests::RunNativeTransfer(*this);
}

#endif
#endif
