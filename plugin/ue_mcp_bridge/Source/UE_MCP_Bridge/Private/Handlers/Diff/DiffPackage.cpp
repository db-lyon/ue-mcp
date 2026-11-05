// MCPDiffPackage (Public/HandlerDiffPackage.h): revision loading and the lifetime of diff packages.

#include "HandlerDiffPackage.h"
#include "HandlerLogCapture.h"

#include "Containers/Ticker.h"
#include "DiffUtils.h"
#include "Editor.h"
#include "HAL/FileManager.h"
#include "ISourceControlModule.h"
#include "ISourceControlProvider.h"
#include "ISourceControlRevision.h"
#include "ISourceControlState.h"
#include "Misc/PackageName.h"
#include "Misc/PackagePath.h"
#include "Misc/Paths.h"
#include "PackageTools.h"
#include "SourceControlOperations.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "Widgets/SWindow.h"

namespace
{
	/** Diff packages kept for an open window or editor, with how many show each. */
	TMap<TWeakObjectPtr<UPackage>, int32>& MCPDiffKeptPackages()
	{
		static TMap<TWeakObjectPtr<UPackage>, int32> Kept;
		return Kept;
	}

	FDelegateHandle GMCPDiffEditorClosedHandle;

	void MCPDiffKeep(UPackage* Package)
	{
		if (Package) MCPDiffKeptPackages().FindOrAdd(Package)++;
	}

	void MCPDiffReleaseNextTick(const TArray<TWeakObjectPtr<UPackage>>& Packages)
	{
		// The window or editor is still tearing down its widgets when it reports the close.
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Packages](float)
		{
			TArray<UPackage*> ToUnload;
			for (const TWeakObjectPtr<UPackage>& Weak : Packages)
			{
				int32* Count = MCPDiffKeptPackages().Find(Weak);
				if (!Count || --(*Count) > 0) continue;
				MCPDiffKeptPackages().Remove(Weak);
				if (UPackage* Package = Weak.Get()) ToUnload.Add(Package);
			}
			MCPDiffPackage::Unload(ToUnload);
			return false;
		}));
	}
}

namespace MCPDiffPackage
{
	TSharedPtr<ISourceControlRevision, ESPMode::ThreadSafe> FindRevision(const TSharedRef<ISourceControlState, ESPMode::ThreadSafe>& State, const FString& Revision, FString& OutError)
	{
		// An exact id, then a revision number, then a unique prefix of 4 or more characters.
		const FString Wanted = Revision.TrimStartAndEnd();
		TArray<TSharedPtr<ISourceControlRevision, ESPMode::ThreadSafe>> Items;
		for (int32 Index = 0; Index < State->GetHistorySize(); ++Index)
		{
			if (TSharedPtr<ISourceControlRevision, ESPMode::ThreadSafe> Item = State->GetHistoryItem(Index)) Items.Add(Item);
		}
		if (Wanted.IsEmpty() && Items.Num()) return Items[0];
		for (const auto& Item : Items)
		{
			if (Item->GetRevision().Equals(Wanted, ESearchCase::IgnoreCase)) return Item;
		}
		if (Wanted.IsNumeric())
		{
			for (const auto& Item : Items)
			{
				if (Item->GetRevisionNumber() == FCString::Atoi(*Wanted)) return Item;
			}
		}
		TSharedPtr<ISourceControlRevision, ESPMode::ThreadSafe> Found;
		TArray<FString> Prefixed;
		for (const auto& Item : Items)
		{
			if (Wanted.Len() >= 4 && Item->GetRevision().StartsWith(Wanted, ESearchCase::IgnoreCase))
			{
				if (!Found.IsValid()) Found = Item;
				Prefixed.Add(Item->GetRevision());
			}
		}
		if (Prefixed.Num() > 1)
		{
			OutError = FString::Printf(TEXT("revision %s matches %d revisions: %s"), *Wanted, Prefixed.Num(), *FString::Join(Prefixed, TEXT(", ")));
			return nullptr;
		}
		if (!Found.IsValid())
		{
			OutError = Wanted.IsEmpty()
				? FString(TEXT("the provider returned no history"))
				: FString::Printf(TEXT("no revision %s among its %d; source_control(history) lists them"), *Wanted, State->GetHistorySize());
		}
		return Found;
	}

	FRevisionPackage LoadAtRevision(const FString& PackageName, const FString& Revision)
	{
		FRevisionPackage Out;
		FString File;
		if (!FPackageName::DoesPackageExist(PackageName, &File))
		{
			Out.Error = FString::Printf(TEXT("%s has no package file on disk"), *PackageName);
			return Out;
		}
		File = FPaths::ConvertRelativePathToFull(File);
		ISourceControlModule& Module = ISourceControlModule::Get();
		if (!Module.IsEnabled() || !Module.GetProvider().IsAvailable())
		{
			Out.Error = TEXT("No source control provider is connected, so there is no revision to load");
			return Out;
		}
		ISourceControlProvider& Provider = Module.GetProvider();
		TSharedRef<FUpdateStatus, ESPMode::ThreadSafe> Update = ISourceControlOperation::Create<FUpdateStatus>();
		Update->SetUpdateHistory(true);
		Provider.Execute(Update, File);
		const FSourceControlStatePtr State = Provider.GetState(File, EStateCacheUsage::Use);
		if (!State.IsValid() || !State->IsSourceControlled())
		{
			Out.Error = FString::Printf(TEXT("%s is not under source control"), *File);
			return Out;
		}
		if (State->GetHistorySize() == 0)
		{
			Out.Error = FString::Printf(TEXT("The %s provider returned no history for %s"), *Provider.GetName().ToString(), *File);
			return Out;
		}

		FString FindError;
		const TSharedPtr<ISourceControlRevision, ESPMode::ThreadSafe> Found = FindRevision(State.ToSharedRef(), Revision, FindError);
		if (!Found.IsValid())
		{
			Out.Error = FString::Printf(TEXT("%s: %s"), *File, *FindError);
			return Out;
		}
		return LoadRevision(Found.ToSharedRef(), PackageName);
	}

