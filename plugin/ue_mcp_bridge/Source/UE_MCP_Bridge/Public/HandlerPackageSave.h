#pragma once

// Writing an asset's package to disk: the file it belongs in, whether it can be
// written, the save itself, and how a result reports whether it reached disk.

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/FeedbackContext.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeLock.h"
#include "HAL/CriticalSection.h"
#include "Engine/World.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "HandlerResult.h"
#include "HandlerAssetResolve.h"

// ── Package save ─────────────────────────────────────────────────────────────

/** True when the package is a map package, i.e. one whose on-disk form is a
 *  ".umap" rather than a ".uasset".
 *
 *  #949: writing a world package with the asset extension does not fail. It
 *  creates a second file that claims the same long package name, so the level
 *  then exists twice on disk and the two copies diverge silently as different
 *  save paths write different files. Unreal resolves the ".uasset" first, so
 *  the stale fork is the one that wins.
 *
 *  ContainsMap is the package flag Unreal sets on world packages and is the
 *  same test editor(save_dirty) branches on. FindWorldInPackage is the backstop
 *  for a world built in memory whose flag has not been stamped yet. One-file-
 *  per-actor packages under __ExternalActors__ hold an AActor and no UWorld, so
 *  both tests say false and they keep the ".uasset" extension OFPA expects. */
inline bool IsMapPackage(UPackage* Package)
{
	if (!Package) return false;
	return Package->ContainsMap() || UWorld::FindWorldInPackage(Package) != nullptr;
}

/** On-disk file extension for a package, dot included. ".umap" for world
 *  packages, ".uasset" for everything else. */
inline const FString& PackageFileExtension(UPackage* Package)
{
	return IsMapPackage(Package)
		? FPackageName::GetMapPackageExtension()
		: FPackageName::GetAssetPackageExtension();
}

/** Resolve the on-disk filename a package must be written to, extension
 *  included. Returns false when the package name has no mounted root, which
 *  keeps callers off FPackageName::LongPackageNameToFilename - that one is
 *  fatal rather than recoverable when the name does not resolve. */
inline bool ResolvePackageFileName(UPackage* Package, FString& OutFileName)
{
	if (!Package) return false;
	return FPackageName::TryConvertLongPackageNameToFilename(
		Package->GetName(), OutFileName, PackageFileExtension(Package));
}

/** Why an asset's package cannot be written, answered BEFORE the save is
 *  attempted. Returns true when the write is blocked, with OutReason carrying
 *  the sentence to hand the caller.
 *
 *  #932: blueprint(reparent) saved as part of the operation, and a read-only
 *  .uasset (a file never checked out of source control) turned the failed save
 *  into a FATAL engine error that took the whole editor process down. The asset
 *  was undamaged and the call replayed cleanly after a checkout, but no handler
 *  may answer a routine, foreseeable condition with a crash.
 *
 *  Asking the file system first is what turns that into an ordinary failure,
 *  and it is the same order asset(set_property) already uses (#931). It lives
 *  here, as one function, because "can this package be written" has to have a
 *  single answer: the protected-mount guardrail had four copies once and two of
 *  them enforced a weaker rule than the others. */
inline bool MCPPackageWriteBlocked(UObject* Asset, FString& OutReason)
{
	OutReason.Reset();

	UPackage* Package = Asset ? Asset->GetOutermost() : nullptr;
	if (!Package)
	{
		OutReason = TEXT("The asset has no package, so there is nothing to write.");
		return true;
	}

	const FString PackageName = Package->GetName();
	if (MCPIsProtectedAssetPath(PackageName))
	{
		OutReason = FString::Printf(
			TEXT("'%s' is on a protected mount, which the bridge never writes to."),
			*PackageName);
		return true;
	}

	FString PackageFileName;
	if (!ResolvePackageFileName(Package, PackageFileName))
	{
		OutReason = FString::Printf(
			TEXT("'%s' has no mounted content root, so there is no file to write it to."),
			*PackageName);
		return true;
	}

	// Only a file that already exists can be read-only. A package saved for the
	// first time has nothing on disk to check, and asking about a missing file
	// answers "not read-only", which is the right answer for a create.
	if (IFileManager::Get().FileExists(*PackageFileName)
		&& IFileManager::Get().IsReadOnly(*PackageFileName))
	{
		OutReason = FString::Printf(
			TEXT("'%s' is read-only on disk. Check it out of source control or clear the read-only flag, then retry."),
			*PackageFileName);
		return true;
	}

	return false;
}

/** Collects the warnings and errors the engine logs about a save while it is
 *  in scope, so a refusal can quote the engine's own reason instead of
 *  pointing at the output log (#1120). */
