#pragma once

// Keeping a Widget Blueprint's variable GUID map in step with what the UMG
// compiler validates, so a widget edit cannot leave the asset ensuring on compile.

#include "CoreMinimal.h"
#include "HandlerUtils.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/CompilerResultsLog.h"

// ── Widget variable GUID metadata (#728, #799) ───────────────────────────────
//
// A UWidgetBlueprint keeps WidgetVariableNameToGuidMap so external references
// to a widget variable survive a rename of that widget. The
// WidgetBlueprintCompiler validates the map on every compile and raises
//
//   "Widget [X] was added but did not get a GUID"
//
// for any widget variable it is about to generate that owns no entry. That is
// an engine ensure, so the editor survives it, but the asset is left in a state
// the UMG editor does not consider valid and every later compile repeats it.
//
// The set the compiler validates is the blueprint's SOURCE widgets, which
// UBaseWidgetBlueprint gathers without walking the hierarchy. Its own header
// says the accessor "avoids calling virtual functions on instances and is
// therefore safe to use throughout compilation", so the set is every UWidget
// the WidgetTree still OWNS, not the widgets a walk down from RootWidget
// reaches. The two part company the moment a widget is detached from its parent
// without being moved out of the tree: the walk stops seeing it while the
// compiler still generates a variable for it. Bookkeeping driven by the walk
// alone therefore deletes the GUID of a widget that still needs one, which is
// how the ensure fires on a widget that was just removed. The enumeration below
// is by outer for that reason, with the walk folded in as a union rather than
// used on its own.
//
// The map is editor-only data that landed in 5.5, so on 5.4 every sync reports
// itself unsupported and changes nothing.
namespace MCPWidgetGuidMap
{
	/** What a sync changed, and what it could not fix. */
	struct FSyncReport
	{
		/** False when this engine has no WidgetVariableNameToGuidMap at all. */
		bool bSupported = false;
		/** True once CompileChecked ran the compiler; false means it refused. */
		bool bCompileAttempted = false;
		/** True only when that compile finished with no errors. */
		bool bCompiled = false;
		/** Error-severity messages from the compile, and how many of them the
		 *  baseline taken before the edit already had. */
		int32 CompileErrors = 0;
		int32 PreexistingErrors = 0;
		TArray<FString> CompileMessages;
		int32 Added = 0;
		int32 Pruned = 0;
		/** Widgets moved out of the tree because nothing reached them. */
		int32 Evicted = 0;
		/** Entries whose stored GUID was not a valid GUID and was replaced. */
		int32 Repaired = 0;
		/** Variables whose map entry the compiler will refuse, after the sync
		 *  has done everything it can. A non-empty list means the next compile
		 *  of this asset ensures. */
		TArray<FName> Unusable;
		/** What is actually wrong, one clause per defect, for the message. */
		TArray<FString> Defects;

		bool IsClean() const { return Unusable.Num() == 0; }

		/** CompileChecked refused to compile: the GUID preflight failed. */
		bool WasRefused() const { return !bCompileAttempted; }

		int32 IntroducedErrors() const { return FMath::Max(0, CompileErrors - PreexistingErrors); }

		/** The compile failed and the edit is to blame: either it added errors
		 *  the baseline did not have, or no baseline accounts for the failure. */
		bool EditBrokeCompile() const
		{
			return bCompileAttempted && !bCompiled && (IntroducedErrors() > 0 || PreexistingErrors == 0);
		}

		FString DefectList() const
		{
			return Defects.Num() ? FString::Join(Defects, TEXT("; ")) : FString(TEXT("(none)"));
		}

		FString UnusableList() const
		{
			TArray<FString> Names;
			Names.Reserve(Unusable.Num());
			for (const FName& Name : Unusable)
			{
				Names.Add(Name.ToString());
			}
			return Names.Num() ? FString::Join(Names, TEXT(", ")) : FString(TEXT("(none)"));
		}
	};