	FRevisionPackage LoadRevision(const TSharedRef<ISourceControlRevision, ESPMode::ThreadSafe>& Found, const FString& PackageName)
	{
		FRevisionPackage Out;
		FString File;
		if (!FPackageName::DoesPackageExist(PackageName, &File))
		{
			Out.Error = FString::Printf(TEXT("%s has no package file on disk"), *PackageName);
			return Out;
		}
		File = FPaths::ConvertRelativePathToFull(File);
		Out.Revision = Found->GetRevision();
		Out.CheckInIdentifier = Found->GetCheckInIdentifier();
		Out.Date = Found->GetDate();
		Out.User = Found->GetUserName();
		Out.Description = Found->GetDescription();

		if (!Found->Get(Out.TempFile) || !IFileManager::Get().FileExists(*Out.TempFile))
		{
			Out.Error = FString::Printf(TEXT("The source control provider could not fetch revision %s of %s"), *Out.Revision, *File);
			return Out;
		}
		FMCPScopedLogCapture Capture({});
		Out.Package = DiffUtils::LoadPackageForDiff(FPackagePath::FromLocalPath(Out.TempFile), FPackagePath::FromLocalPath(File));
		if (!Out.Package)
		{
			FString Why;
			for (const FMCPScopedLogCapture::FLine& Line : Capture.GetLines())
			{
				if (!Why.IsEmpty()) Why += TEXT(" | ");
				Why += Line.Message;
			}
			Out.Error = FString::Printf(TEXT("Revision %s of %s could not be loaded for diff: %s"), *Out.Revision, *PackageName,
				Why.IsEmpty() ? TEXT("LoadPackage returned nothing and logged no reason") : *Why.Left(1200));
			return Out;
		}
		// Release the provider's temp file; the loaded package no longer reads from it.
		ResetLoaders(Out.Package);
		Out.Asset = Out.Package->FindAssetInPackage();
		if (!Out.Asset)
		{
			Unload({ Out.Package });
			Out.Package = nullptr;
			Out.Error = FString::Printf(TEXT("Revision %s of %s holds no asset"), *Out.Revision, *PackageName);
		}
		return Out;
	}

	void Unload(const TArray<UPackage*>& Packages)
	{
		TArray<UPackage*> Live;
		for (UPackage* Package : Packages)
		{
			if (IsValid(Package) && !MCPDiffKeptPackages().Contains(Package)) Live.Add(Package);
		}
		if (Live.Num() == 0) return;
		for (UPackage* Package : Live) ResetLoaders(Package);
		FText Error;
		UPackageTools::UnloadPackages(Live, Error, /*bUnloadDirtyPackages*/ true);
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	}

	void KeepUntilClosed(const TSharedRef<SWindow>& Window, const TArray<UPackage*>& Packages)
	{
		TArray<TWeakObjectPtr<UPackage>> Weak;
		for (UPackage* Package : Packages)
		{
			if (!Package) continue;
			MCPDiffKeep(Package);
			Weak.Add(Package);
		}
		if (Weak.Num() == 0) return;
		Window->GetOnWindowClosedEvent().AddLambda([Weak](const TSharedRef<SWindow>&) { MCPDiffReleaseNextTick(Weak); });
	}

	void KeepUntilEditorCloses(UObject* Asset)
	{
		UAssetEditorSubsystem* Editors = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
		if (!Asset || !Editors) return;
		MCPDiffKeep(Asset->GetOutermost());
		if (!GMCPDiffEditorClosedHandle.IsValid())
		{
			GMCPDiffEditorClosedHandle = Editors->OnAssetClosedInEditor().AddLambda([](UObject* Closed, IAssetEditorInstance*)
			{
				if (Closed && MCPDiffKeptPackages().Contains(Closed->GetOutermost()))
				{
					MCPDiffReleaseNextTick({ TWeakObjectPtr<UPackage>(Closed->GetOutermost()) });
				}
			});
		}
	}

	TArray<TSharedPtr<FJsonValue>> PackageNames(const TArray<UPackage*>& Packages)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const UPackage* Package : Packages)
		{
			if (Package) Out.Add(MakeShared<FJsonValueString>(Package->GetName()));
		}
		return Out;
	}

	int32 KeptCount()
	{
		int32 Count = 0;
		for (const TPair<TWeakObjectPtr<UPackage>, int32>& Pair : MCPDiffKeptPackages())
		{
			if (Pair.Key.IsValid()) ++Count;
		}
		return Count;
	}
}
