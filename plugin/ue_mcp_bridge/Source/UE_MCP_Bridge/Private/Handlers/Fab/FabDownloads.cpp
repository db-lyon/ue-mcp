#include "FabDownloads.h"
#include "FabHandlers.h"
#include "FabLibrary.h"
#include "HandlerUtils.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Async/Async.h"
#include "BuildPatchSettings.h"
#include "BuildPatchMessage.h"
#include "HAL/FileManager.h"
#include "HAL/CriticalSection.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformFileManager.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Interfaces/IBuildInstaller.h"
#include "Interfaces/IBuildPatchServicesModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "Misc/SecureHash.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_FAB_PLUGIN
#include "Utilities/FabAssetsCache.h"
#endif

namespace MCPFab
{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8
namespace DownloadPrivate
{
	bool FileHash(const FString& Path, FSHAHash& Hash)
	{
		TUniquePtr<FArchive> File(IFileManager::Get().CreateFileReader(*Path));
		if (!File) return false;
		FSHA1 Hasher; TArray<uint8> Buffer; Buffer.SetNumUninitialized(1024 * 1024);
		while (!File->AtEnd())
		{
			const int64 Size = FMath::Min<int64>(Buffer.Num(), File->TotalSize() - File->Tell());
			File->Serialize(Buffer.GetData(), Size); if (File->IsError()) return false;
			Hasher.Update(Buffer.GetData(), Size);
		}
		Hash = Hasher.Finalize(); return true;
	}

	bool ContainsString(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, const FString& Value)
	{
		const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
		if (!Object->TryGetArrayField(Field, Array)) return false;
		for (const auto& Entry : *Array) { FString Text; if (Entry && Entry->TryGetString(Text) && Text == Value) return true; }
		return false;
	}

	bool IsListing(const TSharedPtr<FJsonObject>& Item, const FString& Id)
	{
		FString Url;
		if (Item->TryGetStringField(TEXT("url"), Url))
		{
			Url.RemoveFromEnd(TEXT("/"));
			if (Url.EndsWith(TEXT("/") + Id, ESearchCase::CaseSensitive)) return true;
		}
		const TArray<TSharedPtr<FJsonValue>>* Attributes = nullptr;
		if (Item->TryGetArrayField(TEXT("customAttributes"), Attributes)) for (const auto& Attribute : *Attributes)
		{
			FString Value;
			if (Attribute && Attribute->Type == EJson::Object && Attribute->AsObject()->TryGetStringField(TEXT("ListingIdentifier"), Value) && Value == Id) return true;
		}
		return false;
	}

	bool NoSymlinkParents(const FString& Path)
	{
		auto& Files = FPlatformFileManager::Get().GetPlatformFile();
		FString Current = Path;
		while (!Current.IsEmpty())
		{
			if (Files.IsSymlink(*Current) == ESymlinkResult::Symlink) return false;
			const FString Parent = FPaths::GetPath(Current);
			if (Parent == Current) break;
			Current = Parent;
		}
		return true;
	}

	FString EncodeJson(const TSharedPtr<FJsonObject>& Object)
	{
		FString Text; FJsonSerializer::Serialize(Object.ToSharedRef(), TJsonWriterFactory<>::Create(&Text)); return Text;
	}

	// BPS asks for chunk paths after the manifest URL has been resolved. Keep
	// its signature on each path, not in the middle of a concatenated URL.
	class FCdnHandler final : public BuildPatchServices::FMessageHandler
	{
	public:
		explicit FCdnHandler(FString InUrl) : FMessageHandler(BuildPatchServices::EMessageRequests::ChunkUriRequest), Url(MoveTemp(InUrl)) {}
		bool HandleRequest(const BuildPatchServices::FChunkUriRequest& Request, TFunction<void(BuildPatchServices::FChunkUriResponse)> Respond) override
		{
			BuildPatchServices::FChunkUriResponse Response;
			Response.Uri = ChunkUrl(Url, Request.ChunkCloudSubdirectory, Request.ChunkPath);
			Response.bFailed = Response.Uri.IsEmpty(); Respond(MoveTemp(Response)); return true;
		}
	private:
		FString Url;
	};

	struct FReadOnlyVerification
	{
		TAtomic<bool> Cancel{false}, Done{false};
		TAtomic<int32> CheckedFiles{0};
		FString Error;
	};

	struct FJob : TSharedFromThis<FJob>
	{
		FString Id = FGuid::NewGuid().ToString(EGuidFormats::Digits);
		FString State = TEXT("resolving"), Error, Output, Owner;
		FAssetVersion Asset;
#if WITH_DEV_AUTOMATION_TESTS
		FString FixtureCloud;
#endif
		TMap<FString, FSHAHash> CreatedFiles;
		bool bVerifyOnly = false;
		bool bPauseRequested = false;
		FHttpRequestPtr Request;
		IBuildManifestPtr Manifest;
		IBuildInstallerPtr Installer;
		TUniquePtr<FCdnHandler> Cdn;
		TUniquePtr<FSystemWideCriticalSection> OutputLock;
		TSharedPtr<FReadOnlyVerification, ESPMode::ThreadSafe> Verify;
		TFuture<void> Verification;