class FMCPSaveDiagnostics : public FOutputDevice
{
public:
	FMCPSaveDiagnostics() { if (GLog) GLog->AddOutputDevice(this); }
	virtual ~FMCPSaveDiagnostics() override { if (GLog) GLog->RemoveOutputDevice(this); }
	FMCPSaveDiagnostics(const FMCPSaveDiagnostics&) = delete;
	FMCPSaveDiagnostics& operator=(const FMCPSaveDiagnostics&) = delete;

	virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const class FName& Category) override
	{
		const ELogVerbosity::Type Level = (ELogVerbosity::Type)(Verbosity & ELogVerbosity::VerbosityMask);
		if (Level == ELogVerbosity::NoLogging || Level > ELogVerbosity::Warning || !V) return;
		static const FName SavePackageCategory(TEXT("LogSavePackage"));
		const FString Line(V);
		if (Category != SavePackageCategory && !Line.Contains(TEXT("Can't save")) && !Line.Contains(TEXT("Illegal reference")))
		{
			return;
		}
		FScopeLock Lock(&Guard);
		if (Lines.Num() < 8) Lines.Add(Line.Left(2000));
	}
	virtual bool CanBeUsedOnAnyThread() const override { return true; }

	TArray<FString> GetLines() const
	{
		FScopeLock Lock(&Guard);
		return Lines;
	}

	/** The line that names the cause: the engine's "Can't save" sentence when
	 *  there is one, otherwise the first captured line, otherwise empty. */
	FString GetReason() const
	{
		FScopeLock Lock(&Guard);
		for (const FString& Line : Lines)
		{
			if (Line.Contains(TEXT("Can't save"))) return Line;
		}
		return Lines.Num() > 0 ? Lines[0] : FString();
	}

private:
	mutable FCriticalSection Guard;
	TArray<FString> Lines;
};

/** Split the engine's "Illegal reference to private object" sentence into the
 *  private object, the object holding the reference and its property, with a
 *  hint on clearing it. Returns nullptr when Message is not that sentence. */
inline TSharedPtr<FJsonObject> MCPDescribeIllegalReference(const FString& Message)
{
	static const TCHAR* Marker = TEXT("Illegal reference to private object: '");
	const int32 Start = Message.Find(Marker);
	if (Start == INDEX_NONE) return nullptr;
	const FString Rest = Message.Mid(Start + FCString::Strlen(Marker));

	FString PrivateObject, AfterObject, Referencer, AfterReferencer, Outer, AfterOuter, Property, AfterProperty;
	if (!Rest.Split(TEXT("' referenced by '"), &PrivateObject, &AfterObject)) return nullptr;
	if (!AfterObject.Split(TEXT("' (at '"), &Referencer, &AfterReferencer)) return nullptr;
	if (!AfterReferencer.Split(TEXT("') in its '"), &Outer, &AfterOuter)) return nullptr;
	if (!AfterOuter.Split(TEXT("' property"), &Property, &AfterProperty)) return nullptr;

	TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
	Out->SetStringField(TEXT("privateObject"), PrivateObject);
	Out->SetStringField(TEXT("referencer"), Referencer);
	Out->SetStringField(TEXT("referencerOuter"), Outer);
	Out->SetStringField(TEXT("property"), Property);
	Out->SetStringField(TEXT("hint"), FString::Printf(
		TEXT("'%s' (inside '%s') holds, in its '%s' property, a reference to '%s', which is private to another package, so the engine will not save. ")
		TEXT("Point that reference, or the field inside '%s' that carries it, at a saved public asset or clear it to null, for example with asset(set_property) ")
		TEXT("on the referencer's object path, then save again. The private object is usually one that was never saved or was left behind by a move."),
		*Referencer, *Outer, *Property, *PrivateObject, *Property));
	return Out;
}

/** Add what a failed save's diagnostics say to Result: saveDiagnostics with
 *  the captured lines, and illegalReference when the engine named one. */
inline void MCPAttachSaveDiagnostics(const TSharedPtr<FJsonObject>& Result, const FMCPSaveDiagnostics& Diagnostics)
{
	if (!Result.IsValid()) return;
	const TArray<FString> Lines = Diagnostics.GetLines();
	if (Lines.Num() == 0) return;
	TArray<TSharedPtr<FJsonValue>> Json;
	for (const FString& Line : Lines) Json.Add(MakeShared<FJsonValueString>(Line));
	Result->SetArrayField(TEXT("saveDiagnostics"), Json);
	for (const FString& Line : Lines)
	{
		if (TSharedPtr<FJsonObject> Illegal = MCPDescribeIllegalReference(Line))
		{
			Result->SetObjectField(TEXT("illegalReference"), Illegal);
			break;
		}
	}
}