	/** Every name the compiler will generate a variable for: each widget the
	 *  WidgetTree owns (reachable from the root or not), plus each animation. */
	inline TSet<FName> RequiredNames(UWidgetBlueprint* WidgetBP)
	{
		TSet<FName> Names;
		if (!WidgetBP)
		{
			return Names;
		}

		if (UWidgetTree* Tree = WidgetBP->WidgetTree)
		{
			TArray<UObject*> Owned;
			MCPGetDirectSubobjects(Tree, Owned);
			for (UObject* Object : Owned)
			{
				if (UWidget* Widget = Cast<UWidget>(Object))
				{
					Names.Add(Widget->GetFName());
				}
			}

			Tree->ForEachWidget([&Names](UWidget* Widget)
			{
				if (Widget)
				{
					Names.Add(Widget->GetFName());
				}
			});
		}

		// TObjectPtr::GetFName reads the name off the object handle, so it does
		// not need UWidgetAnimation defined; WidgetBlueprint.h only declares it.
		for (const auto& Animation : WidgetBP->Animations)
		{
			const FName AnimationName = Animation.GetFName();
			if (!AnimationName.IsNone())
			{
				Names.Add(AnimationName);
			}
		}
		return Names;
	}

	/**
	 * Make the map say exactly what the compiler is about to check: give every
	 * required name the GUID it lacks, and drop every entry no live name backs.
	 * Call immediately before a compile, and again after, because the compile
	 * itself can rename a widget whose name collided.
	 */
	inline FSyncReport Sync(UWidgetBlueprint* WidgetBP)
	{
		FSyncReport Report;
#if UE_MCP_HAS_5_5_API
		{
			if (!WidgetBP)
			{
				return Report;
			}
			Report.bSupported = true;

			const TSet<FName> Required = RequiredNames(WidgetBP);
			for (const FName& Name : Required)
			{
				if (Name.IsNone())
				{
					continue;
				}
				if (!WidgetBP->WidgetVariableNameToGuidMap.Contains(Name))
				{
					WidgetBP->WidgetVariableNameToGuidMap.Add(Name, FGuid::NewGuid());
					++Report.Added;
				}
			}

			// Pruning is measured against a wider set: an entry naming one of
			// the blueprint's own declared variables is not proof of drift, so
			// it is left alone rather than dropped and re-added.
			TSet<FName> Known = Required;
			for (const auto& Variable : WidgetBP->NewVariables)
			{
				Known.Add(Variable.VarName);
			}

			TArray<FName> Stale;
			for (const auto& Entry : WidgetBP->WidgetVariableNameToGuidMap)
			{
				if (!Known.Contains(Entry.Key))
				{
					Stale.Add(Entry.Key);
				}
			}
			for (const FName& Name : Stale)
			{
				WidgetBP->WidgetVariableNameToGuidMap.Remove(Name);
			}
			Report.Pruned = Stale.Num();

			// What the compiler actually checks, and which half of it the fill
			// above cannot cover.
			//
			// FWidgetBlueprintCompilerContext::ValidateAndFixUpVariableGuids
			// raises ensureAlways on three conditions:
			//   1. a source widget or animation with no entry;
			//   2. an entry whose stored GUID is not valid;
			//   3. two entries carrying the same GUID.
			//
			// Condition 1 cannot survive the fill loop above, which adds a
			// fresh GUID for every required name and prunes nothing from that
			// set, so re-testing it here would be a guard that can never fire.
			// The two the fill loop says nothing about are the ones worth
			// checking, because both arrive with the asset rather than with
			// the mutation:
			//
			// Condition 2 is repairable. An invalid GUID references nothing, so
			// replacing it breaks nothing; the engine replaces it too, after
			// ensuring.
			//
			// Condition 3 is not. The GUID is how external assets refer to a
			// variable across a rename, so reassigning one of a colliding pair
			// silently breaks whichever references chose that one. The engine
			// does not repair it either - its own message tells the user to
			// delete and recreate one of the two variables - so this is the
			// state in which the map cannot be made to match what the compiler
			// checks, and it is what stops the compile.
			TMap<FGuid, FName> GuidOwners;
			for (auto& Entry : WidgetBP->WidgetVariableNameToGuidMap)
			{
				if (!Entry.Value.IsValid())
				{
					Entry.Value = FGuid::NewGuid();
					++Report.Repaired;
				}
				if (const FName* Owner = GuidOwners.Find(Entry.Value))
				{
					Report.Unusable.Add(Entry.Key);
					Report.Defects.Add(FString::Printf(
						TEXT("'%s' carries the same variable GUID as '%s'"),
						*Entry.Key.ToString(), *Owner->ToString()));
					continue;
				}
				GuidOwners.Add(Entry.Value, Entry.Key);
			}
		}
#endif
		return Report;
	}