		void Refresh()
		{
			if (Verify && Verify->Done.Load() && (State == TEXT("downloading") || State == TEXT("pausing") || State == TEXT("cancelling")))
			{
				OutputLock.Reset();
				if (Verify->Cancel.Load()) State = bPauseRequested ? TEXT("paused") : TEXT("cancelled");
				else if (!Verify->Error.IsEmpty()) Fail(Verify->Error);
				else State = TEXT("complete");
				return;
			}
			if (!Installer || !Installer->IsComplete() || (State != TEXT("downloading") && State != TEXT("pausing") && State != TEXT("cancelling"))) return;
			Installer->UnregisterMessageHandler(Cdn.Get()); OutputLock.Reset();
			if (Installer->CompletedSuccessfully()) State = TEXT("complete");
			else if (bPauseRequested && Installer->IsCanceled()) State = TEXT("paused");
			else if (Installer->IsCanceled()) State = TEXT("cancelled");
			else Fail(TEXT("Native BuildPatch verification or download failed. See errorCode; resume_download retries with a fresh signed URL."));
		}
		bool Active() { Refresh(); return State == TEXT("resolving") || State == TEXT("downloading") || State == TEXT("pausing") || State == TEXT("cancelling"); }
		void Fail(const FString& Message) { State = TEXT("failed"); Error = Message; Request.Reset(); OutputLock.Reset(); }
		TSharedPtr<FJsonObject> Describe() const
		{
			auto Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("jobId"), Id); Result->SetStringField(TEXT("state"), State);
			Result->SetStringField(TEXT("listingId"), Asset.ListingId); Result->SetStringField(TEXT("artifactId"), Asset.ArtifactId);
			Result->SetStringField(TEXT("engineVersion"), Asset.EngineVersion); Result->SetStringField(TEXT("platform"), Asset.Platform);
			Result->SetStringField(TEXT("outputDirectory"), Output); Result->SetBoolField(TEXT("verifyOnly"), bVerifyOnly);
			if (!Error.IsEmpty()) Result->SetStringField(TEXT("error"), Error);
			if (Manifest) { Result->SetNumberField(TEXT("files"), Manifest->GetBuildFileList().Num()); Result->SetNumberField(TEXT("installedBytes"), double(Manifest->GetBuildSize())); }
			if (Verify)
			{
				const int32 Total = Manifest->GetBuildFileList().Num();
				Result->SetNumberField(TEXT("progress"), Total ? double(Verify->CheckedFiles.Load()) / Total : 0.0);
				Result->SetStringField(TEXT("phase"), State == TEXT("complete") ? TEXT("Completed") : TEXT("CheckingFiles"));
				Result->SetNumberField(TEXT("downloadedBytes"), 0); Result->SetNumberField(TEXT("downloadRequiredBytes"), 0); Result->SetNumberField(TEXT("bytesPerSecond"), 0);
				if (State == TEXT("failed")) Result->SetStringField(TEXT("errorCode"), TEXT("FILE_VERIFICATION_FAILED"));
			}
			if (Installer)
			{
				Result->SetNumberField(TEXT("progress"), Installer->GetUpdateProgress());
				Result->SetStringField(TEXT("phase"), LexToString(Installer->GetState()));
				Result->SetNumberField(TEXT("downloadedBytes"), double(Installer->GetTotalDownloaded()));
				Result->SetNumberField(TEXT("downloadRequiredBytes"), double(Installer->GetTotalDownloadRequired()));
				Result->SetNumberField(TEXT("bytesPerSecond"), Installer->GetDownloadSpeed());
				if (Installer->IsComplete() && Installer->HasError()) Result->SetStringField(TEXT("errorCode"), Installer->GetErrorCode());
			}
			return Result;
		}

		void StopRequest()
		{
			if (Request) { Request->OnProcessRequestComplete().Unbind(); Request->CancelRequest(); Request.Reset(); }
		}

		bool PrepareOutput(FString& OutError)
		{
			if (!NoSymlinkParents(Output) || !NoSymlinkParents(Output / TEXT(".ue-mcp-fab/staging"))) { OutError = TEXT("Output path contains a symlink."); return false; }
			const FString Marker = Output / TEXT(".ue-mcp-fab/asset.json");
			TArray<FString> Existing;
			IFileManager::Get().FindFiles(Existing, *(Output / TEXT("*")), true, true);
			if (!Existing.IsEmpty() && !bVerifyOnly)
			{
				FString Text; TSharedPtr<FJsonObject> Previous;
				if (!FFileHelper::LoadFileToString(Text, *Marker) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Previous) || !Previous
					|| Previous->GetStringField(TEXT("artifactId")) != Asset.ArtifactId)
				{ OutError = TEXT("Output is not an empty directory or this artifact's resumable download. Choose a separate directory; verifyOnly can inspect an existing pack."); return false; }
			}
			for (const FString& File : Manifest->GetBuildFileList()) if (!NoSymlinkParents(Output / File)) { OutError = TEXT("Output contains a symlink at a manifest path."); return false; }
			if (bVerifyOnly) return true;
			bool bSafeStaging = true;
			FPlatformFileManager::Get().GetPlatformFile().IterateDirectoryRecursively(*(Output / TEXT(".ue-mcp-fab")), [&bSafeStaging](const TCHAR* Path, bool)
			{
				if (!NoSymlinkParents(Path)) { bSafeStaging = false; return false; } return true;
			});
			if (!bSafeStaging) { OutError = TEXT("Staging contains a symlink."); return false; }
			if (!IFileManager::Get().MakeDirectory(*(Output / TEXT(".ue-mcp-fab")), true)) { OutError = TEXT("Cannot create download staging directory."); return false; }
			auto Metadata = MakeShared<FJsonObject>();
			Metadata->SetStringField(TEXT("artifactId"), Asset.ArtifactId); Metadata->SetStringField(TEXT("listingId"), Asset.ListingId);
			Metadata->SetStringField(TEXT("engineVersion"), Asset.EngineVersion); Metadata->SetStringField(TEXT("platform"), Asset.Platform);
			// Never persist tokens, distribution responses or signed URLs.
			if (!FFileHelper::SaveStringToFile(EncodeJson(Metadata), *Marker)) { OutError = TEXT("Cannot persist download identity."); return false; }
			return true;
		}