/** Mark the asset's package dirty and save it to disk. Used by every create/
 *  mutate handler that wants changes persisted across editor restarts.
 *  No-op if Asset or its package is null. Returns true on successful save.
 *
 *  Refuses before the engine is asked to write a file it cannot open (#932),
 *  so the worst outcome of a read-only or protected package is a false return
 *  rather than a fatal error. Callers that want the sentence explaining the
 *  false use SaveAssetPackageChecked. */
inline bool SaveAssetPackage(UObject* Asset)
{
	if (!Asset) return false;
	UPackage* Package = Asset->GetOutermost();
	if (!Package) return false;
	Package->MarkPackageDirty();

	FString BlockedReason;
	if (MCPPackageWriteBlocked(Asset, BlockedReason)) return false;

	// The extension has to follow the package, not the call site. Any handler
	// that mutates an actor or component in the open level reaches this with a
	// world package as the outermost (#949).
	FString PackageFileName;
	if (!ResolvePackageFileName(Package, PackageFileName)) return false;
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Standalone;
	// The default GError treats a save warning as fatal. GWarn logs it, which
	// is what the editor's own save does and what FMCPSaveDiagnostics reads.
	SaveArgs.Error = GWarn;
	return UPackage::SavePackage(Package, nullptr, *PackageFileName, SaveArgs);
}

/** SaveAssetPackage, with the reason when it did not write. A handler that
 *  reports its own persistence uses this so a refusal reads as a named cause
 *  rather than a bare false. */
inline bool SaveAssetPackageChecked(UObject* Asset, FString& OutReason)
{
	if (MCPPackageWriteBlocked(Asset, OutReason)) return false;
	FMCPSaveDiagnostics Diagnostics;
	if (SaveAssetPackage(Asset)) return true;

	UPackage* Package = Asset ? Asset->GetOutermost() : nullptr;
	const FString EngineReason = Diagnostics.GetReason();
	if (EngineReason.IsEmpty())
	{
		OutReason = FString::Printf(
			TEXT("The editor refused to write '%s'. The output log carries the reason."),
			Package ? *Package->GetName() : TEXT("(no package)"));
		return false;
	}
	OutReason = EngineReason;
	if (TSharedPtr<FJsonObject> Illegal = MCPDescribeIllegalReference(EngineReason))
	{
		OutReason += TEXT(" ") + Illegal->GetStringField(TEXT("hint"));
	}
	return false;
}

/** The refusal an action that saves as a side effect returns when the package
 *  cannot be written. Returns nullptr when the write may go ahead, so a handler
 *  reads as `if (auto Blocked = MCPAssetWriteBlockedError(...)) return Blocked;`
 *  placed BEFORE the first mutation.
 *
 *  Operation names what the caller asked for, in the imperative, so the message
 *  reads "Cannot reparent this Blueprint: ...". */
inline TSharedPtr<FJsonValue> MCPAssetWriteBlockedError(
	UObject* Asset,
	const FString& AssetPath,
	const TCHAR* Operation)
{
	FString Reason;
	if (!MCPPackageWriteBlocked(Asset, Reason)) return nullptr;

	TSharedPtr<FJsonObject> Obj = MCPErrorObject(FString::Printf(
		TEXT("Cannot %s: %s Nothing was changed."), Operation, *Reason));
	Obj->SetStringField(TEXT("assetPath"), AssetPath);
	Obj->SetStringField(TEXT("path"), AssetPath);
	Obj->SetStringField(TEXT("reason"), TEXT("package_not_writable"));
	Obj->SetBoolField(TEXT("saved"), false);
	if (UPackage* Package = Asset ? Asset->GetOutermost() : nullptr)
	{
		Obj->SetStringField(TEXT("packageName"), Package->GetName());
		FString PackageFileName;
		if (ResolvePackageFileName(Package, PackageFileName))
		{
			Obj->SetStringField(TEXT("packageFile"), PackageFileName);
		}
	}
	return MakeShared<FJsonValueObject>(Obj);
}

/** Record on a result whether the side-effect save reached disk, and why not
 *  when it did not. A save that did not happen is a failure, not a success with
 *  a footnote: the caller's next read comes off the in-memory object and looks
 *  correct right up until the editor restarts (#931). */
inline void MCPNoteSaveOutcome(
	const TSharedPtr<FJsonObject>& Result,
	const FString& AssetPath,
	bool bSaved,
	const FString& Reason)
{
	if (!Result.IsValid()) return;
	Result->SetBoolField(TEXT("saved"), bSaved);
	if (bSaved) return;

	Result->SetBoolField(TEXT("success"), false);
	Result->SetStringField(TEXT("saveError"), Reason);
	Result->SetStringField(TEXT("error"), FString::Printf(
		TEXT("The change was applied in memory but '%s' was not written: %s"),
		*AssetPath, *Reason));
}