	/**
	 * Move every widget the tree no longer reaches out of the WidgetTree and
	 * into the transient package, and report the ones that would not move.
	 *
	 * Detaching a widget from its parent panel does not end its membership of
	 * the blueprint: ownership is what makes the compiler generate a variable
	 * for it, so a detached widget still outered to the tree is still compiled,
	 * still needs a GUID, and still holds its name against a later add. Only a
	 * handler whose contract is removal may call this, since it is destructive
	 * by design.
	 */
	inline TArray<FName> EvictUnreachableWidgets(UWidgetBlueprint* WidgetBP, int32& OutEvicted)
	{
		OutEvicted = 0;
		TArray<FName> Stuck;
		if (!WidgetBP || !WidgetBP->WidgetTree)
		{
			return Stuck;
		}

		UWidgetTree* Tree = WidgetBP->WidgetTree;

		TSet<const UObject*> Reachable;
		Tree->ForEachWidget([&Reachable](UWidget* Widget)
		{
			if (Widget)
			{
				Reachable.Add(Widget);
			}
		});
		// Named slot content hangs off the tree rather than off the root, so it
		// is reachable in every sense that matters even where the root walk
		// never visits it. Evicting it would delete authored content.
		for (const auto& Binding : Tree->NamedSlotBindings)
		{
			if (Binding.Value)
			{
				Reachable.Add(Binding.Value);
				UWidgetTree::ForWidgetAndChildren(Binding.Value, [&Reachable](UWidget* Widget)
				{
					if (Widget)
					{
						Reachable.Add(Widget);
					}
				});
			}
		}

		TArray<UObject*> Owned;
		MCPGetDirectSubobjects(Tree, Owned);
		for (UObject* Object : Owned)
		{
			UWidget* Widget = Cast<UWidget>(Object);
			if (!Widget || Reachable.Contains(Widget))
			{
				continue;
			}

			// A unique name rather than the one it has: the transient package is
			// shared, and the same widget name is evicted from the same asset
			// on every run of a script that adds and removes it.
			const FName EvictedName = Widget->GetFName();
			const FName ParkedName = MakeUniqueObjectName(
				GetTransientPackage(), Widget->GetClass(), EvictedName);
			Widget->Rename(*ParkedName.ToString(), GetTransientPackage(),
				REN_DontCreateRedirectors | REN_NonTransactional);
			if (Widget->GetOuter() == Tree)
			{
				Stuck.Add(EvictedName);
			}
			else
			{
				++OutEvicted;
			}
		}
		return Stuck;
	}

	/** Compiler errors a Widget Blueprint already had before a handler edits
	 *  it, so an unrelated broken graph does not block the edit. */
	struct FCompileBaseline
	{
		TMap<FString, int32> Errors;
	};

	/** Compile WidgetBP without saving and return its error-severity messages. */
	inline TArray<FString> CompileForErrors(UWidgetBlueprint* WidgetBP, int32& OutNumErrors)
	{
		FCompilerResultsLog CompileLog;
		CompileLog.SetSourcePath(WidgetBP->GetPathName());
		// SkipSave: the editor's SaveOnCompile preference must not persist an
		// edit before the handler has checked the result.
		FKismetEditorUtilities::CompileBlueprint(WidgetBP, EBlueprintCompileOptions::SkipSave, &CompileLog);
		OutNumErrors = CompileLog.NumErrors;
		TArray<FString> Messages;
		for (const TSharedRef<FTokenizedMessage>& Message : CompileLog.Messages)
		{
			if (Message->GetSeverity() == EMessageSeverity::Error)
			{
				Messages.Add(Message->ToText().ToString());
			}
		}
		return Messages;
	}

	/**
	 * Call before mutating. Compiles only when the blueprint already reports
	 * BS_Error, which is the one state where the post-edit compile cannot tell
	 * the edit's errors from the asset's own. Skipped when the GUID map is in
	 * a state CompileChecked would refuse, since that compile is what ensures.
	 */
	inline FCompileBaseline CaptureBaseline(UWidgetBlueprint* WidgetBP)
	{
		FCompileBaseline Baseline;
		if (!WidgetBP || WidgetBP->Status != BS_Error || !Sync(WidgetBP).IsClean())
		{
			return Baseline;
		}
		int32 NumErrors = 0;
		for (const FString& Message : CompileForErrors(WidgetBP, NumErrors))
		{
			++Baseline.Errors.FindOrAdd(Message);
		}
		return Baseline;
	}