		void StartReadOnlyVerification()
		{
			if (Verification.IsValid()) Verification.Wait();
			Verify = MakeShared<FReadOnlyVerification, ESPMode::ThreadSafe>();
			State = TEXT("downloading");
			// BPS may delete failed files even for MakeVerifyOnly. Read the native
			// manifest with Core's streaming SHA here, without any install operation.
			Verification = Async(EAsyncExecution::ThreadPool, [Task = Verify.ToSharedRef(), Snapshot = Manifest.ToSharedRef(), Directory = Output]()
			{
				for (const FString& File : Snapshot->GetBuildFileList())
				{
					if (Task->Cancel.Load()) break;
					FSHAHash Hash; const FSHAHash* Expected = Snapshot->GetFileSHA1Hash(File);
					if (!Expected || !FileHash(Directory / File, Hash) || Hash != *Expected)
					{ Task->Error = TEXT("Read-only SHA verification failed for ") + File + TEXT(". Original file retained."); break; }
					Task->CheckedFiles.Store(Task->CheckedFiles.Load() + 1);
				}
				Task->Done.Store(true);
			});
		}

		void StartInstaller(const FString& ManifestUrl)
		{
			FString Reason;
			OutputLock = MakeUnique<FSystemWideCriticalSection>(TEXT("UE_MCP_Fab_") + FMD5::HashAnsiString(*Output.ToLower()));
			if (!OutputLock->IsValid()) { Fail(TEXT("Another process owns this output directory.")); return; }
			if (!Manifest || !ValidatePackManifest(Manifest.ToSharedRef(), Reason) || !PrepareOutput(Reason)) { Fail(Reason); return; }
			if (bVerifyOnly) { StartReadOnlyVerification(); return; }
			auto& Bps = FModuleManager::LoadModuleChecked<IBuildPatchServicesModule>(TEXT("BuildPatchServices"));
			using namespace BuildPatchServices;
			FBuildInstallerConfiguration Config({FInstallerAction::MakeRepair(Manifest.ToSharedRef())});
			Config.InstallDirectory = Output;
			Config.StagingDirectory = Output / TEXT(".ue-mcp-fab/staging");
			Config.CloudDirectories = {FPaths::GetPath(ManifestUrl.Left(ManifestUrl.Find(TEXT("?")) == INDEX_NONE ? ManifestUrl.Len() : ManifestUrl.Find(TEXT("?"))))};
			Config.bRunRequiredPrereqs = false; Config.bSkipCustomUninstallAction = true; Config.bRejectSymlinks = true;
			Config.bAllowConcurrentExecution = true; Config.VerifyMode = EVerifyMode::ShaVerifyAllFiles;
			Config.ConstructorDisableResumeBelowMB = 0;
			Cdn = MakeUnique<FCdnHandler>(ManifestUrl);
			// BPS requires a bound completion delegate. Job state is observed by
			// Refresh so callers and worker completion cannot race its lifetime.
			Installer = Bps.CreateBuildInstaller(MoveTemp(Config), FBuildPatchInstallerDelegate::CreateLambda([](const IBuildInstallerRef&) {}));
			if (ManifestUrl.StartsWith(TEXT("https://"))) Installer->RegisterMessageHandler(Cdn.Get());
			State = TEXT("downloading");
			if (!Installer->StartInstallation()) Fail(TEXT("Native BuildPatch installer could not start."));
		}

