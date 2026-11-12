#if WITH_DEV_AUTOMATION_TESTS
#include "CoreMinimal.h"
#include "Runtime/Launch/Resources/Version.h"
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8

#include "Handlers/Fab/FabDownloads.h"
#include "Handlers/Fab/FabHandlers.h"
#include "Handlers/Fab/FabLibrary.h"
#include "HandlerRegistry.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HttpManager.h"
#include "HttpModule.h"
#include "HttpPath.h"
#include "Misc/ScopeExit.h"
#include "HttpServerModule.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "IHttpRouter.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

// The whole owned-content flow a user runs, against a local server standing in
// for fab.com and its CDN: sign-in check, a two-page library sync published to
// the Fab Content Browser table, finding the owned item, resolving its signed
// distribution URL and downloading it with BuildPatchServices, installing it into the
// project and removing it again. The pack is a real engine package, so the
// install is checked in the Asset Registry, not only on disk. Needs the Fab
// plugin (its table is where the sync publishes); without it the same calls
// must refuse with PLUGIN_NOT_AVAILABLE.
namespace MCPFabEndToEnd
{
	struct FState
	{
		FMCPHandlerRegistry Registry;
		FString Root, Cloud, Output, Folder, PackagePath, Destination;
		FString ListingId = TEXT("7b1f0c4e-2d3a-4c5b-9e8f-0a1b2c3d4e5f");
		FString AssetId = TEXT("0123456789abcdef0123456789abcdef");
		FString Namespace = TEXT("89efe5924d3d467c839449ab6ab52e7f");
		FString Account = TEXT("fedcba9876543210fedcba9876543210");
		FString Token = TEXT("e2e-token");
		FString Cursor = TEXT("c+1/2=");
		FString JobId;
		uint32 Port = 0;
		TArray<FHttpRouteHandle> Routes;
		TSharedPtr<IHttpRouter> Router;
		int32 LibraryRequests = 0, AuthorizedDistribution = 0, SignedManifests = 0, Chunks = 0;
		double Deadline = 0.0;

		TSharedPtr<FJsonObject> Call(const TCHAR* Method, TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>())
		{
			const TSharedPtr<FJsonValue> Value = Registry.ExecuteHandler(Method, Params);
			return Value.IsValid() && Value->Type == EJson::Object ? Value->AsObject() : MakeShared<FJsonObject>();
		}
	};

	FString Engine() { return FString::Printf(TEXT("UE_%d.%d"), ENGINE_MAJOR_VERSION, ENGINE_MINOR_VERSION); }

	FString Platform()
	{
#if PLATFORM_WINDOWS
		return TEXT("Windows");
#elif PLATFORM_MAC
		return TEXT("Mac");
#else
		return TEXT("Linux");
#endif
	}

	FString Item(const FState& S, const FString& Title, const FString& Listing, const FString& Artifact)
	{
		return FString::Printf(
			TEXT("{\"title\":\"%s\",\"assetId\":\"%s\",\"assetNamespace\":\"%s\",\"url\":\"https://www.fab.com/listings/%s\",")
			TEXT("\"listingType\":\"3d-model\",\"projectVersions\":[{\"artifactId\":\"%s\",\"engineVersions\":[\"%s\"],\"targetPlatforms\":[\"%s\"]}]}"),
			*Title, *S.AssetId, *S.Namespace, *Listing, *Artifact, *Engine(), *Platform());
	}

	FString Header(const FHttpServerRequest& Request, const FString& Name)
	{
		for (const auto& Pair : Request.Headers)
		{
			if (Pair.Key.Equals(Name, ESearchCase::IgnoreCase) && Pair.Value.Num() > 0) return Pair.Value[0];
		}
		return FString();
	}

	FString Query(const FHttpServerRequest& Request, const TCHAR* Name)
	{
		const FString* Value = Request.QueryParams.Find(Name);
		return Value ? FGenericPlatformHttp::UrlDecode(*Value) : FString();
	}

	TUniquePtr<FHttpServerResponse> Json(const FString& Text) { return FHttpServerResponse::Create(Text, TEXT("application/json")); }