	/**
	 * Sync, compile, sync again.
	 *
	 * When the map cannot be made to match what the compiler checks - which in
	 * practice means two variables carrying one GUID, the one defect neither
	 * the sync nor the engine can repair without picking which set of external
	 * references to break - the compile does NOT run and WasRefused() is true.
	 * The ensure inside the compiler is the thing this exists to prevent, and a
	 * handler that compiled anyway would be reporting success on an asset it
	 * had just broken.
	 *
	 * A compile that ran but failed is not refused: the handler reports it
	 * through MCPSetWidgetCompileOutcome and skips the save when
	 * EditBrokeCompile() says the edit caused it.
	 */
	inline FSyncReport CompileChecked(UWidgetBlueprint* WidgetBP, const FCompileBaseline& Baseline = FCompileBaseline())
	{
		FSyncReport Report = Sync(WidgetBP);
		if (!Report.IsClean())
		{
			return Report;
		}

		Report.bCompileAttempted = true;
		int32 NumErrors = 0;
		Report.CompileMessages = CompileForErrors(WidgetBP, NumErrors);
		Report.CompileErrors = FMath::Max(NumErrors, Report.CompileMessages.Num());

		TMap<FString, int32> Remaining = Baseline.Errors;
		for (const FString& Message : Report.CompileMessages)
		{
			int32* Count = Remaining.Find(Message);
			if (Count && *Count > 0)
			{
				--*Count;
				++Report.PreexistingErrors;
			}
		}

		const FSyncReport After = Sync(WidgetBP);
		Report.Added += After.Added;
		Report.Pruned += After.Pruned;
		Report.Repaired += After.Repaired;
		Report.Unusable = After.Unusable;
		Report.Defects = After.Defects;
		Report.bCompiled = Report.CompileErrors == 0 && WidgetBP->Status != BS_Error;
		return Report;
	}

	/** Save unless the edit broke the compile. A failed edit stays in memory,
	 *  unsaved, and the result says so and carries the way back. */
	inline bool SaveIfCompiled(UWidgetBlueprint* WidgetBP, const FSyncReport& Report, FString& OutError)
	{
		if (Report.EditBrokeCompile())
		{
			OutError = FString::Printf(
				TEXT("not saved because the edit left the blueprint with %d compiler error(s)"), Report.CompileErrors);
			return false;
		}
		return SaveAssetPackageChecked(WidgetBP, OutError);
	}

	/** The error a handler returns when CompileChecked refused to compile. */
	inline TSharedPtr<FJsonValue> BlockedError(const FString& AssetPath, const FSyncReport& Report)
	{
		return MCPError(FString::Printf(
			TEXT("Refusing to compile '%s': %s. Two widget variables cannot share a GUID - the UMG ")
			TEXT("compiler reports it and external assets that reference either one by GUID resolve to ")
			TEXT("whichever it reaches first, so the bridge will not reassign one behind your back. ")
			TEXT("Nothing was compiled or saved. Open the asset in the UMG editor and delete and ")
			TEXT("recreate one of %s."),
			*AssetPath, *Report.DefectList(), *Report.UnusableList()));
	}
}

/** Stamp GUID bookkeeping onto a widget mutation result, and withdraw the
 *  success claim when a variable the compiler generates was left with a map
 *  entry the compiler will refuse. Silence there is what turns into an editor
 *  ensure the next time anything compiles the asset. */
inline void MCPSetWidgetGuidOutcome(
	const TSharedPtr<FJsonObject>& Result,
	const MCPWidgetGuidMap::FSyncReport& Report,
	const FString& AssetPath)
{
	if (!Result.IsValid())
	{
		return;
	}

	// Only when there was something to report: a property write that changed no
	// metadata should not carry three zeroes describing the metadata it left
	// alone.
	if (Report.Pruned > 0)
	{
		Result->SetNumberField(TEXT("prunedGuidEntries"), Report.Pruned);
	}
	if (Report.Added > 0)
	{
		Result->SetNumberField(TEXT("widgetGuidEntriesAdded"), Report.Added);
	}
	if (Report.Evicted > 0)
	{
		Result->SetNumberField(TEXT("evictedWidgets"), Report.Evicted);
	}
	if (Report.Repaired > 0)
	{
		Result->SetNumberField(TEXT("repairedGuidEntries"), Report.Repaired);
	}
	if (Report.IsClean())
	{
		return;
	}

	TArray<TSharedPtr<FJsonValue>> Missing;
	for (const FName& Name : Report.Unusable)
	{
		Missing.Add(MakeShared<FJsonValueString>(Name.ToString()));
	}
	Result->SetArrayField(TEXT("widgetsWithUnusableGuid"), Missing);
	Result->SetBoolField(TEXT("success"), false);
	Result->SetStringField(TEXT("error"), FString::Printf(
		TEXT("'%s' was changed, but its widget variable GUID bookkeeping is not in a state the UMG ")
		TEXT("compiler accepts: %s. The bridge does not reassign a shared GUID on its own, because ")
		TEXT("external assets reference these variables by GUID across renames. This asset reports the ")
		TEXT("failure on every compile until one of %s is deleted and recreated in the UMG editor."),
		*AssetPath, *Report.DefectList(), *Report.UnusableList()));
}