		void FetchManifest(const FString& Url)
		{
			if (!Url.StartsWith(TEXT("https://"))) { Fail(TEXT("Fab returned a non-HTTPS manifest URL.")); return; }
			Request = FHttpModule::Get().CreateRequest(); Request->SetVerb(TEXT("GET")); Request->SetURL(Url); Request->SetTimeout(60.0f);
			Request->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnGameThread);
			const TWeakPtr<FJob> Weak = AsShared();
			Request->OnProcessRequestComplete().BindLambda([Weak, Url](FHttpRequestPtr, FHttpResponsePtr Response, bool bOk)
			{
				auto Job = Weak.Pin(); if (!Job || Job->State != TEXT("resolving")) return; Job->Request.Reset();
				if (!bOk || !Response || Response->GetResponseCode() != 200) { Job->Fail(TEXT("Fab manifest download failed. Retry download or resume_download.")); return; }
				auto& Bps = FModuleManager::LoadModuleChecked<IBuildPatchServicesModule>(TEXT("BuildPatchServices"));
				Job->Manifest = Bps.MakeManifestFromData(Response->GetContent());
				if (!Job->Manifest) { Job->Fail(TEXT("Native BuildPatch could not parse the manifest.")); return; }
				Job->StartInstaller(Url);
			});
			if (!Request->ProcessRequest()) Fail(TEXT("Manifest request could not start."));
		}

		void Resolve()
		{
#if WITH_DEV_AUTOMATION_TESTS
			if (!FixtureCloud.IsEmpty()) { bPauseRequested = false; Error.Reset(); StartInstaller(FixtureCloud / TEXT("fixture.manifest")); return; }
#endif
			FSession Session; FString Reason;
			if (!ReadSession(Session, Reason) || Session.AccountId != Owner) { Fail(TEXT("Sign back into the account that owns this download, then retry.")); return; }
			State = TEXT("resolving"); Error.Reset(); bPauseRequested = false;
			Request = FHttpModule::Get().CreateRequest();
			Request->SetURL(Session.BaseUrl / TEXT("e/artifacts") / Asset.ArtifactId / TEXT("manifest")); Request->SetVerb(TEXT("POST"));
			Request->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + Session.Token); Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
			Request->SetHeader(TEXT("Accept"), TEXT("application/json")); Request->SetTimeout(30.0f);
			auto Body = MakeShared<FJsonObject>(); Body->SetStringField(TEXT("item_id"), Asset.AssetId); Body->SetStringField(TEXT("namespace"), Asset.Namespace); Body->SetStringField(TEXT("platform"), Asset.Platform);
			Request->SetContentAsString(EncodeJson(Body)); Request->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnGameThread);
			const TWeakPtr<FJob> Weak = AsShared();
			Request->OnProcessRequestComplete().BindLambda([Weak](FHttpRequestPtr, FHttpResponsePtr Response, bool bOk)
			{
				auto Job = Weak.Pin(); if (!Job || Job->State != TEXT("resolving")) return; Job->Request.Reset();
				const int32 Code = Response ? Response->GetResponseCode() : 0;
				TSharedPtr<FJsonObject> Json; const TArray<TSharedPtr<FJsonValue>>* Infos = nullptr;
				if (!bOk || !Response || Code < 200 || Code >= 300 || !Response->GetContentType().StartsWith(TEXT("application/json"))
					|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()), Json) || !Json || !Json->TryGetArrayField(TEXT("downloadInfo"), Infos))
				{ Job->Fail(FString::Printf(TEXT("Fab distribution resolution failed (HTTP %d). No response body or credentials are exposed."), Code)); return; }
				for (const auto& Info : *Infos)
				{
					if (!Info || Info->Type != EJson::Object) continue; auto Object = Info->AsObject(); FString Artifact;
					const TArray<TSharedPtr<FJsonValue>>* Points = nullptr;
					if (!Object->TryGetStringField(TEXT("artifactId"), Artifact) || Artifact != Job->Asset.ArtifactId || !Object->TryGetArrayField(TEXT("distributionPoints"), Points)) continue;
					for (const auto& Point : *Points)
					{
						if (!Point || Point->Type != EJson::Object) continue; FString Url, Expiry;
						if (!Point->AsObject()->TryGetStringField(TEXT("manifestUrl"), Url)) continue;
						FDateTime Expiration;
						if (Point->AsObject()->TryGetStringField(TEXT("signatureExpiration"), Expiry) && FDateTime::ParseIso8601(*Expiry, Expiration) && Expiration <= FDateTime::UtcNow()) continue;
						Job->FetchManifest(Url); return;
					}
				}
				Job->Fail(TEXT("Fab returned no current distribution for the selected artifact."));
			});
			if (!Request->ProcessRequest()) Fail(TEXT("Distribution request could not start."));
		}
	};

	TMap<FString, TSharedPtr<FJob>> Jobs;

	TSharedPtr<FJob> FindJob(const TSharedPtr<FJsonObject>& Params, FString& Error)
	{
		const FString Id = OptionalString(Params, TEXT("jobId"));
		if (const auto* Found = Jobs.Find(Id)) { (*Found)->Refresh(); return *Found; }
		Error = TEXT("Unknown jobId. After restarting the editor, repeat download with the same listing/version/outputDirectory to reuse its verified files and staging data."); return nullptr;
	}

	TSharedPtr<FJsonValue> MCPFabJobResult(const TSharedPtr<FJob>& Job, bool bUnchanged = true, const FString& Inverse = FString())
	{
		Job->Refresh();
		auto Result = MCPSuccess(); Result->SetObjectField(TEXT("job"), Job->Describe());
		Result->SetBoolField(TEXT("unchanged"), bUnchanged);
		if (!bUnchanged && !Inverse.IsEmpty())
		{
			auto Payload = MakeShared<FJsonObject>(); Payload->SetStringField(TEXT("jobId"), Job->Id);
			MCPSetRollback(Result, Inverse, Payload);
			Result->SetBoolField(TEXT("rollbackLossy"), true);
			Result->SetStringField(TEXT("rollbackNote"), TEXT("Controls the job; transferred bytes and verified staging files remain cached. Wait for terminal or paused state before invoking the inverse."));
		}
		else MCPSetNoRollback(Result, TEXT("This call did not change the job."));
		return MCPResult(Result);
	}
}

