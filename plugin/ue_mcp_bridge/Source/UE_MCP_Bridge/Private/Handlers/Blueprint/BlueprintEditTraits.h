#pragma once

// What a Blueprint declares to the F1 edit scope: a level script Blueprint
// resolves from its umap path (#942), a value edit marks the Blueprint
// modified and a structural one structurally modified, the compile is
// CompileBlueprint, and a Blueprint that does not compile is saved anyway,
// as every Blueprint action did before the scope. Shared by every translation
// unit that edits Blueprints, which a unity build may merge.

#include "CoreMinimal.h"
#include "HandlerUtils.h"
#include "Families/MCPEditScope.h"
#include "Handlers/Blueprint/BlueprintHandlers.h"
#include "Handlers/Blueprint/BlueprintHandlers_Internal.h"
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"

namespace MCPBlueprint
{
	/** An edit that compiles and saves once it changed something. Compile failure saves anyway. */
	inline FMCPCommitPolicy Edit(EMCPSettle Settle)
	{
		return FMCPCommitPolicy::EditCompileSave(Settle, EMCPCompileFailure::SaveAnyway);
	}

	/** Member variables, functions, components, interfaces, graphs: the skeleton changes. */
	inline FMCPCommitPolicy StructureEdit() { return Edit(EMCPSettle::Structure); }

	/** A default, flag or node value: the Blueprint is marked modified. */
	inline FMCPCommitPolicy ValueEdit() { return Edit(EMCPSettle::Value); }

	/** Compile and save with no fixup of its own, as a handler that marks nothing did. */
	inline FMCPCommitPolicy PlainEdit() { return Edit(EMCPSettle::None); }
}

template <>
struct TMCPEditTraits<UBlueprint>
{
	static const TCHAR* TypeName() { return TEXT("Blueprint"); }

	static UBlueprint* Load(const FString& AssetPath, TSharedPtr<FJsonValue>& OutError)
	{
		UBlueprint* Blueprint = FBlueprintHandlers::LoadBlueprint(AssetPath);
		if (!Blueprint) OutError = BlueprintNotFoundError(AssetPath);
		return Blueprint;
	}

	static TSharedPtr<FJsonValue> CheckOpen(UBlueprint*, const FString&) { return nullptr; }

	static void Settle(UBlueprint* Blueprint, EMCPSettle Kind)
	{
		if (Kind == EMCPSettle::Structure) FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		else if (Kind == EMCPSettle::Value) FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	}

	/** Writes no keys: Blueprint actions have never reported their compile. */
	static bool Compile(UBlueprint* Blueprint, const TSharedPtr<FJsonObject>&)
	{
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		return Blueprint->Status != BS_Error;
	}

	static void SaveSet(UBlueprint* Blueprint, TArray<UObject*>& Out)
	{
		Out.Add(Blueprint);
	}

	static constexpr EMCPResultDialect Dialect = EMCPResultDialect::SavedSaveError;
};