/**
 * Compile state of a Widget Blueprint as a stable string (#799). A caller that
 * gets `created: true` still needs to know whether the asset it just changed
 * compiles, so the mutation handlers report this alongside the outcome.
 */
inline FString WidgetCompileStatusString(const UWidgetBlueprint* WidgetBP)
{
	if (!WidgetBP) return TEXT("unknown");
	switch (WidgetBP->Status.GetValue())
	{
	case BS_UpToDate:             return TEXT("upToDate");
	case BS_UpToDateWithWarnings: return TEXT("upToDateWithWarnings");
	case BS_Dirty:                return TEXT("dirty");
	case BS_Error:                return TEXT("error");
	case BS_BeingCreated:         return TEXT("beingCreated");
	default:                      return TEXT("unknown");
	}
}

/**
 * Stamp compile state onto a mutation result (#799, #1195). Call it after the
 * save outcome, so its error replaces the generic unsaved message.
 *
 * - The edit broke the compile: success is withdrawn, the edit stays applied in
 *   memory and unsaved, and the error names the ways back.
 * - The blueprint only has errors it had before the edit: the edit was saved
 *   and keeps its success; compileWarning names the pre-existing errors.
 */
inline void MCPSetWidgetCompileOutcome(
	TSharedPtr<FJsonObject> Result,
	const UWidgetBlueprint* WidgetBP,
	const FString& AssetPath,
	const MCPWidgetGuidMap::FSyncReport& Report,
	const FString& WhatHappened)
{
	if (!Result.IsValid()) return;
	Result->SetStringField(TEXT("compileStatus"), WidgetCompileStatusString(WidgetBP));
	Result->SetBoolField(TEXT("compiled"), Report.bCompiled);
	if (Report.bCompiled) return;

	Result->SetNumberField(TEXT("compileErrors"), Report.CompileErrors);
	Result->SetNumberField(TEXT("introducedCompileErrors"), Report.IntroducedErrors());
	Result->SetNumberField(TEXT("preexistingCompileErrors"), Report.PreexistingErrors);
	TArray<TSharedPtr<FJsonValue>> Messages;
	for (const FString& Message : Report.CompileMessages)
	{
		Messages.Add(MakeShared<FJsonValueString>(Message));
	}
	Result->SetArrayField(TEXT("compileMessages"), Messages);
	const FString Joined = Report.CompileMessages.Num()
		? FString::Join(Report.CompileMessages, TEXT("; "))
		: FString(TEXT("the compiler reported no message; open the blueprint's compiler results"));

	if (!Report.EditBrokeCompile())
	{
		Result->SetStringField(TEXT("compileWarning"), FString::Printf(
			TEXT("'%s' already had %d compiler error(s) before this edit and the edit added none: %s"),
			*AssetPath, Report.PreexistingErrors, *Joined));
		return;
	}

	Result->SetBoolField(TEXT("success"), false);
	Result->SetBoolField(TEXT("appliedInMemory"), true);
	Result->SetStringField(TEXT("error"), FString::Printf(
		TEXT("%s, but '%s' no longer compiles (%d error(s), %d of them new), so it was not saved. The edit is still ")
		TEXT("applied in memory and the package is dirty: undo it with this result's rollback when it carries one, ")
		TEXT("discard it with asset(reload_package), or fix the errors and save. Errors: %s"),
		*WhatHappened, *AssetPath, Report.CompileErrors, Report.IntroducedErrors(), *Joined));
}