bool ResolveVersion(const TArray<TSharedPtr<FJsonValue>>& Items, const FString& ListingId, const FString& EngineVersion, const FString& Platform, FAssetVersion& Out, FString& Error)
{
	FGuid Guid;
	if (!FGuid::ParseExact(ListingId, EGuidFormats::DigitsWithHyphens, Guid)) { Error = TEXT("listingId must be a Fab listing UUID."); return false; }
	const FString Engine = EngineVersion.StartsWith(TEXT("UE_")) ? EngineVersion : TEXT("UE_") + EngineVersion;
	for (const auto& Value : Items)
	{
		if (!Value || Value->Type != EJson::Object || !DownloadPrivate::IsListing(Value->AsObject(), ListingId)) continue;
		const auto Item = Value->AsObject(); const TArray<TSharedPtr<FJsonValue>>* Versions = nullptr;
		if (!Item->TryGetArrayField(TEXT("projectVersions"), Versions)) break;
		TArray<TSharedPtr<FJsonObject>> Matches;
		for (const auto& Version : *Versions) if (Version && Version->Type == EJson::Object
			&& DownloadPrivate::ContainsString(Version->AsObject(), TEXT("engineVersions"), Engine)
			&& DownloadPrivate::ContainsString(Version->AsObject(), TEXT("targetPlatforms"), Platform)) Matches.Add(Version->AsObject());
		if (Matches.Num() != 1) { Error = TEXT("No unique owned artifact matches engineVersion/platform. Inspect list_owned projectVersions and choose an exact supported version."); return false; }
		Out.ListingId = ListingId; Out.EngineVersion = Engine; Out.Platform = Platform;
		if (!Item->TryGetStringField(TEXT("title"), Out.Title) || !Item->TryGetStringField(TEXT("assetId"), Out.AssetId)
			|| !Item->TryGetStringField(TEXT("assetNamespace"), Out.Namespace) || !Matches[0]->TryGetStringField(TEXT("artifactId"), Out.ArtifactId) || Out.ArtifactId.IsEmpty())
		{ Error = TEXT("Owned artifact metadata is incomplete."); return false; }
		for (TCHAR C : Out.ArtifactId) if (!(FChar::IsAlnum(C) || C == '_' || C == '-')) { Error = TEXT("Invalid artifact identifier."); return false; }
		return true;
	}
	Error = TEXT("Listing not present in the owned library. Complete sync_library first."); return false;
}

bool SafePackPath(const FString& Relative)
{
	if (Relative.IsEmpty() || Relative.Contains(TEXT("\\")) || Relative.Contains(TEXT(":")) || !FPaths::IsRelative(Relative)) return false;
	TArray<FString> Parts; Relative.ParseIntoArray(Parts, TEXT("/"), false);
	for (const auto& Part : Parts) if (Part.IsEmpty() || Part == TEXT(".") || Part == TEXT("..")) return false;
	return Parts.Num() >= 2 && (Parts[0] == TEXT("Content") || Parts[0] == TEXT("Config"));
}

bool ValidatePackManifest(const IBuildManifestRef& Manifest, FString& Error)
{
	const auto Files = Manifest->GetBuildFileList();
	if (Files.IsEmpty()) { Error = TEXT("Asset pack manifest is empty."); return false; }
	TSet<FString> Seen;
	for (const FString& File : Files)
	{
		const FString Ext = FPaths::GetExtension(File).ToLower();
		if (!SafePackPath(File) || Ext == TEXT("uplugin") || Ext == TEXT("uproject") || Ext == TEXT("dylib") || Ext == TEXT("dll")
			|| Ext == TEXT("exe") || Ext == TEXT("sh") || Ext == TEXT("command") || Seen.Contains(File.ToLower()))
		{ Error = TEXT("Only non-executable Content/Config asset packs with safe, unique relative paths are supported. Plugins and full projects are excluded."); return false; }
		Seen.Add(File.ToLower());
	}
	return true;
}

FString ChunkUrl(const FString& ManifestUrl, const FString& Subdirectory, const FString& ChunkPath)
{
	// The chunk path is supplied by BPS, never by an MCP caller.
	if (ChunkPath.IsEmpty() || ChunkPath.Contains(TEXT("..")) || Subdirectory.Contains(TEXT(".."))) return {};
	FString Base, Query;
	if (!ManifestUrl.Split(TEXT("?"), &Base, &Query)) Base = ManifestUrl;
	FString Result = FPaths::GetPath(Base);
	if (!Subdirectory.IsEmpty()) Result /= Subdirectory;
	Result /= ChunkPath;
	if (!Query.IsEmpty()) Result += TEXT("?") + Query;
	return Result;
}

} // namespace MCPFab

TSharedPtr<FJsonValue> FFabHandlers::ListOwned(const TSharedPtr<FJsonObject>& Params)
{
	MCPFab::FSession Session; FString Error;
	if (!MCPFab::ReadSession(Session, Error) || Session.AccountId != MCPFab::Library().AccountId) return MCPError(TEXT("Sync the current native Fab account first."));
	const FString Query = OptionalString(Params, TEXT("query"));
	const int32 Offset = OptionalInt(Params, TEXT("offset"), 0), Limit = OptionalInt(Params, TEXT("limit"), 50);
	if (Offset < 0 || Limit < 1 || Limit > 200) return MCPError(TEXT("offset must be nonnegative and limit between 1 and 200."));
	TArray<TSharedPtr<FJsonValue>> Matches;
	for (const auto& Item : MCPFab::Library().Items) if (Query.IsEmpty() || Item->AsObject()->GetStringField(TEXT("title")).Contains(Query)) Matches.Add(Item);
	TArray<TSharedPtr<FJsonValue>> Page;
	for (int32 I = Offset; I < Matches.Num() && Page.Num() < Limit; ++I) Page.Add(Matches[I]);
	auto Result = MCPSuccess(); Result->SetArrayField(TEXT("items"), Page); Result->SetNumberField(TEXT("total"), Matches.Num());
	Result->SetNumberField(TEXT("offset"), Offset); Result->SetObjectField(TEXT("librarySync"), MCPFab::Library().Describe()); return MCPResult(Result);
}

