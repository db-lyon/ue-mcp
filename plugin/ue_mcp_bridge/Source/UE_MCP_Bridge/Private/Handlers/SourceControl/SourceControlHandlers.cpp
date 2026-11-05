#include "SourceControlHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerSourceControl.h"

#include "ISourceControlModule.h"
#include "ISourceControlProvider.h"
#include "ISourceControlState.h"
#include "ISourceControlRevision.h"
#include "ISourceControlChangelist.h"
#include "ISourceControlChangelistState.h"
#include "SourceControlOperations.h"
#include "SourceControlHelpers.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Framework/Application/SlateApplication.h"
#include "MCPEngineStatus.h"
#include "Modules/ModuleManager.h"
#include "PackageTools.h"
#include "Runtime/Launch/Resources/Version.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"

// ISourceControlProvider::CanExecuteOperation, asked before an operation runs.
#define UE_MCP_SCC_HAS_CAN_EXECUTE (ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3))

// ─── Provider progress windows ─────────────────────────────────────────────

namespace MCPSourceControlState
{
	/**
	 * A synchronous provider operation shows the engine's progress window
	 * ("Connecting to Revision Control...", with Continue and Cancel) and only
	 * requests its destruction when it finishes, so the window outlives the
	 * call until the next Slate tick. The bridge's dialog gate would refuse the
	 * next request on the strength of it. Scoped over every provider call here:
	 * on the way out it destroys the windows already queued for destruction
	 * and recaptures the engine status.
	 */
	struct FSettleProviderWindows
	{
		~FSettleProviderWindows()
		{
			if (FSlateApplication::IsInitialized() && !FSlateApplication::Get().IsTicking())
			{
				FSlateApplication::Get().DestroyWindowsImmediately();
			}
			FMCPEngineStatus::Get().CaptureNow();
		}
	};
}

// ─── Checkout on write ─────────────────────────────────────────────────────

namespace MCPSourceControlState
{
	bool& AutoCheckoutFlag()
	{
		static bool bEnabled = FPlatformMisc::GetEnvironmentVariable(TEXT("UE_MCP_CHECKOUT_MODE")).Equals(TEXT("auto"), ESearchCase::IgnoreCase);
		return bEnabled;
	}

	TArray<FString>& AutoCheckedOutFiles()
	{
		static TArray<FString> Files;
		return Files;
	}
}

bool MCPSourceControl::IsAutoCheckoutEnabled()
{
	return MCPSourceControlState::AutoCheckoutFlag();
}

void MCPSourceControl::SetAutoCheckoutEnabled(bool bEnabled)
{
	MCPSourceControlState::AutoCheckoutFlag() = bEnabled;
}

bool MCPSourceControl::TryAutoCheckout(const FString& FileName, FString& OutNote)
{
	const MCPSourceControlState::FSettleProviderWindows Settle;
	OutNote.Reset();
	if (!IsAutoCheckoutEnabled()) return false;

	ISourceControlModule& Module = ISourceControlModule::Get();
	if (!Module.IsEnabled())
	{
		OutNote = TEXT("Checkout mode is auto, but no source control provider is enabled in this editor.");
		return false;
	}
	ISourceControlProvider& Provider = Module.GetProvider();
	if (!Provider.IsAvailable())
	{
		OutNote = FString::Printf(TEXT("Checkout mode is auto, but the %s provider is not connected."), *Provider.GetName().ToString());
		return false;
	}
	if (!Provider.UsesCheckout())
	{
		OutNote = FString::Printf(TEXT("The %s provider does not use checkout, so source control did not make the file read-only."), *Provider.GetName().ToString());
		return false;
	}

	const FString Full = FPaths::ConvertRelativePathToFull(FileName);
	const FSourceControlStatePtr State = Provider.GetState(Full, EStateCacheUsage::ForceUpdate);
	if (!State.IsValid())
	{
		OutNote = TEXT("The provider returned no state for the file, so it was not checked out.");
		return false;
	}
	if (!State->IsCheckedOut() && !State->IsAdded())
	{
		FString Who;
		if (State->IsCheckedOutOther(&Who) && !State->CanCheckout())
		{
			OutNote = FString::Printf(TEXT("Not checked out: it is locked by %s."), *Who);
			return false;
		}
		if (State->IsSourceControlled() && !State->IsCurrent())
		{
			OutNote = TEXT("Not checked out: the local copy is not at the latest revision. Sync it first.");
			return false;
		}
		if (!State->CanCheckout())
		{
			OutNote = FString::Printf(TEXT("Not checked out: the provider reports '%s', which cannot be checked out."), *State->GetDisplayName().ToString());
			return false;
		}
		if (Provider.Execute(ISourceControlOperation::Create<FCheckOut>(), Full) != ECommandResult::Succeeded)
		{
			OutNote = TEXT("The provider refused the checkout. The output log carries its reason.");
			return false;
		}
	}
	if (IFileManager::Get().IsReadOnly(*Full))
	{
		OutNote = TEXT("The file is open in source control but still read-only on disk.");
		return false;
	}
	MCPSourceControlState::AutoCheckedOutFiles().AddUnique(Full);
	return true;
}

int32 MCPSourceControl::AutoCheckoutMark()
{
	return MCPSourceControlState::AutoCheckedOutFiles().Num();
}

void MCPSourceControl::AttachAutoCheckouts(const TSharedPtr<FJsonValue>& Result, int32 Mark)
{
	TArray<FString>& Files = MCPSourceControlState::AutoCheckedOutFiles();
	if (Mark < 0 || Mark >= Files.Num())
	{
		return;
	}
	TArray<FString> Taken;
	for (int32 Index = Mark; Index < Files.Num(); ++Index)
	{
		Taken.Add(Files[Index]);
	}
	Files.SetNum(Mark);
	if (Result.IsValid() && Result->Type == EJson::Object && Result->AsObject().IsValid())
	{
		Result->AsObject()->SetArrayField(TEXT("autoCheckedOut"), MCPStringListToJson(Taken));
	}
}

// ─── Helpers ───────────────────────────────────────────────────────────────

namespace MCPSourceControlDetail
{
	/** One caller path resolved to the file the provider is asked about. */
	struct FResolvedPath
	{
		FString Input;
		FString File;
		FString PackageName;
		bool bDirectory = false;
		FString Error;
	};

	FString FullFilePath(const FString& Path)
	{
		FString Full = FPaths::ConvertRelativePathToFull(Path);
		FPaths::NormalizeFilename(Full);
		return Full;
	}

	FString FullDirectoryPath(const FString& Path)
	{
		FString Full = FullFilePath(Path);
		if (!Full.EndsWith(TEXT("/"))) Full += TEXT("/");
		return Full;
	}

	/** A content path (/Game/X, /Game/X.X, /Game/Folder), or a file or folder
	 *  on disk, absolute or relative to the project directory. */
	FResolvedPath ResolvePath(const FString& InPath)
	{
		FResolvedPath Out;
		Out.Input = InPath;
		FString Path = InPath.TrimStartAndEnd();
		Path.ReplaceInline(TEXT("\\"), TEXT("/"));
		if (Path.IsEmpty())
		{
			Out.Error = TEXT("empty path");
			return Out;
		}

		const bool bDrivePath = Path.Len() > 1 && Path[1] == TEXT(':');
		if (Path.StartsWith(TEXT("/")) && !Path.StartsWith(TEXT("//")) && !bDrivePath)
		{
			FString PackageName = Path;
			const bool bTrailingSlash = PackageName.EndsWith(TEXT("/"));
			if (bTrailingSlash) PackageName.LeftChopInline(1);
			int32 Dot = INDEX_NONE;
			if (PackageName.FindLastChar(TEXT('.'), Dot) && PackageName.Find(TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromEnd) < Dot)
			{
				PackageName.LeftInline(Dot);
			}

			if (FPackageName::IsValidLongPackageName(PackageName))
			{
				FString Existing;
				if (!bTrailingSlash && FPackageName::DoesPackageExist(PackageName, &Existing))
				{
					Out.File = FullFilePath(Existing);
					Out.PackageName = PackageName;
					return Out;
				}
				FString Directory;
				if (FPackageName::TryConvertLongPackageNameToFilename(PackageName + TEXT("/"), Directory)
					&& IFileManager::Get().DirectoryExists(*Directory))
				{
					Out.File = FullDirectoryPath(Directory);
					Out.bDirectory = true;
					return Out;
				}
				// Loaded but never saved, or deleted on disk: the file it maps to.
				UPackage* Loaded = FindPackage(nullptr, *PackageName);
				const FString& Extension = Loaded ? PackageFileExtension(Loaded) : FPackageName::GetAssetPackageExtension();
				FString FileName;
				if (!bTrailingSlash && FPackageName::TryConvertLongPackageNameToFilename(PackageName, FileName, Extension))
				{
					Out.File = FullFilePath(FileName);
					Out.PackageName = PackageName;
					return Out;
				}
			}
			// An absolute POSIX path is handled below like any other file path.
			if (!IFileManager::Get().FileExists(*Path) && !IFileManager::Get().DirectoryExists(*Path))
			{
				Out.Error = TEXT("names no mounted content path and no file or folder on disk");
				return Out;
			}
		}

		FString OnDisk = FPaths::IsRelative(Path) ? FPaths::Combine(FPaths::ProjectDir(), Path) : Path;
		if (IFileManager::Get().DirectoryExists(*OnDisk))
		{
			Out.File = FullDirectoryPath(OnDisk);
			Out.bDirectory = true;
			return Out;
		}
		Out.File = FullFilePath(OnDisk);
		FString PackageName;
		if (FPackageName::TryConvertFilenameToLongPackageName(Out.File, PackageName))
		{
			Out.PackageName = PackageName;
		}
		return Out;
	}

	/** Resolve every entry of a paths array. Returns an error result when one is not a string or names nothing. */
	TSharedPtr<FJsonValue> ResolvePaths(const TArray<TSharedPtr<FJsonValue>>& Values, TArray<FResolvedPath>& Out)
	{
		if (Values.Num() == 0)
		{
			return MCPError(TEXT("'paths' is empty. Name at least one asset, content folder or file."));
		}
		for (const TSharedPtr<FJsonValue>& Value : Values)
		{
			FString Text;
			if (!Value.IsValid() || !Value->TryGetString(Text))
			{
				return MCPError(TEXT("Every entry of 'paths' must be a string."));
			}
			FResolvedPath Resolved = ResolvePath(Text);
			if (!Resolved.Error.IsEmpty())
			{
				TSharedPtr<FJsonObject> Error = MCPErrorObject(FString::Printf(TEXT("'%s' %s. Nothing was done."), *Text, *Resolved.Error));
				Error->SetStringField(TEXT("path"), Text);
				return MCPResult(Error);
			}
			Out.Add(MoveTemp(Resolved));
		}
		return nullptr;
	}