	bool StartServer(const TSharedRef<FState>& S)
	{
		FHttpServerModule& Server = FHttpServerModule::Get();
		for (int32 Attempt = 0; Attempt < 20 && !S->Router.IsValid(); ++Attempt)
		{
			S->Port = 40000 + FMath::RandRange(0, 19999);
			S->Router = Server.GetHttpRouter(S->Port, /*bFailOnBindFailure=*/true);
		}
		if (!S->Router.IsValid()) return false;

		TWeakPtr<FState> Weak = S;
		S->Routes.Add(S->Router->BindRoute(FHttpPath(TEXT("/e")), EHttpServerRequestVerbs::VERB_GET | EHttpServerRequestVerbs::VERB_POST,
			FHttpRequestHandler::CreateLambda([Weak](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			const TSharedPtr<FState> State = Weak.Pin();
			const FString Path = Request.RelativePath.GetPath();
			if (!State) { OnComplete(FHttpServerResponse::Error(EHttpServerResponseCodes::NotFound)); return true; }
			if (Header(Request, TEXT("Authorization")) != TEXT("Bearer ") + State->Token)
			{
				OnComplete(FHttpServerResponse::Error(EHttpServerResponseCodes::Denied)); return true;
			}
			if (Request.Verb == EHttpServerRequestVerbs::VERB_GET && Path.Contains(State->Account + TEXT("/ue/library")))
			{
				++State->LibraryRequests;
				const FString Cursor = Query(Request, TEXT("cursor"));
				if (Cursor.IsEmpty())
				{
					OnComplete(Json(TEXT("{\"results\":[") + Item(*State, TEXT("Decoy Owned Pack"), TEXT("aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee"), TEXT("Decoy"))
						+ TEXT("],\"cursors\":{\"next\":\"") + State->Cursor + TEXT("\"}}")));
				}
				else if (Cursor == State->Cursor)
				{
					OnComplete(Json(TEXT("{\"results\":[") + Item(*State, TEXT("MCP E2E Cube Pack"), State->ListingId, TEXT("MCPFabE2E"))
						+ TEXT("],\"cursors\":{\"next\":null}}")));
				}
				else OnComplete(FHttpServerResponse::Error(EHttpServerResponseCodes::BadRequest, TEXT("cursor"), Cursor));
				return true;
			}
			if (Request.Verb == EHttpServerRequestVerbs::VERB_POST && Path.Contains(TEXT("artifacts/MCPFabE2E/manifest")))
			{
				TSharedPtr<FJsonObject> Body;
				const FString Text = FString(FUTF8ToTCHAR(reinterpret_cast<const ANSICHAR*>(Request.Body.GetData()), Request.Body.Num()));
				if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Body) || !Body
					|| Body->GetStringField(TEXT("item_id")) != State->AssetId || Body->GetStringField(TEXT("namespace")) != State->Namespace
					|| Body->GetStringField(TEXT("platform")) != Platform())
				{
					OnComplete(FHttpServerResponse::Error(EHttpServerResponseCodes::BadRequest)); return true;
				}
				++State->AuthorizedDistribution;
				OnComplete(Json(FString::Printf(
					TEXT("{\"downloadInfo\":[{\"artifactId\":\"MCPFabE2E\",\"distributionPoints\":[{\"manifestUrl\":\"http://127.0.0.1:%u/cloud/fixture.manifest?Signature=s1\",\"signatureExpiration\":\"2999-01-01T00:00:00Z\"}]}]}"),
					State->Port)));
				return true;
			}
			OnComplete(FHttpServerResponse::Error(EHttpServerResponseCodes::NotFound));
			return true;
		})));

		S->Routes.Add(S->Router->BindRoute(FHttpPath(TEXT("/cloud")), EHttpServerRequestVerbs::VERB_GET,
			FHttpRequestHandler::CreateLambda([Weak](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			const TSharedPtr<FState> State = Weak.Pin();
			FString Relative = Request.RelativePath.GetPath();
			while (Relative.StartsWith(TEXT("/"))) Relative.RightChopInline(1);
			Relative.RemoveFromStart(TEXT("cloud/"));
			TArray<uint8> Bytes;
			if (!State || Relative.Contains(TEXT("..")) || !FFileHelper::LoadFileToArray(Bytes, *(State->Cloud / Relative)))
			{
				OnComplete(FHttpServerResponse::Error(EHttpServerResponseCodes::NotFound)); return true;
			}
			// The manifest is fetched from the signed URL distribution handed
			// out. Chunks arrive unsigned here: the signing handler serves only
			// https CDNs (BPS answers it from a ticker this test cannot pump),
			// and UE.MCP.Fab.Download.VersionAndPaths covers how it signs.
			if (Relative.EndsWith(TEXT(".manifest")))
			{
				if (Query(Request, TEXT("Signature")) != TEXT("s1")) { OnComplete(FHttpServerResponse::Error(EHttpServerResponseCodes::Denied)); return true; }
				++State->SignedManifests;
			}
			else ++State->Chunks;
			OnComplete(FHttpServerResponse::Create(MoveTemp(Bytes), TEXT("application/octet-stream")));
			return true;
		})));
		Server.StartAllListeners();
		return true;
	}

	void Cleanup(const TSharedRef<FState>& S)
	{
		if (S->Router.IsValid()) for (const FHttpRouteHandle& Route : S->Routes) S->Router->UnbindRoute(Route);
		S->Routes.Reset();
		MCPFab::ShutdownDownloads();
		MCPFab::ResetLibrary();
		FString Ignored; MCPFab::PublishLibrary({}, Ignored);
		MCPFab::SetSessionForTesting(nullptr);
		IFileManager::Get().DeleteDirectory(*FPaths::GetPath(S->Destination), false, true);
		IFileManager::Get().DeleteDirectory(*S->Root, false, true);
		FAssetRegistryModule::GetRegistry().ScanPathsSynchronous({ S->Folder }, true);
	}

	/**
	 * Runs the HTTP client and the local server until Done says so or the
	 * deadline passes. The bridge runs automation tests on the game thread
	 * with no engine frames in between, so the test pumps both itself: the
	 * client delivers its completions in FHttpManager::Tick and the server
	 * accepts and answers in its own ticker callback.
	 */
	bool PumpUntil(FAutomationTestBase& Test, const TSharedRef<FState>& S, const TCHAR* What, TFunctionRef<bool()> Done)
	{
		FHttpManager& Client = FHttpModule::Get().GetHttpManager();
		FTSTickerObjectBase& Server = FHttpServerModule::Get();
		while (!Done())
		{
			if (FPlatformTime::Seconds() > S->Deadline)
			{
				Test.AddError(FString(TEXT("Timed out waiting for ")) + What);
				return false;
			}
			Client.Tick(0.01f);
			Server.Tick(0.01f);
			FPlatformProcess::Sleep(0.005f);
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFabEndToEndTest, "UE.MCP.Fab.EndToEnd.SyncDownloadInstall",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFabEndToEndTest::RunTest(const FString& Parameters)
{
	using namespace MCPFabEndToEnd;
	TSharedRef<FState> S = MakeShared<FState>();
	FFabHandlers::RegisterHandlers(S->Registry);

	if (!FModuleManager::Get().IsModuleLoaded(TEXT("Fab")))
	{
		// No Fab plugin in this engine: every step of the flow names what is missing.
		auto Download = MakeShared<FJsonObject>(); Download->SetStringField(TEXT("listingId"), S->ListingId);
		for (const TSharedPtr<FJsonObject>& Refused : { S->Call(TEXT("fab_sync_library")), S->Call(TEXT("fab_list_owned")), S->Call(TEXT("fab_download"), Download) })
		{
			TestFalse(TEXT("refused without Fab"), Refused->GetBoolField(TEXT("success")));
			TestEqual(TEXT("refusal code"), Refused->GetStringField(TEXT("errorCode")), FString(TEXT("PLUGIN_NOT_AVAILABLE")));
			TestTrue(TEXT("refusal says how to get Fab"), Refused->GetStringField(TEXT("error")).Contains(TEXT("Fab")));
		}
		return true;
	}

	// The pack: the engine's cube mesh, renamed into a folder of its own, as a
	// BuildPatchServices cloud the local CDN serves.
	const FString Suffix = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	S->Root = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("UE_MCP/FabE2E") / Suffix);
	S->Cloud = S->Root / TEXT("cloud");
	S->Output = S->Root / TEXT("output");
	S->Folder = TEXT("/Game/UEMCP/FabE2E_") + Suffix;
	S->PackagePath = S->Folder + TEXT("/SM_FabCube");
	const FString Relative = TEXT("Content/UEMCP/FabE2E_") + Suffix + TEXT("/SM_FabCube.uasset");
	S->Destination = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / Relative.Mid(8));
	const FString CubeFile = FPaths::ConvertRelativePathToFull(FPaths::EngineContentDir() / TEXT("BasicShapes/Cube.uasset"));
	if (!TestTrue(TEXT("engine cube package exists"), IFileManager::Get().FileExists(*CubeFile))) return false;
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(S->Root / TEXT("source") / Relative), true);
	IFileManager::Get().MakeDirectory(*S->Cloud, true);
	if (!TestEqual(TEXT("pack source staged"), IFileManager::Get().Copy(*(S->Root / TEXT("source") / Relative), *CubeFile), (uint32)COPY_OK)) { Cleanup(S); return false; }
	FString Tool = FPaths::ConvertRelativePathToFull(FPaths::EngineDir() / TEXT("Binaries") / FPlatformProcess::GetBinariesSubdirectory() / TEXT("BuildPatchTool"));