TSharedPtr<FJsonValue> FFabHandlers::Download(const TSharedPtr<FJsonObject>& Params)
{
	FString Listing, Engine, Platform;
	if (auto Err = RequireString(Params, TEXT("listingId"), Listing)) return Err;
	if (auto Err = RequireString(Params, TEXT("engineVersion"), Engine)) return Err;
	if (auto Err = RequireString(Params, TEXT("platform"), Platform)) return Err;
	const bool bVerify = OptionalBool(Params, TEXT("verifyOnly"), false);
	FString Output = OptionalString(Params, TEXT("outputDirectory"));
	MCPFab::FSession Session; FString Error;
	if (!MCPFab::ReadSession(Session, Error) || Session.AccountId != MCPFab::Library().AccountId) return MCPError(TEXT("Sync the current native Fab account before downloading owned content."));
	auto Job = MakeShared<MCPFab::DownloadPrivate::FJob>();
	if (!MCPFab::ResolveVersion(MCPFab::Library().Items, Listing, Engine, Platform, Job->Asset, Error)) return MCPError(Error);
	if (Output.IsEmpty())
	{
#if WITH_FAB_PLUGIN
		Output = FFabAssetsCache::GetCacheLocation() / TEXT("UE-MCP") / Listing / Job->Asset.ArtifactId;
#else
		return MCPError(TEXT("Fab native cache is unavailable; supply outputDirectory."));
#endif
	}
	if (FPaths::IsRelative(Output)) return MCPError(TEXT("outputDirectory must be absolute."));
	Job->Output = FPaths::ConvertRelativePathToFull(Output); FPaths::NormalizeDirectoryName(Job->Output);
	if (!MCPFab::DownloadPrivate::NoSymlinkParents(Job->Output)) return MCPError(TEXT("Output path contains a symlink."));
	for (const auto& Pair : MCPFab::DownloadPrivate::Jobs) if (Pair.Value->Active()
		&& (FPaths::IsUnderDirectory(Job->Output, Pair.Value->Output) || FPaths::IsUnderDirectory(Pair.Value->Output, Job->Output) || FPaths::IsSamePath(Job->Output, Pair.Value->Output)))
		return MCPError(TEXT("Another job owns an overlapping output directory."));
	Job->Owner = Session.AccountId; Job->bVerifyOnly = bVerify;
	MCPFab::DownloadPrivate::Jobs.Add(Job->Id, Job); Job->Resolve(); return MCPFab::DownloadPrivate::MCPFabJobResult(Job, false, TEXT("fab_cancel_download"));
}

TSharedPtr<FJsonValue> FFabHandlers::DownloadStatus(const TSharedPtr<FJsonObject>& Params)
{
	const FString Id = OptionalString(Params, TEXT("jobId"));
	if (!Id.IsEmpty()) { FString Error; auto Job = MCPFab::DownloadPrivate::FindJob(Params, Error); return Job ? MCPFab::DownloadPrivate::MCPFabJobResult(Job) : MCPError(Error); }
	auto Result = MCPSuccess(); TArray<TSharedPtr<FJsonValue>> Rows;
	for (const auto& Pair : MCPFab::DownloadPrivate::Jobs) { Pair.Value->Refresh(); Rows.Add(MakeShared<FJsonValueObject>(Pair.Value->Describe())); }
	Result->SetArrayField(TEXT("jobs"), Rows); return MCPResult(Result);
}

TSharedPtr<FJsonValue> FFabHandlers::PauseDownload(const TSharedPtr<FJsonObject>& Params)
{
	FString Error; auto Job = MCPFab::DownloadPrivate::FindJob(Params, Error); if (!Job) return MCPError(Error);
	const bool bUnchanged = Job->State == TEXT("paused") || Job->State == TEXT("pausing");
	if (Job->State == TEXT("resolving")) { Job->StopRequest(); Job->State = TEXT("paused"); }
	else if (Job->State == TEXT("downloading")) { Job->bPauseRequested = true; Job->State = TEXT("pausing"); if (Job->Verify) Job->Verify->Cancel.Store(true); else Job->Installer->CancelInstall(); }
	else if (Job->State != TEXT("paused") && Job->State != TEXT("pausing")) return MCPError(TEXT("Only an active download can be paused."));
	return MCPFab::DownloadPrivate::MCPFabJobResult(Job, bUnchanged, TEXT("fab_resume_download"));
}

TSharedPtr<FJsonValue> FFabHandlers::ResumeDownload(const TSharedPtr<FJsonObject>& Params)
{
	FString Error; auto Job = MCPFab::DownloadPrivate::FindJob(Params, Error); if (!Job) return MCPError(Error);
	if (Job->State != TEXT("paused") && Job->State != TEXT("failed") && Job->State != TEXT("cancelled")) return MCPError(TEXT("Wait for a paused, failed or cancelled state before resuming."));
	for (const auto& Pair : MCPFab::DownloadPrivate::Jobs) if (Pair.Value != Job && Pair.Value->Active()
		&& (FPaths::IsUnderDirectory(Job->Output, Pair.Value->Output) || FPaths::IsUnderDirectory(Pair.Value->Output, Job->Output) || FPaths::IsSamePath(Job->Output, Pair.Value->Output)))
		return MCPError(TEXT("Another job owns an overlapping output directory."));
	Job->Resolve(); return MCPFab::DownloadPrivate::MCPFabJobResult(Job, false, TEXT("fab_pause_download"));
}