	/** The refusal every action but get_provider returns without a usable provider. */
	TSharedPtr<FJsonValue> RequireProvider(ISourceControlProvider*& OutProvider)
	{
		OutProvider = nullptr;
		ISourceControlModule& Module = ISourceControlModule::Get();
		if (!Module.IsEnabled())
		{
			TSharedPtr<FJsonObject> Error = MCPErrorObject(TEXT("Source control is not enabled in this editor. Configure a provider (Perforce, Git, Unity Version Control or Subversion) under Revision Control, then retry. Nothing was done."));
			Error->SetStringField(TEXT("errorCode"), TEXT("source_control_not_enabled"));
			Error->SetBoolField(TEXT("enabled"), false);
			return MCPResult(Error);
		}
		ISourceControlProvider& Provider = Module.GetProvider();
		if (!Provider.IsAvailable())
		{
			TSharedPtr<FJsonObject> Error = MCPErrorObject(FString::Printf(
				TEXT("The %s provider is configured but not connected: %s. Nothing was done."),
				*Provider.GetName().ToString(), *Provider.GetStatusText().ToString()));
			Error->SetStringField(TEXT("errorCode"), TEXT("source_control_unavailable"));
			Error->SetStringField(TEXT("provider"), Provider.GetName().ToString());
			Error->SetBoolField(TEXT("enabled"), true);
			Error->SetBoolField(TEXT("connected"), false);
			return MCPResult(Error);
		}
		OutProvider = &Provider;
		return nullptr;
	}

	TArray<FString> FilesOf(const TArray<FResolvedPath>& Paths)
	{
		TArray<FString> Files;
		for (const FResolvedPath& Path : Paths) Files.Add(Path.File);
		return Files;
	}

	bool IsPackageFile(const FString& File)
	{
		const FString Extension = TEXT(".") + FPaths::GetExtension(File);
		return Extension == FPackageName::GetAssetPackageExtension() || Extension == FPackageName::GetMapPackageExtension();
	}

	UPackage* LoadedPackageForFile(const FString& File)
	{
		FString PackageName;
		if (!IsPackageFile(File) || !FPackageName::TryConvertFilenameToLongPackageName(File, PackageName)) return nullptr;
		return FindPackage(nullptr, *PackageName);
	}

	TSharedPtr<FJsonObject> StateToJson(const FSourceControlStateRef& State)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		const FString& File = State->GetFilename();
		Obj->SetStringField(TEXT("file"), File);
		FString PackageName;
		if (IsPackageFile(File) && FPackageName::TryConvertFilenameToLongPackageName(File, PackageName))
		{
			Obj->SetStringField(TEXT("packageName"), PackageName);
		}
		Obj->SetStringField(TEXT("state"), State->GetDisplayName().ToString());
		Obj->SetBoolField(TEXT("sourceControlled"), State->IsSourceControlled());
		Obj->SetBoolField(TEXT("checkedOut"), State->IsCheckedOut());
		Obj->SetBoolField(TEXT("added"), State->IsAdded());
		Obj->SetBoolField(TEXT("deleted"), State->IsDeleted());
		Obj->SetBoolField(TEXT("modified"), State->IsModified());
		Obj->SetBoolField(TEXT("conflicted"), State->IsConflicted());
		Obj->SetBoolField(TEXT("ignored"), State->IsIgnored());
		Obj->SetBoolField(TEXT("outOfDate"), State->IsSourceControlled() && !State->IsAdded() && !State->IsCurrent());
		FString Who;
		const bool bOther = State->IsCheckedOutOther(&Who);
		Obj->SetBoolField(TEXT("checkedOutByOther"), bOther);
		if (bOther) Obj->SetStringField(TEXT("otherUser"), Who);
		Obj->SetBoolField(TEXT("canCheckout"), State->CanCheckout());
		Obj->SetBoolField(TEXT("canAdd"), State->CanAdd());
		Obj->SetBoolField(TEXT("canRevert"), State->CanRevert());
		Obj->SetBoolField(TEXT("canSubmit"), State->CanCheckIn());
		if (UPackage* Loaded = LoadedPackageForFile(File))
		{
			Obj->SetBoolField(TEXT("unsavedInEditor"), Loaded->IsDirty());
		}
		return Obj;
	}

	/** Changed in a way list_changes reports: open, added, deleted, modified,
	 *  conflicted, or (with bUntracked) on disk and not under control. */
	bool IsChange(const FSourceControlStateRef& State, bool bUntracked, bool bUsesCheckout)
	{
		// A provider without checkout (Git) reports every tracked file as checked out, so being
		// checked out is a change only where checking out is a step of its own.
		if ((bUsesCheckout && State->IsCheckedOut()) || State->IsAdded() || State->IsDeleted() || State->IsModified() || State->IsConflicted())
		{
			return true;
		}
		return bUntracked && !State->IsSourceControlled() && !State->IsIgnored() && !State->IsUnknown()
			&& IFileManager::Get().FileExists(*State->GetFilename());
	}

	bool IsUnder(const FString& File, const TArray<FString>& Roots)
	{
		for (const FString& Root : Roots)
		{
			if (Root.EndsWith(TEXT("/")) ? File.StartsWith(Root, ESearchCase::IgnoreCase) : File.Equals(Root, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
		return false;
	}

	/** Refresh the provider's cache for Files, then read their states. */
	bool QueryStates(ISourceControlProvider& Provider, const TArray<FString>& Files, TArray<FSourceControlStateRef>& OutStates)
	{
		OutStates.Reset();
		return Provider.GetState(Files, OutStates, EStateCacheUsage::ForceUpdate) == ECommandResult::Succeeded;
	}

	TSharedPtr<FJsonObject> Failure(const FString& Input, const FString& File, const FString& Reason)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("path"), Input);
		Obj->SetStringField(TEXT("file"), File);
		Obj->SetStringField(TEXT("reason"), Reason);
		return Obj;
	}

	TArray<TSharedPtr<FJsonValue>> ToJsonArray(const TArray<TSharedPtr<FJsonObject>>& Objects)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const TSharedPtr<FJsonObject>& Obj : Objects) Out.Add(MakeShared<FJsonValueObject>(Obj));
		return Out;
	}

	/** Mark a result failed, naming how many of the requested files failed, when any did. */
	void NoteFailures(const TSharedPtr<FJsonObject>& Result, const TArray<TSharedPtr<FJsonObject>>& Failed, const TCHAR* Verb)
	{
		Result->SetArrayField(TEXT("failed"), ToJsonArray(Failed));
		if (Failed.Num() == 0) return;
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), FString::Printf(
			TEXT("%d file(s) could not be %s; 'failed' says why for each."), Failed.Num(), Verb));
	}

	// ── Line diff ──

	struct FDiffOp
	{
		TCHAR Kind;
		int32 A;
		int32 B;
	};

	/** Unified diff of two line arrays: LCS over the lines between the common
	 *  prefix and suffix, capped so a huge file reports a summary instead. */
	void UnifiedDiff(const TArray<FString>& A, const TArray<FString>& B, int32 Context, int32 MaxLines,
		TArray<FString>& OutLines, int32& OutAdded, int32& OutRemoved, bool& bOutTruncated, bool& bOutApproximate)
	{
		OutLines.Reset();
		OutAdded = OutRemoved = 0;
		bOutTruncated = bOutApproximate = false;

		int32 Prefix = 0;
		while (Prefix < A.Num() && Prefix < B.Num() && A[Prefix].Equals(B[Prefix], ESearchCase::CaseSensitive)) ++Prefix;
		int32 Suffix = 0;
		while (Suffix < A.Num() - Prefix && Suffix < B.Num() - Prefix
			&& A[A.Num() - 1 - Suffix].Equals(B[B.Num() - 1 - Suffix], ESearchCase::CaseSensitive)) ++Suffix;
		const int32 N = A.Num() - Prefix - Suffix;
		const int32 M = B.Num() - Prefix - Suffix;

		TArray<FDiffOp> Ops;
		for (int32 I = 0; I < Prefix; ++I) Ops.Add({ TEXT(' '), I, I });

		if ((int64)N * (int64)M > 4000000)
		{
			bOutApproximate = true;
			for (int32 I = 0; I < N; ++I) Ops.Add({ TEXT('-'), Prefix + I, Prefix });
			for (int32 J = 0; J < M; ++J) Ops.Add({ TEXT('+'), Prefix + N, Prefix + J });
		}
		else
		{
			const int32 W = M + 1;
			TArray<int32> L;
			L.SetNumZeroed((N + 1) * W);
			for (int32 I = N - 1; I >= 0; --I)
			{
				for (int32 J = M - 1; J >= 0; --J)
				{
					L[I * W + J] = A[Prefix + I].Equals(B[Prefix + J], ESearchCase::CaseSensitive)
						? L[(I + 1) * W + J + 1] + 1
						: FMath::Max(L[(I + 1) * W + J], L[I * W + J + 1]);
				}
			}
			int32 I = 0, J = 0;
			while (I < N && J < M)
			{
				if (A[Prefix + I].Equals(B[Prefix + J], ESearchCase::CaseSensitive)) { Ops.Add({ TEXT(' '), Prefix + I, Prefix + J }); ++I; ++J; }
				else if (L[(I + 1) * W + J] >= L[I * W + J + 1]) { Ops.Add({ TEXT('-'), Prefix + I, Prefix + J }); ++I; }
				else { Ops.Add({ TEXT('+'), Prefix + I, Prefix + J }); ++J; }
			}
			for (; I < N; ++I) Ops.Add({ TEXT('-'), Prefix + I, Prefix + J });
			for (; J < M; ++J) Ops.Add({ TEXT('+'), Prefix + I, Prefix + J });
		}
		for (int32 K = 0; K < Suffix; ++K) Ops.Add({ TEXT(' '), A.Num() - Suffix + K, B.Num() - Suffix + K });

		TArray<bool> Keep;
		Keep.Init(false, Ops.Num());
		for (int32 K = 0; K < Ops.Num(); ++K)
		{
			if (Ops[K].Kind == TEXT(' ')) continue;
			if (Ops[K].Kind == TEXT('+')) ++OutAdded; else ++OutRemoved;
			for (int32 D = FMath::Max(0, K - Context); D <= FMath::Min(Ops.Num() - 1, K + Context); ++D) Keep[D] = true;
		}

		int32 K = 0;
		while (K < Ops.Num())
		{
			if (!Keep[K]) { ++K; continue; }
			const int32 Start = K;
			while (K < Ops.Num() && Keep[K]) ++K;
			int32 ALen = 0, BLen = 0;
			for (int32 Q = Start; Q < K; ++Q)
			{
				if (Ops[Q].Kind != TEXT('+')) ++ALen;
				if (Ops[Q].Kind != TEXT('-')) ++BLen;
			}
			if (OutLines.Num() >= MaxLines) { bOutTruncated = true; return; }
			OutLines.Add(FString::Printf(TEXT("@@ -%d,%d +%d,%d @@"), Ops[Start].A + 1, ALen, Ops[Start].B + 1, BLen));
			for (int32 Q = Start; Q < K; ++Q)
			{
				if (OutLines.Num() >= MaxLines) { bOutTruncated = true; return; }
				const FString& Text = Ops[Q].Kind == TEXT('+') ? B[Ops[Q].B] : A[Ops[Q].A];
				OutLines.Add(FString::Printf(TEXT("%c%s"), Ops[Q].Kind, *Text));
			}
		}
	}

	/** The refusal for a provider that is enabled but cannot run Operation.
	 *  CanExecuteOperation is asked where the engine has it (5.3 and later). */
	TSharedPtr<FJsonValue> RequireOperation(ISourceControlProvider& Provider, const FSourceControlOperationRef& Operation, const TCHAR* Action)
	{
#if UE_MCP_SCC_HAS_CAN_EXECUTE
		if (!Provider.CanExecuteOperation(Operation))
		{
			TSharedPtr<FJsonObject> Error = MCPErrorObject(FString::Printf(
				TEXT("The %s provider does not support %s (operation %s). Nothing was done."),
				*Provider.GetName().ToString(), Action, *Operation->GetName().ToString()));
			Error->SetStringField(TEXT("errorCode"), TEXT("source_control_operation_unsupported"));
			Error->SetStringField(TEXT("provider"), Provider.GetName().ToString());
			Error->SetStringField(TEXT("operation"), Operation->GetName().ToString());
			return MCPResult(Error);
		}
#endif
		return nullptr;
	}

	/** The provider's own messages from an executed operation. */
	void AddResultInfo(const TSharedPtr<FJsonObject>& Result, const FSourceControlOperationRef& Operation)
	{
		const FSourceControlResultInfo& Info = Operation->GetResultInfo();
		TArray<FString> Errors, Messages;
		for (const FText& Text : Info.ErrorMessages) Errors.Add(Text.ToString());
		for (const FText& Text : Info.InfoMessages) Messages.Add(Text.ToString());
		if (Errors.Num() > 0) Result->SetArrayField(TEXT("providerErrors"), MCPStringListToJson(Errors));
		if (Messages.Num() > 0) Result->SetArrayField(TEXT("providerMessages"), MCPStringListToJson(Messages));
	}

	/** The folders list_changes and sync cover when no paths are named. */
	TArray<FString> DefaultProjectRoots()
	{
		TArray<FString> Roots;
		Roots.Add(FullDirectoryPath(FPaths::ProjectContentDir()));
		Roots.Add(FullDirectoryPath(FPaths::ProjectConfigDir()));
		if (IFileManager::Get().DirectoryExists(*FPaths::GameSourceDir())) Roots.Add(FullDirectoryPath(FPaths::GameSourceDir()));
		if (IFileManager::Get().DirectoryExists(*FPaths::ProjectPluginsDir())) Roots.Add(FullDirectoryPath(FPaths::ProjectPluginsDir()));
		if (FPaths::IsProjectFilePathSet()) Roots.Add(FullFilePath(FPaths::GetProjectFilePath()));
		return Roots;
	}

	/** Every loaded package whose file on disk lies under Roots, by that file. */
	TMap<FString, UPackage*> LoadedPackagesUnder(const TArray<FString>& Roots)
	{
		TMap<FString, UPackage*> Out;
		for (TObjectIterator<UPackage> It; It; ++It)
		{
			UPackage* Package = *It;
			if (!Package || Package->HasAnyPackageFlags(PKG_CompiledIn) || Package == GetTransientPackage()) continue;
			FString FileName;
			if (!FPackageName::TryConvertLongPackageNameToFilename(Package->GetName(), FileName, PackageFileExtension(Package))) continue;
			FileName = FullFilePath(FileName);
			if (IsUnder(FileName, Roots) && IFileManager::Get().FileExists(*FileName)) Out.Add(FileName, Package);
		}
		return Out;
	}

	const TCHAR* StatusKeyName(ISourceControlProvider::EStatus Key)
	{
		// Indexed by value: the enum only ever grows at the end.
		static const TCHAR* Names[] = {
			TEXT("enabled"), TEXT("connected"), TEXT("port"), TEXT("user"), TEXT("client"),
			TEXT("repository"), TEXT("remote"), TEXT("branch"), TEXT("email"), TEXT("scmVersion"),
			TEXT("pluginVersion"), TEXT("workspace"), TEXT("workspacePath"), TEXT("changeset"),
		};
		const int32 Index = static_cast<int32>(Key);
		return Index >= 0 && Index < static_cast<int32>(UE_ARRAY_COUNT(Names)) ? Names[Index] : TEXT("other");
	}
}