#if PLATFORM_WINDOWS
	Tool += TEXT(".exe");
#endif
	const FString Arguments = FString::Printf(TEXT("-mode=ChunkBuildDirectory -FeatureLevel=Latest -BuildRoot=\"%s\" -CloudDir=\"%s\" -ArtifactId=MCPFabE2E -BuildVersion=1 -AppLaunch=\"\" -AppArgs=\"\" -OutputFilename=fixture.manifest -abslog=\"%s\""),
		*(S->Root / TEXT("source")), *S->Cloud, *(S->Root / TEXT("chunker.log")));
	int32 ExitCode = -1;
	if (!TestTrue(TEXT("BuildPatchTool ran"), FPlatformProcess::ExecProcess(*Tool, *Arguments, &ExitCode, nullptr, nullptr)) || !TestEqual(TEXT("pack chunked"), ExitCode, 0))
	{
		Cleanup(S); return false;
	}
	if (!TestTrue(TEXT("local Fab server listening"), StartServer(S))) { Cleanup(S); return false; }

	MCPFab::FSession Session{ FString::Printf(TEXT("http://127.0.0.1:%u"), S->Port), S->Account, S->Token };
	MCPFab::SetSessionForTesting(&Session);
	S->Deadline = FPlatformTime::Seconds() + 90.0;
	ON_SCOPE_EXIT { Cleanup(S); };

	// Signed in already: status says so and login opens no window.
	TestTrue(TEXT("status sees the session"), S->Call(TEXT("fab_status"))->GetBoolField(TEXT("authenticated")));
	const auto Login = S->Call(TEXT("fab_login"));
	TestTrue(TEXT("login answers"), Login->GetBoolField(TEXT("success")));
	TestTrue(TEXT("login with a session is unchanged"), Login->GetBoolField(TEXT("unchanged")));

	// Before a sync, the library actions say what to do.
	const auto Early = S->Call(TEXT("fab_list_owned"));
	TestEqual(TEXT("list_owned before a sync"), Early->GetStringField(TEXT("errorCode")), FString(TEXT("FAB_LIBRARY_NOT_SYNCED")));

	auto Sync = MakeShared<FJsonObject>(); Sync->SetNumberField(TEXT("batchSize"), 1);
	TestTrue(TEXT("sync starts"), S->Call(TEXT("fab_sync_library"), Sync)->GetBoolField(TEXT("success")));
	if (!PumpUntil(*this, S, TEXT("library sync"), [&S]()
	{
		const FString State = S->Call(TEXT("fab_status"))->GetObjectField(TEXT("librarySync"))->GetStringField(TEXT("state"));
		return State == TEXT("complete") || State == TEXT("failed");
	})) return false;
	{
		const auto Library = S->Call(TEXT("fab_status"))->GetObjectField(TEXT("librarySync"));
		if (!TestEqual(TEXT("sync completed"), Library->GetStringField(TEXT("state")), FString(TEXT("complete"))))
		{
			AddError(Library->GetStringField(TEXT("error"))); return false;
		}
		TestEqual(TEXT("both pages fetched, cursor round-tripped"), Library->GetIntegerField(TEXT("pages")), 2);
		TestEqual(TEXT("two library requests"), S->LibraryRequests, 2);
		TestEqual(TEXT("snapshot holds both items"), Library->GetIntegerField(TEXT("snapshotItems")), 2);
		TestEqual(TEXT("Content Browser table rows published"), Library->GetIntegerField(TEXT("publishedRows")), 2);

		auto Find = MakeShared<FJsonObject>(); Find->SetStringField(TEXT("query"), TEXT("e2e cube"));
		const auto Owned = S->Call(TEXT("fab_list_owned"), Find);
		const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
		if (!TestTrue(TEXT("list_owned answers"), Owned->GetBoolField(TEXT("success")) && Owned->TryGetArrayField(TEXT("items"), Items)) || !TestEqual(TEXT("title filter finds one"), Items->Num(), 1))
		{
			return false;
		}
		const auto Row = (*Items)[0]->AsObject();
		TestEqual(TEXT("listingId derived"), Row->GetStringField(TEXT("listingId")), S->ListingId);
		TestTrue(TEXT("available for this editor"), Row->GetBoolField(TEXT("availableForThisEditor")));

		// engineVersion and platform default to this editor's.
		auto Download = MakeShared<FJsonObject>();
		Download->SetStringField(TEXT("listingId"), Row->GetStringField(TEXT("listingId")));
		Download->SetStringField(TEXT("outputDirectory"), S->Output);
		const auto Started = S->Call(TEXT("fab_download"), Download);
		if (!TestTrue(TEXT("download starts"), Started->GetBoolField(TEXT("success")))) { AddError(Started->GetStringField(TEXT("error"))); return false; }
		S->JobId = Started->GetObjectField(TEXT("job"))->GetStringField(TEXT("jobId"));
		TestEqual(TEXT("artifact for this editor chosen"), Started->GetObjectField(TEXT("job"))->GetStringField(TEXT("artifactId")), FString(TEXT("MCPFabE2E")));
	}
	auto Params = MakeShared<FJsonObject>(); Params->SetStringField(TEXT("jobId"), S->JobId);
	if (!PumpUntil(*this, S, TEXT("download"), [&S, &Params]()
	{
		const FString State = S->Call(TEXT("fab_download_status"), Params)->GetObjectField(TEXT("job"))->GetStringField(TEXT("state"));
		return State == TEXT("complete") || State == TEXT("failed") || State == TEXT("cancelled");
	})) return false;
	{
		const auto Job = S->Call(TEXT("fab_download_status"), Params)->GetObjectField(TEXT("job"));
		if (!TestEqual(TEXT("download complete"), Job->GetStringField(TEXT("state")), FString(TEXT("complete")))) { AddError(Job->GetStringField(TEXT("error"))); return false; }
		TestEqual(TEXT("distribution resolved with the session's token"), S->AuthorizedDistribution, 1);
		TestEqual(TEXT("manifest fetched from the signed distribution URL"), S->SignedManifests, 1);
		TestTrue(TEXT("chunks fetched from the CDN"), S->Chunks > 0);
		TestTrue(TEXT("bytes transferred"), Job->GetNumberField(TEXT("downloadedBytes")) > 0);

		const auto Installed = S->Call(TEXT("fab_install_pack"), Params);
		if (!TestTrue(TEXT("install succeeds"), Installed->GetBoolField(TEXT("success")))) { AddError(Installed->GetStringField(TEXT("error"))); return false; }
		TestEqual(TEXT("one package copied"), Installed->GetIntegerField(TEXT("copiedFiles")), 1);
		TestTrue(TEXT("package on disk"), IFileManager::Get().FileExists(*S->Destination));
		TArray<FAssetData> Assets;
		FAssetRegistryModule::GetRegistry().GetAssetsByPackageName(FName(*S->PackagePath), Assets);
		if (TestEqual(TEXT("Asset Registry knows the installed asset"), Assets.Num(), 1))
		{
			TestEqual(TEXT("installed asset is a static mesh"), Assets[0].AssetClassPath.GetAssetName(), FName(TEXT("StaticMesh")));
		}

		const auto Removed = S->Call(TEXT("fab_remove_installation"), Params);
		TestTrue(TEXT("removal succeeds"), Removed->GetBoolField(TEXT("success")));
		TestEqual(TEXT("the installed package removed"), Removed->GetIntegerField(TEXT("removedFiles")), 1);
		TestFalse(TEXT("package gone from disk"), IFileManager::Get().FileExists(*S->Destination));
	}
	return true;
}

#endif
#endif
