#pragma once

// What a Widget Blueprint declares to the F1 edit scope: it resolves through
// the widget path forms (MCPWidget::ResolveWidgetBlueprint), it compiles
// through the GUID-checked compile (#728), and a compile the GUID map refuses
// fails the call without saving, as every widget action did before the scope.
// Shared by every widget translation unit, which a unity build may merge.

#include "CoreMinimal.h"
#include "HandlerUtils.h"
#include "Families/MCPEditScope.h"
#include "Handlers/Widget/WidgetHandlers.h"
#include "Handlers/Widget/WidgetGuidMap.h"
#include "WidgetBlueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"

namespace MCPWidgetEdit
{
	/** An edit that compiles through the GUID check and saves once it changed something. */
	inline FMCPCommitPolicy Edit(EMCPSettle Settle)
	{
		return FMCPCommitPolicy::EditCompileSave(Settle, EMCPCompileFailure::Refuse);
	}

	/** The widget tree, an animation or a binding changed: the skeleton does. */
	inline FMCPCommitPolicy StructureEdit() { return Edit(EMCPSettle::Structure); }

	/** A widget property or style: the Blueprint is marked modified. */
	inline FMCPCommitPolicy ValueEdit() { return Edit(EMCPSettle::Value); }

	/** Compile and save with no fixup of its own, as a handler that marks nothing did. */
	inline FMCPCommitPolicy PlainEdit() { return Edit(EMCPSettle::None); }

	/** The default open check: every designer action needs a WidgetTree. */
	inline TSharedPtr<FJsonValue> NeedWidgetTree(UWidgetBlueprint* WidgetBP, const FString& AssetPath)
	{
		return WidgetBP->WidgetTree ? nullptr : MCPWidget::MissingWidgetTreeError(AssetPath);
	}
}

template <>
struct TMCPEditTraits<UWidgetBlueprint>
{
	static const TCHAR* TypeName() { return TEXT("WidgetBlueprint"); }

	static UWidgetBlueprint* Load(const FString& AssetPath, TSharedPtr<FJsonValue>& OutError)
	{
		return MCPWidget::ResolveWidgetBlueprintOrError(AssetPath, OutError);
	}

	static TSharedPtr<FJsonValue> CheckOpen(UWidgetBlueprint* WidgetBP, const FString& AssetPath)
	{
		return MCPWidgetEdit::NeedWidgetTree(WidgetBP, AssetPath);
	}

	static void Settle(UWidgetBlueprint* WidgetBP, EMCPSettle Kind)
	{
		if (Kind == EMCPSettle::Structure) FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBP);
		else if (Kind == EMCPSettle::Value) FBlueprintEditorUtils::MarkBlueprintAsModified(WidgetBP);
	}

	/**
	 * Sync the variable GUID map, compile, sync again. A map the compiler would
	 * ensure on is refused before the compile runs, and the reason is the
	 * error; otherwise the bookkeeping keys and any leftover defect are written.
	 */
	static bool Compile(UWidgetBlueprint* WidgetBP, const TSharedPtr<FJsonObject>& Out)
	{
		const FString AssetPath = WidgetBP->GetPathName();
		const MCPWidgetGuidMap::FSyncReport Report = MCPWidgetGuidMap::CompileChecked(WidgetBP);
		if (!Report.bCompiled)
		{
			const TSharedPtr<FJsonValue> Blocked = MCPWidgetGuidMap::BlockedError(AssetPath, Report);
			FString Reason;
			if (Blocked.IsValid() && Blocked->AsObject().IsValid()) Blocked->AsObject()->TryGetStringField(TEXT("error"), Reason);
			Out->SetStringField(TEXT("error"), Reason);
			return false;
		}
		MCPSetWidgetGuidOutcome(Out, Report, AssetPath);
		return true;
	}

	static void SaveSet(UWidgetBlueprint* WidgetBP, TArray<UObject*>& Out)
	{
		Out.Add(WidgetBP);
	}

	static constexpr EMCPResultDialect Dialect = EMCPResultDialect::SavedSaveError;
};