using namespace MCPSourceControlDetail;

// ─── Actions ───────────────────────────────────────────────────────────────

TSharedPtr<FJsonValue> FSourceControlHandlers::GetProvider(const TSharedPtr<FJsonObject>& Params)
{
	const MCPSourceControlState::FSettleProviderWindows Settle;
	TSharedPtr<FJsonObject> Result = MCPSuccess();
	ISourceControlModule& Module = ISourceControlModule::Get();
	const bool bEnabled = Module.IsEnabled();
	Result->SetBoolField(TEXT("enabled"), bEnabled);
	Result->SetStringField(TEXT("checkoutMode"), MCPSourceControl::IsAutoCheckoutEnabled() ? TEXT("auto") : TEXT("manual"));
	if (!bEnabled)
	{
		Result->SetStringField(TEXT("provider"), TEXT("None"));
		Result->SetBoolField(TEXT("connected"), false);
		Result->SetStringField(TEXT("note"), TEXT("No source control provider is enabled. Configure Perforce, Git, Unity Version Control or Subversion under Revision Control in the editor; every other source_control action refuses until then."));
		return MCPResult(Result);
	}

	ISourceControlProvider& Provider = Module.GetProvider();
	Result->SetStringField(TEXT("provider"), Provider.GetName().ToString());
	Result->SetBoolField(TEXT("connected"), Provider.IsAvailable());
	Result->SetStringField(TEXT("statusText"), Provider.GetStatusText().ToString());

	TSharedPtr<FJsonObject> Info = MakeShared<FJsonObject>();
	for (const TPair<ISourceControlProvider::EStatus, FString>& Pair : Provider.GetStatus())
	{
		Info->SetStringField(StatusKeyName(Pair.Key), Pair.Value);
	}
	Result->SetObjectField(TEXT("info"), Info);

	TSharedPtr<FJsonObject> Caps = MakeShared<FJsonObject>();
	Caps->SetBoolField(TEXT("usesCheckout"), Provider.UsesCheckout());
	Caps->SetBoolField(TEXT("usesChangelists"), Provider.UsesChangelists());
	Caps->SetBoolField(TEXT("usesLocalReadOnlyState"), Provider.UsesLocalReadOnlyState());
	Caps->SetBoolField(TEXT("usesFileRevisions"), Provider.UsesFileRevisions());
	Caps->SetBoolField(TEXT("allowsDiffAgainstDepot"), Provider.AllowsDiffAgainstDepot());
	Result->SetObjectField(TEXT("capabilities"), Caps);

	// Which source_control actions this provider can run, asked of the provider
	// itself rather than inferred from its name.
#if UE_MCP_SCC_HAS_CAN_EXECUTE
	TSharedPtr<FJsonObject> Operations = MakeShared<FJsonObject>();
	Operations->SetBoolField(TEXT("checkout"), Provider.CanExecuteOperation(ISourceControlOperation::Create<FCheckOut>()));
	Operations->SetBoolField(TEXT("add"), Provider.CanExecuteOperation(ISourceControlOperation::Create<FMarkForAdd>()));
	Operations->SetBoolField(TEXT("mark_for_delete"), Provider.CanExecuteOperation(ISourceControlOperation::Create<FDelete>()));
	Operations->SetBoolField(TEXT("revert"), Provider.CanExecuteOperation(ISourceControlOperation::Create<FRevert>()));
	Operations->SetBoolField(TEXT("sync"), Provider.CanExecuteOperation(ISourceControlOperation::Create<FSync>()));
	Operations->SetBoolField(TEXT("submit"), Provider.CanExecuteOperation(ISourceControlOperation::Create<FCheckIn>()));
	Operations->SetBoolField(TEXT("history"), Provider.CanExecuteOperation(ISourceControlOperation::Create<FUpdateStatus>()));
	Operations->SetBoolField(TEXT("connect"), Provider.CanExecuteOperation(ISourceControlOperation::Create<FConnect>()));
	Result->SetObjectField(TEXT("operations"), Operations);
#else
	Result->SetStringField(TEXT("operationsNote"), TEXT("This engine version cannot ask the provider which operations it supports."));
#endif
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FSourceControlHandlers::GetStatus(const TSharedPtr<FJsonObject>& Params)
{
	const MCPSourceControlState::FSettleProviderWindows Settle;
	const TArray<TSharedPtr<FJsonValue>>* PathValues = nullptr;
	if (auto Err = RequireArray(Params, TEXT("paths"), PathValues)) return Err;
	const int32 Limit = FMath::Max(1, OptionalInt(Params, TEXT("limit"), 500));

	TArray<FResolvedPath> Paths;
	if (auto Err = ResolvePaths(*PathValues, Paths)) return Err;
	ISourceControlProvider* Provider = nullptr;
	if (auto Err = RequireProvider(Provider)) return Err;

	TArray<FString> Files, Directories;
	for (const FResolvedPath& Path : Paths)
	{
		(Path.bDirectory ? Directories : Files).Add(Path.File);
	}

	TArray<FSourceControlStateRef> States;
	if (Files.Num() > 0 && !QueryStates(*Provider, Files, States))
	{
		return MCPError(TEXT("The provider could not report the state of the named files. The output log carries its reason."));
	}
	if (Directories.Num() > 0)
	{
		TSharedRef<FUpdateStatus, ESPMode::ThreadSafe> Update = ISourceControlOperation::Create<FUpdateStatus>();
		Provider->Execute(Update, Directories);
		States.Append(Provider->GetCachedStateByPredicate([&Directories](const FSourceControlStateRef& State)
		{
			return IsUnder(State->GetFilename(), Directories);
		}));
	}

	TArray<TSharedPtr<FJsonValue>> Out;
	TSet<FString> Seen;
	bool bTruncated = false;
	for (const FSourceControlStateRef& State : States)
	{
		if (Seen.Contains(State->GetFilename())) continue;
		if (Out.Num() >= Limit) { bTruncated = true; break; }
		Seen.Add(State->GetFilename());
		Out.Add(MakeShared<FJsonValueObject>(StateToJson(State)));
	}

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetStringField(TEXT("provider"), Provider->GetName().ToString());
	Result->SetArrayField(TEXT("files"), Out);
	Result->SetNumberField(TEXT("count"), Out.Num());
	Result->SetBoolField(TEXT("truncated"), bTruncated);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FSourceControlHandlers::ListChanges(const TSharedPtr<FJsonObject>& Params)
{
	const MCPSourceControlState::FSettleProviderWindows Settle;
	const TArray<TSharedPtr<FJsonValue>>* PathValues = nullptr;
	const bool bHasPaths = TryGetArrayParam(Params, TEXT("paths"), PathValues) && PathValues;
	const int32 Limit = FMath::Max(1, OptionalInt(Params, TEXT("limit"), 500));
	const bool bUntracked = OptionalBool(Params, TEXT("includeUntracked"), true);

	TArray<FString> Roots;
	if (bHasPaths)
	{
		TArray<FResolvedPath> Paths;
		if (auto Err = ResolvePaths(*PathValues, Paths)) return Err;
		Roots = FilesOf(Paths);
	}
	ISourceControlProvider* Provider = nullptr;
	if (auto Err = RequireProvider(Provider)) return Err;

	if (!bHasPaths) Roots = DefaultProjectRoots();

	TSharedRef<FUpdateStatus, ESPMode::ThreadSafe> Update = ISourceControlOperation::Create<FUpdateStatus>();
	Update->SetGetOpenedOnly(!bUntracked);
	const bool bUpdated = Provider->Execute(Update, Roots) == ECommandResult::Succeeded;

	const bool bUsesCheckout = Provider->UsesCheckout();
	TArray<FSourceControlStateRef> States = Provider->GetCachedStateByPredicate([&Roots, bUntracked, bUsesCheckout](const FSourceControlStateRef& State)
	{
		return IsUnder(State->GetFilename(), Roots) && IsChange(State, bUntracked, bUsesCheckout);
	});
	States.Sort([](const FSourceControlStateRef& L, const FSourceControlStateRef& R) { return L->GetFilename() < R->GetFilename(); });

	// File -> changelist display name, where the provider groups by changelist.
	TMap<FString, FString> FileChangelist;
	TArray<TSharedPtr<FJsonValue>> Changelists;
	if (Provider->UsesChangelists())
	{
		TSharedRef<FUpdatePendingChangelistsStatus, ESPMode::ThreadSafe> Pending = ISourceControlOperation::Create<FUpdatePendingChangelistsStatus>();
		Pending->SetUpdateAllChangelists(true);
		Pending->SetUpdateFilesStates(true);
		Provider->Execute(Pending);
		for (const FSourceControlChangelistRef& Changelist : Provider->GetChangelists(EStateCacheUsage::Use))
		{
			const FSourceControlChangelistStatePtr ChangelistState = Provider->GetState(Changelist, EStateCacheUsage::Use);
			if (!ChangelistState.IsValid()) continue;
			const FString Name = ChangelistState->GetDisplayText().ToString();
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("changelist"), Name);
			Entry->SetStringField(TEXT("description"), ChangelistState->GetDescriptionText().ToString());
			Entry->SetBoolField(TEXT("isDefault"), Changelist->IsDefault());
			TArray<FString> ChangelistFiles;
			for (const FSourceControlStateRef& FileState : ChangelistState->GetFilesStates())
			{
				ChangelistFiles.Add(FileState->GetFilename());
				FileChangelist.Add(FileState->GetFilename(), Name);
			}
			Entry->SetArrayField(TEXT("files"), MCPStringListToJson(ChangelistFiles));
			Changelists.Add(MakeShared<FJsonValueObject>(Entry));
		}
	}

	TArray<TSharedPtr<FJsonValue>> Out;
	bool bTruncated = false;
	for (const FSourceControlStateRef& State : States)
	{
		if (Out.Num() >= Limit) { bTruncated = true; break; }
		TSharedPtr<FJsonObject> Entry = StateToJson(State);
		if (const FString* Changelist = FileChangelist.Find(State->GetFilename()))
		{
			Entry->SetStringField(TEXT("changelist"), *Changelist);
		}
		Out.Add(MakeShared<FJsonValueObject>(Entry));
	}

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetStringField(TEXT("provider"), Provider->GetName().ToString());
	Result->SetArrayField(TEXT("roots"), MCPStringListToJson(Roots));
	Result->SetArrayField(TEXT("files"), Out);
	Result->SetNumberField(TEXT("count"), Out.Num());
	Result->SetNumberField(TEXT("total"), States.Num());
	Result->SetBoolField(TEXT("truncated"), bTruncated);
	if (Provider->UsesChangelists()) Result->SetArrayField(TEXT("changelists"), Changelists);
	if (!bUpdated)
	{
		Result->SetStringField(TEXT("warning"), TEXT("The provider's status refresh failed, so this list comes from its cache and may be stale."));
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FSourceControlHandlers::Checkout(const TSharedPtr<FJsonObject>& Params)
{
	const MCPSourceControlState::FSettleProviderWindows Settle;
	const TArray<TSharedPtr<FJsonValue>>* PathValues = nullptr;
	if (auto Err = RequireArray(Params, TEXT("paths"), PathValues)) return Err;
	TArray<FResolvedPath> Paths;
	if (auto Err = ResolvePaths(*PathValues, Paths)) return Err;
	ISourceControlProvider* Provider = nullptr;
	if (auto Err = RequireProvider(Provider)) return Err;

	TArray<TSharedPtr<FJsonObject>> Failed;
	TArray<FString> Already, NotRequired, ToCheckout;
	TMap<FString, FString> InputOf;
	for (const FResolvedPath& Path : Paths)
	{
		InputOf.Add(Path.File, Path.Input);
		if (Path.bDirectory) Failed.Add(Failure(Path.Input, Path.File, TEXT("a folder; name the files to check out")));
	}

	TArray<FSourceControlStateRef> States;
	TArray<FString> Files;
	for (const FResolvedPath& Path : Paths) if (!Path.bDirectory) Files.Add(Path.File);
	if (Files.Num() > 0 && !QueryStates(*Provider, Files, States))
	{
		return MCPError(TEXT("The provider could not report the state of the named files, so nothing was checked out."));
	}
	for (const FSourceControlStateRef& State : States)
	{
		const FString& File = State->GetFilename();
		const FString Input = InputOf.FindRef(File);
		FString Who;
		if (!Provider->UsesCheckout()) NotRequired.Add(File);
		else if (State->IsCheckedOut() || State->IsAdded()) Already.Add(File);
		else if (!State->IsSourceControlled()) Failed.Add(Failure(Input, File, TEXT("not under source control; use add")));
		else if (State->IsCheckedOutOther(&Who) && !State->CanCheckout()) Failed.Add(Failure(Input, File, FString::Printf(TEXT("locked by %s"), *Who)));
		else if (!State->IsCurrent()) Failed.Add(Failure(Input, File, TEXT("not at the latest revision; sync it first")));
		else if (!State->CanCheckout()) Failed.Add(Failure(Input, File, FString::Printf(TEXT("cannot be checked out in state '%s'"), *State->GetDisplayName().ToString())));
		else ToCheckout.Add(File);
	}

	TArray<FString> CheckedOut;
	if (ToCheckout.Num() > 0)
	{
		Provider->Execute(ISourceControlOperation::Create<FCheckOut>(), ToCheckout);
		TArray<FSourceControlStateRef> After;
		QueryStates(*Provider, ToCheckout, After);
		for (const FSourceControlStateRef& State : After)
		{
			if (State->IsCheckedOut()) CheckedOut.Add(State->GetFilename());
			else Failed.Add(Failure(InputOf.FindRef(State->GetFilename()), State->GetFilename(), TEXT("the provider refused the checkout; the output log carries its reason")));
		}
	}

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetStringField(TEXT("provider"), Provider->GetName().ToString());
	Result->SetArrayField(TEXT("checkedOut"), MCPStringListToJson(CheckedOut));
	Result->SetArrayField(TEXT("alreadyCheckedOut"), MCPStringListToJson(Already));
	if (NotRequired.Num() > 0)
	{
		Result->SetArrayField(TEXT("checkoutNotRequired"), MCPStringListToJson(NotRequired));
		Result->SetStringField(TEXT("note"), FString::Printf(TEXT("The %s provider does not use checkout; those files are writable as they are."), *Provider->GetName().ToString()));
	}
	Result->SetBoolField(TEXT("unchanged"), CheckedOut.Num() == 0);
	NoteFailures(Result, Failed, TEXT("checked out"));
	if (CheckedOut.Num() > 0)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetArrayField(TEXT("paths"), MCPStringListToJson(CheckedOut));
		MCPSetRollback(Result, TEXT("source_control_revert"), Payload);
	}
	else
	{
		MCPSetNoRollback(Result, TEXT("Nothing was checked out by this call, so there is nothing to revert."));
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FSourceControlHandlers::MarkForAdd(const TSharedPtr<FJsonObject>& Params)
{
	const MCPSourceControlState::FSettleProviderWindows Settle;
	const TArray<TSharedPtr<FJsonValue>>* PathValues = nullptr;
	if (auto Err = RequireArray(Params, TEXT("paths"), PathValues)) return Err;
	TArray<FResolvedPath> Paths;
	if (auto Err = ResolvePaths(*PathValues, Paths)) return Err;
	ISourceControlProvider* Provider = nullptr;
	if (auto Err = RequireProvider(Provider)) return Err;

	TArray<TSharedPtr<FJsonObject>> Failed;
	TArray<FString> Already, ToAdd, Files;
	TMap<FString, FString> InputOf;
	for (const FResolvedPath& Path : Paths)
	{
		InputOf.Add(Path.File, Path.Input);
		if (Path.bDirectory) Failed.Add(Failure(Path.Input, Path.File, TEXT("a folder; name the files to add")));
		else if (!IFileManager::Get().FileExists(*Path.File)) Failed.Add(Failure(Path.Input, Path.File, TEXT("not on disk; save it first")));
		else Files.Add(Path.File);
	}

	TArray<FSourceControlStateRef> States;
	if (Files.Num() > 0 && !QueryStates(*Provider, Files, States))
	{
		return MCPError(TEXT("The provider could not report the state of the named files, so nothing was added."));
	}
	for (const FSourceControlStateRef& State : States)
	{
		const FString& File = State->GetFilename();
		if (State->IsAdded() || (State->IsSourceControlled() && !State->IsDeleted())) Already.Add(File);
		else if (State->IsIgnored()) Failed.Add(Failure(InputOf.FindRef(File), File, TEXT("ignored by the provider")));
		else if (!State->CanAdd()) Failed.Add(Failure(InputOf.FindRef(File), File, FString::Printf(TEXT("cannot be added in state '%s'"), *State->GetDisplayName().ToString())));
		else ToAdd.Add(File);
	}

	TArray<FString> Added;
	if (ToAdd.Num() > 0)
	{
		Provider->Execute(ISourceControlOperation::Create<FMarkForAdd>(), ToAdd);
		TArray<FSourceControlStateRef> After;
		QueryStates(*Provider, ToAdd, After);
		for (const FSourceControlStateRef& State : After)
		{
			if (State->IsAdded() || State->IsSourceControlled()) Added.Add(State->GetFilename());
			else Failed.Add(Failure(InputOf.FindRef(State->GetFilename()), State->GetFilename(), TEXT("the provider refused the add; the output log carries its reason")));
		}
	}

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetStringField(TEXT("provider"), Provider->GetName().ToString());
	Result->SetArrayField(TEXT("added"), MCPStringListToJson(Added));
	Result->SetArrayField(TEXT("alreadyControlled"), MCPStringListToJson(Already));
	Result->SetBoolField(TEXT("unchanged"), Added.Num() == 0);
	NoteFailures(Result, Failed, TEXT("added"));
	if (Added.Num() > 0)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetArrayField(TEXT("paths"), MCPStringListToJson(Added));
		MCPSetRollback(Result, TEXT("source_control_revert"), Payload);
	}
	else
	{
		MCPSetNoRollback(Result, TEXT("Nothing was marked for add by this call, so there is nothing to revert."));
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FSourceControlHandlers::Revert(const TSharedPtr<FJsonObject>& Params)
{
	const MCPSourceControlState::FSettleProviderWindows Settle;
	const TArray<TSharedPtr<FJsonValue>>* PathValues = nullptr;
	if (auto Err = RequireArray(Params, TEXT("paths"), PathValues)) return Err;
	const bool bDryRun = OptionalBool(Params, TEXT("dryRun"), false);
	const bool bDiscardUnsaved = OptionalBool(Params, TEXT("discardUnsaved"), false);
	const TSharedPtr<FJsonObject>* RestoreFromParam = nullptr;
	const TSharedPtr<FJsonObject> RestoreFrom = TryGetObjectParam(Params, TEXT("restoreFrom"), RestoreFromParam) && RestoreFromParam ? *RestoreFromParam : nullptr;
	TArray<FResolvedPath> Paths;
	if (auto Err = ResolvePaths(*PathValues, Paths)) return Err;
	ISourceControlProvider* Provider = nullptr;
	if (auto Err = RequireProvider(Provider)) return Err;

	TArray<TSharedPtr<FJsonObject>> Failed;
	TArray<FString> Files, NotOpen;
	TMap<FString, FString> InputOf;
	for (const FResolvedPath& Path : Paths)
	{
		InputOf.Add(Path.File, Path.Input);
		if (Path.bDirectory) Failed.Add(Failure(Path.Input, Path.File, TEXT("a folder; name the files to revert")));
		else Files.Add(Path.File);
	}

	TArray<FSourceControlStateRef> States;
	if (Files.Num() > 0 && !QueryStates(*Provider, Files, States))
	{
		return MCPError(TEXT("The provider could not report the state of the named files, so nothing was reverted."));
	}

	TArray<FString> Candidates, Unsaved;
	TArray<TSharedPtr<FJsonValue>> WouldLose;
	for (const FSourceControlStateRef& State : States)
	{
		const FString& File = State->GetFilename();
		if (!State->CanRevert())
		{
			NotOpen.Add(File);
			continue;
		}
		Candidates.Add(File);
		TSharedPtr<FJsonObject> Entry = StateToJson(State);
		if (UPackage* Loaded = LoadedPackageForFile(File))
		{
			if (Loaded->IsDirty()) Unsaved.Add(File);
		}
		WouldLose.Add(MakeShared<FJsonValueObject>(Entry));
	}

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetStringField(TEXT("provider"), Provider->GetName().ToString());
	Result->SetArrayField(TEXT("notOpen"), MCPStringListToJson(NotOpen));
	Result->SetArrayField(TEXT("unsavedInEditor"), MCPStringListToJson(Unsaved));
	MCPSetNoRollback(Result, TEXT("A revert discards the local edits, and nothing restores them."));

	if (bDryRun)
	{
		Result->SetBoolField(TEXT("dryRun"), true);
		Result->SetArrayField(TEXT("wouldRevert"), WouldLose);
		Result->SetBoolField(TEXT("unchanged"), true);
		Result->SetArrayField(TEXT("failed"), ToJsonArray(Failed));
		return MCPResult(Result);
	}
	if (Unsaved.Num() > 0 && !bDiscardUnsaved)
	{
		TSharedPtr<FJsonObject> Error = MCPErrorObject(FString::Printf(
			TEXT("%d file(s) have unsaved edits in the editor that a revert would discard. Save them, or pass discardUnsaved=true to drop them. Nothing was reverted."),
			Unsaved.Num()));
		Error->SetArrayField(TEXT("unsavedInEditor"), MCPStringListToJson(Unsaved));
		Error->SetArrayField(TEXT("wouldRevert"), WouldLose);
		return MCPResult(Error);
	}

	TArray<FString> PackageFiles, OtherFiles;
	for (const FString& File : Candidates)
	{
		if (UPackage* Loaded = LoadedPackageForFile(File))
		{
			// The reload below would otherwise ask whether to discard them.
			if (bDiscardUnsaved && Loaded->IsDirty()) Loaded->SetDirtyFlag(false);
		}
		(IsPackageFile(File) ? PackageFiles : OtherFiles).Add(File);
	}
	// Packages are reverted through the helper that unloads and reloads them,
	// so a loaded asset does not keep the reverted file open or stale.
	if (PackageFiles.Num() > 0) USourceControlHelpers::RevertAndReloadPackages(PackageFiles);
	if (OtherFiles.Num() > 0) Provider->Execute(ISourceControlOperation::Create<FRevert>(), OtherFiles);

	// A provider whose revert cannot undo a delete it staged (Git checks the file
	// out of an index that no longer has it) leaves the file gone. When the call
	// that deleted it saved a copy (mark_for_delete's rollback names it under
	// restoreFrom), the copy is put back and marked for add, which returns the file
	// to its state before the delete.
	if (RestoreFrom.IsValid() && OtherFiles.Num() > 0)
	{
		TArray<FSourceControlStateRef> AfterRevert;
		QueryStates(*Provider, OtherFiles, AfterRevert);
		TArray<FString> Restored;
		for (const FSourceControlStateRef& State : AfterRevert)
		{
			const FString File = State->GetFilename();
			FString Copy;
			if (!State->IsDeleted() || FPaths::FileExists(File) || !RestoreFrom->TryGetStringField(File, Copy)) continue;
			if (FPaths::FileExists(Copy) && IFileManager::Get().Copy(*File, *Copy) == COPY_OK) Restored.Add(File);
		}
		if (Restored.Num() > 0) Provider->Execute(ISourceControlOperation::Create<FMarkForAdd>(), Restored);
	}

	TArray<FString> Reverted;
	if (Candidates.Num() > 0)
	{
		TArray<FSourceControlStateRef> After;
		QueryStates(*Provider, Candidates, After);
		for (const FSourceControlStateRef& State : After)
		{
			if (!State->CanRevert()) Reverted.Add(State->GetFilename());
			else Failed.Add(Failure(InputOf.FindRef(State->GetFilename()), State->GetFilename(),
				TEXT("still open after the revert; a loaded map or external actor package must be unloaded first, and the output log carries the provider's reason")));
		}
	}
	Result->SetArrayField(TEXT("reverted"), MCPStringListToJson(Reverted));
	Result->SetBoolField(TEXT("unchanged"), Reverted.Num() == 0);
	NoteFailures(Result, Failed, TEXT("reverted"));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FSourceControlHandlers::Diff(const TSharedPtr<FJsonObject>& Params)
{
	const MCPSourceControlState::FSettleProviderWindows Settle;
	FString InPath;
	if (auto Err = RequireString(Params, TEXT("path"), InPath)) return Err;
	const bool bOpenEditor = OptionalBool(Params, TEXT("openEditor"), false);
	const int32 Context = FMath::Clamp(OptionalInt(Params, TEXT("contextLines"), 3), 0, 50);
	const int32 MaxLines = FMath::Clamp(OptionalInt(Params, TEXT("maxLines"), 400), 1, 20000);

	const FResolvedPath Path = ResolvePath(InPath);
	if (!Path.Error.IsEmpty()) return MCPError(FString::Printf(TEXT("'%s' %s."), *InPath, *Path.Error));
	if (Path.bDirectory) return MCPError(FString::Printf(TEXT("'%s' is a folder. diff takes one file."), *InPath));
	ISourceControlProvider* Provider = nullptr;
	if (auto Err = RequireProvider(Provider)) return Err;

	TSharedRef<FUpdateStatus, ESPMode::ThreadSafe> Update = ISourceControlOperation::Create<FUpdateStatus>();
	Update->SetUpdateHistory(true);
	Provider->Execute(Update, Path.File);
	const FSourceControlStatePtr State = Provider->GetState(Path.File, EStateCacheUsage::Use);
	if (!State.IsValid() || !State->IsSourceControlled())
	{
		return MCPError(FString::Printf(TEXT("'%s' is not under source control, so there is no revision to diff against."), *Path.File));
	}

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetStringField(TEXT("provider"), Provider->GetName().ToString());
	Result->SetStringField(TEXT("file"), Path.File);
	Result->SetObjectField(TEXT("status"), StateToJson(State.ToSharedRef()));
	if (State->IsAdded())
	{
		Result->SetBoolField(TEXT("newFile"), true);
		Result->SetStringField(TEXT("note"), TEXT("The file is marked for add and has no depot revision; all of it is new."));
		return MCPResult(Result);
	}
	if (State->GetHistorySize() == 0)
	{
		return MCPError(FString::Printf(TEXT("The %s provider returned no history for '%s', so it cannot be diffed here."), *Provider->GetName().ToString(), *Path.File));
	}
	const TSharedPtr<ISourceControlRevision, ESPMode::ThreadSafe> Revision = State->GetHistoryItem(0);
	if (!Revision.IsValid())
	{
		return MCPError(TEXT("The provider's latest revision entry is empty, so there is nothing to diff against."));
	}
	TSharedPtr<FJsonObject> Against = MakeShared<FJsonObject>();
	Against->SetStringField(TEXT("revision"), Revision->GetRevision());
	Against->SetStringField(TEXT("user"), Revision->GetUserName());
	Against->SetStringField(TEXT("description"), Revision->GetDescription());
	Against->SetStringField(TEXT("date"), Revision->GetDate().ToIso8601());
	Result->SetObjectField(TEXT("against"), Against);

	if (IsPackageFile(Path.File))
	{
		Result->SetStringField(TEXT("kind"), TEXT("package"));
		Result->SetNumberField(TEXT("localBytes"), (double)IFileManager::Get().FileSize(*Path.File));
		Result->SetNumberField(TEXT("depotBytes"), Revision->GetFileSize());
		if (!bOpenEditor)
		{
			Result->SetStringField(TEXT("note"), TEXT("Packages are binary. Pass openEditor=true to open the editor's asset diff against this revision."));
			return MCPResult(Result);
		}
		if (!Provider->AllowsDiffAgainstDepot())
		{
			return MCPError(FString::Printf(TEXT("The %s provider does not support diffing against the depot."), *Provider->GetName().ToString()));
		}
		UPackage* Package = Path.PackageName.IsEmpty() ? nullptr : LoadPackage(nullptr, *Path.PackageName, LOAD_None);
		UObject* Asset = Package ? Package->FindAssetInPackage() : nullptr;
		if (!Asset)
		{
			return MCPError(FString::Printf(TEXT("'%s' holds no asset the editor can open a diff for."), *Path.File));
		}
		FAssetToolsModule& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
		AssetTools.Get().DiffAgainstDepot(Asset, Path.PackageName, Asset->GetName());
		Result->SetBoolField(TEXT("editorDiffOpened"), true);
		return MCPResult(Result);
	}

	FString DepotCopy;
	if (!Revision->Get(DepotCopy))
	{
		return MCPError(TEXT("The provider could not fetch the depot revision's content."));
	}
	TArray<FString> DepotLines, LocalLines;
	FFileHelper::LoadFileToStringArray(DepotLines, *DepotCopy);
	const bool bLocalExists = IFileManager::Get().FileExists(*Path.File);
	if (bLocalExists) FFileHelper::LoadFileToStringArray(LocalLines, *Path.File);

	TArray<FString> Lines;
	int32 Added = 0, Removed = 0;
	bool bTruncated = false, bApproximate = false;
	UnifiedDiff(DepotLines, LocalLines, Context, MaxLines, Lines, Added, Removed, bTruncated, bApproximate);
	Result->SetStringField(TEXT("kind"), TEXT("text"));
	Result->SetBoolField(TEXT("localExists"), bLocalExists);
	Result->SetBoolField(TEXT("identical"), Added == 0 && Removed == 0);
	Result->SetNumberField(TEXT("addedLines"), Added);
	Result->SetNumberField(TEXT("removedLines"), Removed);
	Result->SetStringField(TEXT("diff"), FString::Join(Lines, TEXT("\n")));
	Result->SetBoolField(TEXT("truncated"), bTruncated);
	if (bApproximate)
	{
		Result->SetStringField(TEXT("note"), TEXT("The changed region is too large to align line by line, so it is shown as one removal and one addition."));
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FSourceControlHandlers::Submit(const TSharedPtr<FJsonObject>& Params)
{
	const MCPSourceControlState::FSettleProviderWindows Settle;
	const TArray<TSharedPtr<FJsonValue>>* PathValues = nullptr;
	if (auto Err = RequireArray(Params, TEXT("paths"), PathValues)) return Err;
	FString Description;
	if (auto Err = RequireString(Params, TEXT("description"), Description)) return Err;
	TArray<FResolvedPath> Paths;
	if (auto Err = ResolvePaths(*PathValues, Paths)) return Err;
	ISourceControlProvider* Provider = nullptr;
	if (auto Err = RequireProvider(Provider)) return Err;

	TArray<TSharedPtr<FJsonObject>> Failed;
	TArray<FString> Files, ToSubmit, Unsaved;
	TMap<FString, FString> InputOf;
	for (const FResolvedPath& Path : Paths)
	{
		InputOf.Add(Path.File, Path.Input);
		if (Path.bDirectory) Failed.Add(Failure(Path.Input, Path.File, TEXT("a folder; name the files to submit")));
		else Files.Add(Path.File);
	}
	TArray<FSourceControlStateRef> States;
	if (Files.Num() > 0 && !QueryStates(*Provider, Files, States))
	{
		return MCPError(TEXT("The provider could not report the state of the named files, so nothing was submitted."));
	}
	for (const FSourceControlStateRef& State : States)
	{
		const FString& File = State->GetFilename();
		if (!State->CanCheckIn())
		{
			Failed.Add(Failure(InputOf.FindRef(File), File, FString::Printf(TEXT("nothing to submit in state '%s'"), *State->GetDisplayName().ToString())));
			continue;
		}
		if (UPackage* Loaded = LoadedPackageForFile(File))
		{
			if (Loaded->IsDirty()) Unsaved.Add(File);
		}
		ToSubmit.Add(File);
	}
	if (Unsaved.Num() > 0)
	{
		TSharedPtr<FJsonObject> Error = MCPErrorObject(TEXT("Some files have unsaved edits in the editor, which a submit would leave out. Save them first. Nothing was submitted."));
		Error->SetArrayField(TEXT("unsavedInEditor"), MCPStringListToJson(Unsaved));
		return MCPResult(Error);
	}

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetStringField(TEXT("provider"), Provider->GetName().ToString());
	MCPSetNoRollback(Result, TEXT("A submit is recorded in the repository history; undoing it is a new change, not a rollback."));
	TArray<FString> Submitted;
	if (ToSubmit.Num() > 0)
	{
		TSharedRef<FCheckIn, ESPMode::ThreadSafe> CheckIn = ISourceControlOperation::Create<FCheckIn>();
		CheckIn->SetDescription(FText::FromString(Description));
		if (Provider->Execute(CheckIn, ToSubmit) == ECommandResult::Succeeded)
		{
			Submitted = ToSubmit;
			Result->SetStringField(TEXT("message"), CheckIn->GetSuccessMessage().ToString());
		}
		else
		{
			for (const FString& File : ToSubmit)
			{
				Failed.Add(Failure(InputOf.FindRef(File), File, TEXT("the provider refused the submit; the output log carries its reason")));
			}
		}
	}
	Result->SetArrayField(TEXT("submitted"), MCPStringListToJson(Submitted));
	Result->SetBoolField(TEXT("unchanged"), Submitted.Num() == 0);
	NoteFailures(Result, Failed, TEXT("submitted"));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FSourceControlHandlers::SetCheckoutMode(const TSharedPtr<FJsonObject>& Params)
{
	const MCPSourceControlState::FSettleProviderWindows Settle;
	FString Mode;
	if (auto Err = RequireString(Params, TEXT("mode"), Mode)) return Err;
	const bool bAuto = Mode.Equals(TEXT("auto"), ESearchCase::IgnoreCase);
	if (!bAuto && !Mode.Equals(TEXT("manual"), ESearchCase::IgnoreCase))
	{
		return MCPError(FString::Printf(TEXT("mode '%s' is not one of manual, auto."), *Mode));
	}
	const bool bWasAuto = MCPSourceControl::IsAutoCheckoutEnabled();
	MCPSourceControl::SetAutoCheckoutEnabled(bAuto);

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetStringField(TEXT("mode"), bAuto ? TEXT("auto") : TEXT("manual"));
	Result->SetStringField(TEXT("previousMode"), bWasAuto ? TEXT("auto") : TEXT("manual"));
	Result->SetBoolField(TEXT("unchanged"), bWasAuto == bAuto);
	Result->SetBoolField(TEXT("providerEnabled"), ISourceControlModule::Get().IsEnabled());
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("mode"), bWasAuto ? TEXT("auto") : TEXT("manual"));
	MCPSetRollback(Result, TEXT("source_control_set_checkout_mode"), Payload);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FSourceControlHandlers::MarkForDelete(const TSharedPtr<FJsonObject>& Params)
{
	const MCPSourceControlState::FSettleProviderWindows Settle;
	const TArray<TSharedPtr<FJsonValue>>* PathValues = nullptr;
	if (auto Err = RequireArray(Params, TEXT("paths"), PathValues)) return Err;
	TArray<FResolvedPath> Paths;
	if (auto Err = ResolvePaths(*PathValues, Paths)) return Err;
	ISourceControlProvider* Provider = nullptr;
	if (auto Err = RequireProvider(Provider)) return Err;
	TSharedRef<FDelete, ESPMode::ThreadSafe> Operation = ISourceControlOperation::Create<FDelete>();
	if (auto Err = RequireOperation(*Provider, Operation, TEXT("mark_for_delete"))) return Err;

	TArray<TSharedPtr<FJsonObject>> Failed;
	TArray<FString> Files, Already, ToDelete, Unsaved, Loaded;
	TMap<FString, FString> InputOf;
	for (const FResolvedPath& Path : Paths)
	{
		InputOf.Add(Path.File, Path.Input);
		if (Path.bDirectory) Failed.Add(Failure(Path.Input, Path.File, TEXT("a folder; name the files to delete")));
		else Files.Add(Path.File);
	}
	TArray<FSourceControlStateRef> States;
	if (Files.Num() > 0 && !QueryStates(*Provider, Files, States))
	{
		return MCPError(TEXT("The provider could not report the state of the named files, so nothing was marked for delete."));
	}
	for (const FSourceControlStateRef& State : States)
	{
		const FString& File = State->GetFilename();
		const FString Input = InputOf.FindRef(File);
		FString Who;
		if (State->IsDeleted()) Already.Add(File);
		else if (State->IsAdded()) Failed.Add(Failure(Input, File, TEXT("marked for add and not in the repository yet; revert the add instead")));
		else if (!State->IsSourceControlled()) Failed.Add(Failure(Input, File, TEXT("not under source control")));
		else if (State->IsCheckedOutOther(&Who) && !State->CanDelete()) Failed.Add(Failure(Input, File, FString::Printf(TEXT("locked by %s"), *Who)));
		else if (!State->CanDelete()) Failed.Add(Failure(Input, File, FString::Printf(TEXT("cannot be deleted in state '%s'"), *State->GetDisplayName().ToString())));
		else
		{
			if (UPackage* Package = LoadedPackageForFile(File))
			{
				if (Package->IsDirty()) Unsaved.Add(File);
				Loaded.Add(File);
			}
			ToDelete.Add(File);
		}
	}
	if (Unsaved.Num() > 0)
	{
		TSharedPtr<FJsonObject> Error = MCPErrorObject(TEXT("Some files have unsaved edits in the editor, which a delete would orphan. Save or discard them first. Nothing was marked for delete."));
		Error->SetArrayField(TEXT("unsavedInEditor"), MCPStringListToJson(Unsaved));
		return MCPResult(Error);
	}

	// A copy of each file, so the rollback can put back a file whose provider
	// cannot revert its own delete.
	TSharedPtr<FJsonObject> Stash = MakeShared<FJsonObject>();
	const FString StashDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("UE-MCP") / TEXT("SourceControlStash") / FGuid::NewGuid().ToString());
	for (const FString& File : ToDelete)
	{
		const FString Copy = StashDir / FPaths::GetCleanFilename(File) + FString::Printf(TEXT(".%08x"), GetTypeHash(File));
		if (IFileManager::Get().Copy(*Copy, *File) == COPY_OK) Stash->SetStringField(File, Copy);
	}

	TArray<FString> Deleted;
	if (ToDelete.Num() > 0)
	{
		Provider->Execute(Operation, ToDelete);
		TArray<FSourceControlStateRef> After;
		QueryStates(*Provider, ToDelete, After);
		for (const FSourceControlStateRef& State : After)
		{
			if (State->IsDeleted()) Deleted.Add(State->GetFilename());
			else Failed.Add(Failure(InputOf.FindRef(State->GetFilename()), State->GetFilename(), TEXT("the provider refused the delete; providerErrors or the output log carries its reason")));
		}
	}

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetStringField(TEXT("provider"), Provider->GetName().ToString());
	Result->SetArrayField(TEXT("markedForDelete"), MCPStringListToJson(Deleted));
	Result->SetArrayField(TEXT("alreadyDeleted"), MCPStringListToJson(Already));
	if (Loaded.Num() > 0)
	{
		Result->SetArrayField(TEXT("loadedInEditor"), MCPStringListToJson(Loaded));
		Result->SetStringField(TEXT("note"), TEXT("Those packages stay loaded in the editor until it unloads them; saving one again would recreate its file."));
	}
	if (ToDelete.Num() > 0) AddResultInfo(Result, Operation);
	Result->SetBoolField(TEXT("unchanged"), Deleted.Num() == 0);
	NoteFailures(Result, Failed, TEXT("marked for delete"));
	if (Deleted.Num() > 0)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetArrayField(TEXT("paths"), MCPStringListToJson(Deleted));
		if (Stash->Values.Num() > 0) Payload->SetObjectField(TEXT("restoreFrom"), Stash);
		MCPSetRollback(Result, TEXT("source_control_revert"), Payload);
	}
	else
	{
		MCPSetNoRollback(Result, TEXT("Nothing was marked for delete by this call, so there is nothing to revert."));
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FSourceControlHandlers::Sync(const TSharedPtr<FJsonObject>& Params)
{
	const MCPSourceControlState::FSettleProviderWindows Settle;
	const TArray<TSharedPtr<FJsonValue>>* PathValues = nullptr;
	const bool bHasPaths = TryGetArrayParam(Params, TEXT("paths"), PathValues) && PathValues;
	TArray<FResolvedPath> Paths;
	if (bHasPaths)
	{
		if (auto Err = ResolvePaths(*PathValues, Paths)) return Err;
	}
	ISourceControlProvider* Provider = nullptr;
	if (auto Err = RequireProvider(Provider)) return Err;
	TSharedRef<FSync, ESPMode::ThreadSafe> Operation = ISourceControlOperation::Create<FSync>();
	if (auto Err = RequireOperation(*Provider, Operation, TEXT("sync"))) return Err;

	const TArray<FString> Roots = bHasPaths ? FilesOf(Paths) : DefaultProjectRoots();

	// A sync rewrites files under loaded packages: unsaved edits would be
	// overwritten, and clean packages are reloaded when their file changes.
	const TMap<FString, UPackage*> LoadedBefore = LoadedPackagesUnder(Roots);
	TArray<FString> Unsaved;
	for (const TPair<FString, UPackage*>& Pair : LoadedBefore)
	{
		if (Pair.Value->IsDirty()) Unsaved.Add(Pair.Key);
	}
	if (Unsaved.Num() > 0)
	{
		Unsaved.Sort();
		TSharedPtr<FJsonObject> Error = MCPErrorObject(TEXT("Some packages under the synced paths have unsaved edits in the editor, which a sync could overwrite on disk. Save or revert them first. Nothing was synced."));
		Error->SetArrayField(TEXT("unsavedInEditor"), MCPStringListToJson(Unsaved));
		return MCPResult(Error);
	}

	// Timestamps of the named files and the loaded packages, to report what the sync changed.
	TMap<FString, FDateTime> Before;
	for (const FResolvedPath& Path : Paths)
	{
		if (!Path.bDirectory) Before.Add(Path.File, IFileManager::Get().GetTimeStamp(*Path.File));
	}
	for (const TPair<FString, UPackage*>& Pair : LoadedBefore)
	{
		Before.Add(Pair.Key, IFileManager::Get().GetTimeStamp(*Pair.Key));
	}

	const bool bSynced = Provider->Execute(Operation, Roots) == ECommandResult::Succeeded;

	TArray<FString> Changed;
	for (const TPair<FString, FDateTime>& Pair : Before)
	{
		if (IFileManager::Get().GetTimeStamp(*Pair.Key) != Pair.Value) Changed.Add(Pair.Key);
	}
	Changed.Sort();

	TArray<UPackage*> ToReload;
	TArray<FString> Reloaded, RemovedWhileLoaded;
	for (const FString& File : Changed)
	{
		UPackage* const* Package = LoadedBefore.Find(File);
		if (!Package) continue;
		if (IFileManager::Get().FileExists(*File))
		{
			ToReload.Add(*Package);
			Reloaded.Add(File);
		}
		else
		{
			RemovedWhileLoaded.Add(File);
		}
	}
	FText ReloadError;
	if (ToReload.Num() > 0)
	{
		UPackageTools::ReloadPackages(ToReload, ReloadError, EReloadPackagesInteractionMode::AssumePositive);
	}

	TSharedPtr<FJsonObject> Result = bSynced ? MCPSuccess() : MCPErrorObject(TEXT("The provider reported the sync as failed; providerErrors or the output log carries its reason."));
	Result->SetStringField(TEXT("provider"), Provider->GetName().ToString());
	Result->SetArrayField(TEXT("roots"), MCPStringListToJson(Roots));
	Result->SetBoolField(TEXT("synced"), bSynced);
	Result->SetArrayField(TEXT("changedFiles"), MCPStringListToJson(Changed));
	Result->SetArrayField(TEXT("reloaded"), MCPStringListToJson(Reloaded));
	if (RemovedWhileLoaded.Num() > 0) Result->SetArrayField(TEXT("removedWhileLoaded"), MCPStringListToJson(RemovedWhileLoaded));
	if (!ReloadError.IsEmpty()) Result->SetStringField(TEXT("reloadError"), ReloadError.ToString());
	AddResultInfo(Result, Operation);
	Result->SetBoolField(TEXT("unchanged"), Changed.Num() == 0);
	MCPSetNoRollback(Result, TEXT("A sync moves the workspace to other repository revisions; going back is another sync, not a rollback."));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FSourceControlHandlers::History(const TSharedPtr<FJsonObject>& Params)
{
	const MCPSourceControlState::FSettleProviderWindows Settle;
	FString InPath;
	if (auto Err = RequireString(Params, TEXT("path"), InPath)) return Err;
	const int32 Limit = FMath::Clamp(OptionalInt(Params, TEXT("limit"), 20), 1, 1000);

	const FResolvedPath Path = ResolvePath(InPath);
	if (!Path.Error.IsEmpty()) return MCPError(FString::Printf(TEXT("'%s' %s."), *InPath, *Path.Error));
	if (Path.bDirectory) return MCPError(FString::Printf(TEXT("'%s' is a folder. history takes one file."), *InPath));
	ISourceControlProvider* Provider = nullptr;
	if (auto Err = RequireProvider(Provider)) return Err;
	TSharedRef<FUpdateStatus, ESPMode::ThreadSafe> Update = ISourceControlOperation::Create<FUpdateStatus>();
	if (auto Err = RequireOperation(*Provider, Update, TEXT("history"))) return Err;

	Update->SetUpdateHistory(true);
	if (Provider->Execute(Update, Path.File) != ECommandResult::Succeeded)
	{
		TSharedPtr<FJsonObject> Error = MCPErrorObject(FString::Printf(TEXT("The provider could not read the history of '%s'."), *Path.File));
		AddResultInfo(Error, Update);
		return MCPResult(Error);
	}
	const FSourceControlStatePtr State = Provider->GetState(Path.File, EStateCacheUsage::Use);
	if (!State.IsValid() || !State->IsSourceControlled())
	{
		return MCPError(FString::Printf(TEXT("'%s' is not under source control, so it has no history."), *Path.File));
	}

	TArray<TSharedPtr<FJsonValue>> Revisions;
	const int32 Total = State->GetHistorySize();
	for (int32 Index = 0; Index < Total && Index < Limit; ++Index)
	{
		const TSharedPtr<ISourceControlRevision, ESPMode::ThreadSafe> Revision = State->GetHistoryItem(Index);
		if (!Revision.IsValid()) continue;
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("revision"), Revision->GetRevision());
		Entry->SetNumberField(TEXT("revisionNumber"), Revision->GetRevisionNumber());
		Entry->SetNumberField(TEXT("checkInIdentifier"), Revision->GetCheckInIdentifier());
		Entry->SetStringField(TEXT("user"), Revision->GetUserName());
		Entry->SetStringField(TEXT("clientSpec"), Revision->GetClientSpec());
		Entry->SetStringField(TEXT("date"), Revision->GetDate().ToIso8601());
		Entry->SetStringField(TEXT("action"), Revision->GetAction());
		Entry->SetStringField(TEXT("description"), Revision->GetDescription());
		Entry->SetNumberField(TEXT("fileSize"), Revision->GetFileSize());
		Revisions.Add(MakeShared<FJsonValueObject>(Entry));
	}

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetStringField(TEXT("provider"), Provider->GetName().ToString());
	Result->SetStringField(TEXT("file"), Path.File);
	if (!Path.PackageName.IsEmpty()) Result->SetStringField(TEXT("packageName"), Path.PackageName);
	Result->SetArrayField(TEXT("revisions"), Revisions);
	Result->SetNumberField(TEXT("count"), Revisions.Num());
	Result->SetNumberField(TEXT("total"), Total);
	Result->SetBoolField(TEXT("truncated"), Total > Limit);
	if (Total == 0)
	{
		Result->SetStringField(TEXT("note"), TEXT("The provider returned no history for the file. A file marked for add has none yet."));
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FSourceControlHandlers::Connect(const TSharedPtr<FJsonObject>& Params)
{
	const MCPSourceControlState::FSettleProviderWindows Settle;
	const FString Password = OptionalString(Params, TEXT("password"), FString());
	ISourceControlModule& Module = ISourceControlModule::Get();
	if (!Module.IsEnabled())
	{
		TSharedPtr<FJsonObject> Error = MCPErrorObject(TEXT("Source control is not enabled in this editor, so there is no provider to connect. Configure one under Revision Control, then retry. Nothing was done."));
		Error->SetStringField(TEXT("errorCode"), TEXT("source_control_not_enabled"));
		Error->SetBoolField(TEXT("enabled"), false);
		return MCPResult(Error);
	}
	ISourceControlProvider& Provider = Module.GetProvider();
	TSharedRef<FConnect, ESPMode::ThreadSafe> Operation = ISourceControlOperation::Create<FConnect>();
	if (auto Err = RequireOperation(Provider, Operation, TEXT("connect"))) return Err;
	// The password goes to the provider only; it is never echoed on the result.
	if (!Password.IsEmpty()) Operation->SetPassword(Password);

	const bool bWasConnected = Provider.IsAvailable();
	const bool bSucceeded = Provider.Execute(Operation, EConcurrency::Synchronous) == ECommandResult::Succeeded;
	const bool bConnected = Provider.IsAvailable();

	TSharedPtr<FJsonObject> Result = bSucceeded && bConnected
		? MCPSuccess()
		: MCPErrorObject(Operation->GetErrorText().IsEmpty()
			? FString::Printf(TEXT("The %s provider did not connect: %s"), *Provider.GetName().ToString(), *Provider.GetStatusText().ToString())
			: Operation->GetErrorText().ToString());
	if (!(bSucceeded && bConnected)) Result->SetStringField(TEXT("errorCode"), TEXT("source_control_unavailable"));
	Result->SetStringField(TEXT("provider"), Provider.GetName().ToString());
	Result->SetBoolField(TEXT("wasConnected"), bWasConnected);
	Result->SetBoolField(TEXT("connected"), bConnected);
	Result->SetBoolField(TEXT("passwordSent"), !Password.IsEmpty());
	Result->SetStringField(TEXT("statusText"), Provider.GetStatusText().ToString());
	AddResultInfo(Result, Operation);
	Result->SetBoolField(TEXT("unchanged"), bWasConnected == bConnected);
	MCPSetNoRollback(Result, TEXT("Connecting changes no files; the provider stays connected for the editor session."));
	return MCPResult(Result);
}

// ─── Registration ──────────────────────────────────────────────────────────

void FSourceControlHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	FMCPHandlerRegistry::FCategoryScope CategoryScope(Registry, TEXT("source_control"));
	using EType = EMCPParamType;
	const FMCPParamSpec PathsSpec = MCPParam::Required(TEXT("paths"), EType::Array,
		TEXT("Asset or package paths (/Game/X), content folders, or files on disk (absolute or relative to the project)")).Items(EType::String);

	Registry.RegisterHandler(TEXT("source_control_get_provider"), &GetProvider, {});
	Registry.RegisterHandlerWithTimeout(TEXT("source_control_get_status"), &GetStatus, 120.0f, {
		PathsSpec,
		MCPParam::Optional(TEXT("limit"), EType::Integer, TEXT("Most files to report (default 500); truncated says when more matched")),
	});
	Registry.RegisterHandlerWithTimeout(TEXT("source_control_list_changes"), &ListChanges, 120.0f, {
		MCPParam::Optional(TEXT("paths"), EType::Array, TEXT("Folders or files to look under (default the project's Content, Config, Source and Plugins folders and its .uproject)")).Items(EType::String),
		MCPParam::Optional(TEXT("limit"), EType::Integer, TEXT("Most files to report (default 500); truncated says when more matched")),
		MCPParam::Optional(TEXT("includeUntracked"), EType::Boolean, TEXT("Also list files on disk the provider does not track (default true)")),
	});
	Registry.RegisterHandler(TEXT("source_control_checkout"), &Checkout, { PathsSpec },
		MCPSpec::ContractExempt(TEXT("Checks files out of the configured provider when called")));
	Registry.RegisterHandler(TEXT("source_control_add"), &MarkForAdd, { PathsSpec },
		MCPSpec::ContractExempt(TEXT("Marks files for add in the configured provider when called")));
	Registry.RegisterHandler(TEXT("source_control_revert"), &Revert, {
		PathsSpec,
		MCPParam::Optional(TEXT("dryRun"), EType::Boolean, TEXT("Report what would be reverted and lost, and change nothing")),
		MCPParam::Optional(TEXT("discardUnsaved"), EType::Boolean, TEXT("Drop unsaved editor edits to the named packages instead of refusing (default false)")),
		MCPParam::Optional(TEXT("restoreFrom"), EType::Object, TEXT("File to saved copy, as mark_for_delete's rollback carries it: a deleted file the provider cannot revert is put back from its copy and marked for add")),
	}, MCPSpec::ContractExempt(TEXT("Reverts files in the configured provider when called")));
	Registry.RegisterHandlerWithTimeout(TEXT("source_control_diff"), &Diff, 120.0f, {
		MCPParam::Required(TEXT("path"), EType::String, TEXT("One asset path or file to diff against its latest repository revision")),
		MCPParam::Optional(TEXT("openEditor"), EType::Boolean, TEXT("For a package, open the editor's asset diff window (default false)")),
		MCPParam::Optional(TEXT("contextLines"), EType::Integer, TEXT("Unchanged lines around each text change (default 3)")),
		MCPParam::Optional(TEXT("maxLines"), EType::Integer, TEXT("Most diff lines to return (default 400); truncated says when cut")),
	});
	Registry.RegisterHandlerWithTimeout(TEXT("source_control_submit"), &Submit, 120.0f, {
		PathsSpec,
		MCPParam::Required(TEXT("description"), EType::String, TEXT("The change description or commit message")),
	}, MCPSpec::ContractExempt(TEXT("Submits files to the repository when called")));
	Registry.RegisterHandler(TEXT("source_control_set_checkout_mode"), &SetCheckoutMode, {
		MCPParam::Required(TEXT("mode"), EType::String, TEXT("manual refuses a write to a read-only file; auto checks it out first")).Enum({ TEXT("manual"), TEXT("auto") }),
	}, MCPSpec::ContractExempt(TEXT("Changes the editor session's checkout mode when called")));
	Registry.RegisterHandler(TEXT("source_control_mark_for_delete"), &MarkForDelete, { PathsSpec },
		MCPSpec::ContractExempt(TEXT("Marks files for delete in the configured provider when called")));
	Registry.RegisterHandlerWithTimeout(TEXT("source_control_sync"), &Sync, 300.0f, {
		MCPParam::Optional(TEXT("paths"), EType::Array, TEXT("Folders or files to sync to the latest revision (default the project's Content, Config, Source and Plugins folders and its .uproject)")).Items(EType::String),
	}, MCPSpec::ContractExempt(TEXT("Syncs the workspace from the repository when called")));
	Registry.RegisterHandlerWithTimeout(TEXT("source_control_history"), &History, 120.0f, {
		MCPParam::Required(TEXT("path"), EType::String, TEXT("One asset path or file whose revision history to read")),
		MCPParam::Optional(TEXT("limit"), EType::Integer, TEXT("Most revisions to return, newest first (default 20); truncated says when more exist")),
	});
	Registry.RegisterHandlerWithTimeout(TEXT("source_control_connect"), &Connect, 120.0f, {
		MCPParam::Optional(TEXT("password"), EType::String, TEXT("Password or ticket for providers that log in (Perforce); sent to the provider only, never echoed")),
	}, MCPSpec::ContractExempt(TEXT("Connects the editor's configured provider when called")));
}