TSharedPtr<FJsonValue> FFabHandlers::CancelDownload(const TSharedPtr<FJsonObject>& Params)
{
	FString Error; auto Job = MCPFab::DownloadPrivate::FindJob(Params, Error); if (!Job) return MCPError(Error);
	const bool bUnchanged = Job->State == TEXT("cancelled") || Job->State == TEXT("complete");
	Job->StopRequest(); Job->bPauseRequested = false;
	if (Job->Verify && !Job->Verify->Done.Load()) { Job->State = TEXT("cancelling"); Job->Verify->Cancel.Store(true); }
	else if (Job->Installer && !Job->Installer->IsComplete()) { Job->State = TEXT("cancelling"); Job->Installer->CancelInstall(); }
	else if (Job->State != TEXT("complete")) Job->State = TEXT("cancelled");
	return MCPFab::DownloadPrivate::MCPFabJobResult(Job, bUnchanged, TEXT("fab_resume_download"));
}

TSharedPtr<FJsonValue> FFabHandlers::InstallPack(const TSharedPtr<FJsonObject>& Params)
{
	FString Error; auto Job = MCPFab::DownloadPrivate::FindJob(Params, Error); if (!Job) return MCPError(Error);
	if (Job->State != TEXT("complete") || !Job->Manifest) return MCPError(TEXT("The download must complete native SHA verification before installation."));
	TArray<TPair<FString, FString>> Pending;
	TMap<FString, FSHAHash> ExpectedHashes;
	int32 Unchanged = 0;
	// Check every source and collision before creating a single project file.
	for (const FString& File : Job->Manifest->GetBuildFileList())
	{
		if (!File.StartsWith(TEXT("Content/"))) continue;
		const FString Extension = FPaths::GetExtension(File).ToLower();
		if (Extension != TEXT("uasset") && Extension != TEXT("umap") && Extension != TEXT("ubulk") && Extension != TEXT("uexp") && Extension != TEXT("uptnl"))
			return MCPError(TEXT("Install supports native UE packages and their bulk companions only; import loose files using import_file."));
		const FString Source = Job->Output / File;
		const FString Destination = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / File.Mid(8));
		if (!MCPFab::DownloadPrivate::NoSymlinkParents(Source) || !MCPFab::DownloadPrivate::NoSymlinkParents(Destination)) return MCPError(TEXT("Pack source or project destination contains a symlink."));
		FSHAHash SourceHash; const FSHAHash* Expected = Job->Manifest->GetFileSHA1Hash(File);
		if (!Expected || !MCPFab::DownloadPrivate::FileHash(Source, SourceHash) || SourceHash != *Expected) return MCPError(TEXT("A downloaded file changed since verification. Run download with verifyOnly before installing."));
		if (IFileManager::Get().FileExists(*Destination))
		{
			FSHAHash Hash;
			if (!MCPFab::DownloadPrivate::FileHash(Destination, Hash) || Hash != *Expected) return MCPError(TEXT("A different project file occupies a pack path. Nothing was copied; choose an isolated reference project."));
			++Unchanged;
		}
		else { Pending.Emplace(Source, Destination); ExpectedHashes.Add(Destination, *Expected); }
	}
	if (Pending.IsEmpty() && Unchanged == 0) return MCPError(TEXT("No Content files in this asset pack."));
	TArray<FString> Created;
	for (const auto& File : Pending)
	{
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(File.Value), true);
		if (IFileManager::Get().Copy(*File.Value, *File.Key, false, false) != COPY_OK)
		{
			for (const FString& Path : Created) { IFileManager::Get().Delete(*Path); Job->CreatedFiles.Remove(Path); }
			return MCPError(TEXT("Copy failed; files created by this call were removed. Existing project files were retained."));
		}
		Created.Add(File.Value);
		FSHAHash Hash;
		if (!MCPFab::DownloadPrivate::FileHash(File.Value, Hash) || Hash != ExpectedHashes[File.Value])
		{
			for (const FString& Path : Created) { IFileManager::Get().Delete(*Path); Job->CreatedFiles.Remove(Path); }
			return MCPError(TEXT("Copied file failed SHA verification; files created by this call were removed."));
		}
		Job->CreatedFiles.Add(File.Value, Hash);
	}
	auto& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	TArray<FString> Packages;
	for (const FString& Path : Created) if (Path.EndsWith(TEXT(".uasset")) || Path.EndsWith(TEXT(".umap"))) Packages.Add(Path);
	Registry.ScanFilesSynchronous(Packages, true);
	auto Result = MCPSuccess(); Result->SetNumberField(TEXT("copiedFiles"), Created.Num()); Result->SetNumberField(TEXT("unchangedFiles"), Unchanged);
	Result->SetBoolField(TEXT("unchanged"), Created.IsEmpty());
	if (!Created.IsEmpty())
	{
		auto Payload = MakeShared<FJsonObject>(); Payload->SetStringField(TEXT("jobId"), Job->Id);
		MCPSetRollback(Result, TEXT("fab_remove_installation"), Payload);
	}
	else MCPSetNoRollback(Result, TEXT("All pack files already existed with identical content, so this call created nothing."));
	Result->SetStringField(TEXT("note"), TEXT("Content paths preserved. Config, plugins and project settings were not installed."));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FFabHandlers::RemoveInstallation(const TSharedPtr<FJsonObject>& Params)
{
	FString Error; auto Job = MCPFab::DownloadPrivate::FindJob(Params, Error); if (!Job) return MCPError(Error);
	TArray<FString> Existing;
	TSet<FName> OwnedPackages;
	for (const auto& Pair : Job->CreatedFiles)
	{
		FString Package;
		if (FPackageName::TryConvertFilenameToLongPackageName(Pair.Key, Package)) OwnedPackages.Add(FName(Package));
	}
	auto& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	for (const auto& Pair : Job->CreatedFiles)
	{
		if (!IFileManager::Get().FileExists(*Pair.Key)) continue;
		FSHAHash Hash;
		if (!MCPFab::DownloadPrivate::NoSymlinkParents(Pair.Key) || !MCPFab::DownloadPrivate::FileHash(Pair.Key, Hash) || Hash != Pair.Value)
			return MCPError(TEXT("A file created by this installation changed. No files were removed."));
		FString Package;
		if (FPackageName::TryConvertFilenameToLongPackageName(Pair.Key, Package) && FindPackage(nullptr, *Package))
			return MCPError(TEXT("A package from this installation is loaded. Close its editor or level before removing it."));
		TArray<FName> Referencers;
		if (!Package.IsEmpty()) AssetRegistry.GetReferencers(FName(Package), Referencers);
		for (FName Referencer : Referencers) if (!OwnedPackages.Contains(Referencer)) return MCPError(TEXT("A project asset outside this installation references a pack package. No files were removed."));
		Existing.Add(Pair.Key);
	}
	for (const FString& Path : Existing) if (!IFileManager::Get().Delete(*Path)) return MCPError(TEXT("Could not remove an installation file. Re-run install_pack to restore verified content."));
	TArray<FString> Packages;
	for (const FString& Path : Existing) if (Path.EndsWith(TEXT(".uasset")) || Path.EndsWith(TEXT(".umap"))) Packages.Add(Path);
	AssetRegistry.ScanModifiedAssetFiles(Packages);
	Job->CreatedFiles.Reset();
	auto Result = MCPSuccess(); Result->SetNumberField(TEXT("removedFiles"), Existing.Num()); Result->SetBoolField(TEXT("unchanged"), Existing.IsEmpty());
	if (!Existing.IsEmpty()) { auto Payload = MakeShared<FJsonObject>(); Payload->SetStringField(TEXT("jobId"), Job->Id); MCPSetRollback(Result, TEXT("fab_install_pack"), Payload); }
	else MCPSetNoRollback(Result, TEXT("No files created by this job remained on disk."));
	return MCPResult(Result);
}

namespace MCPFab
{
bool HasActiveDownloads()
{
	for (const auto& Pair : DownloadPrivate::Jobs) if (Pair.Value->Active()) return true;
	return false;
}

void ShutdownDownloads()
{
	for (const auto& Pair : DownloadPrivate::Jobs)
	{
		Pair.Value->StopRequest();
		if (Pair.Value->Verify) Pair.Value->Verify->Cancel.Store(true);
		if (Pair.Value->Verification.IsValid()) Pair.Value->Verification.Wait();
		if (Pair.Value->Installer)
		{
			Pair.Value->Installer->UnregisterMessageHandler(Pair.Value->Cdn.Get());
			Pair.Value->Installer->CancelInstall();
			while (!Pair.Value->Installer->IsComplete()) FPlatformProcess::Sleep(0.01f);
		}
	}
	// Only this category's jobs are cancelled; native Fab downloads remain owned by Fab.
	DownloadPrivate::Jobs.Reset();
}

#if WITH_DEV_AUTOMATION_TESTS
FString StartFixtureDownload(const IBuildManifestRef& Manifest, const FString& Cloud, const FString& Output, bool bVerifyOnly)
{
	auto Job = MakeShared<DownloadPrivate::FJob>(); Job->Manifest = Manifest; Job->Output = Output; Job->Asset.ArtifactId = TEXT("fixture"); Job->FixtureCloud = Cloud; Job->bVerifyOnly = bVerifyOnly;
	DownloadPrivate::Jobs.Add(Job->Id, Job); Job->StartInstaller(Cloud / TEXT("fixture.manifest")); return Job->Id;
}
#endif
#else
} // namespace MCPFab
TSharedPtr<FJsonValue> FFabHandlers::ListOwned(const TSharedPtr<FJsonObject>&) { return MCPError(TEXT("Owned content downloads require UE 5.8 or newer.")); }
TSharedPtr<FJsonValue> FFabHandlers::Download(const TSharedPtr<FJsonObject>& Params) { return ListOwned(Params); }
TSharedPtr<FJsonValue> FFabHandlers::DownloadStatus(const TSharedPtr<FJsonObject>& Params) { return ListOwned(Params); }
TSharedPtr<FJsonValue> FFabHandlers::PauseDownload(const TSharedPtr<FJsonObject>& Params) { return ListOwned(Params); }
TSharedPtr<FJsonValue> FFabHandlers::ResumeDownload(const TSharedPtr<FJsonObject>& Params) { return ListOwned(Params); }
TSharedPtr<FJsonValue> FFabHandlers::CancelDownload(const TSharedPtr<FJsonObject>& Params) { return ListOwned(Params); }
TSharedPtr<FJsonValue> FFabHandlers::InstallPack(const TSharedPtr<FJsonObject>& Params) { return ListOwned(Params); }
TSharedPtr<FJsonValue> FFabHandlers::RemoveInstallation(const TSharedPtr<FJsonObject>& Params) { return ListOwned(Params); }
namespace MCPFab
{
bool HasActiveDownloads() { return false; }
void ShutdownDownloads() {}
#endif
}
